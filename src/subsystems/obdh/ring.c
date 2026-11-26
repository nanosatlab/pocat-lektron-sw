/**
 * @file ring.c
 * @brief Ring store: blocks in fixed-size slots of a flash region, the oldest overwritten page by page.
 * @details The layout and the rules are in ring.h. Every function runs in the
 *          OBDH task, called by a store.
 * @date 2026-11-21
 */

#include "ring.h"
#include "flash.h"
#include "crc32.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* ---- Layout ---- */

#define APPEND_PLACES  2u     /**< Places an append tries: the next slot, then the start of the next page. */
#define VERIFY_CHUNK   32u    /**< Data bytes read back at a time. */
#define PAD8(n)        (((uint32_t)(n) + 7u) & ~7u)

/** @brief Start of a slot. */
typedef struct __attribute__((packed)) {
    uint16_t check;    /**< header_check() of len and seq. */
    uint16_t len;      /**< Bytes of data. */
    uint32_t seq;      /**< Sequence number. */
} ring_header_t;

/** @brief End of a slot, programmed last. */
typedef struct __attribute__((packed)) {
    uint32_t crc;            /**< CRC-32 of the header and the len data bytes. */
    uint32_t crc_inverted;   /**< ~crc, so an erased or zeroed trailer is never valid. */
} ring_trailer_t;

_Static_assert(sizeof(ring_header_t) == RING_HEADER_SIZE, "the slot header must be one doubleword");
_Static_assert(sizeof(ring_trailer_t) == RING_TRAILER_SIZE, "the slot trailer must be one doubleword");

static HAL_StatusTypeDef read_within(const ring_t *ring, uint32_t limit, uint8_t *data, uint32_t *len, uint32_t *seq);
static bool read_header(const ring_t *ring, uint32_t slot, ring_header_t *h);
static bool read_block(const ring_t *ring, uint32_t slot, const ring_header_t *h, uint8_t *data);
static bool program_slot(const ring_t *ring, uint32_t slot, const ring_header_t *h, const uint8_t *data, const ring_trailer_t *t);
static bool slot_reads_back(const ring_t *ring, uint32_t slot, const ring_header_t *h, const uint8_t *data, const ring_trailer_t *t);
static uint16_t header_check(const ring_header_t *h);
static ring_trailer_t trailer_of(const ring_header_t *h, const uint8_t *data);
static void skip_page(ring_t *ring);
static uint32_t slot_offset(const ring_t *ring, uint32_t slot);
static uint32_t trailer_offset(const ring_t *ring, uint32_t slot);
static uint32_t slots_per_page(const ring_t *ring);

/* ---- Public API ---- */

void ring_load(ring_t *ring)
{
    ring->slots = flash_region_size(ring->config.region) / ring->config.slot_size;

    bool found = false;
    uint32_t newest_slot = 0u;
    uint32_t newest_seq = 0u;
    for (uint32_t slot = 0u; slot < ring->slots; slot++) {
        ring_header_t h;
        /* (int32_t)(a - b) > 0: a comes after b, across the wrap too */
        if (read_header(ring, slot, &h) && (!found || ((int32_t)(h.seq - newest_seq) > 0))) {
            found = true;
            newest_slot = slot;
            newest_seq = h.seq;
        }
    }

    ring->write = found ? (newest_slot + 1u) % ring->slots : 0u;
    ring->next_seq = found ? newest_seq + 1u : 0u;
}

HAL_StatusTypeDef ring_append(ring_t *ring, const uint8_t *data, uint32_t len)
{
    if (len > RING_DATA_MAX(ring->config.slot_size)) {
        return HAL_ERROR;
    }

    for (uint32_t place = 0u; place < APPEND_PLACES; place++) {
        uint32_t slot = ring->write;

        /* Entering a page: erase it, which drops the oldest blocks a full ring keeps there */
        if (((slot % slots_per_page(ring)) == 0u) &&
            (flash_erase_page(ring->config.region, slot_offset(ring, slot)) != HAL_OK)) {
            ring->stats.erase_failures++;
            skip_page(ring);
            continue;
        }

        ring_header_t h = { 0u, (uint16_t)len, ring->next_seq++ };
        h.check = header_check(&h);
        ring_trailer_t t = trailer_of(&h, data);
        if (!program_slot(ring, slot, &h, data, &t) || !slot_reads_back(ring, slot, &h, data, &t)) {
            ring->stats.program_failures++;
            skip_page(ring);
            continue;
        }

        ring->write = (slot + 1u) % ring->slots;
        ring->stats.appended++;
        return HAL_OK;
    }

    ring->stats.dropped++;
    return HAL_ERROR;
}

