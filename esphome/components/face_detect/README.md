# face_detect (custom component)

Face detection for ESP32-S3 under ESP-IDF, bridging ESPHome's `esp32_camera`
frames into `espressif/human_face_detect` (esp-who model family, via esp-dl).

## Requirements

- ESP32-S3, `framework: type: esp-idf`, `psram:` enabled.
- Camera configured with `pixel_format: RGB565` and `frame_buffer_count: 2`.
- Build host with registry access (pulls `espressif/esp-dl:3.3.8` and
  `espressif/human_face_detect:0.5.0` as IDF components).

## Bring-up notes

- Endianness (`RGB565LE` vs `BE`) and box scaling must be verified against
  live frames; see `MODEL_INPUT 160x120` in esp-who's `object_detect` example.
- Inference is throttled (`throttle`, default `500ms`) and runs in `loop()`
  on the stashed frame; the frame buffer is released right after inference.
- JPEG streaming and RGB565 detection conflict; this phase targets
  detect-optimized RGB565 configs.
