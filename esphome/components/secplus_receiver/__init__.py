"""
ESPHome external component: secplus_receiver

Decodes Security+ 2.0 (Manchester-encoded OOK) transmissions from
Chamberlain / LiftMaster / Craftsman garage door remotes using a CC1101
transceiver and the remote_receiver component.

The component attaches itself directly to a remote_receiver as a listener
(no on_raw lambda required), matching the structure of the acurite component:
https://github.com/swoboda1337/acurite-esphome

On first build this module downloads secplus.c and secplus.h from the
argilo/secplus repository (master branch) into the component directory.

Minimal YAML usage:

    remote_receiver:
      pin: GPIO2

    secplus_receiver:
      remote_id_sensor:
        name: "Remote ID"
      rolling_code_sensor:
        name: "Rolling Code"
"""

import logging
import os
import urllib.request

import esphome.codegen as cg
from esphome.components import remote_base, remote_receiver, text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID

_LOGGER = logging.getLogger(__name__)

DOMAIN = "secplus_receiver"
CODEOWNERS = ["@greenbus4"]
DEPENDENCIES = ["remote_receiver", "text_sensor"]
AUTO_LOAD = ["text_sensor"]

secplus_ns = cg.esphome_ns.namespace("secplus_receiver")
SecplusReceiverComponent = secplus_ns.class_("SecplusReceiverComponent", cg.Component)

CONF_REMOTE_ID_SENSOR = "remote_id_sensor"
CONF_ROLLING_CODE_SENSOR = "rolling_code_sensor"

# ── Upstream C library ────────────────────────────────────────────────────────
# argilo/secplus has no versioned releases; we track master.
# To pin to a specific commit, replace "master" with a SHA, e.g. "f62ed51".
SECPLUS_COMMIT = "master"
SECPLUS_BASE_URL = f"https://raw.githubusercontent.com/argilo/secplus/{SECPLUS_COMMIT}/src/"
SECPLUS_FILES = ["secplus.c", "secplus.h"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SecplusReceiverComponent),
        cv.GenerateID(remote_base.CONF_RECEIVER_ID): cv.use_id(
            remote_receiver.RemoteReceiverComponent
        ),
        cv.Optional(CONF_REMOTE_ID_SENSOR): text_sensor.text_sensor_schema(
            icon="mdi:remote",
        ),
        cv.Optional(CONF_ROLLING_CODE_SENSOR): text_sensor.text_sensor_schema(
            icon="mdi:counter",
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


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


async def to_code(config: dict) -> None:
    # Fetch the upstream C library before the compiler runs.
    _fetch_secplus_sources()

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await remote_base.register_listener(var, config)

    if remote_id_config := config.get(CONF_REMOTE_ID_SENSOR):
        sens = await text_sensor.new_text_sensor(remote_id_config)
        cg.add(var.set_remote_id_sensor(sens))

    if rolling_config := config.get(CONF_ROLLING_CODE_SENSOR):
        sens = await text_sensor.new_text_sensor(rolling_config)
        cg.add(var.set_rolling_code_sensor(sens))