HAL_StatusTypeDef ring_read(const ring_t *ring, uint32_t from_seq, uint8_t *data, uint32_t *len, uint32_t *seq)
{
    return read_within(ring, ring->next_seq - from_seq, data, len, seq);
}

HAL_StatusTypeDef ring_read_oldest(const ring_t *ring, uint8_t *data, uint32_t *len, uint32_t *seq)
{
    return read_within(ring, (uint32_t)INT32_MAX, data, len, seq);
}

/* ---- Reading ---- */

/**
 * @brief Reads the oldest valid block among those at most limit numbers back from next_seq.
 * @details A block's age is next_seq - seq, 1 for the newest. A limit of 0, or
 *          above INT32_MAX (a from_seq after next_seq), finds nothing. A block
 *          whose trailer does not match is skipped for the next younger one.
 * @param ring A loaded ring.
 * @param limit Largest age wanted.
 * @param data Receives the block.
 * @param len Receives its length.
 * @param seq Receives its sequence number.
 * @todo This function can be made faster by implementing it as a one-pass. 
 * @return HAL_OK, or HAL_ERROR if no valid block is that young.
 */
static HAL_StatusTypeDef read_within(const ring_t *ring, uint32_t limit, uint8_t *data, uint32_t *len, uint32_t *seq)
{
    while ((limit > 0u) && (limit <= (uint32_t)INT32_MAX)) {
        uint32_t oldest_age = 0u;
        uint32_t oldest_slot = 0u;
        ring_header_t oldest = { 0 };

        for (uint32_t slot = 0u; slot < ring->slots; slot++) {
            ring_header_t h;
            if (!read_header(ring, slot, &h)) {
                continue;
            }
            uint32_t age = ring->next_seq - h.seq;
            if ((age <= limit) && (age > oldest_age)) {
                oldest_age = age;
                oldest_slot = slot;
                oldest = h;
            }
        }

        if (oldest_age == 0u) {
            return HAL_ERROR;
        }
        if (read_block(ring, oldest_slot, &oldest, data)) {
            *len = oldest.len;
            *seq = oldest.seq;
            return HAL_OK;
        }
        limit = oldest_age - 1u;   /* damaged: look among the younger blocks */
    }
    return HAL_ERROR;
}

/**
 * @brief Reads the header of a slot.
 * @param ring A loaded ring.
 * @param slot Slot index.
 * @param h Receives the header.
 * @return true if it passes its check and its length fits the slot.
 */
static bool read_header(const ring_t *ring, uint32_t slot, ring_header_t *h)
{
    return (flash_read(ring->config.region, slot_offset(ring, slot), (uint8_t *)h, sizeof(*h)) == HAL_OK) &&
           (h->check == header_check(h)) &&
           (h->len <= RING_DATA_MAX(ring->config.slot_size));
}

/**
 * @brief Reads the data of a slot whose header is valid, and checks it against the trailer.
 * @param ring A loaded ring.
 * @param slot Slot index.
 * @param h Its header, from read_header().
 * @param data Receives h->len bytes.
 * @return true if the trailer matches the header and the data.
 */
static bool read_block(const ring_t *ring, uint32_t slot, const ring_header_t *h, uint8_t *data)
{
    ring_trailer_t t;
    if ((flash_read(ring->config.region, slot_offset(ring, slot) + RING_HEADER_SIZE, data, h->len) != HAL_OK) ||
        (flash_read(ring->config.region, trailer_offset(ring, slot), (uint8_t *)&t, sizeof(t)) != HAL_OK)) {
        return false;
    }

    ring_trailer_t expected = trailer_of(h, data);
    return (t.crc == expected.crc) && (t.crc_inverted == expected.crc_inverted);
}

/* ---- Writing ---- */

