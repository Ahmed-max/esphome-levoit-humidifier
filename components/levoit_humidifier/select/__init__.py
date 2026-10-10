import esphome.codegen as cg
from esphome.components import select
from esphome.const import CONF_ICON, CONF_TYPE

from .. import CONF_LEVOIT_HUMIDIFIER_ID, levoit_humidifier_ns, typed_schema

LevoitSelect = levoit_humidifier_ns.class_("LevoitSelect", select.Select)
SelectType = levoit_humidifier_ns.enum("SelectType", is_class=True)

# option order matches the C++ indexes: 0 = manual, 1 = auto
TYPES = {
    "mode": (SelectType.MODE, ["Manual", "Auto"], {CONF_ICON: "mdi:auto-mode"}),
}

CONFIG_SCHEMA = typed_schema(select.select_schema(LevoitSelect), TYPES)


async def to_code(config):
    kind, options, _ = TYPES[config[CONF_TYPE]]
    hub = await cg.get_variable(config[CONF_LEVOIT_HUMIDIFIER_ID])
    var = await select.new_select(config, options=options)
    cg.add(var.set_parent(hub))
    cg.add(var.set_type(kind))
    cg.add(hub.register_select(kind, var))
