import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_DIAGNOSTIC
from esphome.types import ConfigType

from . import CONF_MODEL, CONF_OBJECT_DETECT_ID, ObjectDetect

DEPENDENCIES = ["object_detect"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
        cv.GenerateID(CONF_OBJECT_DETECT_ID): cv.use_id(ObjectDetect),
        cv.Optional(CONF_MODEL): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:chip",
        ),
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_OBJECT_DETECT_ID])
    if model_config := config.get(CONF_MODEL):
        sens = await text_sensor.new_text_sensor(model_config)
        cg.add(hub.set_model_text_sensor(sens))
