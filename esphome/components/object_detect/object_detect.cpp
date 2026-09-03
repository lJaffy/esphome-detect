#include "object_detect.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

// One include per detector family; only the component selected via
// `object_type:` is downloaded (see __init__.py), the rest are skipped.
#if __has_include("human_face_detect.hpp")
#include "human_face_detect.hpp"
#define OBJECT_DETECT_HAS_FACE 1
#define OBJECT_DETECT_ANY_MODEL 1
#endif
#if __has_include("pedestrian_detect.hpp")
#include "pedestrian_detect.hpp"
#define OBJECT_DETECT_HAS_PEDESTRIAN 1
#define OBJECT_DETECT_ANY_MODEL 1
#endif
#if __has_include("cat_detect.hpp")
#include "cat_detect.hpp"
#define OBJECT_DETECT_HAS_CAT 1
#define OBJECT_DETECT_ANY_MODEL 1
#endif
#if __has_include("dog_detect.hpp")
#include "dog_detect.hpp"
#define OBJECT_DETECT_HAS_DOG 1
#define OBJECT_DETECT_ANY_MODEL 1
#endif
#ifndef OBJECT_DETECT_ANY_MODEL
#error "No detection model headers found - IDF managed components missing. Check src/idf_component.yml contains the detector for your object_type, run 'esphome clean', then rebuild with registry access."
#endif
#include "dl_detect_base.hpp"
#include "img_converters.h"

#include "esp_heap_caps.h"

#include <cstring>

namespace esphome::object_detect {

static const char *const TAG = "object_detect";

// Max sensor frame accepted for conversion (640x480 RGB888 ~= 900KB PSRAM).
static constexpr uint16_t OBJECT_DETECT_MAX_DIM = 640;

void ObjectDetect::setup() {
  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "No camera configured, check camera_id");
    this->mark_failed();
    return;
  }
  this->camera_->add_listener(this);
  // Created late to avoid early heap fragmentation, mirroring esp-who examples.
  // No-arg constructors use the Kconfig default model internally, selected
  // via the `object_type:`/`model:` YAML options.
  this->detector_ = this->create_detector_();
  this->model_ready_ = this->detector_ != nullptr;
  if (!this->model_ready_) {
    ESP_LOGE(TAG, "Failed to create detector for object_type '%s'", this->object_type_.c_str());
    this->mark_failed();
  } else {
    ESP_LOGI(TAG, "Detector created (%s)", this->model_name_.c_str());
    if (this->model_text_sensor_ != nullptr) {
      this->model_text_sensor_->publish_state(this->model_name_);
    }
  }
}

dl::detect::Detect *ObjectDetect::create_detector_() {
  if (this->object_type_ == "face") {
#ifdef OBJECT_DETECT_HAS_FACE
    return new HumanFaceDetect();
#else
    ESP_LOGE(TAG, "object_type 'face' needs espressif/human_face_detect (missing from build)");
#endif
  } else if (this->object_type_ == "pedestrian") {
#ifdef OBJECT_DETECT_HAS_PEDESTRIAN
    return new PedestrianDetect();
#else
    ESP_LOGE(TAG, "object_type 'pedestrian' needs espressif/pedestrian_detect (missing from build)");
#endif
  } else if (this->object_type_ == "cat") {
#ifdef OBJECT_DETECT_HAS_CAT
    return new CatDetect();
#else
    ESP_LOGE(TAG, "object_type 'cat' needs espressif/cat_detect (missing from build)");
#endif
  } else if (this->object_type_ == "dog") {
#ifdef OBJECT_DETECT_HAS_DOG
    return new DogDetect();
#else
    ESP_LOGE(TAG, "object_type 'dog' needs espressif/dog_detect (missing from build)");
#endif
  } else {
    ESP_LOGE(TAG, "Unknown object_type '%s'", this->object_type_.c_str());
  }
  return nullptr;
}

ObjectDetect::~ObjectDetect() {
  if (this->conv_buf_ != nullptr) {
    heap_caps_free(this->conv_buf_);
    this->conv_buf_ = nullptr;
    this->conv_buf_size_ = 0;
  }
  if (this->detector_ != nullptr) {
    delete this->detector_;
    this->detector_ = nullptr;
  }
}

