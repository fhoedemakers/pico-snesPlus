/* Pico-snes9x+ port: host-side stubs that snes9x's core expects the port
 * to provide. Input mapping (S9xReadJoypad) bridges the framework's unified
 * GamePadState to SNES button bits. Display init parks GFX.Screen on the
 * caller-provided HSTX framebuffer pointer. */

#include <stdio.h>

extern "C" {
#include "snes9x.h"
#include "memmap.h"
#include "gfx.h"
#include "display.h"
#include "ppu.h"
}
#include "port_alloc.h"

#include "gamepad.h"
#include "nespad.h"
#include "wiipad.h"
#include "settings.h"

/* Refreshed once per frame by host_tick() in main.cpp. */
extern uint16_t wiipad_raw_cached;
extern uint32_t g_rapid_fire_counter;

#if RENDER_TO_FB
/* Strip renderer. S9xUpdateScreen (gfx.c) renders every scanline block in
 * chunks of S9X_STRIP_ROWS rows into the four small SRAM staging strips
 * below, then calls s9x_port_strip_copyout() to memcpy the FINISHED rows
 * into the centered window of the 320x240 HSTX framebuffer. Two effects:
 *   - scan-out only ever sees fully-composited pixels (old frame or new),
 *     never the backdrop/partial-layer/pre-color-math intermediate states
 *     that a direct-to-framebuffer render exposes as rolling flicker;
 *   - screen, subscreen AND both Z buffers live in SRAM, so the per-pixel
 *     render traffic that used to hit PSRAM (screen writes, Z clears/RMW,
 *     subscreen composite, blit read-back) is gone entirely.
 * The strips keep the native 512-byte pitch: GFX.Delta / GFX.DepthDelta
 * stay constant because all four bases get the same per-chunk offset.
 * Works because the renderer already supports arbitrary [StartY,EndY]
 * blocks (mid-frame FLUSH_REDRAW blocks do this today) — chunking just
 * splits them finer; seam tiles use the existing clipped-tile paths. */
#include "hstx.h"
#include "pico/platform.h"

#define FB_WIDTH  320
#define FB_HEIGHT 240

/* Strip bases (row 0 = chunk start). +1 guard row: the force-lores guard
 * (gfx.c) contains mode 5/6, but its double-width writers still spill up
 * to one row past the last chunk row. */
#define STRIP_GUARD_ROWS (S9X_STRIP_ROWS + 1)

static uint8_t *strip_screen;
static uint8_t *strip_sub;
static uint8_t *strip_z;
static uint8_t *strip_subz;

extern "C" {
int s9x_port_max_endy = SNES_HEIGHT - 1;
/* Centered SNES window inside the framebuffer (copy-out target, also the
 * FPS overlay target in main.cpp). */
uint16_t *s9x_port_fb_window = nullptr;
/* 239x128 scratch for S9xSetupOBJ's FirstSprite+Y case — it used to abuse
 * GFX.SubScreen, which is now a strip far too small for it. PSRAM, same
 * tier SubScreen was in before. */
uint8_t *s9x_port_objonline = nullptr;
/* Optional hook fired just before the TOP strip (absolute row 0) is copied
 * to the single-buffered framebuffer. main.cpp registers it to stamp the FPS
 * overlay INTO the strip, so the digits are published together with the
 * frame's pixels in one copy-out. Stamping the overlay into the live FB
 * *after* the copy-out (as the port used to) left a sub-frame window where
 * the copy-out had already overwritten last frame's digits with game pixels
 * but the re-stamp had not run yet — scan-out caught that gap and the overlay
 * flickered. Passed the strip base (uint16_t), its stride in pixels, and the
 * chunk's absolute [block_start, block_end] row range so it can stamp only
 * the overlay rows this chunk publishes (strip physical row 0 == block_start;
 * a game may split the top of the frame into several short redraw chunks). */
void (*s9x_port_strip_top_hook)(uint16_t *strip, int stride,
                                int block_start, int block_end) = nullptr;
}

/* (Re-)anchor the framebuffer window for the current PPU.ScreenHeight
 * (224 or 239 — the overscan bit flips it at runtime; main.cpp calls this
 * again when it changes). Exports the last row the copy-out may write so
 * S9xUpdateScreen can clamp a mid-frame flip. */
