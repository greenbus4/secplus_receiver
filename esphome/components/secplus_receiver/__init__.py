"""
ESPHome external component: secplus_receiver

Decodes Security+ 2.0 (Manchester-encoded OOK) transmissions from
Chamberlain / LiftMaster / Craftsman garage door remotes using a CC1101
transceiver and the remote_receiver component.

On first build this module downloads secplus.c and secplus.h from the
argilo/secplus repository (master branch) and places them in a small
PlatformIO library (libsecplus/) inside the build tree. This keeps them
off PlatformIO's src/ compilation glob so the linker sees each symbol
exactly once.

Minimal YAML usage:

    secplus_receiver:
      id: my_receiver
      remote_id_sensor:
        name: "Remote ID"
      rolling_code_sensor:
        name: "Rolling Code"

    remote_receiver:
      pin: GPIO2
      dump: []
      on_raw:
        - lambda: id(my_receiver).process(x);
"""

import urllib.request
import os
import logging

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import CONF_ID
from esphome.core import CORE

_LOGGER = logging.getLogger(__name__)

# ── Component identity ────────────────────────────────────────────────────────

DOMAIN = "secplus_receiver"
CODEOWNERS = []
DEPENDENCIES = ["text_sensor"]
AUTO_LOAD = ["text_sensor"]

secplus_ns = cg.esphome_ns.namespace("secplus_receiver")
SecplusReceiverComponent = secplus_ns.class_(
    "SecplusReceiverComponent", cg.Component
)

# ── Config keys ───────────────────────────────────────────────────────────────

CONF_REMOTE_ID_SENSOR = "remote_id_sensor"
CONF_ROLLING_CODE_SENSOR = "rolling_code_sensor"

# ── Upstream C library ────────────────────────────────────────────────────────

# argilo/secplus has no versioned releases; we track master.
# To pin to a specific commit, replace "master" with a SHA, e.g. "f62ed51".
SECPLUS_COMMIT = "master"
SECPLUS_BASE_URL = (
    f"https://raw.githubusercontent.com/argilo/secplus/{SECPLUS_COMMIT}/src/"
)
SECPLUS_FILES = ["secplus.c", "secplus.h"]

# ── Config schema ─────────────────────────────────────────────────────────────

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SecplusReceiverComponent),
        cv.Optional(CONF_REMOTE_ID_SENSOR): text_sensor.text_sensor_schema(
            icon="mdi:remote",
        ),
        cv.Optional(CONF_ROLLING_CODE_SENSOR): text_sensor.text_sensor_schema(
            icon="mdi:counter",
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


# ── Build-time helpers ────────────────────────────────────────────────────────

def _get_lib_dir() -> str:
    """
    Return (and create) a libsecplus/ directory inside the PlatformIO build
    tree for the current node.

    Placing the downloaded sources here means they are compiled as a library
    — with their own translation unit — and are NOT picked up by PlatformIO's
    src_filter glob that sweeps src/*.c.  That eliminates the duplicate-symbol
    linker error that would occur if secplus.c lived inside the component's
    own directory (which ESPHome mirrors into src/).
    """
    build_dir = CORE.build_path          # e.g. /config/.esphome/build/<node>
    lib_dir = os.path.join(build_dir, "lib", "libsecplus")
    os.makedirs(lib_dir, exist_ok=True)
    return lib_dir


def _fetch_secplus_sources() -> str:
    """
    Download secplus.c and secplus.h into the build-tree library directory.
    Returns the library directory path.
    Skips files that are already present.
    """
    lib_dir = _get_lib_dir()
    for filename in SECPLUS_FILES:
        dest = os.path.join(lib_dir, filename)
        if os.path.isfile(dest):
            _LOGGER.debug(
                "secplus_receiver: %s already present at %s", filename, dest
            )
            continue
        url = SECPLUS_BASE_URL + filename
        _LOGGER.info("secplus_receiver: downloading %s → %s", url, dest)
        try:
            urllib.request.urlretrieve(url, dest)
        except Exception as exc:  # noqa: BLE001
            raise RuntimeError(
                f"secplus_receiver: failed to download {filename} from {url}: {exc}\n"
                "Check your internet connection, or manually place secplus.c and "
                f"secplus.h in:\n  {lib_dir}"
            ) from exc
    return lib_dir


# ── ESPHome hooks ─────────────────────────────────────────────────────────────

async def to_code(config: dict) -> None:
    # Download upstream sources into the build-tree lib/ directory.
    lib_dir = _fetch_secplus_sources()

    # Register libsecplus as a PlatformIO library so the build system
    # compiles it as a separate static archive — not as part of src/.
    # The "symlink" format (file://) works for local absolute paths.
    cg.add_library("libsecplus", None, f"file://{lib_dir}")

    # Instantiate the C++ component.
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Wire up whichever sensors the user declared.
    if remote_id_config := config.get(CONF_REMOTE_ID_SENSOR):
        sens = await text_sensor.new_text_sensor(remote_id_config)
        cg.add(var.set_remote_id_sensor(sens))

    if rolling_config := config.get(CONF_ROLLING_CODE_SENSOR):
        sens = await text_sensor.new_text_sensor(rolling_config)
        cg.add(var.set_rolling_code_sensor(sens))
