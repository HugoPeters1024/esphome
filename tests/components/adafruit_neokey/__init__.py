import esphome.codegen as cg
from esphome.types import ConfigType
from tests.testing_helpers import ComponentManifestOverride


def override_manifest(manifest: ComponentManifestOverride) -> None:
    manifest.multi_conf = False

    async def to_code(config: ConfigType) -> None:
        cg.add_define("USE_ADAFRUIT_NEOKEY_BINARY_SENSOR")

    manifest.to_code = to_code