extern "C" void s9x_port_anchor_screen(void)
{
    int h = PPU.ScreenHeight ? PPU.ScreenHeight
                             : (Settings.PAL ? SNES_HEIGHT_EXTENDED : SNES_HEIGHT);
    const int marginTop  = (FB_HEIGHT - h) / 2;
    const int marginLeft = (FB_WIDTH - SNES_WIDTH) / 2;
    s9x_port_fb_window = (uint16_t *)hstx_getframebuffer()
                       + marginTop * FB_WIDTH + marginLeft;
    s9x_port_max_endy = FB_HEIGHT - marginTop - 1;
}

/* Point the GFX buffers at the strips so absolute rows [row, row+N-1]
 * land at strip rows [0, N-1]. The intermediate (base - row*pitch) points
 * below the allocation; only rows >= row are ever dereferenced.
 * Both per-chunk helpers run from RAM (__not_in_flash_func): they are
 * called 14+ times per rendered frame from the SRAM-resident gfx.c code
 * and must not fetch through the XIP/QMI bus they exist to relieve. */
extern "C" void __not_in_flash_func(s9x_port_strip_repoint)(uint32_t row)
{
    GFX.Screen     = strip_screen - (size_t)row * SNES_WIDTH * 2;
    GFX.SubScreen  = strip_sub    - (size_t)row * SNES_WIDTH * 2;
    GFX.ZBuffer    = strip_z      - (size_t)row * SNES_WIDTH;
    GFX.SubZBuffer = strip_subz   - (size_t)row * SNES_WIDTH;
}

/* Copy finished rows [start_row, end_row] from the screen strip into the
 * framebuffer window. SRAM->SRAM, ~8 KB per 16-row chunk. */
extern "C" void __not_in_flash_func(s9x_port_strip_copyout)(uint32_t start_row, uint32_t end_row)
{
    /* Stamp the overlay into any chunk overlapping the overlay band (rows
     * 0..7) before publishing it, so it rides out with the frame's own pixels
     * (no post-copy-out re-stamp gap) even when the top of the frame is split
     * across several short redraw chunks. 8 == overlay height in main.cpp. */
    if (start_row < 8 && s9x_port_strip_top_hook)
        s9x_port_strip_top_hook((uint16_t *)strip_screen, SNES_WIDTH,
                                (int)start_row, (int)end_row);
    const uint8_t *src = strip_screen;
    uint16_t      *dst = s9x_port_fb_window + start_row * FB_WIDTH;
    for (uint32_t y = start_row; y <= end_row; y++) {
        memcpy(dst, src, SNES_WIDTH * 2);
        src += SNES_WIDTH * 2;
        dst += FB_WIDTH;
    }
}

/* SRAM-first with PSRAM fallback (needs PICO_MALLOC_PANIC=0), boot log
 * prints the tier — same pattern as FillRAM/MapInfo in memmap.c. A strip
 * in PSRAM still fixes the flicker but forfeits its share of the perf
 * win, so the log matters. FillRAM is forced to PSRAM under RENDER_TO_FB
 * (memmap.c) precisely so all four strips fit in SRAM. */
static uint8_t *strip_alloc(size_t bytes, const char *name)
{
    uint8_t *p = (uint8_t *)port_alloc_sram(bytes);
    if (p) {
        printf("%s (%u B) in SRAM\n", name, (unsigned)bytes);
        return p;
    }
    p = (uint8_t *)port_alloc_psram(bytes);
    printf("%s (%u B) in PSRAM (SRAM heap full)\n", name, (unsigned)bytes);
    return p;
}

extern "C" bool S9xInitDisplay(void)
{
    GFX.Pitch  = SNES_WIDTH * 2;   /* native 512 — strips decouple us from the fb stride */
    GFX.ZPitch = SNES_WIDTH;
    s9x_port_anchor_screen();

    strip_screen = strip_alloc((size_t)STRIP_GUARD_ROWS * SNES_WIDTH * 2, "strip-screen");
    strip_sub    = strip_alloc((size_t)STRIP_GUARD_ROWS * SNES_WIDTH * 2, "strip-sub");
    strip_z      = strip_alloc((size_t)STRIP_GUARD_ROWS * SNES_WIDTH,     "strip-z");
    strip_subz   = strip_alloc((size_t)STRIP_GUARD_ROWS * SNES_WIDTH,     "strip-subz");
    s9x_port_objonline = (uint8_t *)port_alloc_psram((size_t)SNES_HEIGHT_EXTENDED * 128);

    GFX.Screen     = strip_screen;
    GFX.SubScreen  = strip_sub;
    GFX.ZBuffer    = strip_z;
    GFX.SubZBuffer = strip_subz;
    return strip_screen && strip_sub && strip_z && strip_subz && s9x_port_objonline;
}

