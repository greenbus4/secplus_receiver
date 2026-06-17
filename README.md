# secplus_receiver — ESPHome External Component

An ESPHome external component that decodes **Security+ 2.0** rolling-code
transmissions from Chamberlain, LiftMaster, and Craftsman garage door remotes
using a **CC1101** sub-GHz transceiver and ESPHome's `remote_receiver`.



Decoded results are exposed as Home Assistant `text_sensor` entities:

| Entity | Value |
|---|---|
| **Remote ID** | 40-bit fixed remote identifier (decimal string) |
| **Rolling Code** | Current rolling code counter (decimal string) |

Both sensors are optional — declare only the ones you need.


---
## Note:

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

### 1 — Add the external component

```yaml
external_components:
  - source: github://greenbus4/secplus_receiver@main
    components: [secplus_receiver]
    refresh: 1d
```

### 2 — Declare the component and sensors

```yaml
secplus_receiver:
  id: garage_receiver
  remote_id_sensor:
    name: "Garage Remote ID"
  rolling_code_sensor:
    name: "Garage Rolling Code"
```

### 3 — Feed pulses from remote_receiver

```yaml
remote_receiver:
  pin: GPIO2
  dump: []
  on_raw:
    - lambda: id(garage_receiver).process(x);
```

No `globals:`, no `on_boot:` lambda, no manual callback wiring needed.
`SecplusReceiverComponent::setup()` handles all of that internally.

---

## CC1101 frequency

Set the `cc1101:` block's `frequency:` to match your remotes:

| Frequency | Common use |
|---|---|
| `390.00MHz` | LiftMaster / Chamberlain 891LM, 893LM, 895LM (most common in North America) |
| `315.00MHz` | Older LiftMaster and Craftsman remotes |
| `310.00MHz` | Some regional and older variants |

Security+ 2.0 can use all three frequencies.

See [`secplus_receiver_example.yaml`](secplus_receiver_example.yaml) for a
complete working configuration.

---

## `secplus.c` / `secplus.h` — fetched automatically

At build time, `__init__.py` downloads **`secplus.c`** and **`secplus.h`**
from [argilo/secplus](https://github.com/argilo/secplus) (`master` branch)
into the component directory. An internet connection is required on the first
build; subsequent builds use the cached copy.

To pin to a specific upstream commit (for reproducible builds), edit
`SECPLUS_COMMIT` in `__init__.py`:

```python
SECPLUS_COMMIT = "f62ed51"   # pin to a known-good commit SHA
```

To vendor the files instead, manually place `secplus.c` and `secplus.h` in
`esphome/components/secplus_receiver/` — the downloader will skip them.

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

## Credits

- Security+ 2.0 decoding: [argilo/secplus](https://github.com/argilo/secplus)
  by Clayton Smith (GPL-3.0)

## License

`secplus_parser.c`, `secplus_parser.h`, `secplus_receiver.h`, and
`secplus_shim.h` are provided under the MIT License. `secplus.c` and
`secplus.h` (downloaded at build time) are © Clayton Smith and licensed
under the **GPL-3.0**.
