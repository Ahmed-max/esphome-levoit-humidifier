import esphome.codegen as cg
from esphome.components import switch
from esphome.const import CONF_ENTITY_CATEGORY, CONF_ICON, CONF_TYPE, ENTITY_CATEGORY_CONFIG

from .. import CONF_LEVOIT_HUMIDIFIER_ID, levoit_humidifier_ns, typed_schema

LevoitSwitch = levoit_humidifier_ns.class_("LevoitSwitch", switch.Switch)
SwitchType = levoit_humidifier_ns.enum("SwitchType", is_class=True)

TYPES = {
    "power": (SwitchType.POWER, {CONF_ICON: "mdi:power"}),
    "display": (
        SwitchType.DISPLAY,
        {CONF_ICON: "mdi:numeric", CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_CONFIG},
    ),
    "auto_stop": (
        SwitchType.AUTO_STOP,
        {CONF_ICON: "mdi:water-off", CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_CONFIG},
    ),
}

CONFIG_SCHEMA = typed_schema(switch.switch_schema(LevoitSwitch), TYPES)


async def to_code(config):
    kind, _ = TYPES[config[CONF_TYPE]]
    hub = await cg.get_variable(config[CONF_LEVOIT_HUMIDIFIER_ID])
    var = await switch.new_switch(config)
    cg.add(var.set_parent(hub))
    cg.add(var.set_type(kind))
    cg.add(hub.register_switch(kind, var))
