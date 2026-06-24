# secplus_receiver — ESPHome External Component

An ESPHome external component that decodes **Security+ 2.0** rolling-code
transmissions from Chamberlain, LiftMaster, Craftsman and other knock-off garage door remotes
using a **CC1101** sub-GHz transceiver and ESPHome's `remote_receiver`.

Purpose: Remote transmitters are widely available, very inexpensive and can have
a good range. By knowing a remote's fixed identifier (as well as its rolling code) lights, gates, etc.
can be controlled by Home Assistant automations using these remotes.

Decoded results are exposed as Home Assistant sensor entities 
and/or via an event sent to Home Assistant.  

Zero or more text entities can be set up, and their names can be changed via a `substitutions`
secton in your YAML.  If using the `packages` approach (as shown below) only "Fixed Data"
and "Rolling Code" entities are set up.

| Entity Name | Value |
|---|---|
| **Fixed Data** | 40-bit fixed remote identifier |
| **Rolling Code** | Current rolling code counter |
| **Remote ID** | Attempted parse of the remote's specific ID |
| **Button ID** | Attempted parse of the individual button pressed |

The "Fixed Data" should be unique to the remote + specific button pushed.
The fixed data may be broken into two parts which typically holds the remote's ID
and the specific button pressed, but decoding those may be inconsistent across different
remote manufactures. That is why those entites are disabled by default.

This can also send an event to Home Assistant when a button press is detected. This is disabled by default.
The event is "esphome.secplus_received" and includes in the event data:
"fixed_data", "rolling_code", "remote_id", and "button". This is useful if you want to trigger on a button
press AND have all the data at the same time. (Triggering on a sensor may mean the other sensors are not in sync at
that instant.)

To enable events add this to your YAML:

```
secplus_receiver:
  fire_homeassistant_event: true
```

See [Notes](#notes) below on this being beta software and how AI was used.

---

## How it works

Security+ 2.0 transmits on 310 / 315 / 390 MHz using **OOK modulation** at
4000 baud with **Manchester encoding**. The CC1101 demodulates the OOK signal
on its `GDO0` pin; ESPHome's `remote_receiver` captures the raw pulse timing
and passes it to the decoder. The decoder assembles two-frame packet pairs and
calls [`decode_v2()`](https://github.com/argilo/secplus) from the
[argilo/secplus](https://github.com/argilo/secplus) C library to extract the
remote ID and rolling code.

```
CC1101 GDO0 ──► remote_receiver (on_raw) ──► id(receiver).process(x)
                                                      │
                                           SecplusReceiverComponent
                                           (secplus_receiver.h)
                                                      │
                                         secplus_parser.c + secplus.c
                                                      │
                                    ┌─────────────────┴────────────────┐
                                    │                                  │
                             remote_id_sensor                 rolling_code_sensor
                             (text_sensor)                    (text_sensor)
```

---

## Hardware

| Part | Notes |
|---|---|
| ESP32 (any variant) | Tested on ESP32-S3 |
| CC1101 module | 310 / 315 / 390 MHz |

### Wiring

| CC1101 pin | ESP32 pin | Notes |
|---|---|---|
| VCC | 3.3 V | |
| GND | GND | |
| MOSI | GPIO9 | |
| MISO | GPIO8 | |
| SCK | GPIO7 | |
| CS / CSN | GPIO4 | SPI chip select |
| GDO0 | GPIO2 | Demodulated OOK output → `remote_receiver` |

Adjust GPIO numbers to match your board.

---

## Installation

See [`secplus_receiver_example.yaml`](secplus_receiver_example.yaml) for a
complete working configuration.


### 1 - Add the remote package to your YAML configuration:

```yaml
packages:
  remote_packate_shorthand: github://greenbus4/secplus_receiver/secplus_receiver.yaml@main
```

### 2 — Optionally add `substitutions` section in your YAML

Adjust depending on your ESP device's wiring and the CC1101 you use.

```yaml
substitutions:
  remote_fixed_data_name:   "Fixed Data"    # Name of fixed data returned by remote as a t
  remote_rolling_code_name: "Rolling Code"  # Name of the rolling code text sensor
  remote_id_name:           "Remote ID"     # Name of the remote ID text sensor
  remote_button_name:       "Button ID"     # Name of the button text sensor

  # Pin connections to CC1101
  spi_clk_pin: GPIO07
  spi_miso_pin: GPIO08
  spi_mosi_pin: GPIO09

  # CC1101 Receiver
  cc1101_cs_pin: GPIO04
  cc1101_frequency: 390.00MHz  # 310MHz, 315Mhz, 390Mhz

  receiver_pin: GPIO02

```
### 3 - Optionally add a `secplus_receiver:` section to enable or disable entites and enable HA events:

```
secplus_receiver:
  remote_id_sensor:
    name: ${remote_id_name}

  button_sensor:
    name: ${remote_button_name}

  fire_homeassistant_event: true
```


---

## CC1101 frequency

Set the `cc1101:` block's `frequency:` to match your remotes:

| Frequency | Common use |
|---|---|
| `390.00MHz` | LiftMaster / Chamberlain 891LM, 893LM, 895LM (most common in North America) |
| `315.00MHz` | Older LiftMaster and Craftsman remotes |
| `310.00MHz` | Some regional and older variants |

Security+ 2.0 can use all three frequencies.


---

## `secplus.c` / `secplus.h` — fetched automatically

At build time, `__init__.py` downloads **`secplus.c`** and **`secplus.h`**
from [argilo/secplus](https://github.com/argilo/secplus) (`master` branch)
into the component directory. An internet connection is required on the first
build; subsequent builds use the cached copy.

---

## Repository layout

```
secplus_receiver/
├── README.md
├── secplus_receiver_example.yaml
└── esphome/
    └── components/
        └── secplus_receiver/
            ├── __init__.py           ← config schema + build-time downloader
            ├── secplus_receiver.h    ← SecplusReceiverComponent C++ class
            ├── secplus_parser.c      ← Manchester / Security+ 2.0 frame decoder
            ├── secplus_parser.h
            └── secplus_shim.h        ← ManchesterDecoder C++ wrapper
            # secplus.c & secplus.h are downloaded here at build time
```
---
## Notes:

### Beta code

This is currently under development and not ready for use.

### AI use

A fair amount of AI was used here.

This code started out as a Python implementation modeled after 
[secplus_rx_secplus_v2_decode.py](https://github.com/argilo/secplus/blob/master/secplus_rx_secplus_v2_decode.py)
from the [secplus](https://github.com/argilo/secplus) repository.

I rewrote the code in C, but still using byte arrays to represent binary data.
Claude AI (Sonnet 4.6) was used to convert it to using bit data to save space
for using on ESP32. Claude was also used to build the C++ glue to connect ESPHome
to the C code due to my lack of C++ knowledge.
AI (Claude and Gemini) were used to speed up creating this external_component.

As a result, much of this README looks AI generated, because it is.

---

## Credits

- Security+ 2.0 decoding: [argilo/secplus](https://github.com/argilo/secplus)
  by Clayton Smith (GPL-3.0)

## License

`secplus_parser.c`, `secplus_parser.h`, `secplus_receiver.h`, and
`secplus_shim.h` are provided under the MIT License. `secplus.c` and
`secplus.h` (downloaded at build time) are © Clayton Smith and licensed
under the **GPL-3.0**.
