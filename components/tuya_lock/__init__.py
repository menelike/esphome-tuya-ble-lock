import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.automation as automation
from esphome.components import ble_client
from esphome.const import CONF_ID

CODEOWNERS = ["@menelike"]
DEPENDENCIES = ["ble_client"]
MULTI_CONF = True

tuya_lock_ns = cg.esphome_ns.namespace("tuya_lock")
TuyaLock = tuya_lock_ns.class_("TuyaLock", cg.Component, ble_client.BLEClientNode)

CONF_LOCAL_KEY = "local_key"
CONF_UUID = "uuid"
CONF_DEVICE_ID = "device_id"
CONF_PASSCODE = "passcode"
CONF_ON_SUCCESS = "on_success"
CONF_ON_ERROR = "on_error"
CONF_STATUS_ON_BOOT = "status_on_boot"
# shared by the sensor / text_sensor / button sub-platforms
CONF_TUYA_LOCK_ID = "tuya_lock_id"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TuyaLock),
            # local_key must be >= 6 chars — login_key = md5(local_key[:6])
            cv.Required(CONF_LOCAL_KEY): cv.All(cv.string, cv.Length(min=6)),
            cv.Required(CONF_UUID): cv.All(cv.string, cv.Length(min=1)),
            cv.Required(CONF_DEVICE_ID): cv.All(cv.string, cv.Length(min=1)),
            # passcode is the lock's numeric code
            cv.Required(CONF_PASSCODE): cv.All(cv.string, cv.Length(min=1)),
            cv.Optional(CONF_STATUS_ON_BOOT, default=True): cv.boolean,
            cv.Optional(CONF_ON_SUCCESS): automation.validate_automation(single=True),
            cv.Optional(CONF_ON_ERROR): automation.validate_automation(single=True),
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    cg.add(var.set_local_key(config[CONF_LOCAL_KEY]))
    cg.add(var.set_uuid(config[CONF_UUID]))
    cg.add(var.set_device_id(config[CONF_DEVICE_ID]))
    cg.add(var.set_passcode(config[CONF_PASSCODE]))
    cg.add(var.set_status_on_boot(config[CONF_STATUS_ON_BOOT]))

    if CONF_ON_SUCCESS in config:
        await automation.build_automation(
            var.get_success_trigger(),
            [(cg.std_string, "x")],
            config[CONF_ON_SUCCESS],
        )
    if CONF_ON_ERROR in config:
        await automation.build_automation(
            var.get_error_trigger(),
            [(cg.std_string, "x")],
            config[CONF_ON_ERROR],
        )
