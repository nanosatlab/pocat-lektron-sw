#define _DEFAULT_SOURCE   /* MAP_ANONYMOUS with glibc */

#include "map_at.h"

#ifdef _WIN32
#include <windows.h>

void *map_at(uintptr_t addr, size_t size)
{
    /* With an address given, VirtualAlloc() either maps there or fails */
    void *p = VirtualAlloc((void *)addr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if ((p != NULL) && (p != (void *)addr)) {
        VirtualFree(p, 0, MEM_RELEASE);
        p = NULL;
    }
    return p;
}

#else
#include <sys/mman.h>

void *map_at(uintptr_t addr, size_t size)
{
    /* No MAP_FIXED: that would replace whatever is there. The address is a hint, so check where it landed */
    void *p = mmap((void *)addr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        return NULL;
    }
    if (p != (void *)addr) {
        munmap(p, size);
        return NULL;
    }
    return p;
}

#endif
