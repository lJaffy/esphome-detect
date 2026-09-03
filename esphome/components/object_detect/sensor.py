import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_COUNT,
    CONF_ID,
    STATE_CLASS_MEASUREMENT,
)
from esphome.types import ConfigType

from . import CONF_OBJECT_DETECT_ID, ObjectDetect

DEPENDENCIES = ["object_detect"]

CONF_BOX_X = "box_x"
CONF_BOX_Y = "box_y"
CONF_BOX_W = "box_w"
CONF_BOX_H = "box_h"
CONF_SCORE = "score"

PIXEL_SCHEMA = sensor.sensor_schema(
    state_class=STATE_CLASS_MEASUREMENT,
    accuracy_decimals=0,
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
        cv.GenerateID(CONF_OBJECT_DETECT_ID): cv.use_id(ObjectDetect),
        cv.Optional(CONF_BOX_X): PIXEL_SCHEMA,
        cv.Optional(CONF_BOX_Y): PIXEL_SCHEMA,
        cv.Optional(CONF_BOX_W): PIXEL_SCHEMA,
        cv.Optional(CONF_BOX_H): PIXEL_SCHEMA,
        cv.Optional(CONF_SCORE): sensor.sensor_schema(
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=3,
        ),
        cv.Optional(CONF_COUNT): sensor.sensor_schema(
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
        ),
    }
)


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_OBJECT_DETECT_ID])
    if (box_x_config := config.get(CONF_BOX_X)) is not None:
        sens = await sensor.new_sensor(box_x_config)
        cg.add(hub.set_box_x_sensor(sens))
    if (box_y_config := config.get(CONF_BOX_Y)) is not None:
        sens = await sensor.new_sensor(box_y_config)
        cg.add(hub.set_box_y_sensor(sens))
    if (box_w_config := config.get(CONF_BOX_W)) is not None:
        sens = await sensor.new_sensor(box_w_config)
        cg.add(hub.set_box_w_sensor(sens))
    if (box_h_config := config.get(CONF_BOX_H)) is not None:
        sens = await sensor.new_sensor(box_h_config)
        cg.add(hub.set_box_h_sensor(sens))
    if (score_config := config.get(CONF_SCORE)) is not None:
        sens = await sensor.new_sensor(score_config)
        cg.add(hub.set_score_sensor(sens))
    if (count_config := config.get(CONF_COUNT)) is not None:
        sens = await sensor.new_sensor(count_config)
        cg.add(hub.set_count_sensor(sens))
