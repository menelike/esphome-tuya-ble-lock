import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC
from .. import TuyaLock, CONF_TUYA_LOCK_ID

DEPENDENCIES = ["tuya_lock"]

CONF_LAST_STATUS = "last_status"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_TUYA_LOCK_ID): cv.use_id(TuyaLock),
        # Summary published on each status read, e.g. "battery 70% · @342s". The @Ns uptime
        # suffix makes the value differ every press, so it shows a fresh row in the Home Assistant
        # logbook / device activity (Home Assistant only logs state *changes*).
        cv.Optional(CONF_LAST_STATUS): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TUYA_LOCK_ID])
    if CONF_LAST_STATUS in config:
        s = await text_sensor.new_text_sensor(config[CONF_LAST_STATUS])
        cg.add(parent.set_status_text_sensor(s))
