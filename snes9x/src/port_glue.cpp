/* Pico-snes9x+ port: host-side stubs that snes9x's core expects the port
 * to provide. Input mapping (S9xReadJoypad) bridges the framework's unified
 * GamePadState to SNES button bits. Display init parks GFX.Screen on the
 * caller-provided HSTX framebuffer pointer. */

extern "C" {
#include "snes9x.h"
#include "memmap.h"
#include "gfx.h"
#include "display.h"
#include "ppu.h"
}
#include "port_alloc.h"

#include "gamepad.h"

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

extern "C" uint32_t S9xReadJoypad(int32_t port)
{
    if (port != 0) return 0;

    auto &pad = io::getCurrentGamePadState(0);
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
