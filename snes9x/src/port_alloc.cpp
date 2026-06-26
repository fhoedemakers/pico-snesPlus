/* Pico-snes9x+ port: two-tier allocator implementation. */

#include "port_alloc.h"

#include <stdlib.h>
#include <stdint.h>

#if PICO_RP2350
#include "pico/types.h"
#include "PicoPlusPsram.h"
#endif

extern "C" void *port_alloc_sram(size_t bytes)
{
    return malloc(bytes);
}

extern "C" void *port_alloc_psram(size_t bytes)
{
#if PICO_RP2350
    return PicoPlusPsram::getInstance().Malloc(bytes);
#else
    return malloc(bytes);
#endif
}

/* The framework's lwmem-backed PSRAM heap and the libc heap each track block
 * size internally, but lwmem_free on a libc pointer (or vice versa) would
 * corrupt state. Pico SDK's libc malloc places blocks in internal SRAM, and
 * PicoPlusPsram returns lwmem-owned PSRAM pointers — so we discriminate by
 * address range. */
extern "C" void port_alloc_free(void *p)
{
    if (!p) return;
#if PICO_RP2350
    /* RP2350 PSRAM is mapped via the QMI/XIP aperture starting at 0x11000000
     * (PSRAM XIP base). Internal SRAM lives in 0x20000000..0x20081FFF. */
    uintptr_t addr = (uintptr_t)p;
    if (addr >= 0x11000000u && addr < 0x18000000u) {
        PicoPlusPsram::getInstance().Free(p);
        return;
    }
#endif
    free(p);
}
