import esphome.codegen as cg
from esphome.components import event
from esphome.const import CONF_DEVICE_CLASS, CONF_ICON, CONF_TYPE

from .. import CONF_LEVOIT_HUMIDIFIER_ID, typed_schema

# Long-presses on the humidifier, as reported by its controller. The stock firmware
# used them for Wi-Fi pairing, factory reset and factory test mode; here they are
# events you can automate on.
EVENT_TYPES = ["pairing", "factory_reset", "test_mode"]

TYPES = {
    "button": (None, {CONF_ICON: "mdi:gesture-tap-button", CONF_DEVICE_CLASS: "button"}),
}

CONFIG_SCHEMA = typed_schema(event.event_schema(), TYPES)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_LEVOIT_HUMIDIFIER_ID])
    var = await event.new_event(config, event_types=EVENT_TYPES)
    cg.add(hub.register_button_event(var))
