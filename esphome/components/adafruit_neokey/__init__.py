import esphome.codegen as cg
from esphome.components import i2c
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_I2C_ID,
    CONF_ID as CONF_ID,
    CONF_UPDATE_INTERVAL,
)
import esphome.final_validate as fv
from esphome.types import ConfigType

CODEOWNERS = ["@HugoPeters1024"]
DEPENDENCIES = ["i2c"]
DOMAIN = "adafruit_neokey"
MULTI_CONF = True

CONF_ADAFRUIT_NEOKEY_ID = "adafruit_neokey_id"
CONF_READ_DELAY = "read_delay"
adafruit_neokey_ns = cg.esphome_ns.namespace(DOMAIN)
AdafruitNeoKey = adafruit_neokey_ns.class_(
    "AdafruitNeoKey", cg.Component, i2c.I2CDevice
)
KEY_PUBLISHER = cg.HubFunction(
    "keys",
    cg.void,
    [(cg.uint8, "pressed"), (cg.uint8, "changed")],
    define="USE_ADAFRUIT_NEOKEY_BINARY_SENSOR",
)
DUMP_PUBLISHER = cg.HubFunction("dump", cg.void, [(cg.const_char_ptr, "tag")])

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AdafruitNeoKey),
            cv.Optional(CONF_UPDATE_INTERVAL, default="16ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(milliseconds=1)),
            ),
            cv.Optional(
                CONF_READ_DELAY, default="8ms", visibility=cv.Visibility.ADVANCED
            ): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=cv.TimePeriod(milliseconds=1),
                    max=cv.TimePeriod(milliseconds=100),
                ),
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x30))
    .extend(
        {
            cv.Optional(CONF_ADDRESS, default=0x30): cv.All(
                cv.i2c_address, cv.Range(min=0x30, max=0x3F)
            )
        }
    )
)


def _final_validate(config: ConfigType) -> None:
    boards = fv.full_config.get()[DOMAIN]
    matches = sum(
        board[CONF_I2C_ID] == config[CONF_I2C_ID]
        and board[CONF_ADDRESS] == config[CONF_ADDRESS]
        for board in boards
    )
    if matches > 1:
        raise cv.Invalid("Each NeoKey on the same I2C bus must have a unique address")


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config: ConfigType) -> None:
    var = await i2c.new_i2c_component(config)
    cg.add(var.set_read_delay(config[CONF_READ_DELAY]))
    cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))