bool ObjectDetect::ensure_conv_buf_(size_t size) {
  if (size == 0) {
    return false;
  }
  if (this->conv_buf_ != nullptr && this->conv_buf_size_ >= size) {
    return true;
  }
  if (this->conv_buf_ != nullptr) {
    heap_caps_free(this->conv_buf_);
    this->conv_buf_ = nullptr;
    this->conv_buf_size_ = 0;
  }
  // PSRAM first (8MB boards hold 640x480 RGB888 ~= 900KB); DRAM fallback for
  // tiny frames. Never throws: null is checked by the caller.
  uint8_t *buf = static_cast<uint8_t *>(
      heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) {
    buf = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT));
  }
  if (buf == nullptr) {
    if (!this->warned_alloc_) {
      ESP_LOGW(TAG, "Conversion buffer alloc failed (%u bytes), skipping frame",
               static_cast<unsigned>(size));
      this->warned_alloc_ = true;
    }
    return false;
  }
  this->conv_buf_ = buf;
  this->conv_buf_size_ = size;
  return true;
}

void ObjectDetect::on_camera_image(const std::shared_ptr<camera::CameraImage> &image) {
  if (!this->model_ready_) {
    return;
  }
  const uint32_t now = millis();
  if (now - this->last_run_ < this->throttle_ms_) {
    return;
  }
  this->pending_image_ = image;
}

void ObjectDetect::loop() {
  if (!this->model_ready_ || this->pending_image_ == nullptr) {
    return;
  }
  const uint32_t now = App.get_loop_component_start_time();
  if (now - this->last_run_ < this->throttle_ms_) {
    return;
  }
  this->last_run_ = now;
  auto image = this->pending_image_;
  this->pending_image_.reset();
  // Moved in so inference can release the driver fb before the slow model
  // runs (keeps fb_count: 2 pool + API streaming flowing).
  this->run_inference_(std::move(image));
}

// Takes ownership: the driver fb is returned to the pool before inference
// in both branches, so a ~130ms+ model run never stalls capture/streaming.
bool ObjectDetect::run_inference_(std::shared_ptr<camera::CameraImage> image) {
  auto esp_image = std::static_pointer_cast<esp32_camera::ESP32CameraImage>(image);
  if (esp_image == nullptr) {
    return false;
  }
  camera_fb_t *fb = esp_image->get_raw_buffer();
  if (fb == nullptr || fb->buf == nullptr) {
    return false;
  }
  if (fb->format == PIXFORMAT_RGB565) {
    // Single PSRAM copy (BE order: sensor/driver emits MSB first; LE
    // byte-swaps the input and the model silently finds nothing), then the
    // driver fb is released before inference.
    const size_t need = static_cast<size_t>(fb->width) * fb->height * 2;
    if (!this->ensure_conv_buf_(need)) {
      return false;
    }
    memcpy(this->conv_buf_, fb->buf, need);
    dl::image::img_t img{this->conv_buf_, static_cast<uint16_t>(fb->width),
                         static_cast<uint16_t>(fb->height), dl::image::DL_IMAGE_PIX_TYPE_RGB565BE};
    image.reset();  // return fb to driver before the slow model runs
    return this->detect_and_publish_(img);
  }
  // Generic fallback: JPEG (format 4) and other non-RGB565 captures are
  // converted to RGB888 via esp32-camera's fmt2rgb888 (documented for face
  // detection) into a reusable PSRAM buffer.
  if (fb->width == 0 || fb->height == 0 || fb->width > OBJECT_DETECT_MAX_DIM ||
      fb->height > OBJECT_DETECT_MAX_DIM) {
    ESP_LOGW(TAG, "Unsupported frame size %dx%d, skipping", fb->width, fb->height);
    return false;
  }
  const size_t need = static_cast<size_t>(fb->width) * fb->height * 3;
  if (!this->ensure_conv_buf_(need)) {
    return false;
  }
  const uint32_t t0 = millis();
  if (!fmt2rgb888(fb->buf, fb->len, fb->format, this->conv_buf_)) {
    if (!this->warned_format_) {
      ESP_LOGW(TAG, "Failed to convert pixel_format %d to RGB888, skipping", fb->format);
      this->warned_format_ = true;
    }
    return false;
  }
  dl::image::img_t img{this->conv_buf_, static_cast<uint16_t>(fb->width),
                       static_cast<uint16_t>(fb->height), dl::image::DL_IMAGE_PIX_TYPE_RGB888};
  image.reset();  // return fb to driver before the slow model runs
  const bool ok = this->detect_and_publish_(img);
  ESP_LOGV(TAG, "Converted %dx%d fmt %d in %" PRIu32 "ms", fb->width, fb->height, fb->format,
           millis() - t0);
  return ok;
}

