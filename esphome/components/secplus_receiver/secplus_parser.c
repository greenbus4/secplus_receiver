/*
 * secplus_parse.c
 *
 * Decodes Manchester-encoded (G.E. Thomas convention) pulse data at 4000 baud.
 *
 * Input format:
 *   int pulses[] = array of signed microsecond durations.
 *   Negative = signal LOW (off), Positive = signal HIGH (on).
 *   |value| = duration in microseconds.
 *
 * Encoding:
 *   Clock period = 250 us (4000 baud).
 *   Every bit has a mid-cycle transition.
 *   G.E. Thomas convention:
 *     Bit 0  ->  HIGH first half, LOW second half  ->  "10"
 *     Bit 1  ->  LOW first half, HIGH second half  ->  "01"
 *
 * Preamble:
 *   Data 0x0F (00001111) encodes to:  "1010101001010101"
 *   = 0xAA55 as a 16-bit packed value.
 *   Searched via a sliding 16-bit window over the packed bit buffer.
 *
 * Memory layout (all working buffers use packed bits):
 *   out[]      Manchester bit buffer:  MANCHESTER_BYTES  (64 bytes)
 *   packet1[]  Decoded packet 1:       MAX_DECODED_BYTES (32 bytes)
 *   packet2[]  Decoded packet 2:       MAX_DECODED_BYTES (32 bytes)
 *
 * Results are delivered by invoking the caller-supplied callback from within
 * the pulse processing loop, immediately when a complete packet pair is
 * decoded. Multiple packets within a single call to process_raw_signals()
 * will each trigger a separate callback invocation.
 * No heap allocation. No C++ dependencies.
 */

#include "secplus_parser.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "secplus.h"


#define TAG "secplus_rx"

#define SECPLUS_LOGD(TAG, fmt, ...)  printf("[D][%s] " fmt "\n", TAG, ##__VA_ARGS__)
#define SECPLUS_LOGI(TAG, fmt, ...)  printf("[I][%s] " fmt "\n", TAG, ##__VA_ARGS__)
#define SECPLUS_LOGW(TAG, fmt, ...)  printf("[W][%s] " fmt "\n", TAG, ##__VA_ARGS__)
#define SECPLUS_LOGE(TAG, fmt, ...)  printf("[E][%s] " fmt "\n", TAG, ##__VA_ARGS__)


/* ── Tuneable constants ──────────────────────────────────────────────────── */

#define CLOCK_US         250    /* microseconds per half-bit clock period     */
#define LONG_THRESH      625    /* |dur| >= this → packet gap / flush         */

#define MAX_MANCHESTER   512    /* max Manchester bits in flight               */
#define MAX_DECODED_BITS 256    /* max decoded payload bits per packet         */
#define MAX_DECODED_BYTES  ((MAX_DECODED_BITS + 7) / 8)
#define MANCHESTER_BYTES   ((MAX_MANCHESTER   + 7) / 8)

/* Preamble: 0x0F Manchester-encoded = "1010101001010101" = 0xAA55           */
#define PREAMBLE_BITS    16
#define PREAMBLE_VAL     0xAA55u

/* ── Packed bit-array helpers ────────────────────────────────────────────── */
/*
 * Bits stored MSB-first within each byte:
 *   index 0 → byte 0 bit 7 (0x80)
 *   index 7 → byte 0 bit 0 (0x01)
 *   index 8 → byte 1 bit 7 (0x80)
 */

static inline void bitarr_set(uint8_t *arr, int idx, int val)
{
    if (val)
        arr[idx >> 3] |=  (0x80u >> (idx & 7));
    else
        arr[idx >> 3] &= ~(0x80u >> (idx & 7));
}

static inline int bitarr_get(const uint8_t *arr, int idx)
{
    return (arr[idx >> 3] >> (7 - (idx & 7))) & 1;
}

/* ── find_preamble ───────────────────────────────────────────────────────── */
/*
 * Sliding 16-bit window search for PREAMBLE_VAL (0xAA55).
 * Returns bit index immediately after the preamble, or -1 if not found.
 */
static int find_preamble(const uint8_t *bits, int nbits)
{
    if (nbits < PREAMBLE_BITS) return -1;

    uint16_t window = 0;

    for (int i = 0; i < PREAMBLE_BITS - 1; i++)
        window = (uint16_t)((window << 1) | bitarr_get(bits, i));

    for (int i = PREAMBLE_BITS - 1; i < nbits; i++) {
        window = (uint16_t)((window << 1) | bitarr_get(bits, i));
        if (window == PREAMBLE_VAL)
            return i + 1;
    }
    return -1;
}

/* ── manchester_decode: packed-in → packed-out ───────────────────────────── */
/*
 * G.E. Thomas pairs:
 *   "10" → data bit 0
 *   "01" → data bit 1
 *   other → sync error, skip one bit and retry
 *
 * Returns number of bits written to `out`, or -1 on overflow.
 */
static int manchester_decode(const uint8_t *bits, int start, int nbits,
                              uint8_t *out, int out_size_bits,
                              int *sync_errors_out)
{
    int pos  = 0;
    int errs = 0;
    int i    = start;

    while (i + 1 < nbits) {
        int a = bitarr_get(bits, i);
        int b = bitarr_get(bits, i + 1);

        if (a == 1 && b == 0) {
            if (pos >= out_size_bits) return -1;
            bitarr_set(out, pos++, 0);
            i += 2;
        } else if (a == 0 && b == 1) {
            if (pos >= out_size_bits) return -1;
            bitarr_set(out, pos++, 1);
            i += 2;
        } else {
            errs++;
            i++;
        }
    }

    if (sync_errors_out) *sync_errors_out = errs;
    return pos;
}

