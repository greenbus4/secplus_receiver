/*
 * secplus_receiver.h
 *
 * ESPHome component that decodes Security+ 2.0 (Manchester-encoded OOK)
 * transmissions from Chamberlain / LiftMaster / Craftsman garage door
 * remotes.
 *
 * The component registers itself directly as a remote_receiver listener and
 * receives decoded pulse trains via on_receive(), so no on_raw lambda wiring
 * is needed in YAML. This mirrors the structure of the acurite component:
 *   https://github.com/swoboda1337/acurite-esphome
 *
 * The Manchester / Security+ v2 decode is implemented in secplus_receiver.cpp
 * on top of the upstream argilo/secplus C library (secplus.c / secplus.h),
 * which is downloaded at build time by __init__.py.
 */

#pragma once

#include "esphome/core/component.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace secplus_receiver {

class SecplusReceiverComponent : public Component, public remote_base::RemoteReceiverListener {
 public:
  void set_remote_id_sensor(text_sensor::TextSensor *sensor) { this->remote_id_sensor_ = sensor; }
  void set_rolling_code_sensor(text_sensor::TextSensor *sensor) { this->rolling_code_sensor_ = sensor; }

  void dump_config() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;

 protected:
  // Feed a burst of signed pulse durations (us) through the Manchester decoder.
  // Negative = signal LOW (space), positive = signal HIGH (mark).
  void decode_raw_(const int32_t *pulses, int n_pulses);

  // Fire the sensors / log line once a Security+ v2 packet pair is decoded.
  void publish_(uint64_t remote_id, uint32_t rolling, uint32_t data, uint8_t frame_type);

  // ── Manchester decoder state ───────────────────────────────────────────────
  // Retained across on_receive() calls so the two Security+ frames can be
  // accumulated even when they arrive in separate receive bursts.
  uint8_t manchester_[64]{};  // MANCHESTER_BYTES
  int manchester_pos_{0};
  uint8_t packet1_[32]{};  // MAX_DECODED_BYTES
  int packet1_len_{0};
  uint8_t packet2_[32]{};
  int packet2_len_{0};

  text_sensor::TextSensor *remote_id_sensor_{nullptr};
  text_sensor::TextSensor *rolling_code_sensor_{nullptr};
};

}  // namespace secplus_receiver
}  // namespace esphome
