import esphome.codegen as cg
from esphome.components import binary_sensor
from esphome.const import (
    CONF_DEVICE_CLASS,
    CONF_ICON,
    CONF_TYPE,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_RUNNING,
)

from .. import CONF_LEVOIT_HUMIDIFIER_ID, levoit_humidifier_ns, typed_schema

BinarySensorType = levoit_humidifier_ns.enum("BinarySensorType", is_class=True)

TYPES = {
    "water_tank_lifted": (
        BinarySensorType.WATER_TANK_LIFTED,
        {CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM, CONF_ICON: "mdi:cup-water"},
    ),
    "low_water": (
        BinarySensorType.LOW_WATER,
        {CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM, CONF_ICON: "mdi:water-alert"},
    ),
    "mist_active": (
        BinarySensorType.MIST_ACTIVE,
        {CONF_DEVICE_CLASS: DEVICE_CLASS_RUNNING, CONF_ICON: "mdi:weather-fog"},
    ),
    # target humidity reached and the humidifier paused itself (auto off)
    "auto_stopped": (
        BinarySensorType.AUTO_STOPPED,
        {CONF_ICON: "mdi:water-check"},
    ),
    # tank-cleaning reminder kept by the humidifier controller (MCU 2.0.1 and later)
    "clean_reminder": (
        BinarySensorType.CLEAN_REMINDER,
        {CONF_DEVICE_CLASS: DEVICE_CLASS_PROBLEM, CONF_ICON: "mdi:spray-bottle"},
    ),
}

CONFIG_SCHEMA = typed_schema(binary_sensor.binary_sensor_schema(), TYPES)


async def to_code(config):
    kind, _ = TYPES[config[CONF_TYPE]]
    hub = await cg.get_variable(config[CONF_LEVOIT_HUMIDIFIER_ID])
    var = await binary_sensor.new_binary_sensor(config)
    cg.add(hub.register_binary_sensor(kind, var))
