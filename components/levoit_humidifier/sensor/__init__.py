import esphome.codegen as cg
from esphome.components import sensor
from esphome.const import (
    CONF_ACCURACY_DECIMALS,
    CONF_DEVICE_CLASS,
    CONF_STATE_CLASS,
    CONF_TYPE,
    CONF_UNIT_OF_MEASUREMENT,
    DEVICE_CLASS_HUMIDITY,
    DEVICE_CLASS_TEMPERATURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
    UNIT_PERCENT,
)

from .. import CONF_LEVOIT_HUMIDIFIER_ID, levoit_humidifier_ns, typed_schema

SensorType = levoit_humidifier_ns.enum("SensorType", is_class=True)

TYPES = {
    "humidity": (
        SensorType.HUMIDITY,
        {
            CONF_UNIT_OF_MEASUREMENT: UNIT_PERCENT,
            CONF_DEVICE_CLASS: DEVICE_CLASS_HUMIDITY,
            CONF_STATE_CLASS: STATE_CLASS_MEASUREMENT,
            CONF_ACCURACY_DECIMALS: 0,
        },
    ),
    "temperature": (
        SensorType.TEMPERATURE,
        {
            CONF_UNIT_OF_MEASUREMENT: UNIT_CELSIUS,
            CONF_DEVICE_CLASS: DEVICE_CLASS_TEMPERATURE,
            CONF_STATE_CLASS: STATE_CLASS_MEASUREMENT,
            CONF_ACCURACY_DECIMALS: 0,
        },
    ),
}

CONFIG_SCHEMA = typed_schema(sensor.sensor_schema(), TYPES)


async def to_code(config):
    kind, _ = TYPES[config[CONF_TYPE]]
    hub = await cg.get_variable(config[CONF_LEVOIT_HUMIDIFIER_ID])
    var = await sensor.new_sensor(config)
    cg.add(hub.register_sensor(kind, var))
