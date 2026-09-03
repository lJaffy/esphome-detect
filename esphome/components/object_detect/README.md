# object_detect (custom component)

On-device object detection for ESP32-S3 under ESP-IDF, bridging ESPHome's
`esp32_camera` frames into Espressif's esp-dl detector family. Set
`object_type` + `model` in YAML and the component switches detector modes:

- `face` — `espressif/human_face_detect:0.5.0`
  (`msrmnp` | `espdet_224` | `espdet_416`)
- `pedestrian` — `espressif/pedestrian_detect:0.3.2` (`pico`)
- `cat` — `espressif/cat_detect:0.3.0` (`espdet_224` | `espdet_416`)
- `dog` — `espressif/dog_detect:0.2.0` (`espdet_224` | `espdet_416`)

All four require `espressif/esp-dl ~3.3.0` (pinned to `3.3.8` here).
Invalid `object_type`/`model` combinations are rejected at config
validation; omitting `model:` selects the per-type default.

## Requirements

- ESP32-S3, `framework: type: esp-idf`, `psram:` enabled (8MB recommended
  at 320X240 and above; conversion buffers alone are ~225KB at 320X240
  RGB888 and ~900KB at 640X480).
- Camera with `frame_buffer_count: 2`, `frame_buffer_location: PSRAM`.
- Build host with registry access (pulls `esp-dl` plus the detector for
  your `object_type` as IDF managed components).

## Entities

Same shape for every type: `presence` binary sensor, `box_x/y/w/h`,
`score`, `count` sensors, `model` diagnostic text sensor (reports e.g.
`pedestrian/pico`), and the `on_detection` automation trigger
`(x, y, w, h, score, count)`.

`score` publishes the strongest candidate every frame (`0` when nothing
found) — even below `confidence_threshold` — so it shows update rate and
a calibration baseline. `presence`, `count`, box sensors and
`on_detection` stay gated on the threshold.

## Latency guidance (S3, per registry READMEs)

| type/model | input | model time | suggested sensor | suggested `throttle` |
|---|---|---|---|---|
| face/msrmnp | 120x160 | ~33ms | 160X120 | 500ms |
| face/cat/dog espdet_224 | 224x224 | ~124-132ms | 320X240 | 500ms |
| face/cat/dog espdet_416 | 416x416 | ~435-438ms | 640X480 | 1000ms+ |
| pedestrian/pico | 224x224 | ~118ms | 320X240 | 500ms |

## Bring-up notes

- Raw RGB565 frames are big-endian; the direct path tags them `RGB565BE`.
- Inference runs in `loop()` on a throttled, stashed frame. The driver
  framebuffer is returned to the pool *before* inference in both paths
  (single PSRAM copy for RGB565, `fmt2rgb888` otherwise).
- Changing `object_type`/`model` rewrites IDF deps + Kconfig: run
  `esphome clean <yaml>` before recompiling, and confirm
  `<build>/src/idf_component.yml` + `dependencies.lock` show the right
  detector.
- Streaming load: `pixel_format: JPEG` gives 5-10x smaller payloads than
  RGB565; lower `max_framerate` or raise `throttle` if HA logs API
  `Buffer full, ping queued`.
- Build host RAM: compiling `esp-dl` needs well over 1GB per file at
  `-O3`; use `CONFIG_COMPILER_OPTIMIZATION_SIZE: y` or add swap.