bool ObjectDetect::detect_and_publish_(dl::image::img_t &img) {
  const uint32_t t0 = millis();
  auto &results = this->detector_->run(img);
  ESP_LOGV(TAG, "Inference (%s) on %dx%d in %" PRIu32 "ms", this->model_name_.c_str(), img.width,
           img.height, millis() - t0);

  ObjectBox best;
  float best_score = 0.0f;
  bool has_any = false;
  int count = 0;
  for (auto &r : results) {
    // Strongest signal overall, published every frame even below threshold
    // so the score sensor shows update rate + calibration baseline.
    if (!has_any || r.score > best_score) {
      best_score = r.score;
      has_any = true;
    }
    if (r.score < this->confidence_threshold_) {
      continue;
    }
    count++;
    float area = (r.box[2] - r.box[0]) * (r.box[3] - r.box[1]);
    float best_area = best.w * best.h;
    if (area > best_area) {
      best.x = r.box[0];
      best.y = r.box[1];
      best.w = r.box[2] - r.box[0];
      best.h = r.box[3] - r.box[1];
      best.score = r.score;
    }
    if (count >= this->max_boxes_) {
      break;
    }
  }
  // Score always reflects the latest frame: max candidate score, or 0 when
  // the model found nothing (distinguishes clean negative from stall).
  if (this->score_ != nullptr) {
    this->score_->publish_state(has_any ? best_score : 0.0f);
  }
  if (count == 0) {
    this->publish_no_detection_();
  } else {
    this->publish_detection_(best, count);
  }
  return true;
}

void ObjectDetect::publish_no_detection_() {
  if (this->presence_ != nullptr) {
    this->presence_->publish_state(false);
  }
  if (this->count_ != nullptr) {
    this->count_->publish_state(0);
  }
}

void ObjectDetect::publish_detection_(const ObjectBox &box, int count) {
  if (this->presence_ != nullptr) {
    this->presence_->publish_state(true);
  }
  if (this->box_x_ != nullptr) {
    this->box_x_->publish_state(box.x);
  }
  if (this->box_y_ != nullptr) {
    this->box_y_->publish_state(box.y);
  }
  if (this->box_w_ != nullptr) {
    this->box_w_->publish_state(box.w);
  }
  if (this->box_h_ != nullptr) {
    this->box_h_->publish_state(box.h);
  }
  // Score is published unconditionally in detect_and_publish_ (strongest
  // signal every frame, even below threshold), so it is not repeated here.
  if (this->count_ != nullptr) {
    this->count_->publish_state(count);
  }
  this->detection_callback_.call(box.x, box.y, box.w, box.h, box.score, count);
}

void ObjectDetect::dump_config() {
  ESP_LOGCONFIG(TAG, "Object detection:");
  ESP_LOGCONFIG(TAG, "  Object type: %s", this->object_type_.c_str());
  ESP_LOGCONFIG(TAG, "  Model: %s", this->model_name_.c_str());
  ESP_LOGCONFIG(TAG, "  Confidence threshold: %.2f", this->confidence_threshold_);
  ESP_LOGCONFIG(TAG, "  Throttle: %" PRIu32 "ms", this->throttle_ms_);
  ESP_LOGCONFIG(TAG, "  Max boxes: %u", this->max_boxes_);
  LOG_BINARY_SENSOR("  ", "Presence", this->presence_);
  LOG_SENSOR("  ", "Box X", this->box_x_);
  LOG_SENSOR("  ", "Box Y", this->box_y_);
  LOG_SENSOR("  ", "Box W", this->box_w_);
  LOG_SENSOR("  ", "Box H", this->box_h_);
  LOG_SENSOR("  ", "Score", this->score_);
  LOG_SENSOR("  ", "Count", this->count_);
  LOG_TEXT_SENSOR("  ", "Model", this->model_text_sensor_);
}

}  // namespace esphome::object_detect
