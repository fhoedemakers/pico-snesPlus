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
#include "nespad.h"
#include "wiipad.h"
#include "settings.h"

/* Refreshed once per frame by host_tick() in main.cpp. */
extern uint16_t wiipad_raw_cached;
extern uint32_t g_rapid_fire_counter;

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
