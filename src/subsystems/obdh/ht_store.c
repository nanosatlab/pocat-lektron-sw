/**
 * @file ht_store.c
 * @brief Telemetry history store: the credit, the POCKET+ blocks and the ring.
 * @details
 * The rules are in ht_store.h and docs/FLASH-USAGE-SPECIFICATION.md §5.4. The
 * functions under "OBDH task only" run in the OBDH task, which therefore owns
 * the POCKET+ compressor: compression.c keeps its state in static variables.
 * They refuse any other caller (obdh_is_caller(), spec FLS-REQ-110).
 * Moved here from comms/ht_handling.c in step 4b of the flash plan.
 * @date 2026-11-21
 */

#include "ht_store.h"
#include "obdh.h"
#include "obdh_requests.h"
#include "compression.h"
#include "uptime.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdbool.h>
#include <string.h>

_Static_assert(RING_SLOT_SIZE_VALID(HT_SLOT_SIZE), "the HT ring's slot size must be valid");
_Static_assert((HT_PACKET_SIZE % 4u) == 0u, "POCKET+ takes whole 32-bit words");
_Static_assert(HT_BEACON_SIZE <= HT_PACKET_SIZE, "the beacon body must fit one sample");
_Static_assert(HT_PACKET_SIZE <= HT_BLOCK_MAX, "a block must hold at least its first sample");
/* safeStartPocket() fails only for a packet larger than its buffers or an update rate below its protection level, 1 */
_Static_assert(HT_PACKET_SIZE <= P_P_MAX_PACKET_SIZE, "a sample must fit POCKET+'s buffers");
_Static_assert(POCKET_PLUS_UPDATE_RATE >= 1u, "safeStartPocket() refuses an update rate below 1");

static ring_t ring = { .config = { FLASH_REGION_HT, HT_SLOT_SIZE } };

static uint8_t  open_block[HT_BLOCK_MAX];   /**< The block being filled. */
static uint32_t open_len;                   /**< Its length; 0 while no block is open. */

static uint32_t credit_s;                   /**< Seconds of credit: 0 at boot (O21). */
static uint32_t credit_at_s;                /**< Uptime the credit was last brought up to; boot at first. */

static uint32_t posts_dropped;
static uint32_t beacons_skipped;
static uint32_t samples;

static bool take_credit(uint32_t now_s);
static bool append_compressed(uint32_t words[]);
static void to_words(const uint8_t packet[HT_PACKET_SIZE], uint32_t words[HT_PACKET_SIZE / 4u]);


/* ---- Any task ---- */

void ht_beacon_post(const uint8_t beacon[HT_BEACON_SIZE])
{
    if (obdh_ht_post(beacon) != HAL_OK) {
        taskENTER_CRITICAL();
        posts_dropped++;
        taskEXIT_CRITICAL();
    }
}

HAL_StatusTypeDef ht_read_request(uint32_t from_seq, uint8_t block[HT_BLOCK_MAX], uint32_t *len, uint32_t *seq, TickType_t ticks_to_wait)
{
    return obdh_ht_read_request(from_seq, block, len, seq, ticks_to_wait);
}

ht_store_stats_t ht_store_stats(void)
{
    return (ht_store_stats_t){ .posts_dropped = posts_dropped, .beacons_skipped = beacons_skipped,
                               .samples = samples, .ring = ring.stats };
}

/* ---- OBDH task only ---- */

HAL_StatusTypeDef ht_store_load(void)
{
    if (!obdh_is_caller()) {
        return HAL_ERROR;   /* another task: nothing read, nothing changed (FLS-REQ-110) */
    }

    ring_load(&ring);
    open_len = 0u;
    return HAL_OK;
}

HAL_StatusTypeDef ht_store_add(const uint8_t beacon[HT_BEACON_SIZE])
{
    if (!obdh_is_caller()) {
        return HAL_ERROR;   /* another task: no credit taken, nothing changed (FLS-REQ-110) */
    }

    if (!take_credit(uptime_get_s())) {
        beacons_skipped++;
        return HAL_OK;
    }
    samples++;

    uint8_t packet[HT_PACKET_SIZE] = { 0 };
    uint32_t words[HT_PACKET_SIZE / 4u];
    memcpy(packet, beacon, HT_BEACON_SIZE);
    to_words(packet, words);

    if (open_len > 0u) {
        if (append_compressed(words)) {
            return HAL_OK;
        }
        (void)ring_append(&ring, open_block, open_len);   /* the sample does not fit: store the block; the ring counts a block it drops */
    }
    memcpy(open_block, packet, HT_PACKET_SIZE);   /* a block starts with its first sample raw */
    open_len = HT_PACKET_SIZE;
    (void)safeStartPocket(words, HT_PACKET_SIZE, POCKET_PLUS_UPDATE_RATE);   /* cannot fail: see the asserts above */
    return HAL_OK;
}

HAL_StatusTypeDef ht_store_read(uint32_t from_seq, uint8_t block[HT_BLOCK_MAX], uint32_t *len, uint32_t *seq)
{
    if (!obdh_is_caller()) {
        return HAL_ERROR;   /* another task: nothing read (FLS-REQ-110) */
    }

    return ring_read(&ring, from_seq, block, len, seq);
}

/* ---- Helpers ---- */

/**
 * @brief Brings the credit up to now, then takes one sample's worth if it is there.
 * @param now_s Uptime in seconds; it never goes back.
 * @return true if a sample may be stored now.
 */
static bool take_credit(uint32_t now_s)
{
    uint32_t gained = now_s - credit_at_s;
    credit_at_s = now_s;
    credit_s = (gained >= HT_CREDIT_MAX_S - credit_s) ? HT_CREDIT_MAX_S : (credit_s + gained);

    if (credit_s < HT_SAMPLE_PERIOD_S) {
        return false;
    }
    credit_s -= HT_SAMPLE_PERIOD_S;
    return true;
}

/**
 * @brief Compresses a sample against the open block's earlier ones and appends it if it fits.
 * @param words The sample, as POCKET+ takes it.
 * @return true if it was appended; false if it does not fit or POCKET+ failed, and the block is unchanged.
 */
static bool append_compressed(uint32_t words[])
{
    uint8_t compressed[HT_PACKET_SIZE * 4u];   /* compressPacket() clears 4 times the packet size */
    int_fast16_t n = compressPacket(compressed, words);

    if ((n <= 0) || ((uint32_t)n > HT_BLOCK_MAX - open_len)) {
        return false;
    }
    memcpy(&open_block[open_len], compressed, (size_t)n);
    open_len += (uint32_t)n;
    return true;
}

/** @brief Packs a sample into the big-endian 32-bit words POCKET+ takes. */
static void to_words(const uint8_t packet[HT_PACKET_SIZE], uint32_t words[HT_PACKET_SIZE / 4u])
{
    for (uint32_t w = 0; w < HT_PACKET_SIZE / 4u; w++) {
        words[w] = ((uint32_t)packet[4u * w] << 24) | ((uint32_t)packet[4u * w + 1u] << 16) |
                   ((uint32_t)packet[4u * w + 2u] << 8) | (uint32_t)packet[4u * w + 3u];
    }
}
