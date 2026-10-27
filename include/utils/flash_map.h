/**
 * @file flash_map.h
 * @brief Internal flash memory map: where every region lives, with compile-time checks.
 * @date 2026-10-27
 *
 * @details
 * The 1 MB internal flash has two 512 KB banks of 2 KB pages. Code and data are
 * kept in different banks, so erasing a data page never stalls the code
 * (docs/FLASH-USAGE-SPECIFICATION.md §5.3).
 *
 * | Address    | Size   | Bank | Content                                  |
 * |------------|--------|------|------------------------------------------|
 * | 0x08000000 | 256 KB | 1    | Code                                     |
 * | 0x08040000 | 256 KB | 1    | Reserved: backup program image           |
 * | 0x08080000 | 256 KB | 2    | Reserved: staging for an uploaded image  |
 * | 0x080C0000 | 256 KB | 2    | Data area                                |
 *
 * The data area has two parts:
 * - The packed part, from 0x080C0000 up. Its sizes are placeholders until each
 *   owner confirms them. Every base is computed from the region before it, so
 *   changing one size moves only the regions after it, into the free space.
 * - The fixed block, from 0x080F0000 to the end of flash. Its addresses never move.
 *
 * To change a size, edit its *_SIZE line and increase FLASH_MAP_VERSION. The
 * checks at the end of this file stop the build if regions overlap, split a
 * page, or leave the data area. The map is frozen before flight acceptance.
 *
 * Flash addresses belong in this file only (spec FLS-GEN-050).
 */

#ifndef INC_FLASH_MAP_H_
#define INC_FLASH_MAP_H_

/** @brief Version of this layout, reported in telemetry. Increase it on every change below. */
#define FLASH_MAP_VERSION           1u

/* ---- Chip geometry (RM0351) ---- */

#define FLASH_MAP_PAGE_SIZE         0x00000800u   /**< Erase unit: 2 KB. */
#define FLASH_MAP_BANK2_START       0x08080000u   /**< First address of bank 2. */
#define FLASH_MAP_END               0x08100000u   /**< One past the last address of flash. */

/* ---- Top level ---- */

#define FLASH_CODE_BASE             0x08000000u   /**< Program image. */
#define FLASH_CODE_SIZE             0x00040000u   /**< 256 KB. The linker script must limit FLASH to this. */
#define FLASH_BACKUP_IMAGE_BASE     0x08040000u   /**< Reserved for a backup program image; kept erased. */
#define FLASH_BACKUP_IMAGE_SIZE     0x00040000u   /**< 256 KB. */
#define FLASH_STAGING_BASE          0x08080000u   /**< Reserved for staging an uploaded image; kept erased. */
#define FLASH_STAGING_SIZE          0x00040000u   /**< 256 KB. */
#define FLASH_DATA_BASE             0x080C0000u   /**< Data area: every region below lies inside it. */
#define FLASH_DATA_SIZE             0x00040000u   /**< 256 KB. */

/* ---- Data area: packed part (placeholder sizes, bases computed) ---- */
/* Ordered by how certain each size is, so a size that is still a guess never moves a settled region. */

#define FLASH_HT_BASE               FLASH_DATA_BASE
#define FLASH_HT_SIZE               0x00010000u   /**< Telemetry history, 64 KB: set by the wear analysis (spec §8). */
#define FLASH_PAYLOAD_BASE          (FLASH_HT_BASE + FLASH_HT_SIZE)
#define FLASH_PAYLOAD_SIZE          0x00010000u   /**< Payload results, 64 KB: placeholder, TBC by Payload. */
#define FLASH_FREE_BASE             (FLASH_PAYLOAD_BASE + FLASH_PAYLOAD_SIZE)
#define FLASH_FREE_SIZE             (FLASH_FIXED_BASE - FLASH_FREE_BASE)   /**< Room left for the packed regions to grow. */

/* ---- Data area: fixed block (addresses never move) ---- */

#define FLASH_FIXED_BASE            0x080F0000u
#define FLASH_PARAM_SIZE            0x00004000u   /**< 16 KB per parameter copy: two halves of 4 pages. */
#define FLASH_PARAM_B_BASE          0x080F0000u   /**< Parameter store, copy B. */
#define FLASH_OBC_LOG_BASE          0x080F4000u   /**< OBC event log (TC_REQUEST_OBC_LOG). */
#define FLASH_OBC_LOG_SIZE          0x00004000u   /**< 16 KB: placeholder, TBC by OBC; may grow into the spare. */
#define FLASH_SPARE_BASE            (FLASH_OBC_LOG_BASE + FLASH_OBC_LOG_SIZE)
#define FLASH_SPARE_SIZE            (FLASH_PARAM_A_BASE - FLASH_SPARE_BASE)
#define FLASH_PARAM_A_BASE          0x080FA000u   /**< Parameter store, copy A; kept apart from copy B. */
#define FLASH_UNIT_BASE             0x080FE000u   /**< Unit data: written once on the ground, then write-protected. */
#define FLASH_UNIT_SIZE             0x00002000u   /**< 8 KB. */

/* ---- Region table ---- */

/** @brief Who writes a region. */
typedef enum {
    FLASH_OWNER_OBDH,       /**< Parameter store, on behalf of every subsystem. */
    FLASH_OWNER_OBC,
    FLASH_OWNER_COMMS,
    FLASH_OWNER_PAYLOAD,
    FLASH_OWNER_GROUND      /**< Programmed with the ST-Link before flight; the software only reads it. */
} flash_owner_t;

