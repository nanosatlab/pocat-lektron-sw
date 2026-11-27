/**
 * @file map_at.h
 * @brief Maps zeroed RAM at a fixed address of the host, so code that uses absolute addresses can run there.
 * @date 2026-11-24
 *
 * @details
 * The flash driver reads and programs flash through its absolute addresses
 * (the data area starts at 0x080C0000). On a 64-bit host that address is
 * usually free, so test_flash_driver maps RAM there and the driver's
 * pointers land in it. The mapping lasts until the process ends.
 */
#ifndef MAP_AT_H
#define MAP_AT_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Maps @p size bytes of zeroed, readable and writable RAM at @p addr.
 * @details VirtualAlloc() on Windows, mmap() elsewhere. Nothing is mapped
 * over memory already in use: if the range is taken, the call fails.
 * @param addr First address; a multiple of 64 KB.
 * @param size Number of bytes.
 * @return @p addr as a pointer; NULL if the range could not be mapped there.
 */
void *map_at(uintptr_t addr, size_t size);

#endif /* MAP_AT_H */
