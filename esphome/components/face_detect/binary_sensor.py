import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_OCCUPANCY,
    ICON_ACCOUNT,
)
from esphome.types import ConfigType

from . import CONF_FACE_DETECT_ID, FaceDetect

DEPENDENCIES = ["face_detect"]

CONF_PRESENCE = "presence"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
        cv.GenerateID(CONF_FACE_DETECT_ID): cv.use_id(FaceDetect),
        cv.Optional(CONF_PRESENCE): binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_OCCUPANCY,
            icon=ICON_ACCOUNT,
        ),
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_FACE_DETECT_ID])
    if presence_config := config.get(CONF_PRESENCE):
        sens = await binary_sensor.new_binary_sensor(presence_config)
        cg.add(hub.set_presence_binary_sensor(sens))
