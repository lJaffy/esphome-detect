# esp-detect — on-device face detection for ESPHome (ESP32-S3)

A custom ESPHome component that runs human face detection entirely on-device
on an ESP32-S3, bridging `esp32_camera` frames into Espressif's
`human_face_detect` models (esp-who family, via `esp-dl`). No cloud, no
external server: presence, bounding box, score and count are native
Home Assistant entities.

The component lives at `esphome/components/face_detect/`. Copy that
directory next to your YAML and reference it via `external_components`
(local path) or from GitHub.

## Requirements

- ESP32-S3, `framework: type: esp-idf`, `psram:` enabled (8MB recommended
  for the `espdet_*` models at 320X240 and above).
- `esp32_camera` with `frame_buffer_count: 2`,
  `frame_buffer_location: PSRAM`.
- Build host with registry access: the build pulls
  `espressif/esp-dl:3.3.8` and `espressif/human_face_detect:0.5.0` as IDF
  managed components.

## Detection models (`model:` option, default `msrmnp`)

| model | accuracy (mAP50-95) | S3 latency | suggested sensor | suggested `throttle` |
|---|---|---|---|---|
| `msrmnp` | 0.37 | ~33ms | 160X120 | 500ms |
| `espdet_224` | 0.50 | ~132ms | 320X240 | 500ms |
| `espdet_416` | 0.60 | ~437ms | 640X480 | 1000ms+ |

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

```yaml
external_components:
  - source:
      type: local
      path: face_detect_parent_dir  # directory containing face_detect/

esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf

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
  pixel_format: RGB565
  # max_framerate: 5fps  # uncomment + lower if HA logs API 'Buffer full, ping queued'
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

A fully commented version of the same config is kept at
`esphome/components/face_detect/sample.yaml`.

## Notes and gotchas

- **Pixel formats:** `RGB565` and `JPEG` both work. RGB565 takes one PSRAM
  copy; JPEG is auto-converted via `fmt2rgb888`. Prefer JPEG when Home
  Assistant streams the camera (5-10x smaller payloads). Note: with
  `pixel_format: RGB565` and default `jpeg_quality: 0`, ESPHome serves raw
  RGB565 that HA cannot display — set `jpeg_quality: 10` for a viewable
  stream (see `esphome-issue-rgb565-stream.md`).
- **Streaming load:** if HA logs `Buffer full, ping queued`, lower
  `max_framerate`/`idle_framerate`, raise `throttle`, or switch to JPEG.
- **Build host RAM:** compiling `esp-dl` needs well over 1GB per file at
  `-O3`; on small builders set `CONFIG_COMPILER_OPTIMIZATION_SIZE: y`
  (see `sample.yaml`) or add swap.
- **After changing `model:` or IDF deps:** `esphome clean` before
  recompiling.

## Repository layout

- `esphome/components/face_detect/` — the component (`__init__.py`,
  `face_detect.{h,cpp}`, `binary_sensor.py`, `sensor.py`,
  `text_sensor.py`, `sample.yaml`, `README.md` with full details).
- `ROADMAP.md` — future work (e.g. ESP32-P4 support).
- `esphome-issue-rgb565-stream.md` — upstream ESPHome bug-report draft.
- `face_detect_plan.md` — original design notes.
- `scratch/` — local reference checkouts (ESPHome, esp-who), not part of
  the component.
