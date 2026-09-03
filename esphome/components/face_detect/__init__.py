import esphome.codegen as cg
from esphome import automation
from esphome.components import esp32
from esphome.components.esp32 import const as esp32_const
from esphome.components.esp32_camera import ESP32Camera
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TRIGGER_ID, CONF_THROTTLE
from esphome.types import ConfigType

AUTO_LOAD = ["binary_sensor", "sensor"]
DEPENDENCIES = ["esp32_camera"]
MULTI_CONF = True

face_detect_ns = cg.esphome_ns.namespace("face_detect")
FaceDetect = face_detect_ns.class_("FaceDetect", cg.Component)
FaceDetectedTrigger = face_detect_ns.class_(
    "FaceDetectedTrigger", automation.Trigger.template()
)

CONF_FACE_DETECT_ID = "face_detect_id"
CONF_CAMERA_ID = "camera_id"
CONF_CONFIDENCE_THRESHOLD = "confidence_threshold"
CONF_ON_FACE = "on_face"
CONF_MAX_BOXES = "max_boxes"
CONF_MODEL = "model"

MODEL_MSRMNP = "msrmnp"
MODEL_ESPDET_224 = "espdet_224"
MODEL_ESPDET_416 = "espdet_416"

# Kconfig choice symbols in espressif/human_face_detect (see its Kconfig).
# The FLASH_* symbol must be enabled alongside the model choice, otherwise the
# choice dependency (MODEL_IN_SDCARD || FLASH_<MODEL>) is unsatisfied and the
# default silently stays on MSRMNP.
MODEL_SDKCONFIG = {
    MODEL_MSRMNP: (
        "HUMAN_FACE_DETECT_MSRMNP_S8_V1",
        "FLASH_HUMAN_FACE_DETECT_MSRMNP_S8_V1",
    ),
    MODEL_ESPDET_224: (
        "ESPDET_PICO_224_224_FACE",
        "FLASH_ESPDET_PICO_224_224_FACE",
    ),
    MODEL_ESPDET_416: (
        "ESPDET_PICO_416_416_FACE",
        "FLASH_ESPDET_PICO_416_416_FACE",
    ),
}

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FaceDetect),
            cv.GenerateID(CONF_CAMERA_ID): cv.use_id(ESP32Camera),
            cv.Optional(CONF_CONFIDENCE_THRESHOLD, default=0.5): cv.float_range(
                min=0.0, max=1.0
            ),
            cv.Optional(
                CONF_THROTTLE, default="500ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_MAX_BOXES, default=1): cv.int_range(min=1, max=8),
            cv.Optional(CONF_MODEL, default=MODEL_MSRMNP): cv.enum(
                {
                    MODEL_MSRMNP: MODEL_MSRMNP,
                    MODEL_ESPDET_224: MODEL_ESPDET_224,
                    MODEL_ESPDET_416: MODEL_ESPDET_416,
                }
            ),
            cv.Optional(CONF_ON_FACE): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(FaceDetectedTrigger),
                }
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    cv.only_with_framework("esp-idf"),
    esp32.only_on_variant(supported=[esp32_const.VARIANT_ESP32S3]),
    cv.requires_component("psram"),
)


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    camera = await cg.get_variable(config[CONF_CAMERA_ID])
    cg.add(var.set_camera(camera))
    cg.add(var.set_confidence_threshold(config[CONF_CONFIDENCE_THRESHOLD]))
    cg.add(var.set_throttle(config[CONF_THROTTLE]))
    cg.add(var.set_max_boxes(config[CONF_MAX_BOXES]))
    cg.add(var.set_model_name(config[CONF_MODEL]))

    model = config[CONF_MODEL]
    for symbol in MODEL_SDKCONFIG[model]:
        esp32.add_idf_sdkconfig_option(symbol, True)

    # Proven set from esp-who object_detect lockfile (IDF 5.5.5): esp-dl 3.3.8
    # satisfies human_face_detect 0.5.0's esp-dl ~3.3.0 requirement.
    # After changing these, run 'esphome clean' so src/idf_component.yml
    # and dependencies.lock are regenerated; otherwise the new headers
    # never reach the compiler (previously masked as runtime mark_failed).
    esp32.add_idf_component(name="espressif/esp-dl", ref="3.3.8")
    esp32.add_idf_component(name="espressif/human_face_detect", ref="0.5.0")

    for conf in config.get(CONF_ON_FACE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger,
            [
                (cg.float_, "x"),
                (cg.float_, "y"),
                (cg.float_, "w"),
                (cg.float_, "h"),
                (cg.float_, "score"),
                (cg.int_, "count"),
            ],
            conf,
        )
