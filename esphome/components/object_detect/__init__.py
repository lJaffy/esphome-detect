import esphome.codegen as cg
from esphome import automation
from esphome.components import esp32
from esphome.components.esp32 import const as esp32_const
from esphome.components.esp32_camera import ESP32Camera
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TRIGGER_ID, CONF_THROTTLE
from esphome.types import ConfigType

AUTO_LOAD = ["binary_sensor", "sensor", "text_sensor"]
DEPENDENCIES = ["esp32_camera"]
MULTI_CONF = True

object_detect_ns = cg.esphome_ns.namespace("object_detect")
ObjectDetect = object_detect_ns.class_("ObjectDetect", cg.Component)
ObjectDetectedTrigger = object_detect_ns.class_(
    "ObjectDetectedTrigger", automation.Trigger.template()
)

CONF_OBJECT_DETECT_ID = "object_detect_id"
CONF_OBJECT_TYPE = "object_type"
CONF_CAMERA_ID = "camera_id"
CONF_CONFIDENCE_THRESHOLD = "confidence_threshold"
CONF_ON_DETECTION = "on_detection"
CONF_MAX_BOXES = "max_boxes"
CONF_MODEL = "model"

OBJECT_FACE = "face"
OBJECT_PEDESTRIAN = "pedestrian"
OBJECT_CAT = "cat"
OBJECT_DOG = "dog"

MODEL_MSRMNP = "msrmnp"
MODEL_PICO = "pico"
MODEL_ESPDET_224 = "espdet_224"
MODEL_ESPDET_416 = "espdet_416"

# Valid models per object type.
OBJECT_MODELS = {
    OBJECT_FACE: [MODEL_MSRMNP, MODEL_ESPDET_224, MODEL_ESPDET_416],
    OBJECT_PEDESTRIAN: [MODEL_PICO],
    OBJECT_CAT: [MODEL_ESPDET_224, MODEL_ESPDET_416],
    OBJECT_DOG: [MODEL_ESPDET_224, MODEL_ESPDET_416],
}

TYPE_DEFAULT_MODEL = {
    OBJECT_FACE: MODEL_MSRMNP,
    OBJECT_PEDESTRIAN: MODEL_PICO,
    OBJECT_CAT: MODEL_ESPDET_224,
    OBJECT_DOG: MODEL_ESPDET_224,
}

# (object_type, model) -> (Kconfig model choice, Kconfig FLASH_* symbol).
# The FLASH_* symbol must be enabled alongside the model choice, otherwise
# the choice dependency (MODEL_IN_SDCARD || FLASH_<MODEL>) is unsatisfied
# and the default silently stays on the component default.
MODEL_SDKCONFIG = {
    (OBJECT_FACE, MODEL_MSRMNP): (
        "HUMAN_FACE_DETECT_MSRMNP_S8_V1",
        "FLASH_HUMAN_FACE_DETECT_MSRMNP_S8_V1",
    ),
    (OBJECT_FACE, MODEL_ESPDET_224): (
        "ESPDET_PICO_224_224_FACE",
        "FLASH_ESPDET_PICO_224_224_FACE",
    ),
    (OBJECT_FACE, MODEL_ESPDET_416): (
        "ESPDET_PICO_416_416_FACE",
        "FLASH_ESPDET_PICO_416_416_FACE",
    ),
    (OBJECT_PEDESTRIAN, MODEL_PICO): (
        "PEDESTRIAN_DETECT_PICO_S8_V1",
        "FLASH_PEDESTRIAN_DETECT_PICO_S8_V1",
    ),
    (OBJECT_CAT, MODEL_ESPDET_224): (
        "ESPDET_PICO_224_224_CAT",
        "FLASH_ESPDET_PICO_224_224_CAT",
    ),
    (OBJECT_CAT, MODEL_ESPDET_416): (
        "ESPDET_PICO_416_416_CAT",
        "FLASH_ESPDET_PICO_416_416_CAT",
    ),
    (OBJECT_DOG, MODEL_ESPDET_224): (
        "ESPDET_PICO_224_224_DOG",
        "FLASH_ESPDET_PICO_224_224_DOG",
    ),
    (OBJECT_DOG, MODEL_ESPDET_416): (
        "ESPDET_PICO_416_416_DOG",
        "FLASH_ESPDET_PICO_416_416_DOG",
    ),
}

