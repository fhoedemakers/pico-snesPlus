/* Pico-snes9x+ port: two-tier allocator.
 *
 *   port_alloc_sram(n)  -> internal SRAM heap (fast, scarce: ~280 KB free
 *                          after framework). Use for CPU/PPU/APU hot working
 *                          set: WRAM, VRAM, APU RAM, scanline scratch.
 *   port_alloc_psram(n) -> external PSRAM (slow, abundant: 8 MB).
 *                          Use for ROM, cart SRAM, large tile/echo buffers.
 *   port_alloc_free(p)  -> frees from either tier; safe on NULL.
 *
 * The framework's PSRAM allocator (PicoPlusPsram) tracks block size so the
 * generic free() form works without remembering which tier each pointer
 * came from. */

#ifndef PICO_SNESPLUS_PORT_ALLOC_H
#define PICO_SNESPLUS_PORT_ALLOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *port_alloc_sram(size_t bytes);
void *port_alloc_psram(size_t bytes);
void  port_alloc_free(void *p);

#ifdef __cplusplus
}
#endif

#endif
