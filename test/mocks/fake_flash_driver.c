#include "fake_flash_driver.h"
#include "unity.h"
#include <string.h>

#define PAGE   FLASH_MAP_PAGE_SIZE
#define DWORDS (FLASH_DATA_SIZE / 8u)

static uint8_t  image[FLASH_DATA_SIZE];         /* the data area, 0x080C0000 .. 0x080FFFFF */
static bool     ecc_bad[DWORDS];
static bool     program_wrong[DWORDS];          /* the next program of the doubleword stores three bits wrong */
static uint32_t erases[FLASH_DATA_SIZE / PAGE];
static bool     erase_fails[FLASH_DATA_SIZE / PAGE];
static uint32_t programs;
static uint32_t reads;                          /* flash_read() calls */
static long     steps_left = -1;                /* -1: no cut planned */
static bool     power_cut;
static bool     cut_ecc = true;

/* The regions of flash_map.h, as the real driver sees them */
static const struct { uint32_t base; uint32_t size; flash_access_t access; } regions[FLASH_REGION_COUNT] = {
#define FAKE_REGION_ENTRY(id, base, size, access) [id] = { (base), (size), (access) },
    FLASH_REGIONS(FAKE_REGION_ENTRY)
#undef FAKE_REGION_ENTRY
};

void fake_flashdrv_reset(void)
{
    memset(image, 0xFF, sizeof(image));
    memset(ecc_bad, 0, sizeof(ecc_bad));
    memset(program_wrong, 0, sizeof(program_wrong));
    memset(erases, 0, sizeof(erases));
    memset(erase_fails, 0, sizeof(erase_fails));
    programs = 0;
    reads = 0;
    steps_left = -1;
    power_cut = false;
    cut_ecc = true;
}

void fake_flashdrv_cut_power_after(long steps) { steps_left = steps; }
void fake_flashdrv_power_on(void) { power_cut = false; steps_left = -1; }
bool fake_flashdrv_power_is_cut(void) { return power_cut; }
void fake_flashdrv_set_cut_ecc(bool on) { cut_ecc = on; }
uint32_t fake_flashdrv_programs(void) { return programs; }
uint32_t fake_flashdrv_reads(void) { return reads; }

/* Like the driver: the range must lie inside the region; unit data is never written. Gives the image index. */
static bool resolve(flash_region_id_t region, uint32_t offset, uint32_t n, bool write, uint32_t *index)
{
    if ((unsigned)region >= FLASH_REGION_COUNT) return false;
    if (write && regions[region].access == FLASH_ACCESS_READ_ONLY) return false;
    if (offset > regions[region].size || n > regions[region].size - offset) return false;
    *index = regions[region].base - FLASH_DATA_BASE + offset;
    return true;
}

/* Counts one program or erase step; true if the power goes at this step. */
static bool cut_now(void)
{
    if (steps_left < 0) return false;
    if (steps_left == 0) {
        power_cut = true;
        steps_left = -1;
        return true;
    }
    steps_left--;
    return false;
}

uint8_t *fake_flashdrv_bytes(flash_region_id_t region, uint32_t offset)
{
    uint32_t i;
    TEST_ASSERT_TRUE_MESSAGE(resolve(region, offset, 0, false, &i), "fake_flashdrv_bytes: offset outside the region");
    return &image[i];
}

uint32_t fake_flashdrv_erases(flash_region_id_t region, uint32_t offset)
{
    uint32_t i;
    TEST_ASSERT_TRUE_MESSAGE(resolve(region, offset, 1, false, &i), "fake_flashdrv_erases: offset outside the region");
    return erases[i / PAGE];
}

void fake_flashdrv_inject_ecc(flash_region_id_t region, uint32_t offset)
{
    uint32_t i;
    TEST_ASSERT_TRUE_MESSAGE(resolve(region, offset, 8, false, &i), "fake_flashdrv_inject_ecc: offset outside the region");
    ecc_bad[i / 8u] = true;
}