# Registry components per object type (all require esp-dl ~3.3.0, satisfied
# by the pinned esp-dl below).
OBJECT_IDF_COMPONENTS = {
    OBJECT_FACE: ("espressif/human_face_detect", "0.5.0"),
    OBJECT_PEDESTRIAN: ("espressif/pedestrian_detect", "0.3.2"),
    OBJECT_CAT: ("espressif/cat_detect", "0.3.0"),
    OBJECT_DOG: ("espressif/dog_detect", "0.2.0"),
}


def validate_combination(config: ConfigType) -> ConfigType:
    obj = config[CONF_OBJECT_TYPE]
    allowed = OBJECT_MODELS[obj]
    model = config.get(CONF_MODEL)
    if model is None:
        config[CONF_MODEL] = TYPE_DEFAULT_MODEL[obj]
    elif model not in allowed:
        raise cv.Invalid(
            f"model '{model}' is not valid for object_type '{obj}'; choose from {allowed}"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ObjectDetect),
            cv.GenerateID(CONF_OBJECT_DETECT_ID): cv.declare_id(ObjectDetect),
            cv.Required(CONF_CAMERA_ID): cv.use_id(ESP32Camera),
            cv.Optional(CONF_OBJECT_TYPE, default=OBJECT_FACE): cv.enum(
                {
                    OBJECT_FACE: OBJECT_FACE,
                    OBJECT_PEDESTRIAN: OBJECT_PEDESTRIAN,
                    OBJECT_CAT: OBJECT_CAT,
                    OBJECT_DOG: OBJECT_DOG,
                }
            ),
            cv.Optional(CONF_MODEL): cv.enum(
                {
                    MODEL_MSRMNP: MODEL_MSRMNP,
                    MODEL_PICO: MODEL_PICO,
                    MODEL_ESPDET_224: MODEL_ESPDET_224,
                    MODEL_ESPDET_416: MODEL_ESPDET_416,
                }
            ),
            cv.Optional(CONF_CONFIDENCE_THRESHOLD, default=0.5): cv.float_range(
                min=0.0, max=1.0
            ),
            cv.Optional(
                CONF_THROTTLE, default="500ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_MAX_BOXES, default=1): cv.int_range(min=1, max=8),
            cv.Optional(CONF_ON_DETECTION): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                        ObjectDetectedTrigger
                    ),
                }
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_combination,
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
    obj = config[CONF_OBJECT_TYPE]
    model = config[CONF_MODEL]
    cg.add(var.set_object_type(obj))
    cg.add(var.set_model_name(f"{obj}/{model}"))
    cg.add(var.set_confidence_threshold(config[CONF_CONFIDENCE_THRESHOLD]))
    cg.add(var.set_throttle(config[CONF_THROTTLE]))
    cg.add(var.set_max_boxes(config[CONF_MAX_BOXES]))

    for symbol in MODEL_SDKCONFIG[(obj, model)]:
        esp32.add_idf_sdkconfig_option(symbol, True)

    # Proven esp-dl pin: satisfies every detector's esp-dl ~3.3.0 requirement.
    # After changing object_type/model, run 'esphome clean' so
    # src/idf_component.yml and dependencies.lock are regenerated.
    esp32.add_idf_component(name="espressif/esp-dl", ref="3.3.8")
    comp_name, comp_ref = OBJECT_IDF_COMPONENTS[obj]
    esp32.add_idf_component(name=comp_name, ref=comp_ref)

    for conf in config.get(CONF_ON_DETECTION, []):
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
