import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from .. import TuyaLock, tuya_lock_ns, CONF_TUYA_LOCK_ID

CONF_ACTION = "action"

UnlockButton = tuya_lock_ns.class_("UnlockButton", button.Button)
LockButton = tuya_lock_ns.class_("LockButton", button.Button)
StatusButton = tuya_lock_ns.class_("StatusButton", button.Button)

ACTIONS = {
    "unlock": UnlockButton,
    "lock": LockButton,
    "status": StatusButton,
}

CONFIG_SCHEMA = cv.typed_schema(
    {
        action: button.button_schema(cls).extend(
            {cv.GenerateID(CONF_TUYA_LOCK_ID): cv.use_id(TuyaLock)}
        )
        for action, cls in ACTIONS.items()
    },
    key=CONF_ACTION,
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_TUYA_LOCK_ID])
    var = await button.new_button(config)
    await cg.register_parented(var, parent)