/* ── process_packet ──────────────────────────────────────────────────────── */
/*
 * Finds the preamble in `bits[0..nbits)`, reads the 2-field header
 * (frame ID + packet length), then Manchester-decodes the payload into
 * the packed `packet` buffer.
 *
 * Returns frame_id (1 or 2) on success, or:
 *   -1  preamble not found / insufficient data
 *   -2  unknown header values / decode overflow
 */
static int process_packet(const uint8_t *bits, int nbits,
                           uint8_t *packet, int *packet_length)
{
    int start = find_preamble(bits, nbits);
    if (start < 0) return -1;
    if (start + 8 > nbits) return -1;

    /* Frame ID: 4 Manchester bits after preamble
     *   "1010" (data 00) → frame 1
     *   "1001" (data 01) → frame 2
     */
    int a0 = bitarr_get(bits, start+0), a1 = bitarr_get(bits, start+1);
    int a2 = bitarr_get(bits, start+2), a3 = bitarr_get(bits, start+3);

    int frame_id;
    if      (a0==1 && a1==0 && a2==1 && a3==0) frame_id = 1;
    else if (a0==1 && a1==0 && a2==0 && a3==1) frame_id = 2;
    else return -2;

    start += 4;

    /* Packet length: next 4 Manchester bits
     *   "1010" → 40-bit payload
     *   "1001" → 64-bit payload
     */
    int b0 = bitarr_get(bits, start+0), b1 = bitarr_get(bits, start+1);
    int b2 = bitarr_get(bits, start+2), b3 = bitarr_get(bits, start+3);

    int pkt_length;
    if      (b0==1 && b1==0 && b2==1 && b3==0) pkt_length = 40;
    else if (b0==1 && b1==0 && b2==0 && b3==1) pkt_length = 64;
    else return -2;

    int sync_errs    = 0;
    int nbits_decoded = manchester_decode(bits, start, nbits,
                                          packet, MAX_DECODED_BITS,
                                          &sync_errs);
    if (nbits_decoded < 0) return -2;

    (void)pkt_length;       /* available for length validation if desired */
    *packet_length = nbits_decoded;
    return frame_id;
}

/* ── process_raw_signals (public) ────────────────────────────────────────── */

void process_raw_signals(const int          *pulses,
                          int                 n_pulses,
                          secplus_callback_t  callback,
                          void               *ctx)
{
    /* All static: lives in .bss, nothing on the stack */
    static uint8_t out[MANCHESTER_BYTES];
    static int     pos = 0;

    static uint8_t packet1[MAX_DECODED_BYTES];
    static int     packet1_len = 0;

    static uint8_t packet2[MAX_DECODED_BYTES];
    static int     packet2_len = 0;

    for (int i = 0; i < n_pulses; i++) {
        int p   = pulses[i];
        int dur = (p < 0) ? -p : p;
        int lvl = (p > 0) ?  1 :  0;

        bool flush = (pos >= MAX_MANCHESTER - 4);

        /* ── Gap or buffer-full: attempt decode ── */
        if (dur >= LONG_THRESH || flush) {

            if (flush)
                /* buffer full - not normal operation */
                SECPLUS_LOGW(TAG,
                    "Manchester buffer full (%d bits), flushing. Could mean a noisy radio. Maybe try another frequency",
                    pos
                );

            if (pos < MAX_MANCHESTER)
                bitarr_set(out, pos++, lvl);

            bool is_packet_1 = (packet1_len == 0);

            int frame_id = is_packet_1
                ? process_packet(out, pos, packet1, &packet1_len)
                : process_packet(out, pos, packet2, &packet2_len);

            if (frame_id > 0)
                /* successful frame decode - debug-level info */
                SECPLUS_LOGD(TAG, "frame #%d decoded (%d bits)", frame_id,
                             frame_id == 1 ? packet1_len : packet2_len);

            /* Reject out-of-order frame 2 arriving before frame 1 */
            if (is_packet_1 && frame_id == 2) {
                /* out-of-order frame - worth flagging */
                SECPLUS_LOGW(TAG, "got frame 2 before frame 1, discarding");

                packet2_len = 0;
                frame_id    = -1;
            }

            /* Both frames received: run decode_v2 and fire callback */
            if (frame_id == 2) {
                uint8_t  frame_type = (packet1_len == 40) ? 0 : 1;
                int32_t  rolling    = 0;
                uint64_t fixed      = 0;
                uint32_t data       = 0;

                int ret = decode_v2(frame_type, packet1, packet2,
                                    &rolling, &fixed, &data);

                if (ret < 0) {
                    /* decode_v2 failure - an error */
                    SECPLUS_LOGE(TAG, "secplus.decode_v2 failed and returned: %d", ret);
                } else {
                    decode_result_t result;
                    result.remote_id  = fixed & 0xf0ffffffffULL;
                    result.rolling    = rolling;
                    result.data       = data;
                    result.frame_type = frame_type;

                    /* successful decode - the happy path */
                    SECPLUS_LOGD(TAG, "rolling=%u remote=%llu data=%u",
                         (unsigned)rolling,
                         (unsigned long long)result.remote_id,
                         (unsigned)data);


                    callback(&result, ctx);
                }

                packet1_len = packet2_len = 0;
            }

            pos = 0;
            continue;
        }

        /* ── Accumulate: round to 1 or 2 clock slots ── */
        int slots = (dur + CLOCK_US / 2) / CLOCK_US;
        if (slots < 1) slots = 1;
        if (slots > 2) slots = 2;

        for (int s = 0; s < slots && pos < MAX_MANCHESTER; s++)
            bitarr_set(out, pos++, lvl);
    }
}
