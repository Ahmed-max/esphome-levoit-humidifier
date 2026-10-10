import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
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
        }
    )
    .extend(cv.polling_component_schema("5s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "levoit_humidifier", baud_rate=9600, require_tx=True, require_rx=True
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
