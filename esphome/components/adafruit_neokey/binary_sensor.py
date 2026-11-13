from esphome.automation import parent_ref
import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_BINARY_SENSOR, CONF_KEY, CONF_PLATFORM
import esphome.final_validate as fv
from esphome.types import ConfigType

from . import (
    CONF_ADAFRUIT_NEOKEY_ID,
    DOMAIN,
    DUMP_PUBLISHER,
    KEY_PUBLISHER,
    AdafruitNeoKey,
)

DEPENDENCIES = [DOMAIN]

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema().extend(
    {
        cv.GenerateID(CONF_ADAFRUIT_NEOKEY_ID): cv.use_id(AdafruitNeoKey),
        cv.Required(CONF_KEY): cv.int_range(min=1, max=4),
    }
)


def _final_validate(config: ConfigType) -> None:
    matches = sum(
        sensor.get(CONF_PLATFORM) == DOMAIN
        and sensor[CONF_ADAFRUIT_NEOKEY_ID] == config[CONF_ADAFRUIT_NEOKEY_ID]
        and sensor[CONF_KEY] == config[CONF_KEY]
        for sensor in fv.full_config.get().get(CONF_BINARY_SENSOR, [])
    )
    if matches > 1:
        raise cv.Invalid("Each key can only be configured once per NeoKey")


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config: ConfigType) -> None:
    var = await binary_sensor.new_binary_sensor(config)
    hub = await cg.get_variable(config[CONF_ADAFRUIT_NEOKEY_ID])
    mask = 1 << (config[CONF_KEY] - 1)
    changed = KEY_PUBLISHER.param("changed") & mask
    publish = parent_ref(var).publish_state(KEY_PUBLISHER.param("pressed") & mask)
    KEY_PUBLISHER.add(hub, cg.RawStatement(f"if ({changed}) {{ {publish}; }}"))
    DUMP_PUBLISHER.add(
        hub,
        cg.RawStatement(
            f'binary_sensor::log_binary_sensor({DUMP_PUBLISHER.param("tag")}, "  ", "Key", {parent_ref(var)});'
        ),
    )
