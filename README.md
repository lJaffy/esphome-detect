# esp-detect — on-device face detection for ESPHome (ESP32-S3)

A custom ESPHome component that runs human face detection entirely on-device
on an ESP32-S3, bridging `esp32_camera` frames into Espressif's
`human_face_detect` models (esp-who family, via `esp-dl`). Presence, bounding box, score and count are native
Home Assistant entities.

## Requirements

- ESP32-S3, `framework: type: esp-idf`, `psram:` enabled (8MB recommended
  for the `espdet_*` models at 320X240 and above).
- `esp32_camera` with `frame_buffer_count: 2`,
  `frame_buffer_location: PSRAM`.
- Build host with registry access: the build pulls
  `espressif/esp-dl:3.3.8` and `espressif/human_face_detect:0.5.0` as IDF
  managed components.

## Installation

```yaml
external_components:
  - source: github://ljaffy/esphome-detect@main
    components: [face_detect]
    refresh: 0h
```

## Detection models (`model:` option, default `msrmnp`)

| model | accuracy (mAP50-95) | S3 latency | suggested sensor | suggested `throttle` |
|---|---|---|---|---|
| `msrmnp` | 0.367 | ~33ms | 160X120 | 500ms |
| `espdet_224` | 0.504 | ~132ms | 320X240 | 500ms |
| `espdet_416` | 0.598 | ~437ms | 640X480 | 1000ms+ |

Latencies are the `model(ms)` column for ESP32-S3 from the upstream model
table, measured at the model's native input size — see the Model Latency
table in
[`models/human_face_detect/README.md`](https://github.com/espressif/esp-dl/blob/master/models/human_face_detect/README.md)
(same table is published on the
[`espressif/human_face_detect` registry page](https://components.espressif.com/components/espressif/human_face_detect)).
`msrmnp` is two-stage (`msr_s8_v1_s3` ~33.1ms plus `mnp_s8_v1_s3` ~5.8ms per
candidate); totals above exclude preprocess/postprocess.

Changing `model:` writes new `sdkconfig` Kconfig choices — run
`esphome clean <yaml>` before recompiling. The active model is published
at boot to the optional `model` text sensor (diagnostic entity) so Home
Assistant always shows which weights are running.

## Entities

| platform | key | meaning |
|---|---|---|
| `binary_sensor` | `presence` | `true` while a face is above `confidence_threshold` |
| `sensor` | `box_x/y/w/h` | bounding box of the largest above-threshold face |
| `sensor` | `score` | strongest candidate score **every frame**, even below threshold (`0` when nothing found) — shows update rate and calibration baseline |
| `sensor` | `count` | faces above threshold (capped by `max_boxes`) |
| `text_sensor` | `model` | active detection model (diagnostic) |

Plus the `on_face` automation trigger `(x, y, w, h, score, count)`,
fired only for above-threshold detections.

## Sample configuration

Camera pins below are for the Seeed Studio XIAO ESP32S3 Sense module.

```yaml
external_components:
  - source: github://ljaffy/esphome-detect@main
    components: [face_detect]
    refresh: 0h

esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf
  # Uncomment if the build host runs out of RAM compiling esp-dl
  #sdkconfig_options:
  #  CONFIG_COMPILER_OPTIMIZATION_SIZE: y

psram:

esp32_camera:
  id: s3_cam
  data_pins: [15, 17, 18, 16, 14, 12, 11, 48]
  vsync_pin: 38
  href_pin: 47
  pixel_clock_pin: 13
  external_clock:
    pin: 10
    frequency: 20MHz
  i2c_pins:
    sda: 40
    scl: 39
  resolution: 320X240
  pixel_format: JPEG  # keep JPEG: RGB565 caused buffer issues
  jpeg_quality: 10
  idle_framerate: 1fps  # 1fps detection when the camera is not streaming
  frame_buffer_count: 2
  frame_buffer_location: PSRAM

face_detect:
  id: face
  camera_id: s3_cam
  model: espdet_224
  confidence_threshold: 0.5
  throttle: 500ms
  max_boxes: 1
  on_face:
    - logger.log:
        format: "face x=%.0f y=%.0f w=%.0f h=%.0f score=%.2f count=%d"
        args: [x, y, w, h, score, count]

binary_sensor:
  - platform: face_detect
    face_detect_id: face
    presence:
      name: Face present

sensor:
  - platform: face_detect
    face_detect_id: face
    box_x:
      name: Face box X
    box_y:
      name: Face box Y
    box_w:
      name: Face box width
    box_h:
      name: Face box height
    score:
      name: Face score
    count:
      name: Face count

text_sensor:
  - platform: face_detect
    face_detect_id: face
    model:
      name: Detection model
```

## Notes and gotchas

- **Pixel format:** keep `pixel_format: JPEG`. `RGB565` caused buffer
  issues; JPEG is auto-converted via `fmt2rgb888` for detection and gives
  5-10x smaller payloads when Home Assistant streams the camera.
- **Streaming load:** if HA logs `Buffer full, ping queued`, lower
  `max_framerate`/`idle_framerate`, raise `throttle`, or lower `jpeg_quality`
  / resolution.
- **Build host RAM:** compiling `esp-dl` needs well over 1GB per file at
  `-O3`; on small builders set `CONFIG_COMPILER_OPTIMIZATION_SIZE: y`
  (see the commented `sdkconfig_options` in the sample config above) or add
  swap.
- **After changing `model:` or IDF deps:** `esphome clean` before
  recompiling.