/**
 * @brief Programs one slot: the header, the data, then the trailer last.
 * @param ring A loaded ring.
 * @param slot Slot index; the slot must be erased.
 * @param h Header.
 * @param data h->len bytes of data.
 * @param t Trailer.
 * @return true if every part was programmed.
 */
static bool program_slot(const ring_t *ring, uint32_t slot, const ring_header_t *h, const uint8_t *data, const ring_trailer_t *t)
{
    uint32_t offset = slot_offset(ring, slot);
    uint32_t whole = h->len & ~7u;   /* data bytes that fill whole doublewords */
    uint8_t tail[8];
    memset(tail, 0xFF, sizeof(tail));
    memcpy(tail, data + whole, h->len - whole);

    flash_region_id_t region = ring->config.region;
    return (flash_program(region, offset, (const uint8_t *)h, sizeof(*h)) == HAL_OK) &&
           (flash_program(region, offset + RING_HEADER_SIZE, data, whole) == HAL_OK) &&
           (flash_program(region, offset + RING_HEADER_SIZE + whole, tail, PAD8(h->len - whole)) == HAL_OK) &&
           (flash_program(region, trailer_offset(ring, slot), (const uint8_t *)t, sizeof(*t)) == HAL_OK);
}

/**
 * @brief Reads back a slot just programmed and compares it with what was meant.
 * @param ring A loaded ring.
 * @param slot Slot index.
 * @param h Header programmed.
 * @param data Data programmed.
 * @param t Trailer programmed.
 * @return true if header, data and trailer read back as programmed.
 */
static bool slot_reads_back(const ring_t *ring, uint32_t slot, const ring_header_t *h, const uint8_t *data, const ring_trailer_t *t)
{
    uint32_t offset = slot_offset(ring, slot);
    flash_region_id_t region = ring->config.region;
    ring_header_t read_h;
    ring_trailer_t read_t;
    uint8_t chunk[VERIFY_CHUNK];

    if ((flash_read(region, offset, (uint8_t *)&read_h, sizeof(read_h)) != HAL_OK) || (memcmp(&read_h, h, sizeof(read_h)) != 0)) {
        return false;
    }
    for (uint32_t done = 0u; done < h->len; ) {
        uint32_t n = ((h->len - done) < VERIFY_CHUNK) ? (h->len - done) : VERIFY_CHUNK;
        if ((flash_read(region, offset + RING_HEADER_SIZE + done, chunk, n) != HAL_OK) || (memcmp(chunk, data + done, n) != 0)) {
            return false;
        }
        done += n;
    }
    return (flash_read(region, trailer_offset(ring, slot), (uint8_t *)&read_t, sizeof(read_t)) == HAL_OK) &&
           (memcmp(&read_t, t, sizeof(read_t)) == 0);
}

/* ---- Helpers ---- */

/** @brief The check a header carries: the low 16 bits of the CRC-32 of the header bytes after it, len and seq. */
static uint16_t header_check(const ring_header_t *h)
{
    const uint8_t *after_check = (const uint8_t *)h + offsetof(ring_header_t, len);
    return (uint16_t)crc32_finish(crc32_update(CRC32_INIT, after_check, sizeof(*h) - offsetof(ring_header_t, len)));
}

static ring_trailer_t trailer_of(const ring_header_t *h, const uint8_t *data)
{
    uint32_t crc = crc32_update(CRC32_INIT, (const uint8_t *)h, sizeof(*h));
    crc = crc32_finish(crc32_update(crc, data, h->len));
    ring_trailer_t t = { crc, ~crc };
    return t;
}

/** @brief Moves the writer to the start of the next page. */
static void skip_page(ring_t *ring)
{
    uint32_t per_page = slots_per_page(ring);
    ring->write = ((ring->write / per_page + 1u) * per_page) % ring->slots;
}

static uint32_t slot_offset(const ring_t *ring, uint32_t slot)
{
    return slot * ring->config.slot_size;
}

static uint32_t trailer_offset(const ring_t *ring, uint32_t slot)
{
    return slot_offset(ring, slot) + ring->config.slot_size - RING_TRAILER_SIZE;
}

static uint32_t slots_per_page(const ring_t *ring)
{
    return FLASH_MAP_PAGE_SIZE / ring->config.slot_size;
}
