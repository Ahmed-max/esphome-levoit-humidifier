import esphome.codegen as cg
from esphome.components import number
from esphome.const import (
    CONF_ICON,
    CONF_TYPE,
    CONF_UNIT_OF_MEASUREMENT,
    UNIT_MINUTE,
    UNIT_PERCENT,
)

from .. import CONF_LEVOIT_HUMIDIFIER_ID, levoit_humidifier_ns, typed_schema

LevoitNumber = levoit_humidifier_ns.class_("LevoitNumber", number.Number)
NumberType = levoit_humidifier_ns.enum("NumberType", is_class=True)

# type -> (enum, min, max, step, defaults)
TYPES = {
    # setting a target switches the humidifier to auto mode
    "target_humidity": (
        NumberType.TARGET_HUMIDITY,
        30,
        80,
        1,
        {CONF_ICON: "mdi:water-percent", CONF_UNIT_OF_MEASUREMENT: UNIT_PERCENT},
    ),
    # setting a mist level switches the humidifier to manual mode
    "mist_level": (
        NumberType.MIST_LEVEL,
        1,
        2,
        1,
        {CONF_ICON: "mdi:weather-fog"},
    ),
    # minutes until the power flips; shows the remaining time, 0 = off
    "timer": (
        NumberType.TIMER,
        0,
        1440,
        1,
        {CONF_ICON: "mdi:timer-outline", CONF_UNIT_OF_MEASUREMENT: UNIT_MINUTE},
    ),
}

CONFIG_SCHEMA = typed_schema(number.number_schema(LevoitNumber), TYPES)


async def to_code(config):
    kind, min_value, max_value, step, _ = TYPES[config[CONF_TYPE]]
    hub = await cg.get_variable(config[CONF_LEVOIT_HUMIDIFIER_ID])
    var = await number.new_number(config, min_value=min_value, max_value=max_value, step=step)
    cg.add(var.set_parent(hub))
    cg.add(var.set_type(kind))
    cg.add(hub.register_number(kind, var))
