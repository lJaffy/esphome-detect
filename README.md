# esp-detect — on-device object detection for ESPHome (ESP32-S3)

Custom ESPHome components that run human/object detection entirely
on-device on an ESP32-S3, bridging `esp32_camera` frames into Espressif's
esp-dl detector family. No cloud, no external server: presence, bounding
box, score and count are native Home Assistant entities.

- `esphome/components/face_detect/` — the original face-only component
  (stable, still supported).
- `esphome/components/object_detect/` — generalised sibling: `object_type`
  (`face` | `pedestrian` | `cat` | `dog`) + `model` switching over the
  same pipeline, same entities plus `on_detection`.

Copy the component directory next to your YAML and reference it via
`external_components` (local path) or from GitHub.

## Requirements

- ESP32-S3, `framework: type: esp-idf`, `psram:` enabled (8MB recommended
  for the `espdet_*` models at 320X240 and above).
- `esp32_camera` with `frame_buffer_count: 2`,
  `frame_buffer_location: PSRAM`.
- Build host with registry access: the build pulls `espressif/esp-dl:3.3.8`
  plus the detector for your `object_type` as IDF managed components.

## Detection models

`object_detect` matrix (all detectors require esp-dl `~3.3.0`, satisfied
by the pinned `3.3.8`):

| `object_type` | `model` values | registry component |
|---|---|---|
| `face` | `msrmnp` \| `espdet_224` \| `espdet_416` | `espressif/human_face_detect:0.5.0` |
| `pedestrian` | `pico` | `espressif/pedestrian_detect:0.3.2` |
| `cat` | `espdet_224` \| `espdet_416` | `espressif/cat_detect:0.3.0` |
| `dog` | `espdet_224` \| `espdet_416` | `espressif/dog_detect:0.2.0` |

Latency guidance (S3): pico/msrmnp ~33-130ms (320X240, `throttle: 500ms`);
`*_416` ~435ms (640X480, `throttle: 1000ms+`). Omitting `model:` selects
the per-type default; invalid combinations are rejected at validation.
Changing `object_type`/`model` rewrites IDF deps + Kconfig — run
`esphome clean <yaml>` before recompiling. The active `type/model` is
published at boot to the optional `model` text sensor (diagnostic).

## Entities (both components)

| platform | key | meaning |
|---|---|---|
| `binary_sensor` | `presence` | `true` while an object is above `confidence_threshold` |
| `sensor` | `box_x/y/w/h` | bounding box of the largest above-threshold object |
| `sensor` | `score` | strongest candidate score **every frame**, even below threshold (`0` when nothing found) — shows update rate and calibration baseline |
| `sensor` | `count` | objects above threshold (capped by `max_boxes`) |
| `text_sensor` | `model` | active detector (diagnostic) |

Plus the automation trigger (`on_face` / `on_detection`) with
`(x, y, w, h, score, count)`, fired only for above-threshold detections.

## Sample configuration (`object_detect`)

```yaml
external_components:
  - source:
      type: local
      path: object_detect_parent_dir  # directory containing object_detect/

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

object_detect:
  id: detect
  camera_id: s3_cam
  object_type: pedestrian
  confidence_threshold: 0.5
  throttle: 500ms
  max_boxes: 1
  on_detection:
    - logger.log:
        format: "object x=%.0f y=%.0f w=%.0f h=%.0f score=%.2f count=%d"
        args: [x, y, w, h, score, count]

binary_sensor:
  - platform: object_detect
    object_detect_id: detect
    presence:
      name: Object present

sensor:
  - platform: object_detect
    object_detect_id: detect
    box_x:
      name: Object box X
    box_y:
      name: Object box Y
    box_w:
      name: Object box width
    box_h:
      name: Object box height
    score:
      name: Object score
    count:
      name: Object count

text_sensor:
  - platform: object_detect
    object_detect_id: detect
    model:
      name: Detection model
```

Fully commented samples live at
`esphome/components/object_detect/sample.yaml` (and the face equivalent).

## Notes and gotchas

- **Pixel formats:** `RGB565` and `JPEG` both work. Prefer JPEG when Home
  Assistant streams the camera (5-10x smaller payloads). Note: with
  `pixel_format: RGB565` and default `jpeg_quality: 0`, ESPHome serves raw
  RGB565 that HA cannot display — set `jpeg_quality: 10` for a viewable
  stream (see `esphome-issue-rgb565-stream.md`).
- **Streaming load:** if HA logs `Buffer full, ping queued`, lower
  `max_framerate`/`idle_framerate`, raise `throttle`, or switch to JPEG.
- **Build host RAM:** compiling `esp-dl` needs well over 1GB per file at
  `-O3`; on small builders set `CONFIG_COMPILER_OPTIMIZATION_SIZE: y`
  (see `sample.yaml`) or add swap.
- **After changing `object_type`/`model` or IDF deps:** `esphome clean`
  before recompiling.

## Repository layout

- `esphome/components/face_detect/` — face-only component with full docs.
- `esphome/components/object_detect/` — generalised component with full docs.
- `ROADMAP.md` — future work (e.g. ESP32-P4 support).
- `generalisation_roadmap.md` — design notes behind `object_detect`.
- `esphome-issue-rgb565-stream.md` — upstream ESPHome bug-report draft.
- `scratch/` — local reference checkouts (ESPHome, esp-who), not part of
  the components.
