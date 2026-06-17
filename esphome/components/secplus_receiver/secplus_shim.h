/*
 * secplus_shim.h
 *
 * Thin C++ wrapper around the C Manchester / Security+ v2 decoder (secplus_parsser.c).
 *
 * Usage in an ESPHome lambda:
 *
 *   static ManchesterDecoder decoder;
 *
 *   // Wire up once (e.g. in on_boot or first call guard):
 *   decoder.on_result([](const SecplusResult &r) {
 *       id(sensor_remote_id).publish_state((float)r.remote_id);
 *       id(sensor_rolling).publish_state((float)r.rolling);
 *   });
 *
 *   // Feed pulses (e.g. from remote_receiver on_raw):
 *   decoder.process(pulses.data(), pulses.size());
 *
 * Thread / ISR safety:
 *   Not re-entrant. Call only from the main ESPHome loop task (the default
 *   for remote_receiver on_raw lambdas).
 */

#pragma once

#ifdef __cplusplus
#include <cstdint>
#include <functional>
#include <vector>

extern "C" {
#include "secplus_parser.h"
}

/* ── Result struct exposed to C++ callers ────────────────────────────────── */

struct SecplusResult {
    uint64_t remote_id;     /* fixed remote ID (40-bit masked)   */
    int32_t  rolling;       /* rolling code counter              */
    uint32_t data;          /* button / data field               */
    uint8_t  frame_type;    /* 0 = 40-bit frame, 1 = 64-bit      */
};

/* ── ManchesterDecoder ───────────────────────────────────────────────────── */

class ManchesterDecoder {
public:
    using ResultCallback = std::function<void(const SecplusResult &)>;

    ManchesterDecoder() = default;

    /* Disable copy – the underlying C state is static/global */
    ManchesterDecoder(const ManchesterDecoder &) = delete;
    ManchesterDecoder &operator=(const ManchesterDecoder &) = delete;

    /**
     * on_result()
     *
     * Register a callback to be invoked whenever a complete Security+ v2
     * packet pair is successfully decoded.
     *
     * Call once, typically in on_boot or with a first-call guard.
     * Overwrites any previously registered callback.
     *
     * Example:
     *   decoder.on_result([](const SecplusResult &r) {
     *       id(my_sensor).publish_state((float)r.remote_id);
     *   });
     */
    void on_result(ResultCallback cb)
    {
        callback_ = std::move(cb);
    }

    /**
     * process()
     *
     * Feed raw signed pulse durations (µs) to the decoder.
     * Negative = LOW, positive = HIGH.
     *
     * Invoke from the ESPHome remote_receiver on_raw lambda:
     *
     *   on_raw:
     *     lambda: |-
     *       decoder.process(x.data(), x.size());
     *
     * The registered callback may be invoked zero or more times
     * synchronously during this call — once for each complete packet pair
     * decoded from the supplied pulse buffer.
     */
    void process(const int *pulses, int n_pulses)
    {
        process_raw_signals(pulses, n_pulses, &ManchesterDecoder::c_callback, this);
    }

    /**
     * process() overload for ESPHome pulse containers.
     * Automatically handles std::vector<long int>, std::vector<int>, etc.
     */
    template <typename Container>
    void process(const Container &pulses)
    {
        if constexpr (std::is_same_v<typename Container::value_type, int>) {
            process(pulses.data(), static_cast<int>(pulses.size()));
        } else {
            std::vector<int> standard_pulses(pulses.begin(), pulses.end());
            process(standard_pulses.data(), static_cast<int>(standard_pulses.size()));
        }
    }

private:
    ResultCallback callback_;

    /*
     * c_callback()
     *
     * Static trampoline with C linkage compatible with secplus_callback_t.
     * `ctx` is the ManchesterDecoder instance pointer passed via process().
     * Translates the C decode_result_t into a C++ SecplusResult and forwards
     * it to the registered std::function callback.
     */
    static void c_callback(const decode_result_t *result, void *ctx)
    {
        auto *self = static_cast<ManchesterDecoder *>(ctx);
        if (!self->callback_) return;

        SecplusResult r;
        r.remote_id  = result->remote_id;
        r.rolling    = result->rolling;
        r.data       = result->data;
        r.frame_type = result->frame_type;
        self->callback_(r);
    }
};

#endif // __cplusplus
