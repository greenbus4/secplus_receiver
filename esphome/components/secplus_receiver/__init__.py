"""
ESPHome external component: secplus_receiver

Decodes Security+ 2.0 (Manchester-encoded OOK) transmissions from
Chamberlain / LiftMaster / Craftsman garage door remotes using a CC1101
transceiver and the remote_receiver component.

On first build this module downloads secplus.c and secplus.h from the
argilo/secplus repository (master branch) into the component directory.

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

def _fetch_secplus_sources() -> None:
    """Download secplus.c and secplus.h from GitHub if not already present."""
    component_dir = os.path.dirname(__file__)
    for filename in SECPLUS_FILES:
        dest = os.path.join(component_dir, filename)
        if os.path.isfile(dest):
            _LOGGER.debug(
                "secplus_receiver: %s already present, skipping download", filename
            )
            continue
        url = SECPLUS_BASE_URL + filename
        _LOGGER.info("secplus_receiver: downloading %s", url)
        try:
            urllib.request.urlretrieve(url, dest)
        except Exception as exc:  # noqa: BLE001
            raise RuntimeError(
                f"secplus_receiver: failed to download {filename} from {url}: {exc}\n"
                "Check your internet connection, or manually place secplus.c and "
                f"secplus.h in:\n  {component_dir}"
            ) from exc


# ── ESPHome hooks ─────────────────────────────────────────────────────────────

async def to_code(config: dict) -> None:
    # Fetch the upstream C library before the compiler runs.
    # Skips silently on subsequent builds when files already exist.
    _fetch_secplus_sources()

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
