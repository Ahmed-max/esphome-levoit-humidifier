import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
import esphome.final_validate as fv
from esphome.const import CONF_ID, CONF_TYPE

CODEOWNERS = ["@Ahmed-max"]
DEPENDENCIES = ["uart"]
# the hub publishes to every entity type, so their base classes are always built
AUTO_LOAD = [
    "switch",
    "number",
    "select",
    "sensor",
    "binary_sensor",
    "text_sensor",
    "button",
]
MULTI_CONF = True

CONF_LEVOIT_HUMIDIFIER_ID = "levoit_humidifier_id"
CONF_MODEL = "model"
CONF_MQTT_HUMIDIFIER = "mqtt_humidifier"
CONF_MQTT_DISCOVERY_PREFIX = "mqtt_discovery_prefix"

MODELS = ["DUAL200S"]

levoit_humidifier_ns = cg.esphome_ns.namespace("levoit_humidifier")
LevoitHumidifier = levoit_humidifier_ns.class_(
    "LevoitHumidifier", cg.PollingComponent, uart.UARTDevice
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LevoitHumidifier),
            cv.Optional(CONF_MODEL, default="DUAL200S"): cv.one_of(*MODELS, upper=True),
            # Home Assistant humidifier entity over MQTT discovery (needs the mqtt: component)
            cv.Optional(CONF_MQTT_HUMIDIFIER, default=False): cv.boolean,
            cv.Optional(CONF_MQTT_DISCOVERY_PREFIX, default="homeassistant"): cv.publish_topic,
        }
    )
    .extend(cv.polling_component_schema("5s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

def _final_validate_mqtt(config):
    if config[CONF_MQTT_HUMIDIFIER] and "mqtt" not in fv.full_config.get():
        raise cv.Invalid(
            f"{CONF_MQTT_HUMIDIFIER} needs the mqtt: component (with discovery: false, so "
            "ESPHome does not announce every entity a second time)"
        )
    return config


FINAL_VALIDATE_SCHEMA = cv.All(
    uart.final_validate_device_schema(
        "levoit_humidifier", baud_rate=9600, require_tx=True, require_rx=True
    ),
    _final_validate_mqtt,
)

HUB_SCHEMA = cv.Schema(
    {cv.GenerateID(CONF_LEVOIT_HUMIDIFIER_ID): cv.use_id(LevoitHumidifier)}
)


def typed_schema(base_schema, types):
    """Entity schema with a required `type:`; each type's defaults (last tuple
    element in `types`) are filled in before validation, unless the user set them."""
    schema = base_schema.extend(HUB_SCHEMA).extend(
        {cv.Required(CONF_TYPE): cv.one_of(*types, lower=True)}
    )

    def validate(config):
        kind = str(config.get(CONF_TYPE, "")).lower()
        if kind in types:
            config = {**types[kind][-1], **config}
        return schema(config)

    return validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    if config[CONF_MQTT_HUMIDIFIER]:
        cg.add_define("USE_LEVOIT_MQTT_HUMIDIFIER")
        cg.add(var.set_mqtt_discovery_prefix(config[CONF_MQTT_DISCOVERY_PREFIX]))
