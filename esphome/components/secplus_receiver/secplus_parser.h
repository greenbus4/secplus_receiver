/*
 * secplus_parse.h
 *
 * Public C interface for the Security+ v2 Manchester decoder.
 *
 * The only entry point for callers is process_raw_signals().
 * Results are delivered by invoking a caller-supplied callback function
 * synchronously from inside process_raw_signals() at the moment a complete
 * packet pair is decoded. No heap allocation. No C++ dependencies.
 *
 * The C++ wrapper (secplus_shim.h) sits on top of this and bridges
 * the C callback to a std::function suitable for ESPHome lambdas.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Result struct ───────────────────────────────────────────────────────── */

typedef struct {
    uint64_t remote_id;     /* fixed ID of the remote (40-bit, masked)        */
    int32_t  rolling;       /* rolling code counter                           */
    uint32_t data;          /* button/data field from decode_v2               */
    uint8_t  frame_type;    /* 0 = 40-bit frame, 1 = 64-bit frame            */
} decode_result_t;

/* ── Callback type ───────────────────────────────────────────────────────── */

/*
 * secplus_callback_t
 *
 * Invoked synchronously by process_raw_signals() each time a complete
 * Security+ v2 packet pair is successfully decoded.
 *
 * @param result   Pointer to a stack-allocated result struct. Valid only
 *                 for the duration of the callback; do not retain the pointer.
 * @param ctx      Opaque pointer forwarded unchanged from the call to
 *                 process_raw_signals(). Use it to pass object pointers,
 *                 state, etc. May be NULL if unused.
 */
typedef void (*secplus_callback_t)(const decode_result_t *result, void *ctx);

/* ── Entry point ─────────────────────────────────────────────────────────── */

/**
 * process_raw_signals()
 *
 * Feed a burst of signed pulse durations (µs) into the decoder.
 * Negative values = signal LOW, positive = signal HIGH.
 *
 * Call repeatedly as pulses arrive (e.g. from an RMT or remote_receiver
 * component). The function maintains static internal state across calls so
 * partial bursts are accumulated correctly.
 *
 * When a complete Security+ v2 packet pair has been decoded, `callback` is
 * called synchronously (before this function returns) with the decoded
 * result and the forwarded `ctx` pointer.
 *
 * @param pulses    Array of signed µs durations.
 * @param n_pulses  Number of elements in `pulses`.
 * @param callback  Function to invoke on each successfully decoded packet.
 *                  Must not be NULL.
 * @param ctx       Opaque context pointer forwarded to `callback`. May be NULL.
 */
void process_raw_signals(const int          *pulses,
                          int                 n_pulses,
                          secplus_callback_t  callback,
                          void               *ctx);

#ifdef __cplusplus
}
#endif
