import esphome.codegen as cg
from esphome.components import button
from esphome.const import CONF_ENTITY_CATEGORY, CONF_ICON, CONF_TYPE, ENTITY_CATEGORY_CONFIG

from .. import CONF_LEVOIT_HUMIDIFIER_ID, levoit_humidifier_ns, typed_schema

LevoitButton = levoit_humidifier_ns.class_("LevoitButton", button.Button)
ButtonType = levoit_humidifier_ns.enum("ButtonType", is_class=True)

TYPES = {
    "reset_clean_reminder": (
        ButtonType.RESET_CLEAN_REMINDER,
        {CONF_ICON: "mdi:spray-bottle", CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_CONFIG},
    ),
}

CONFIG_SCHEMA = typed_schema(button.button_schema(LevoitButton), TYPES)


async def to_code(config):
    kind, _ = TYPES[config[CONF_TYPE]]
    hub = await cg.get_variable(config[CONF_LEVOIT_HUMIDIFIER_ID])
    var = await button.new_button(config)
    cg.add(var.set_parent(hub))
    cg.add(var.set_type(kind))