void fake_flashdrv_inject_wrong_program(flash_region_id_t region, uint32_t offset)
{
    uint32_t i;
    TEST_ASSERT_TRUE_MESSAGE(resolve(region, offset, 8, false, &i), "fake_flashdrv_inject_wrong_program: offset outside the region");
    program_wrong[i / 8u] = true;
}

void fake_flashdrv_fail_erase(flash_region_id_t region, uint32_t offset)
{
    uint32_t i;
    TEST_ASSERT_TRUE_MESSAGE(resolve(region, offset, 1, false, &i), "fake_flashdrv_fail_erase: offset outside the region");
    erase_fails[i / PAGE] = true;
}

uint32_t flash_region_size(flash_region_id_t region)
{
    return ((unsigned)region < FLASH_REGION_COUNT) ? regions[region].size : 0u;
}

HAL_StatusTypeDef flash_read(flash_region_id_t region, uint32_t offset, uint8_t *data, uint32_t n_bytes)
{
    uint32_t i;
    reads++;
    if (!resolve(region, offset, n_bytes, false, &i)) return HAL_ERROR;
    memcpy(data, &image[i], n_bytes);
    for (uint32_t dw = i / 8u; dw * 8u < i + n_bytes; dw++) {
        if (ecc_bad[dw]) return HAL_ERROR;   /* the bytes are copied anyway, as on the satellite */
    }
    return HAL_OK;
}

HAL_StatusTypeDef flash_program(flash_region_id_t region, uint32_t offset, const uint8_t *data, uint32_t n_bytes)
{
    uint32_t i;
    if (power_cut) return HAL_ERROR;
    if (!resolve(region, offset, n_bytes, true, &i)) return HAL_ERROR;
    if ((offset % 8u) != 0u || (n_bytes % 8u) != 0u) return HAL_ERROR;
    for (uint32_t k = 0; k < n_bytes; k++) {
        if (image[i + k] != 0xFFu) return HAL_ERROR;   /* like the driver: the target must read as erased */
    }

    for (uint32_t k = 0; k < n_bytes; k += 8u) {
        if (cut_now()) {
            for (uint32_t b = 0; b < 4u; b++) image[i + k + b] &= data[k + b];   /* half programmed */
            if (cut_ecc) ecc_bad[(i + k) / 8u] = true;
            return HAL_ERROR;
        }
        for (uint32_t b = 0; b < 8u; b++) image[i + k + b] &= data[k + b];
        if (program_wrong[(i + k) / 8u]) {   /* three bits wrong, more than ECC corrects, and no error reported */
            program_wrong[(i + k) / 8u] = false;
            image[i + k] ^= 0x01u;
            image[i + k + 3u] ^= 0x10u;
            image[i + k + 6u] ^= 0x80u;
        }
        programs++;
    }
    return HAL_OK;
}

HAL_StatusTypeDef flash_erase_page(flash_region_id_t region, uint32_t offset)
{
    uint32_t i;
    if (power_cut) return HAL_ERROR;
    if (!resolve(region, offset, 1, true, &i)) return HAL_ERROR;

    uint32_t page = i / PAGE;
    erases[page]++;
    if (erase_fails[page]) return HAL_ERROR;   /* a page that no longer erases: left as it was */
    if (cut_now()) {
        /* Cut short: the first half of the page is erased, the second keeps its old bytes */
        memset(&image[page * PAGE], 0xFF, PAGE / 2u);
        memset(&ecc_bad[page * PAGE / 8u], 0, PAGE / 16u);
        if (cut_ecc) memset(&ecc_bad[(page * PAGE + PAGE / 2u) / 8u], 1, PAGE / 16u);
        return HAL_ERROR;
    }
    memset(&image[page * PAGE], 0xFF, PAGE);
    memset(&ecc_bad[page * PAGE / 8u], 0, PAGE / 8u);
    return HAL_OK;
}
