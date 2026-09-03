#ifdef USE_ESP32

#include "face_detect.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

#if !__has_include("human_face_detect.hpp")
#error "human_face_detect.hpp not found - IDF managed components missing. Check src/idf_component.yml contains espressif/human_face_detect, run 'esphome clean', then rebuild with registry access."
#endif
#include "human_face_detect.hpp"
#if __has_include("dl_image_define.hpp")
#include "dl_image_define.hpp"
#else
#include "dl_image.hpp"
#endif
#include "img_converters.h"

#include "esp_heap_caps.h"

namespace esphome::face_detect {

static const char *const TAG = "face_detect";

// Max sensor frame accepted for conversion (640x480 RGB888 ~= 900KB PSRAM).
static constexpr uint16_t FACE_DETECT_MAX_DIM = 640;

void FaceDetect::setup() {
  if (this->camera_ == nullptr) {
    ESP_LOGE(TAG, "No camera configured, check camera_id");
    this->mark_failed();
    return;
  }
  this->camera_->add_listener(this);
  // Created late to avoid early heap fragmentation, mirroring esp-who examples.
  // No-arg constructor uses Kconfig DEFAULT_HUMAN_FACE_DETECT_MODEL internally,
  // selected via the `model:` YAML option (msrmnp/espdet_224/espdet_416).
  this->detector_ = new HumanFaceDetect();
  this->model_ready_ = this->detector_ != nullptr;
  if (!this->model_ready_) {
    ESP_LOGE(TAG, "Failed to create HumanFaceDetect model");
    this->mark_failed();
  } else {
    ESP_LOGI(TAG, "HumanFaceDetect model created (yaml model: %s)", this->model_name_.c_str());
  }
}

FaceDetect::~FaceDetect() {
  if (this->conv_buf_ != nullptr) {
    heap_caps_free(this->conv_buf_);
    this->conv_buf_ = nullptr;
    this->conv_buf_size_ = 0;
  }
}

bool FaceDetect::ensure_conv_buf_(size_t size) {
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

void FaceDetect::on_camera_image(const std::shared_ptr<camera::CameraImage> &image) {
  if (!this->model_ready_) {
    return;
  }
  const uint32_t now = millis();
  if (now - this->last_run_ < this->throttle_ms_) {
    return;
  }
  this->pending_image_ = image;
}

void FaceDetect::loop() {
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
  this->run_inference_(image);
}

bool FaceDetect::run_inference_(const std::shared_ptr<camera::CameraImage> &image) {
  auto esp_image = std::static_pointer_cast<esp32_camera::ESP32CameraImage>(image);
  if (esp_image == nullptr) {
    return false;
  }
  camera_fb_t *fb = esp_image->get_raw_buffer();
  if (fb == nullptr || fb->buf == nullptr) {
    return false;
  }
  if (fb->format == PIXFORMAT_RGB565) {
    // Zero-copy fast path (preferred: set pixel_format: RGB565 in YAML;
    // ESPHome still streams JPEG via frame2jpg conversion).
    dl::image::img_t img{fb->buf, static_cast<uint16_t>(fb->width),
                         static_cast<uint16_t>(fb->height), dl::image::DL_IMAGE_PIX_TYPE_RGB565LE};
    return this->detect_and_publish_(img);
  }
  // Generic fallback: JPEG (format 4) and other non-RGB565 captures are
  // converted to RGB888 via esp32-camera's fmt2rgb888 (documented for face
  // detection) into a reusable PSRAM buffer.
  if (fb->width == 0 || fb->height == 0 || fb->width > FACE_DETECT_MAX_DIM ||
      fb->height > FACE_DETECT_MAX_DIM) {
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
  const bool ok = this->detect_and_publish_(img);
  ESP_LOGV(TAG, "Converted %dx%d fmt %d in %" PRIu32 "ms", fb->width, fb->height, fb->format,
           millis() - t0);
  return ok;
}

bool FaceDetect::detect_and_publish_(dl::image::img_t &img) {
  auto *detector = static_cast<HumanFaceDetect *>(this->detector_);
  const uint32_t t0 = millis();
  auto &results = detector->run(img);
  ESP_LOGV(TAG, "Inference (%s) on %dx%d in %" PRIu32 "ms", this->model_name_.c_str(), img.width,
           img.height, millis() - t0);

  FaceBox best;
  int count = 0;
  for (auto &r : results) {
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
  if (count == 0) {
    this->publish_no_face_();
  } else {
    this->publish_face_(best, count);
  }
  return true;
}

void FaceDetect::publish_no_face_() {
  if (this->presence_ != nullptr) {
    this->presence_->publish_state(false);
  }
  if (this->count_ != nullptr) {
    this->count_->publish_state(0);
  }
}

void FaceDetect::publish_face_(const FaceBox &box, int count) {
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
  if (this->score_ != nullptr) {
    this->score_->publish_state(box.score);
  }
  if (this->count_ != nullptr) {
    this->count_->publish_state(count);
  }
  this->face_callback_.call(box.x, box.y, box.w, box.h, box.score, count);
}

void FaceDetect::dump_config() {
  ESP_LOGCONFIG(TAG, "Face detection:");
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
}

}  // namespace esphome::face_detect

#endif
