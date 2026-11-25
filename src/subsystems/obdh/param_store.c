/**
 * @file param_store.c
 * @brief Parameter store: records appended to two copies in flash, values kept in RAM.
 * @details
 * Layout (docs/FLASH-USAGE-SPECIFICATION.md §5.4). Each copy (FLASH_REGION_PARAM_A
 * and FLASH_REGION_PARAM_B) has two halves of 4 pages. The active half starts
 * with a half header and holds records appended one after another; the other
 * half is erased, waiting. A record is an 8-byte header (ID, length, check,
 * sequence number), the value padded to a multiple of 8 bytes, and an 8-byte
 * trailer with the CRC, which is programmed last: without a valid trailer the
 * record does not exist.
 *
 * The sequence number grows by one with every write and is the same in both
 * copies, so the value of an ID is its valid record with the highest sequence
 * number in either copy. When the active half is full, the newest record of
 * every ID is copied to the waiting half, its half header is programmed (from
 * then on it is the active half), and the old half is erased. A power cut at
 * any point leaves every value at its old or its new value.
 *
 * All flash access runs in the OBDH task, except param_store_init(), which only
 * reads and runs in main() before the scheduler starts.
 * @date 2026-10-27
 */

#include "param_store.h"
#include "flash.h"
#include "obdh_requests.h"
#include "crc32.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stdbool.h>
#include <string.h>

/* ---- Layout ---- */

#define COPIES               2
#define HALF_SIZE            (FLASH_PARAM_SIZE / 2u)   /**< 8 KB: 4 pages. */
#define HALF_HEADER_SIZE     16u
#define RECORD_HEADER_SIZE   8u
#define RECORD_TRAILER_SIZE  8u
#define PAD8(n)              (((uint32_t)(n) + 7u) & ~7u)
#define RECORD_SIZE(len)     (RECORD_HEADER_SIZE + PAD8(len) + RECORD_TRAILER_SIZE)
#define PARAM_FORMAT         1u
#define TAG_A                0x414D5250u               /**< "PRMA" as stored (little-endian). */
#define TAG_B                0x424D5250u               /**< "PRMB". */
#define LIVE_SET_MAX         2048u                     /**< Budget for the newest record of every ID (spec §5.3). */
#define BLANK_CHECK_CHUNK    256u

/** @brief Start of a half: names the copy and orders the two halves. */
typedef struct __attribute__((packed)) {
    uint32_t tag;            /**< TAG_A or TAG_B. */
    uint8_t  format;         /**< PARAM_FORMAT. */
    uint8_t  reserved[3];    /**< 0xFF. */
    uint32_t half_seq;       /**< One more at every compaction: the valid half with the higher value is active. */
    uint32_t crc;            /**< CRC-32 of the 12 bytes above. */
} half_header_t;

/** @brief Start of a record. */
typedef struct __attribute__((packed)) {
    uint8_t  id;             /**< Parameter ID; 0x00 and 0xFF (erased) are never valid. */
    uint8_t  len;            /**< Bytes of data: the parameter's size. Lets the scan step over IDs it does not know. */
    uint16_t check;          /**< ~(id | len << 8): rejects a damaged header before its length is trusted. */
    uint32_t seq;            /**< Write sequence number, the same in both copies. */
} record_header_t;

/** @brief End of a record, programmed last. */
typedef struct __attribute__((packed)) {
    uint32_t crc;            /**< CRC-32 of the header and the len data bytes. */
    uint32_t crc_inverted;   /**< ~crc, so an erased or zeroed trailer is never valid. */
} record_trailer_t;

_Static_assert(sizeof(half_header_t) == HALF_HEADER_SIZE, "the half header must be 16 bytes");
_Static_assert(sizeof(record_header_t) == RECORD_HEADER_SIZE, "the record header must be one doubleword");
_Static_assert(sizeof(record_trailer_t) == RECORD_TRAILER_SIZE, "the record trailer must be one doubleword");
_Static_assert((HALF_SIZE % FLASH_MAP_PAGE_SIZE) == 0u, "a half must be whole pages");
_Static_assert((HALF_SIZE % BLANK_CHECK_CHUNK) == 0u, "the blank check must cover the half in whole chunks");