extern "C" void S9xDeinitDisplay(void)
{
    port_alloc_free(strip_screen); strip_screen = nullptr;
    port_alloc_free(strip_sub);    strip_sub    = nullptr;
    port_alloc_free(strip_z);      strip_z      = nullptr;
    port_alloc_free(strip_subz);   strip_subz   = nullptr;
    port_alloc_free(s9x_port_objonline); s9x_port_objonline = nullptr;
    GFX.Screen = GFX.SubScreen = GFX.ZBuffer = GFX.SubZBuffer = nullptr;
}
#else
/* Private PSRAM screen buffer. snes9x renders here at native 256-wide
 * pitch; main.cpp blits it into the HSTX framebuffer after S9xMainLoop
 * returns. Decoupling render from scan-out eliminates the lower-third
 * tearing caused by HSTX reading the framebuffer mid-PPU-write. Cost is
 * one PSRAM-to-SRAM memcpy per visible frame (~240 KB/s) which fits
 * comfortably in the vblank-to-bottom-of-content window. */
uint16_t *g_snes_private_screen = nullptr;

extern "C" bool S9xInitDisplay(void)
{
    const size_t pitch_bytes = (size_t)SNES_WIDTH * 2;                  /* 512 */
    const size_t z_stride    = pitch_bytes >> 1;                        /* 256, matches S9xInitGFX */

    g_snes_private_screen = (uint16_t *)port_alloc_psram(pitch_bytes * SNES_HEIGHT_EXTENDED);

    GFX.Pitch  = pitch_bytes;
    GFX.ZPitch = z_stride;
    GFX.Screen = (uint8_t *)g_snes_private_screen;
    GFX.SubScreen  = (uint8_t *)port_alloc_psram(pitch_bytes * SNES_HEIGHT_EXTENDED);
    GFX.ZBuffer    = (uint8_t *)port_alloc_psram(z_stride    * SNES_HEIGHT_EXTENDED);
    GFX.SubZBuffer = (uint8_t *)port_alloc_psram(z_stride    * SNES_HEIGHT_EXTENDED);
    return GFX.Screen && GFX.SubScreen && GFX.ZBuffer && GFX.SubZBuffer;
}

extern "C" void S9xDeinitDisplay(void)
{
    port_alloc_free(g_snes_private_screen); g_snes_private_screen = nullptr;
    port_alloc_free(GFX.SubScreen);  GFX.SubScreen  = nullptr;
    port_alloc_free(GFX.ZBuffer);    GFX.ZBuffer    = nullptr;
    port_alloc_free(GFX.SubZBuffer); GFX.SubZBuffer = nullptr;
    GFX.Screen = nullptr;
}
#endif /* RENDER_TO_FB */

/* USB HID/XInput pads (io::GamePadState) -> SNES joypad bits. */
static uint32_t pad_to_snes(const io::GamePadState &pad)
{
    uint32_t out = 0;   /* snes9x's S9xUpdateJoypads sets the high bits itself. */

    if (pad.buttons & io::GamePadState::Button::UP)     out |= SNES_UP_MASK;
    if (pad.buttons & io::GamePadState::Button::DOWN)   out |= SNES_DOWN_MASK;
    if (pad.buttons & io::GamePadState::Button::LEFT)   out |= SNES_LEFT_MASK;
    if (pad.buttons & io::GamePadState::Button::RIGHT)  out |= SNES_RIGHT_MASK;

    if (pad.buttons & io::GamePadState::Button::A)      out |= SNES_A_MASK;
    if (pad.buttons & io::GamePadState::Button::B)      out |= SNES_B_MASK;
    if (pad.buttons & io::GamePadState::Button::X)      out |= SNES_X_MASK;
    if (pad.buttons & io::GamePadState::Button::Y)      out |= SNES_Y_MASK;
    if (pad.buttons & io::GamePadState::Button::L)      out |= SNES_TL_MASK;
    if (pad.buttons & io::GamePadState::Button::R)      out |= SNES_TR_MASK;
    if (pad.buttons & io::GamePadState::Button::START)  out |= SNES_START_MASK;
    if (pad.buttons & io::GamePadState::Button::SELECT) out |= SNES_SELECT_MASK;

    return out;
}

