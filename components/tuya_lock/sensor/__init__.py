import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_BATTERY_LEVEL,
    DEVICE_CLASS_BATTERY,
    STATE_CLASS_MEASUREMENT,
    UNIT_PERCENT,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from .. import TuyaLock, CONF_TUYA_LOCK_ID

DEPENDENCIES = ["tuya_lock"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TUYA_LOCK_ID): cv.use_id(TuyaLock),
        cv.Optional(CONF_BATTERY_LEVEL): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TUYA_LOCK_ID])
    if CONF_BATTERY_LEVEL in config:
        s = await sensor.new_sensor(config[CONF_BATTERY_LEVEL])
        cg.add(parent.set_battery_sensor(s))