/* ---- Parameter table ---- */

/** @brief The values in RAM, one member per parameter. */
typedef struct {
#define PARAM_FIELD(id, name, size, ...) uint8_t name##_value[size];
    PARAM_TABLE(PARAM_FIELD)
#undef PARAM_FIELD
} param_values_t;

#define PARAM_DEFAULT(id, name, size, ...) .name##_value = { __VA_ARGS__ },
#define PARAM_DEFAULTS { PARAM_TABLE(PARAM_DEFAULT) }

static const param_values_t defaults = PARAM_DEFAULTS;

/** @brief One row per parameter, in PARAM_TABLE order. */
static const struct {
    uint8_t  id;
    uint8_t  size;
    uint16_t offset;         /**< Of the value in param_values_t. */
} table[] = {
#define PARAM_ROW(id, name, size, ...) { (id), (size), (uint16_t)offsetof(param_values_t, name##_value) },
    PARAM_TABLE(PARAM_ROW)
#undef PARAM_ROW
};

#define PARAM_COUNT ((int)(sizeof(table) / sizeof(table[0])))

#define PARAM_CHECK(id, name, size, ...) \
    _Static_assert(((size) >= 1u) && ((size) <= 255u), #name ": the size must be 1 to 255 bytes"); \
    _Static_assert(((id) >= 0x01u) && ((id) <= 0xDFu), #name ": the ID is outside the subsystem ranges");
PARAM_TABLE(PARAM_CHECK)
#undef PARAM_CHECK

/* The newest record of every ID must fit the live-set budget, so a compacted half always has room */
#define PARAM_RECORD_BYTES(id, name, size, ...) + RECORD_SIZE(size)
_Static_assert((0u PARAM_TABLE(PARAM_RECORD_BYTES)) <= LIVE_SET_MAX, "the parameters exceed the 2 KB live set (spec §5.3)");
#undef PARAM_RECORD_BYTES

/* ---- State ---- */

/** @brief What the scan or the last write found for one parameter. */
typedef struct {
    uint32_t seq;              /**< Sequence number of the newest valid record; 0 = none, the default applies. */
    bool     in_copy[COPIES];  /**< Whether copy A / copy B holds that record. */
} param_state_t;

/** @brief Where one copy stands. */
typedef struct {
    flash_region_id_t region;
    uint32_t tag;
    int      active;           /**< Active half, 0 or 1; -1 = no valid half (blank, foreign or damaged copy). */
    uint32_t half_seq;         /**< half_seq of the active half. */
    uint32_t write_pos;        /**< Offset in the region where the next record goes. */
    bool     damaged;          /**< A damaged record header ended the log: rebuild before the next record. */
} param_copy_t;

static param_values_t values = PARAM_DEFAULTS;   /**< What param_get() returns. */
static param_state_t  state[PARAM_COUNT];
static param_copy_t   copies[COPIES] = {
    { FLASH_REGION_PARAM_A, TAG_A, -1, 0u, 0u, false },
    { FLASH_REGION_PARAM_B, TAG_B, -1, 0u, 0u, false },
};
static uint32_t next_seq = 1u;
static uint8_t  scratch[RECORD_SIZE(255u)];      /**< One record being built or read, or a blank-check chunk. */
static param_store_stats_t stats;

static int param_index(uint32_t id);
static void scan_copy(int ci);
static void take_record(int ci, const record_header_t *h, const uint8_t *data);
static bool copy_write(int ci, int skip_row, const record_header_t *h, const uint8_t *data);
static bool append(int ci, const record_header_t *h, const uint8_t *data);
static bool compact(int ci);
static bool program_record(const param_copy_t *c, uint32_t pos, const record_header_t *h, const uint8_t *data);
static bool read_record_body(const param_copy_t *c, uint32_t pos, const record_header_t *h);
static bool read_half_header(const param_copy_t *c, int half, half_header_t *hh);
static bool half_blank(const param_copy_t *c, int half);
static bool erase_half(const param_copy_t *c, int half);
static record_header_t make_header(uint8_t id, uint8_t len, uint32_t seq);
static bool header_valid(const record_header_t *h);
static uint32_t record_crc(const record_header_t *h, const uint8_t *data);
static uint32_t half_header_crc(const half_header_t *hh);
static bool all_erased(const uint8_t *bytes, uint32_t n);
static uint8_t *value_of(int row);
static const uint8_t *default_of(int row);
static void values_lock(void);
static void values_unlock(void);


/* ---- Any task ---- */

param_source_t param_get(param_id_t id, void *value, size_t size)
{
    int row = param_index((uint32_t)id);
    if ((row < 0) || (value == NULL) || (size != table[row].size)) {
        return PARAM_INVALID;
    }

    values_lock();
    memcpy(value, value_of(row), size);
    values_unlock();

    return (memcmp(value, default_of(row), size) == 0) ? PARAM_FROM_DEFAULT : PARAM_FROM_FLASH;
}

HAL_StatusTypeDef param_set(param_id_t id, const void *value, size_t size)
{
    int row = param_index((uint32_t)id);
    if ((row < 0) || (value == NULL) || (size != table[row].size)) {
        return HAL_ERROR;
    }
    return obdh_param_write_request((uint8_t)id, (const uint8_t *)value, size);
}

HAL_StatusTypeDef param_reset(param_id_t id)
{
    int row = param_index((uint32_t)id);
    if (row < 0) {
        return HAL_ERROR;
    }
    return param_set(id, default_of(row), table[row].size);
}

param_store_stats_t param_store_stats(void)
{
    return stats;
}

/* ---- Boot and OBDH task ---- */

void param_store_init(void)
{
    memcpy(&values, &defaults, sizeof(values));
    memset(state, 0, sizeof(state));
    memset(&stats, 0, sizeof(stats));
    next_seq = 1u;

    for (int ci = 0; ci < COPIES; ci++) {
        scan_copy(ci);
    }
}

HAL_StatusTypeDef param_store_write(uint8_t id, const uint8_t *value, size_t size)
{
    int row = param_index(id);
    if ((row < 0) || (value == NULL) || (size != table[row].size)) {
        return HAL_ERROR;
    }

    /* A write that would not change the value is not issued (spec FLS-DRV-070) */
    if (memcmp(value_of(row), value, size) == 0) {
        return HAL_OK;
    }

    param_state_t *s = &state[row];
    record_header_t h = make_header(id, (uint8_t)size, next_seq++);
    bool stored[COPIES];
    for (int ci = 0; ci < COPIES; ci++) {   /* copy A, read back, then copy B (spec FLS-INT-020) */
        stored[ci] = copy_write(ci, row, &h, value);
        if (!stored[ci]) {
            stats.copy_failures[ci]++;
        }
    }
    if (!stored[0] && !stored[1]) {
        return HAL_ERROR;
    }

    values_lock();
    memcpy(value_of(row), value, size);
    values_unlock();
    s->seq = h.seq;
    for (int ci = 0; ci < COPIES; ci++) {
        s->in_copy[ci] = stored[ci];
    }
    return HAL_OK;
}

/* ---- Scan at boot ---- */

/**
 * @brief Finds the active half of copy ci and reads its records, oldest first. Reads flash only.
 * @param ci Copy: 0 = A, 1 = B.
 */
static void scan_copy(int ci)
{
    param_copy_t *c = &copies[ci];
    half_header_t hh;

    c->active = -1;
    c->half_seq = 0u;
    c->write_pos = 0u;
    c->damaged = false;
    for (int half = 0; half < 2; half++) {
        if (read_half_header(c, half, &hh) && ((c->active < 0) || (hh.half_seq > c->half_seq))) {
            c->active = half;
            c->half_seq = hh.half_seq;
        }
    }
    if (c->active < 0) {
        return;   /* blank, foreign or damaged: rebuilt at the next write */
    }

    uint32_t pos = (uint32_t)c->active * HALF_SIZE + HALF_HEADER_SIZE;
    uint32_t end = (uint32_t)c->active * HALF_SIZE + HALF_SIZE;
    while (pos + RECORD_HEADER_SIZE <= end) {
        record_header_t h;
        if (flash_read(c->region, pos, (uint8_t *)&h, sizeof(h)) != HAL_OK) {
            c->damaged = true;   /* ECC error: the length cannot be trusted */
            break;
        }
        if (all_erased((const uint8_t *)&h, sizeof(h))) {
            break;               /* end of the log */
        }
        if (!header_valid(&h) || (RECORD_SIZE(h.len) > end - pos)) {
            c->damaged = true;   /* a header cut short by a power cut, or corrupted */
            break;
        }
        if (read_record_body(c, pos, &h)) {
            take_record(ci, &h, scratch);
        }
        pos += RECORD_SIZE(h.len);
    }
    c->write_pos = pos;
}

/**
 * @brief Takes a valid record found in copy ci: it becomes the value of its ID if it is the newest so far.
 * @param ci Copy the record is in.
 * @param h Its header.
 * @param data Its data (h->len bytes).
 */
static void take_record(int ci, const record_header_t *h, const uint8_t *data)
{
    if (h->seq >= next_seq) {
        next_seq = h->seq + 1u;
    }

    int row = param_index(h->id);
    if ((row < 0) || (h->seq == 0u) || (h->len != table[row].size)) {
        return;   /* not a parameter of this software: dropped at the next compaction */
    }

    param_state_t *s = &state[row];
    if (h->seq > s->seq) {
        s->seq = h->seq;
        s->in_copy[ci] = true;
        s->in_copy[1 - ci] = false;
        memcpy(value_of(row), data, h->len);
    } else if (h->seq == s->seq) {
        s->in_copy[ci] = true;
    }
}

/* ---- Writing ---- */

/**
 * @brief Brings copy ci up to date, then appends record h to it.
 * @details A copy without a valid half, or with a damaged log, is rebuilt
 *          first. Then the copy gets every newest record it lacks (a power cut
 *          between the two copies, or a failed write), except the one of the
 *          ID being written, which the new record replaces.
 * @param ci Copy: 0 = A, 1 = B.
 * @param skip_row Row of the ID being written.
 * @param h Header of the new record.
 * @param data Its data.
 * @return true if the copy holds the new record.
 */
static bool copy_write(int ci, int skip_row, const record_header_t *h, const uint8_t *data)
{
    param_copy_t *c = &copies[ci];

    if (((c->active < 0) || c->damaged) && !compact(ci)) {
        return false;
    }

    for (int row = 0; row < PARAM_COUNT; row++) {
        if ((row == skip_row) || (state[row].seq == 0u) || state[row].in_copy[ci]) {
            continue;
        }
        record_header_t missed = make_header(table[row].id, table[row].size, state[row].seq);
        if (!append(ci, &missed, value_of(row))) {
            return false;
        }
        state[row].in_copy[ci] = true;
    }

    return append(ci, h, data);
}

/**
 * @brief Appends record h to the active half of copy ci, compacting the copy first if the half has no room.
 * @param ci Copy, with a valid active half and an undamaged log.
 * @param h Header of the record.
 * @param data Its data.
 * @return true if the record reads back valid.
 */
static bool append(int ci, const record_header_t *h, const uint8_t *data)
{
    param_copy_t *c = &copies[ci];
    uint32_t size = RECORD_SIZE(h->len);

    if ((c->write_pos + size > (uint32_t)c->active * HALF_SIZE + HALF_SIZE) && !compact(ci)) {
        return false;
    }
    if (!program_record(c, c->write_pos, h, data)) {
        c->damaged = true;   /* the space after write_pos is no longer known to be erased */
        return false;
    }
    c->write_pos += size;
    return true;
}

/**
 * @brief Rebuilds copy ci in its other half from the values in RAM, then erases the old half.
 * @details Until the new half header is programmed, the old half stays the
 *          active one, so a power cut at any point loses nothing. The waiting
 *          half is normally erased already; it is erased here only if it is not.
 * @param ci Copy: 0 = A, 1 = B.
 * @return true if the new half is active.
 */
static bool compact(int ci)
{
    param_copy_t *c = &copies[ci];
    int target = (c->active < 0) ? 0 : 1 - c->active;
    uint32_t base = (uint32_t)target * HALF_SIZE;

    if (!half_blank(c, target) && !erase_half(c, target)) {
        return false;
    }

    uint32_t pos = base + HALF_HEADER_SIZE;
    for (int row = 0; row < PARAM_COUNT; row++) {
        if (state[row].seq == 0u) {
            continue;   /* never written: the default needs no record */
        }
        record_header_t h = make_header(table[row].id, table[row].size, state[row].seq);
        if (!program_record(c, pos, &h, value_of(row))) {
            return false;
        }
        pos += RECORD_SIZE(h.len);
    }

    half_header_t hh = {
        .tag      = c->tag,
        .format   = PARAM_FORMAT,
        .reserved = { 0xFFu, 0xFFu, 0xFFu },
        .half_seq = c->half_seq + 1u,
    };
    hh.crc = half_header_crc(&hh);
    half_header_t back;
    if ((flash_program(c->region, base, (const uint8_t *)&hh, sizeof(hh)) != HAL_OK) ||
        !read_half_header(c, target, &back) || (back.half_seq != hh.half_seq)) {
        return false;
    }

    int old = c->active;
    c->active = target;
    c->half_seq = hh.half_seq;
    c->write_pos = pos;
    c->damaged = false;
    stats.compactions[ci]++;
    for (int row = 0; row < PARAM_COUNT; row++) {
        if (state[row].seq != 0u) {
            state[row].in_copy[ci] = true;
        }
    }

    if (old >= 0) {
        (void)erase_half(c, old);   /* if this fails, the half is erased before it is used again */
    }
    return true;
}

/**
 * @brief Programs one record at pos of copy c: header and data first, the trailer last, then reads it back.
 * @param c Copy.
 * @param pos Offset in the region; the record's space must be erased.
 * @param h Header of the record.
 * @param data Its data.
 * @return true if the record reads back valid.
 */
static bool program_record(const param_copy_t *c, uint32_t pos, const record_header_t *h, const uint8_t *data)
{
    uint32_t data_size = PAD8(h->len);

    memcpy(scratch, h, RECORD_HEADER_SIZE);
    memset(scratch + RECORD_HEADER_SIZE, 0xFF, data_size);
    memcpy(scratch + RECORD_HEADER_SIZE, data, h->len);
    uint32_t crc = record_crc(h, scratch + RECORD_HEADER_SIZE);
    record_trailer_t t = { crc, ~crc };

    if ((flash_program(c->region, pos, scratch, RECORD_HEADER_SIZE + data_size) != HAL_OK) ||
        (flash_program(c->region, pos + RECORD_HEADER_SIZE + data_size, (const uint8_t *)&t, sizeof(t)) != HAL_OK)) {
        return false;
    }

    record_header_t back;
    return (flash_read(c->region, pos, (uint8_t *)&back, sizeof(back)) == HAL_OK) &&
           (memcmp(&back, h, sizeof(back)) == 0) && read_record_body(c, pos, &back);
}

/* ---- Reading ---- */

/**
 * @brief Reads the data and trailer of the record at pos into scratch.
 * @param c Copy.
 * @param pos Offset of the record in the region.
 * @param h Its header, already checked with header_valid().
 * @return true if the CRC in the trailer matches.
 */
static bool read_record_body(const param_copy_t *c, uint32_t pos, const record_header_t *h)
{
    uint32_t data_size = PAD8(h->len);
    if (flash_read(c->region, pos + RECORD_HEADER_SIZE, scratch, data_size + RECORD_TRAILER_SIZE) != HAL_OK) {
        return false;
    }

    record_trailer_t t;
    memcpy(&t, scratch + data_size, sizeof(t));
    uint32_t crc = record_crc(h, scratch);
    return (t.crc == crc) && (t.crc_inverted == ~crc);
}

/**
 * @brief Reads the header of one half.
 * @param c Copy.
 * @param half 0 or 1.
 * @param hh Receives the header.
 * @return true if it is a valid header of this copy.
 */
static bool read_half_header(const param_copy_t *c, int half, half_header_t *hh)
{
    return (flash_read(c->region, (uint32_t)half * HALF_SIZE, (uint8_t *)hh, sizeof(*hh)) == HAL_OK) &&
           (hh->tag == c->tag) && (hh->format == PARAM_FORMAT) && (hh->crc == half_header_crc(hh));
}

/**
 * @brief Tells whether every byte of one half reads as erased.
 * @param c Copy.
 * @param half 0 or 1.
 * @return true if the half is blank.
 */
static bool half_blank(const param_copy_t *c, int half)
{
    for (uint32_t off = 0u; off < HALF_SIZE; off += BLANK_CHECK_CHUNK) {
        if ((flash_read(c->region, (uint32_t)half * HALF_SIZE + off, scratch, BLANK_CHECK_CHUNK) != HAL_OK) ||
            !all_erased(scratch, BLANK_CHECK_CHUNK)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Erases the 4 pages of one half.
 * @param c Copy.
 * @param half 0 or 1.
 * @return true if every page was erased.
 */
static bool erase_half(const param_copy_t *c, int half)
{
    for (uint32_t off = 0u; off < HALF_SIZE; off += FLASH_MAP_PAGE_SIZE) {
        if (flash_erase_page(c->region, (uint32_t)half * HALF_SIZE + off) != HAL_OK) {
            return false;
        }
    }
    return true;
}

/* ---- Helpers ---- */

/**
 * @brief Row of a parameter ID in the table. A repeated ID in PARAM_TABLE fails to compile here (duplicate case).
 * @return The row, or -1 if the ID is unknown.
 */
static int param_index(uint32_t id)
{
    enum {
#define PARAM_ROW_INDEX(id, name, size, ...) name##_ROW,
        PARAM_TABLE(PARAM_ROW_INDEX)
#undef PARAM_ROW_INDEX
    };

    switch (id) {
#define PARAM_CASE(id, name, size, ...) case (id): return name##_ROW;
    PARAM_TABLE(PARAM_CASE)
#undef PARAM_CASE
    default:
        return -1;
    }
}

static record_header_t make_header(uint8_t id, uint8_t len, uint32_t seq)
{
    record_header_t h = { id, len, (uint16_t)~((uint16_t)id | ((uint16_t)len << 8)), seq };
    return h;
}

static bool header_valid(const record_header_t *h)
{
    return (h->id != 0x00u) && (h->id != 0xFFu) &&
           (h->check == (uint16_t)~((uint16_t)h->id | ((uint16_t)h->len << 8)));
}

static uint32_t record_crc(const record_header_t *h, const uint8_t *data)
{
    uint32_t crc = crc32_update(CRC32_INIT, (const uint8_t *)h, RECORD_HEADER_SIZE);
    return crc32_finish(crc32_update(crc, data, h->len));
}

static uint32_t half_header_crc(const half_header_t *hh)
{
    return crc32_finish(crc32_update(CRC32_INIT, (const uint8_t *)hh, offsetof(half_header_t, crc)));
}

static bool all_erased(const uint8_t *bytes, uint32_t n)
{
    for (uint32_t i = 0u; i < n; i++) {
        if (bytes[i] != 0xFFu) {
            return false;
        }
    }
    return true;
}

static uint8_t *value_of(int row)
{
    return (uint8_t *)&values + table[row].offset;
}

static const uint8_t *default_of(int row)
{
    return (const uint8_t *)&defaults + table[row].offset;
}

/* param_get() runs in any task while the OBDH task may be changing a value.
   Before the scheduler starts there is only main(), and a critical section
   would leave interrupts masked until the scheduler starts. */
static void values_lock(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        taskENTER_CRITICAL();
    }
}

static void values_unlock(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        taskEXIT_CRITICAL();
    }
}