/** @brief How a region is written. */
typedef enum {
    FLASH_STORE_PARAM,      /**< Records appended; when a half fills, the newest of each ID moves to the other half. */
    FLASH_STORE_RING,       /**< Fixed slots, oldest overwritten; one erase per page per lap. */
    FLASH_STORE_ONCE        /**< Written once, then only read. */
} flash_store_t;

/**
 * @brief Every data region in address order, as X(id, base, size, owner, store).
 * @details Free space, spare space and the reserved areas are not regions:
 *          nothing in flight may write there.
 */
#define FLASH_REGIONS(X) \
    X(FLASH_REGION_HT,      FLASH_HT_BASE,      FLASH_HT_SIZE,      FLASH_OWNER_COMMS,   FLASH_STORE_RING)  \
    X(FLASH_REGION_PAYLOAD, FLASH_PAYLOAD_BASE, FLASH_PAYLOAD_SIZE, FLASH_OWNER_PAYLOAD, FLASH_STORE_RING)  \
    X(FLASH_REGION_PARAM_B, FLASH_PARAM_B_BASE, FLASH_PARAM_SIZE,   FLASH_OWNER_OBDH,    FLASH_STORE_PARAM) \
    X(FLASH_REGION_OBC_LOG, FLASH_OBC_LOG_BASE, FLASH_OBC_LOG_SIZE, FLASH_OWNER_OBC,     FLASH_STORE_RING)  \
    X(FLASH_REGION_PARAM_A, FLASH_PARAM_A_BASE, FLASH_PARAM_SIZE,   FLASH_OWNER_OBDH,    FLASH_STORE_PARAM) \
    X(FLASH_REGION_UNIT,    FLASH_UNIT_BASE,    FLASH_UNIT_SIZE,    FLASH_OWNER_GROUND,  FLASH_STORE_ONCE)

/** @brief Region identifiers, in address order. */
typedef enum {
#define FLASH_REGION_ID(id, base, size, owner, store) id,
    FLASH_REGIONS(FLASH_REGION_ID)
#undef FLASH_REGION_ID
    FLASH_REGION_COUNT
} flash_region_id_t;

/* ---- Compile-time checks ---- */

/* Top level: code and the backup image fill bank 1; staging and the data area fill bank 2. */
_Static_assert(FLASH_CODE_BASE + FLASH_CODE_SIZE == FLASH_BACKUP_IMAGE_BASE, "code must end where the backup image starts");
_Static_assert(FLASH_BACKUP_IMAGE_BASE + FLASH_BACKUP_IMAGE_SIZE == FLASH_MAP_BANK2_START, "bank 1 must hold only code and the backup image");
_Static_assert(FLASH_STAGING_BASE == FLASH_MAP_BANK2_START, "upload staging must start bank 2");
_Static_assert(FLASH_STAGING_BASE + FLASH_STAGING_SIZE == FLASH_DATA_BASE, "the data area must follow upload staging");
_Static_assert(FLASH_DATA_BASE + FLASH_DATA_SIZE == FLASH_MAP_END, "the data area must end at the end of flash");

/* Every region: whole pages, inside the data area. */
#define FLASH_REGION_CHECK(id, base, size, owner, store) \
    _Static_assert((base) % FLASH_MAP_PAGE_SIZE == 0u, #id ": base is not on a page boundary"); \
    _Static_assert((size) > 0u && (size) % FLASH_MAP_PAGE_SIZE == 0u, #id ": size is not a whole number of pages"); \
    _Static_assert((base) >= FLASH_DATA_BASE && (base) + (size) <= FLASH_MAP_END, #id ": lies outside the data area");
FLASH_REGIONS(FLASH_REGION_CHECK)
#undef FLASH_REGION_CHECK

/* Packed part: it must not run into the fixed block. */
_Static_assert(FLASH_FREE_BASE <= FLASH_FIXED_BASE, "packed regions run into the fixed block: reduce a placeholder size");

/* Fixed block: in address order, without overlaps. */
_Static_assert(FLASH_PARAM_B_BASE == FLASH_FIXED_BASE, "parameter copy B must start the fixed block");
_Static_assert(FLASH_PARAM_B_BASE + FLASH_PARAM_SIZE <= FLASH_OBC_LOG_BASE, "parameter copy B overlaps the OBC event log");
_Static_assert(FLASH_OBC_LOG_BASE + FLASH_OBC_LOG_SIZE <= FLASH_PARAM_A_BASE, "the OBC event log overlaps parameter copy A");
_Static_assert(FLASH_PARAM_A_BASE + FLASH_PARAM_SIZE <= FLASH_UNIT_BASE, "parameter copy A overlaps the unit data");
_Static_assert(FLASH_UNIT_BASE + FLASH_UNIT_SIZE == FLASH_MAP_END, "unit data must end at the end of flash");

/* Parameter store: each copy splits into two halves of whole pages, and the two copies are not adjacent. */
_Static_assert(FLASH_PARAM_SIZE % (2u * FLASH_MAP_PAGE_SIZE) == 0u, "a parameter copy must split into two halves of whole pages");
_Static_assert(FLASH_PARAM_A_BASE > FLASH_PARAM_B_BASE + FLASH_PARAM_SIZE, "the two parameter copies must not be adjacent");

#endif /* INC_FLASH_MAP_H_ */
