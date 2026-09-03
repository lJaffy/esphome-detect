#pragma once

#ifdef USE_ESP32

#include <memory>
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/camera/camera.h"
#include "esphome/components/sensor/sensor.h"

#include "esphome/components/esp32_camera/esp32_camera.h"

#if __has_include("dl_image_define.hpp")
#include "dl_image_define.hpp"
#else
#include "dl_image.hpp"
#endif

namespace esphome::face_detect {

struct FaceBox {
  float x{0};
  float y{0};
  float w{0};
  float h{0};
  float score{0};
};

class FaceDetect : public Component, public camera::CameraListener {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_camera(camera::Camera *camera) { this->camera_ = camera; }
  void set_confidence_threshold(float threshold) { this->confidence_threshold_ = threshold; }
  void set_throttle(uint32_t throttle_ms) { this->throttle_ms_ = throttle_ms; }
  void set_max_boxes(uint8_t max_boxes) { this->max_boxes_ = max_boxes; }

  void set_presence_binary_sensor(binary_sensor::BinarySensor *s) { this->presence_ = s; }
  void set_box_x_sensor(sensor::Sensor *s) { this->box_x_ = s; }
  void set_box_y_sensor(sensor::Sensor *s) { this->box_y_ = s; }
  void set_box_w_sensor(sensor::Sensor *s) { this->box_w_ = s; }
  void set_box_h_sensor(sensor::Sensor *s) { this->box_h_ = s; }
  void set_score_sensor(sensor::Sensor *s) { this->score_ = s; }
  void set_count_sensor(sensor::Sensor *s) { this->count_ = s; }

  void on_camera_image(const std::shared_ptr<camera::CameraImage> &image) override;

  template<typename F> void add_on_face_callback(F &&callback) {
    this->face_callback_.add(std::forward<F>(callback));
  }

 protected:
  bool run_inference_(const std::shared_ptr<camera::CameraImage> &image);
  bool detect_and_publish_(dl::image::img_t &img);
  void publish_no_face_();
  void publish_face_(const FaceBox &box, int count);

  camera::Camera *camera_{nullptr};
  float confidence_threshold_{0.5f};
  uint32_t throttle_ms_{500};
  uint8_t max_boxes_{1};

  binary_sensor::BinarySensor *presence_{nullptr};
  sensor::Sensor *box_x_{nullptr};
  sensor::Sensor *box_y_{nullptr};
  sensor::Sensor *box_w_{nullptr};
  sensor::Sensor *box_h_{nullptr};
  sensor::Sensor *score_{nullptr};
  sensor::Sensor *count_{nullptr};

  CallbackManager<void(float, float, float, float, float, int)> face_callback_;

  std::shared_ptr<camera::CameraImage> pending_image_;
  uint32_t last_run_{0};
  bool model_ready_{false};
  bool warned_format_{false};
  void *detector_{nullptr};
};

class FaceDetectedTrigger : public Trigger<float, float, float, float, float, int> {
 public:
  explicit FaceDetectedTrigger(FaceDetect *parent) {
    parent->add_on_face_callback(
        [this](float x, float y, float w, float h, float score, int count) {
          this->trigger(x, y, w, h, score, count);
        });
  }
};

}  // namespace esphome::face_detect

#endif