#if NES_PIN_CLK != -1
/* GPIO NES/SNES pad. nespad_states_ext is in SNES serial order (bit0=B ...
 * bit11=R), which is the SNES joypad register order mirrored: serial bit i
 * == register bit 15-i. A NES pad only delivers bits 0-7 (A,B,Select,
 * Start,dpad), so its A lands on SNES B and its B on SNES Y — the natural
 * positional feel (jump/run in most games). */
static uint32_t nespad_to_snes(uint16_t raw)
{
    uint32_t out = 0;
    for (int i = 0; i < 12; i++)
    {
        if (raw & (1u << i))
            out |= 1u << (15 - i);
    }
    return out;
}
#endif

#if WII_PIN_SDA >= 0 && WII_PIN_SCL >= 0
/* Wii Classic / SNES-Classic-mini pad; encoding documented in wiipad.h.
 * Labels equal SNES positions on these pads, so this is a 1:1 map. */
static uint32_t wiipad_to_snes(uint16_t v)
{
    uint32_t out = 0;
    if (v & (1 << 0))  out |= SNES_A_MASK;
    if (v & (1 << 1))  out |= SNES_B_MASK;
    if (v & (1 << 2))  out |= SNES_SELECT_MASK;
    if (v & (1 << 3))  out |= SNES_START_MASK;
    if (v & (1 << 4))  out |= SNES_UP_MASK;
    if (v & (1 << 5))  out |= SNES_DOWN_MASK;
    if (v & (1 << 6))  out |= SNES_LEFT_MASK;
    if (v & (1 << 7))  out |= SNES_RIGHT_MASK;
    if (v & (1 << 8))  out |= SNES_X_MASK;
    if (v & (1 << 9))  out |= SNES_Y_MASK;
    if (v & (1 << 10)) out |= SNES_TL_MASK;
    if (v & (1 << 11)) out |= SNES_TR_MASK;
    return out;
}
#endif

extern "C" uint32_t S9xReadJoypad(int32_t port)
{
    if (port < 0 || port > 1) return 0;

    uint32_t out = pad_to_snes(io::getCurrentGamePadState(port));

    /* Sibling convention (pico-infonesPlus): with a USB pad connected the
     * GPIO NES/SNES and Wii Classic pads act as player 2, otherwise they
     * are player 1 (resp. players 1 and 2 for two GPIO pads). */
    const bool usb = io::getCurrentGamePadState(0).isConnected();
#if NES_PIN_CLK != -1
    if (usb)
    {
        if (port == 1)
            out |= nespad_to_snes(nespad_states_ext[0] | nespad_states_ext[1]);
    }
    else
    {
        out |= nespad_to_snes(nespad_states_ext[port]);
    }
#endif
#if WII_PIN_SDA >= 0 && WII_PIN_SCL >= 0
    if (port == (usb ? 1 : 0))
        out |= wiipad_to_snes(wiipad_raw_cached);
#endif

    /* Rapid fire on A/B (menu setting) — 15 presses/sec, same cadence as
     * the sibling emulators. */
    if (g_rapid_fire_counter & 2)
    {
        if (settings.flags.rapidFireOnA) out &= ~SNES_A_MASK;
        if (settings.flags.rapidFireOnB) out &= ~SNES_B_MASK;
    }

    return out;
}

extern "C" bool S9xReadMousePosition(int32_t, int32_t *, int32_t *, uint32_t *) { return false; }
extern "C" bool S9xReadSuperScopePosition(int32_t *, int32_t *, uint32_t *)    { return false; }
extern "C" bool JustifierOffscreen(void)                                       { return true; }
extern "C" void JustifierButtons(uint32_t *)                                   { }
extern "C" void S9xToggleSoundChannel(int32_t)                                 { }
/* S9xNextController is defined by snes9x's own ppu.c. */

#if MIX_ON_CORE1
/* Cross-core sound-state lock — see the comment at S9xSetAPUDSP (apu.c).
 * spin_lock_unsafe: neither holder takes it from an IRQ, so no irq-save
 * variant is needed. Held ~µs on core0 (one DSP register write) and up
 * to ~200 µs on core1 (one 64-sample mix chunk). */
#include "pico/sync.h"
static spin_lock_t *snd_lock;

extern "C" void port_sound_lock_init(void)
{
    if (!snd_lock)
        snd_lock = spin_lock_instance(spin_lock_claim_unused(true));
}
extern "C" void __not_in_flash_func(port_sound_lock)(void)
{
    spin_lock_unsafe_blocking(snd_lock);
}
extern "C" void __not_in_flash_func(port_sound_unlock)(void)
{
    spin_unlock_unsafe(snd_lock);
}
#endif
