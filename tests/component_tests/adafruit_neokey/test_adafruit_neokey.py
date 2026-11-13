"""Configuration and code generation for independently addressed NeoKey boards."""

from collections.abc import Callable
from pathlib import Path

import pytest

from esphome import config_validation as cv, final_validate as fv
from esphome.components.adafruit_neokey import CONFIG_SCHEMA, _final_validate
from esphome.components.adafruit_neokey.binary_sensor import (
    CONFIG_SCHEMA as KEY_SCHEMA,
    _final_validate as validate_key,
)
from esphome.types import ConfigType


def hub(**options: object) -> ConfigType:
    return CONFIG_SCHEMA({"id": "neokey_a", "i2c_id": "bus_a", **options})


def key(**options: object) -> ConfigType:
    return KEY_SCHEMA(
        {"name": "Key", "adafruit_neokey_id": "neokey_a", "key": 1, **options}
    )


@pytest.mark.parametrize("address", [0x30, 0x31, 0x3F])
def test_address_range(address: int) -> None:
    assert hub(address=address)["address"] == address


@pytest.mark.parametrize("address", [0x2F, 0x40])
def test_invalid_address(address: int) -> None:
    with pytest.raises(cv.Invalid):
        hub(address=address)


@pytest.mark.parametrize("number", [1, 2, 3, 4])
def test_key_number(number: int) -> None:
    assert key(key=number)["key"] == number


@pytest.mark.parametrize("number", [0, 5])
def test_invalid_key_number(number: int) -> None:
    with pytest.raises(cv.Invalid):
        key(key=number)


@pytest.mark.parametrize("delay", ["1ms", "8ms", "100ms"])
def test_supported_delays(delay: str) -> None:
    assert hub(read_delay=delay)["read_delay"].total_milliseconds >= 1


@pytest.mark.parametrize("delay", ["0ms", "250us", "101ms"])
def test_invalid_delays(delay: str) -> None:
    with pytest.raises(cv.Invalid):
        hub(read_delay=delay)


def test_default_timing() -> None:
    config = hub()
    assert config["update_interval"].total_milliseconds == 16
    assert config["read_delay"].total_milliseconds == 8


def test_zero_polling_interval_is_rejected() -> None:
    with pytest.raises(cv.Invalid):
        hub(update_interval="0ms")


def test_duplicate_address_on_same_bus_is_rejected() -> None:
    first, second = hub(), hub(id="neokey_b")
    fv.full_config.get()["adafruit_neokey"] = [first, second]
    with pytest.raises(cv.Invalid, match="unique address"):
        _final_validate(first)


def test_same_address_on_different_buses_is_valid() -> None:
    first, second = hub(), hub(id="neokey_b", i2c_id="bus_b")
    fv.full_config.get()["adafruit_neokey"] = [first, second]
    _final_validate(first)
    _final_validate(second)


def test_duplicate_key_on_same_board_is_rejected() -> None:
    first, second = key(), key(name="Duplicate")
    first["platform"] = second["platform"] = "adafruit_neokey"
    fv.full_config.get()["binary_sensor"] = [first, second]
    with pytest.raises(cv.Invalid, match="once per NeoKey"):
        validate_key(first)


def test_same_key_on_different_boards_is_valid() -> None:
    first, second = key(), key(name="Other Key", adafruit_neokey_id="neokey_b")
    first["platform"] = second["platform"] = "adafruit_neokey"
    fv.full_config.get()["binary_sensor"] = [first, second]
    validate_key(first)
    validate_key(second)


def test_codegen_keeps_boards_and_keys_separate(
    generate_main: Callable[[Path], str], tmp_path: Path
) -> None:
    common = Path(__file__).parents[2] / "components/adafruit_neokey/common.yaml"
    config = tmp_path / "neokey.yaml"
    config.write_text(
        "esphome:\n  name: neokey-test\nesp32:\n  variant: esp32s3\n"
        "  framework:\n    type: esp-idf\nlogger:\ni2c:\n  id: i2c_bus\n"
        "  sda: GPIO8\n  scl: GPIO9\n" + common.read_text(encoding="utf-8"),
        encoding="utf-8",
    )
    main = generate_main(config)
    for board in ("a", "b"):
        assert f"neokey_{board}->set_key_sensor" in main
    assert "set_read_delay(1)" in main
    assert "set_read_delay(8)" in main
    assert "set_update_interval(16)" in main
    assert "set_update_interval(20)" in main
    assert main.count("set_key_sensor(") == 8
