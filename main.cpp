/* pico_snesPlus — snes9x port for RP2350 + PSRAM, HSTX HDMI out.
 *
 * Frame loop is markedly different from pico-infonesPlus's NES:
 *   - InfoNES drives the host via per-scanline callbacks
 *     (InfoNES_PreDrawLine / InfoNES_PostDrawLine) and host-supplied audio
 *     waveform mixer (InfoNES_SoundOutput).
 *   - snes9x runs as a one-shot S9xMainLoop() that emulates a full SNES
 *     frame and returns at V-blank, writing pixels directly into GFX.Screen
 *     and queuing samples via S9xMixSamples (pulled by us after the frame).
 *
 * SNES native resolution 256x224 (NTSC) or 256x239 (PAL) is centered in
 * the 320x240 HSTX framebuffer with a 32-px L/R margin. Pixel format is
 * RGB555 (HSTX scan-out format — snes9x's PIXEL_FORMAT is set to RGB555
 * via PICO_SNESPLUS_HSTX in port.h).
 *
 * With RENDER_TO_FB (default) snes9x renders in S9X_STRIP_ROWS-row chunks
 * into small SRAM staging strips and copies each finished chunk into that
 * centered framebuffer window (strip renderer, see port_glue.cpp) — no
 * private PSRAM screen, no blit, and scan-out never sees mid-composite
 * pixels. Without it, the legacy path renders into a 256-wide PSRAM
 * buffer that is blitted here after S9xMainLoop (optionally on core1,
 * BLIT_ON_CORE1). */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "tusb.h"
#include "ff.h"

#include "FrensHelpers.h"
#include "FrensFonts.h"
#include "settings.h"
#include "menu.h"
#include "menu_settings.h"
#include "vumeter.h"
#include "gamepad.h"
#include "nespad.h"
#include "wiipad.h"

#if HSTX
#include "hstx.h"
#endif

#include "hardware/structs/scb.h"

/* snes9x core */
extern "C" {
#include "snes9x.h"
#include "memmap.h"
#include "gfx.h"
#include "apu.h"
#include "soundux.h"
#include "ppu.h"
#include "cpuexec.h"
#include "display.h"
}

#if RENDER_TO_FB
/* port glue — strip renderer. Re-anchors the framebuffer window for the
 * current PPU.ScreenHeight (the overscan bit flips 224<->239 at runtime)
 * and exposes the window pointer for the FPS overlay. */
extern "C" void s9x_port_anchor_screen(void);
extern "C" uint16_t *s9x_port_fb_window;
#else
/* port glue — snes9x's render target, 256-wide RGB555 in PSRAM. */
extern uint16_t *g_snes_private_screen;
#endif

#define EMULATOR_CLOCKFREQ_KHZ 378000      /* RP2350 default clock for this emulator */
#define EMULATOR_MAX_CLOCKFREQ_KHZ 504000  /* RP2350 max overclock — 504 MHz (experiment, may be unstable) */
#define AUDIOBUFFERSIZE 1024
/* 44100, matching everything downstream: the TLV320 DAC's register
 * script is written for 44.1 kHz (its BCLK-fed PLL runs below spec at
 * 32 kHz), the I2S setup in Frens::initAll uses the 44100 default, and
 * hstx_init's HDMI ACR default is 44100 too. An earlier 32000 setting
 * (to cut SPC700 sample-synthesis cost on core0) became pointless once
 * S9xMixSamples moved to core1 (MIX_ON_CORE1) — the synthesis cost now
 * lands on core1's idle time. */
#define SNES_AUDIO_HZ 44100

bool isFatalError = false;
char *romName = nullptr;
char selectedRom[FF_MAX_LFN] = {0};

/* Frames rendered in the last ~1 s window, updated by the per-second block in
 * run_emulator() and read by the on-screen FPS overlay. */
static uint32_t g_fps = 60;

/* -------------------------------------------------------------------------
 * Hardfault reporter — overrides the SDK's weak isr_hardfault (a bare
 * breakpoint) on BOTH cores. At 378 MHz this board lives near its silicon
 * margin, so when a fault fires we want the stacked frame and fault status
 * on serial without needing a debug probe attached. RAM-resident so it
 * still runs if XIP is unavailable (printf itself is flash-resident; if
 * the fault happened mid-flash-write the prints are lost but the
 * breakpoint below still lands). */
extern "C" void __not_in_flash_func(hardfault_report)(uint32_t *sp, uint32_t exc_lr)
{
    armv8m_scb_hw_t *scb = scb_hw;
    printf("\n*** HARDFAULT core%u ***\n", (unsigned)get_core_num());
    printf("  r0=%08lx r1=%08lx r2=%08lx  r3=%08lx\n",
           (unsigned long)sp[0], (unsigned long)sp[1],
           (unsigned long)sp[2], (unsigned long)sp[3]);
    printf(" r12=%08lx lr=%08lx pc=%08lx psr=%08lx\n",
           (unsigned long)sp[4], (unsigned long)sp[5],
           (unsigned long)sp[6], (unsigned long)sp[7]);
    printf("  sp=%08lx exc_lr=%08lx\n", (unsigned long)(uintptr_t)sp,
           (unsigned long)exc_lr);
    printf("  CFSR=%08lx HFSR=%08lx MMFAR=%08lx BFAR=%08lx\n",
           (unsigned long)scb->cfsr, (unsigned long)scb->hfsr,
           (unsigned long)scb->mmfar, (unsigned long)scb->bfar);
    while (true)
        __breakpoint();
}

extern "C" __attribute__((naked)) void __not_in_flash_func(isr_hardfault)(void)
{
    __asm volatile(
        "tst lr, #4          \n" /* which stack holds the exception frame? */
        "ite eq              \n"
        "mrseq r0, msp       \n"
        "mrsne r0, psp       \n"
        "mov r1, lr          \n"
        "b hardfault_report  \n");
}

/* GPIO/I2C pad state refreshed by host_tick(), consumed by S9xReadJoypad
 * (port_glue.cpp) and wantsMenu(). Encoding: see wiipad.h. */
uint16_t wiipad_raw_cached = 0;
/* Frame counter for the rapid-fire A/B menu setting (port_glue.cpp gates
 * the A/B bits on bit 1, giving a 15 Hz autofire). */
uint32_t g_rapid_fire_counter = 0;
/* ErrorMessage[] is owned by the framework (FrensHelpers.cpp); declared
 * extern in FrensHelpers.h. */

static uint32_t CPUFreqKHz = EMULATOR_CLOCKFREQ_KHZ;

/* SNES menu visibility — same shape as NES, drop FDS/DMG/border options
 * that don't apply. Non-const because settings might toggle entries later. */
int8_t g_settings_visibility_snes[MOPT_COUNT] = {
    0,                  /* MOPT_EXIT_GAME — set 1 at runtime when in-game */
    0,                  /* MOPT_RESET_GAME — set 1 at runtime when in-game */
    BOOTLOADER_BUILD,   /* MOPT_REBOOT_TO_LOADER */
    -1,                  /* MOPT_SAVE_RESTORE_STATE — deferred */
    1,                  /* MOPT_SCREENMODE */
    0,                  /* MOPT_SCANLINES — superseded by Screen Mode */
    HSTX,               /* MOPT_SCANLINE_TYPE */
    1,                  /* MOPT_FPS_OVERLAY */
    1,                  /* MOPT_AUDIO_ENABLE */
    1,                  /* MOPT_FRAMESKIP */
    HSTX && ENABLEDVI,  /* MOPT_DISPLAY_MODE */
    EXT_AUDIO_IS_ENABLED,
    1,                  /* MOPT_FONT_COLOR */
    1,                  /* MOPT_FONT_BACK_COLOR */
    ENABLE_VU_METER,
    (HW_CONFIG == 8),   /* MOPT_FRUITJAM_VOLUME_CONTROL */
    0,                  /* MOPT_DMG_PALETTE — NES/GB only */
    0,                  /* MOPT_BORDER_MODE — GB only */
    1,                  /* MOPT_RAPID_FIRE_ON_A */
    1,                  /* MOPT_RAPID_FIRE_ON_B */
    0,                  /* MOPT_AUTO_INSERT_FDS_DISK_A — NES/FDS only */
    0,                  /* MOPT_AUTO_SWAP_FDS_DISK — NES/FDS only */
    0,                  /* MOPT_FDS_DISK_SWAP — NES/FDS only */
    HSTX,                  /* MOPT_OVERCLOCK — already overclocked by default here */
    0,                  /* MOPT_FM_AUDIO — SMS-only */
    1,                  /* MOPT_ENTER_BOOTSEL_MODE */
    1,                  /* MOPT_CONTROLLER_TEST */
};

static const uint8_t g_available_screen_modes_snes[] = {
    1,  /* SCANLINE_8_7 */
    1,  /* NOSCANLINE_8_7 */
    1,  /* SCANLINE_1_1 */
    1,  /* NOSCANLINE_1_1 */
};

/* -------------------------------------------------------------------------
 * Audio pump — pull stereo samples from snes9x and push into the
 * framework's HDMI Data Island queue (or external I2S if enabled).
 *
 * audio_route_to_ext() is the single source of truth for where samples go
 * (setting on, OR headphone jack plugged in). Pacing and routing MUST use
 * the same answer: pacing against one queue while enqueueing into the
 * other lets the mixer run unthrottled — the unfed queue never fills, so
 * the real sink overflows and drops samples, garbling the audio. */
static inline bool audio_route_to_ext(void)
{
#if EXT_AUDIO_IS_ENABLED
    return settings.flags.useExtAudio || Frens::isHeadPhoneJackConnected();
#else
    return false;
#endif
}

static int audio_free_samples(bool toExtAudio)
{
#if EXT_AUDIO_IS_ENABLED
    if (toExtAudio) {
        return audio_i2s_get_freebuffer_size();
    }
#else
    (void)toExtAudio;
#endif
#if HSTX
    int level = hstx_di_queue_get_level();
    int free_packets = HSTX_AUDIO_DI_HIGH_WATERMARK - level;
    if (free_packets <= 0) return 0;
    return free_packets << 2;  /* 4 samples per DI packet */
#else
    return 0;
#endif
}

#if !(HSTX && MIX_ON_CORE1)
static int16_t mix_buf[512];  /* up to 256 stereo frames per pump call */
#endif

#if HSTX && BLIT_ON_CORE1
/* -------------------------------------------------------------------------
 * Off-load the GFX.Screen (PSRAM) → HSTX framebuffer (SRAM) blit to core1.
 * core1's video work all happens in dma_irq_handler; its thread context
 * (video_output_core1_run) is an idle watchdog loop with a background-task
 * hook, so the ~2.5 ms blit runs there for free while core0 emulates the
 * next (frameskipped) SNES frame.
 *
 * Safety: GFX.Screen is only written by S9xMainLoop on rendered frames.
 * Core0 submits the job right after a rendered frame and never touches
 * GFX.Screen until the next rendered frame, so the only sync needed is
 * "wait for pending==0 before starting a rendered S9xMainLoop" (and before
 * the menu repaints the framebuffer). With frameskip on, the blit has two
 * whole skip-frames (~33 ms) to finish; the wait never actually spins. */
static struct {
    const uint16_t *src;
    uint16_t       *dst;
    int             rows;
    volatile bool   pending;
} blit_job;

static void __not_in_flash_func(core1_blit_task)(void)
{
    if (!blit_job.pending) return;
    __dmb();  /* order: job fields were written before pending was set */
    const uint16_t * __restrict src = blit_job.src;
    uint16_t       * __restrict dst = blit_job.dst;
    for (int y = 0; y < blit_job.rows; y++) {
        memcpy(dst, src, SNES_WIDTH * sizeof(uint16_t));
        src += SNES_WIDTH;
        dst += 320;
    }
    __dmb();
    blit_job.pending = false;
}

static inline void blit_wait_done(void)
{
    while (blit_job.pending) tight_loop_contents();
    __dmb();
}

static inline void blit_submit(const uint16_t *src, uint16_t *dst, int rows)
{
    blit_job.src  = src;
    blit_job.dst  = dst;
    blit_job.rows = rows;
    __dmb();
    blit_job.pending = true;
}
#endif

#if HSTX && MIX_ON_CORE1
/* -------------------------------------------------------------------------
 * Audio mixing + HDMI data-island encode on core1. Two wins:
 *   - core0 gets ~850 us/frame back (the entire pump_audio cost);
 *   - the DI queue is topped up continuously instead of once per frame
 *     capped at 256 samples — which could never keep up with the 533
 *     samples/frame the scan-out consumes at 32 kHz, so audio was
 *     chronically interleaved with silence-fallback packets.
 * Exclusion vs the SPC700's DSP writes (core0): port_sound_lock, taken
 * per 64-sample chunk here and around S9xSetAPUDSP there. The park
 * protocol makes the mixer quiescent for the menu (whose wavplayer is
 * the other DI-queue producer) and during sound init/teardown. */
extern "C" {
    void port_sound_lock_init(void);
    void port_sound_lock(void);
    void port_sound_unlock(void);
}
#if PROFILE_BUCKETS
extern "C" bool g_prof_bypass_apu;   /* defined in the PROFILE block below */
#endif

static volatile bool mix_c1_enable = false;
static volatile bool mix_c1_parked = true;
static int16_t mix_buf_c1[128];   /* one 64-stereo-frame chunk */

static void __not_in_flash_func(core1_mix_task)(void)
{
    if (!mix_c1_enable) {
        mix_c1_parked = true;
        return;
    }
    mix_c1_parked = false;
#if ENABLE_VU_METER
    /* All NeoPixel writes stay on this core: turnOffAllLeds() from core0
     * could interleave with a 5-pixel update here and leave garbage lit.
     * Clear on the on->off transition instead (menu entry/apply clears
     * separately, while the mixer is parked). */
    static bool vu_was_on;
    bool vu_on = settings.flags.enableVUMeter != 0;
    if (vu_was_on && !vu_on) turnOffAllLeds();
    vu_was_on = vu_on;
#endif
    if (!settings.flags.audioEnabled) return;
#if PROFILE_BUCKETS
    if (g_prof_bypass_apu) return;
#endif
    bool toExtAudio = audio_route_to_ext();
    int free_slots = audio_free_samples(toExtAudio);
    while (free_slots > 0 && mix_c1_enable) {
        int n = free_slots > 64 ? 64 : free_slots;
        port_sound_lock();
        S9xMixSamples(mix_buf_c1, n * 2);
        port_sound_unlock();
#if ENABLE_VU_METER
        if (vu_on) {
            for (int i = 0; i < n; i++) {
                addSampleToVUMeter(mix_buf_c1[i*2]);
            }
        }
#endif
#if EXT_AUDIO_IS_ENABLED
        if (toExtAudio) {
            for (int i = 0; i < n; i++) {
                EXT_AUDIO_ENQUEUE_SAMPLE(mix_buf_c1[i*2], mix_buf_c1[i*2+1]);
            }
        } else
#endif
        {
            for (int i = 0; i < n; i++) {
                hstx_push_audio_sample(mix_buf_c1[i*2], mix_buf_c1[i*2+1]);
            }
        }
        free_slots -= n;
    }
}

static void mix_c1_park(void)
{
    mix_c1_enable = false;
    while (!mix_c1_parked) tight_loop_contents();
    __dmb();
}

static inline void mix_c1_resume(void)
{
    __dmb();
    mix_c1_enable = true;
}
#endif

#if HSTX && (BLIT_ON_CORE1 || MIX_ON_CORE1)
/* Single background task for core1's idle loop. Blit first — it has a
 * (soft) deadline against the next rendered frame; audio has ~25 ms of
 * queue headroom. */
static void __not_in_flash_func(core1_background_task)(void)
{
#if BLIT_ON_CORE1
    core1_blit_task();
#endif
#if MIX_ON_CORE1
    core1_mix_task();
#endif
}
#endif

#if PROFILE_BUCKETS
/* Frame-time accumulators, us. Reset once per second alongside the fps
 * printf. All updates happen on core0 in run_emulator() so no locking.
 * main is split by rendered vs skipped so we can see render cost
 * separately from CPU+APU cost. */
static uint32_t prof_us_host_tick;
static uint32_t prof_us_main_r;      /* S9xMainLoop on rendered frames */
static uint32_t prof_us_main_s;      /* S9xMainLoop on skipped frames  */
static uint32_t prof_frames_r;
static uint32_t prof_frames_s;
static uint32_t prof_us_blit;
static uint32_t prof_us_pump;
static uint32_t prof_us_pace;
/* SuperFX GSU cost, accumulated inside S9xSuperFXExec (fxemu.c). The GSU
 * runs inside S9xMainLoop, so this is a *subset* of mainR — sfx/mainR is
 * the share of emulation spent in the GSU. */
extern "C" uint32_t g_prof_us_sfx;
extern "C" uint32_t g_prof_sfx_runs;

/* Phase 0 A/B toggles. Cycles every ~5 s through a 4-state pattern so a
 * single UART capture gives all combinations. When bypass_apu is on we
 * toggle BOTH IAPU.APUExecuting and Settings.APUEnabled — the latter is
 * necessary because dma.c:381 and apu.c:71/104/136 re-derive APUExecuting
 * from Settings.APUEnabled on every DMA and every port r/w, so leaving
 * Settings.APUEnabled=true means the bypass is defeated within µs.
 * When bypass_pace is on we skip paceFrame() so raw emulator fps shows. */
extern "C" bool g_prof_bypass_apu;
extern "C" bool g_prof_bypass_pace;
bool g_prof_bypass_apu = false;
bool g_prof_bypass_pace = false;

static bool s_prof_apu_bypass_active = false;
static bool s_prof_saved_apuenabled  = true;
static void dbg_apply_apu_bypass(bool on)
{
    if (on && !s_prof_apu_bypass_active) {
        s_prof_saved_apuenabled = Settings.APUEnabled;
        s_prof_apu_bypass_active = true;
    } else if (!on && s_prof_apu_bypass_active) {
        Settings.APUEnabled = s_prof_saved_apuenabled;
        IAPU.APUExecuting   = s_prof_saved_apuenabled;
        s_prof_apu_bypass_active = false;
        return;
    }
    if (on) {
        Settings.APUEnabled = false;
        IAPU.APUExecuting   = false;
    }
}
#endif

/* Core0 audio pump — only compiled when mixing has NOT moved to core1
 * (core1_mix_task replaces it under MIX_ON_CORE1; keeping this compiled
 * would waste mix_buf's 1 KB of .bss, which counts against the SRAM
 * heap budget that decides whether FillRAM fits in SRAM). */
#if !(HSTX && MIX_ON_CORE1)
static void __not_in_flash_func(pump_audio)(void)
{
#if PROFILE_BUCKETS
    if (g_prof_bypass_apu) return;
#endif
#if ENABLE_VU_METER
    /* Same on->off LED clear as core1_mix_task, for the core0-pump build. */
    static bool vu_was_on;
    bool vu_on = settings.flags.enableVUMeter != 0;
    if (vu_was_on && !vu_on) turnOffAllLeds();
    vu_was_on = vu_on;
#endif
    if (!settings.flags.audioEnabled) return;

    bool toExtAudio = audio_route_to_ext();
    int free_slots = audio_free_samples(toExtAudio);
    if (free_slots <= 0) return;
    if (free_slots > 256) free_slots = 256;  /* cap to mix_buf */

    /* S9xMixSamples expects "count" = stereo*2 (number of int16 slots). */
    S9xMixSamples(mix_buf, free_slots * 2);
#if ENABLE_VU_METER
    if (vu_on) {
        for (int i = 0; i < free_slots; i++) {
            addSampleToVUMeter(mix_buf[i*2]);
        }
    }
#endif

#if EXT_AUDIO_IS_ENABLED
    if (toExtAudio) {
        for (int i = 0; i < free_slots; i++) {
            EXT_AUDIO_ENQUEUE_SAMPLE(mix_buf[i*2], mix_buf[i*2+1]);
        }
        return;
    }
#endif
#if HSTX
    for (int i = 0; i < free_slots; i++) {
        hstx_push_audio_sample(mix_buf[i*2], mix_buf[i*2+1]);
    }
#endif
}
#endif /* !(HSTX && MIX_ON_CORE1) */

/* -------------------------------------------------------------------------
 * Frame pacing — 60 Hz NTSC via PaceFrames60fps, 50 Hz PAL via sleep_until.
 * Same model as pico-infonesPlus's paceFrame().
 *
 * PACE_SOFT_60FPS replaces the vsync-locked NTSC pacer with a time-based
 * budget (target += 16716 us per frame; sleep only if ahead). The
 * pico_shared vsync pacer aligns every SNES frame to a display vsync
 * tick — great for tearing, but when frameskip is on and work per
 * SNES-frame is uneven (render 42 ms, skip 4 ms, skip 4 ms) it aligns
 * each frame to its own tick, forcing 3 SNES frames into 4 vsync
 * intervals = 45 fps even when the emulator could produce 60. Soft
 * pacing lets the emulator free-run at its true rate; may introduce
 * tearing on the blit if it lands mid-scan. */
static absolute_time_t pal_next_frame;
#if PACE_SOFT_60FPS
static absolute_time_t soft_next_frame;
#endif
static void paceFrame(bool init)
{
    if (Settings.PAL) {
        if (init) {
            pal_next_frame = make_timeout_time_us(20000);
            return;
        }
        sleep_until(pal_next_frame);
        pal_next_frame = delayed_by_us(pal_next_frame, 20000);
        /* Resync if we drift more than two frames behind. */
        if (absolute_time_diff_us(get_absolute_time(), pal_next_frame) < -40000) {
            pal_next_frame = make_timeout_time_us(20000);
        }
    } else {
#if PACE_SOFT_60FPS
        if (init) {
            soft_next_frame = make_timeout_time_us(16716);
            return;
        }
        if (absolute_time_diff_us(get_absolute_time(), soft_next_frame) > 0) {
            sleep_until(soft_next_frame);
        }
        soft_next_frame = delayed_by_us(soft_next_frame, 16716);
        /* Resync when persistently far behind schedule (a heavy scene the
         * emulator can't sustain 60fps in). Two things matter here:
         *   - Threshold big enough that resyncs are rare (200 ms).
         *   - Set target = now - 16716, NOT now + 16716: the latter puts
         *     target ahead of now and triggers a burst of 12+ ms sleeps
         *     on the next skip frames, offsetting the real emulator rate.
         *     Starting one frame in the PAST means the immediate next
         *     frame ends up naturally behind schedule — no catch-up sleeps. */
        if (absolute_time_diff_us(get_absolute_time(), soft_next_frame) < -200000) {
            soft_next_frame = delayed_by_us(get_absolute_time(), -16716);
        }
#else
        Frens::PaceFrames60fps(init);
#endif
    }
}

/* -------------------------------------------------------------------------
 * Host tick — must run once per frame. Pumps the TinyUSB host stack
 * (otherwise USB HID gamepad state never updates) and refreshes the
 * GPIO/Wii pad latches that the framework's getCurrentGamePadState()
 * reads. Mirrors the pump done in pico-infonesPlus's InfoNES_LoadFrame. */
static void host_tick(void)
{
#if NES_PIN_CLK != -1
    nespad_read_start();
#endif
    Frens::pollHeadPhoneJack();
#if NES_PIN_CLK != -1
    nespad_read_finish();
#endif
    tuh_task();
#if WII_PIN_SDA >= 0 && WII_PIN_SCL >= 0
    wiipad_raw_cached = wiipad_read();
#endif
#if ENABLE_VU_METER
    /* Physical toggle button (see vumeter.h). Only flips the setting —
     * the audio pump owning the NeoPixel PIO clears the LEDs on the
     * on->off transition. */
    if (isVUMeterToggleButtonPressed()) {
        settings.flags.enableVUMeter = !settings.flags.enableVUMeter;
        FrensSettings::savesettings();
    }
#endif
    g_rapid_fire_counter++;
}

/* Input — check for menu trigger (SELECT+START combo) on any connected
 * pad: both USB players plus the GPIO NES/SNES and Wii Classic pads
 * (without which NESPAD/Wii-only boards could never open the menu).
 * snes9x's joypad read goes through S9xReadJoypad (in port_glue.cpp),
 * called by S9xUpdateJoypads from inside S9xMainLoop. */
static bool wantsMenu(void)
{
    constexpr uint32_t combo = io::GamePadState::Button::SELECT |
                               io::GamePadState::Button::START;
    for (int i = 0; i < 2; i++)
    {
        if ((io::getCurrentGamePadState(i).buttons & combo) == combo)
            return true;
    }
    /* nespad raw and wiipad share the low-bit layout: Select=bit2, Start=bit3. */
    uint32_t aux = 0;
#if NES_PIN_CLK != -1
    aux |= nespad_states[0];
#endif
#if NES_PIN_CLK_1 != -1
    aux |= nespad_states[1];
#endif
#if WII_PIN_SDA >= 0 && WII_PIN_SCL >= 0
    aux |= wiipad_raw_cached;
#endif
    return (aux & 0x0C) == 0x0C;
}

/* -------------------------------------------------------------------------
 * snes9x bring-up: configure Settings, allocate buffers, hand the
 * PSRAM-resident ROM to LoadROM(NULL). */
static bool snes9x_setup_settings(void)
{
    memset(&Settings, 0, sizeof(Settings));
    Settings.CyclesPercentage  = 100;
    Settings.H_Max             = SNES_CYCLES_PER_SCANLINE;
    Settings.HBlankStart       = (256 * Settings.H_Max) / SNES_HCOUNTER_MAX;
    Settings.FrameTimePAL      = 20000;
    Settings.FrameTimeNTSC     = 16667;
    Settings.ControllerOption  = SNES_JOYPAD;
    Settings.SoundPlaybackRate = SNES_AUDIO_HZ;
    Settings.SoundInputRate    = SNES_AUDIO_HZ;
    Settings.SoundBufferSize   = 1024;
    Settings.SoundMixInterval  = 0;
    /* Linear interpolation in the mixer (~25-30% of MixStereo's cost).
     * Flip to true when CPU headroom recovers — mostly affects chip-music
     * tracks with high-freq detail (F-Zero, Castlevania IV). */
    Settings.InterpolatedSound = false;
    Settings.DisableSoundEcho  = false;
    Settings.Mute              = false;
    Settings.APUEnabled        = true;
    Settings.Shutdown          = true;
    return true;
}

static bool snes9x_load_rom_from_psram(uintptr_t psram_ptr, size_t romsize)
{
    if (!psram_ptr || !romsize) return false;

    /* Hand the PSRAM buffer to snes9x. LoadROM(NULL) treats Memory.ROM as
     * already populated; AllocSize doubles as "file size" in that path. */
    Memory.ROM           = (uint8_t *)psram_ptr;
    Memory.ROM_AllocSize = romsize;
    Memory.ROM_Offset    = 0;

    if (!LoadROM(NULL)) {
        snprintf(ErrorMessage, ERRORMESSAGESIZE, "Not a SNES ROM.");
        return false;
    }

    /* Reject special-chip ROMs we don't emulate. Emulated and allowed through:
     * DSP-1/2/3/4 (dsp.c), SuperFX/GSU (fxinst.c/fxemu.c), C4 (c4.c/c4emu.c),
     * OBC1 (obc1.c), S-RTC (srtc.c) and SA-1 (sa1.c/sa1cpu.c) -- so Super Mario
     * Kart, Pilotwings, Star Fox, Yoshi's Island, Mega Man X2/X3, Metal Combat,
     * Dai Kaijuu Monogatari II, Super Mario RPG and Kirby Super Star all load.
     * The S-DD1/SPC7110 decompressors have no implementation here (declared-
     * only), so those carts still bail out. Note: SETA (ST01x) and BS-X are
     * equally unimplemented but cannot be tested for -- InitROM never sets
     * Settings.SETA/BS, so such carts slip through and run without the chip. */
    if (Settings.SDD1 || Settings.SPC7110) {
        snprintf(ErrorMessage, ERRORMESSAGESIZE,
                 "Special chip ROMs not supported.");
        return false;
    }

    S9xReset();
    return true;
}

/* -------------------------------------------------------------------------
 * On-screen FPS overlay. Stamps the two-digit g_fps value into the top-left
 * of the freshly-rendered SNES frame (RGB555) after S9xMainLoop returns.
 * RENDER_TO_FB: target is the anchored framebuffer window (stride 320);
 * legacy: g_snes_private_screen (stride SNES_WIDTH) just before the blit,
 * so it rides along with it (incl. the core1 offload path) at no extra
 * sync cost. 8x8 font, white-on-black, cols 4..19 / rows 0..7. */
static void draw_fps_overlay(uint16_t *screen, int stride)
{
    const uint16_t fg = 0x7FFF;  /* white, RGB555 */
    const uint16_t bg = 0x0000;  /* black         */
    char d0 = (char)('0' + (g_fps / 10) % 10);
    char d1 = (char)('0' + (g_fps % 10));
    for (int row = 0; row < 8; row++) {
        uint16_t *dst = screen + row * stride + 4;
        char s0 = getcharslicefrom8x8font(d0, row);  /* LSB = leftmost pixel */
        char s1 = getcharslicefrom8x8font(d1, row);
        for (int b = 0; b < 8; b++) { *dst++ = (s0 & 1) ? fg : bg; s0 >>= 1; }
        for (int b = 0; b < 8; b++) { *dst++ = (s1 & 1) ? fg : bg; s1 >>= 1; }
    }
}

/* -------------------------------------------------------------------------
 * Cartridge battery SRAM persistence. snes9x keeps the save in Memory.SRAM;
 * the real battery size is Memory.SRAMMask+1 when Memory.SRAMSize>0 (and there
 * is no battery when SRAMSize==0). S-RTC carts now load, but snes9x only writes
 * its RTC trailer past the battery in S9xSRTCPreSaveState, which this port never
 * calls (no save states) -- so there is still no trailer to persist, and the
 * in-game clock restarts each power cycle. Saves live in /SAVES/SNES/<rom>.SAV.
 *
 * FIL (~550 B, embeds a 512 B sector window) and FILINFO (~276 B) are far too
 * large for the 3 KB core0 stack (PICO_STACK_SIZE) — allocate them in PSRAM via
 * Frens::f_malloc / f_free (panics on OOM, so never NULL). */
#define SNES_SAVE_DIR (GAMESAVEDIR "/SNES")   /* "/SAVES/SNES" */

static void snes_sram_path(char *out, size_t n)
{
    char base[FF_MAX_LFN];
    strncpy(base, Frens::GetfileNameFromFullPath(romName), sizeof(base) - 1);
    base[sizeof(base) - 1] = 0;
    Frens::stripextensionfromfilename(base);
    snprintf(out, n, "%s/%s.SAV", SNES_SAVE_DIR, base);
}

static void snes_load_sram(void)
{
    if (Memory.SRAMSize == 0) return;                 /* no battery */
    size_t sz = (size_t)Memory.SRAMMask + 1;
    memset(Memory.SRAM, 0, sz);                       /* deterministic first boot */

    char path[FF_MAX_LFN];
    snes_sram_path(path, sizeof(path));

    FILINFO *fno = (FILINFO *)Frens::f_malloc(sizeof(FILINFO));
    if (f_stat(path, fno) != FR_OK) {
        printf("SRAM: no save file %s\n", path);
        Frens::f_free(fno);
        return;
    }
    FIL *file = (FIL *)Frens::f_malloc(sizeof(FIL));
    if (f_open(file, path, FA_READ) == FR_OK) {
        UINT br = 0;
        UINT toread = (fno->fsize < sz) ? (UINT)fno->fsize : (UINT)sz;
        if (f_read(file, Memory.SRAM, toread, &br) == FR_OK)
            printf("SRAM: loaded %u bytes from %s\n", (unsigned)br, path);
        else
            printf("SRAM: read error %s\n", path);
        f_close(file);
    } else {
        printf("SRAM: cannot open %s for read\n", path);
    }
    Frens::f_free(file);
    Frens::f_free(fno);
}

static void snes_save_sram(void)
{
    if (Memory.SRAMSize == 0) return;                 /* no battery */
    size_t sz = (size_t)Memory.SRAMMask + 1;

    f_mkdir(SNES_SAVE_DIR);                            /* idempotent; FR_EXIST ok */

    char path[FF_MAX_LFN];
    snes_sram_path(path, sizeof(path));

    FIL *file = (FIL *)Frens::f_malloc(sizeof(FIL));
    if (f_open(file, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
        UINT bw = 0;
        if (f_write(file, Memory.SRAM, (UINT)sz, &bw) == FR_OK)
            printf("SRAM: saved %u bytes to %s\n", (unsigned)bw, path);
        else
            printf("SRAM: write error %s\n", path);
        f_close(file);
    } else {
        printf("SRAM: cannot open %s for write\n", path);
    }
    Frens::f_free(file);
}

/* -------------------------------------------------------------------------
 * One emulator session — runs until the user quits to the ROM menu.
 * In-game reset is handled in place via S9xReset(). */
static void run_emulator(void)
{
#if !RENDER_TO_FB
    /* Compute centered placement of native SNES frame in 320x240 HSTX FB. */
    const int snes_h = Settings.PAL ? SNES_HEIGHT_EXTENDED : SNES_HEIGHT;
    const int marginTop = (240 - snes_h) / 2;
    const int marginLeft = (320 - SNES_WIDTH) / 2;
#endif

#if HSTX
    uint16_t * const fb = (uint16_t *)hstx_getframebuffer();
    /* Clear border once. SNES region gets overwritten every rendered frame. */
    memset(fb, 0, 320 * 240 * sizeof(uint16_t));
#endif

    if (!S9xInitDisplay()) { snprintf(ErrorMessage, ERRORMESSAGESIZE, "Display init failed"); return; }
    Frens::dumpHeapStats("after-Display");
    if (!S9xInitGFX())     { snprintf(ErrorMessage, ERRORMESSAGESIZE, "GFX init failed");     return; }
    Frens::dumpHeapStats("after-GFX");

#if HSTX && MIX_ON_CORE1
    /* Sound is fully initialized (S9xInitSound + S9xSetPlaybackRate ran
     * in main()) — core1 may start mixing. */
    mix_c1_resume();
#endif

    paceFrame(true);

    uint32_t frame = 0;
    uint8_t  skipFrames = 0;
#if HSTX && RENDER_TO_FB
    int last_screen_h = PPU.ScreenHeight;
#endif
#if HSTX
    uint32_t audio_min_level = UINT32_MAX;
    uint32_t audio_underruns_last = hstx_di_queue_get_underrun_count();
#endif
    while (true) {
#if PROFILE_BUCKETS
        uint32_t t0 = time_us_32();
#endif
        host_tick();
#if PROFILE_BUCKETS
        uint32_t t1 = time_us_32(); prof_us_host_tick += (t1 - t0);
#endif

        if (wantsMenu()) {
#if HSTX && BLIT_ON_CORE1
            /* Menu repaints the framebuffer — wait for any in-flight blit. */
            blit_wait_done();
#endif
#if HSTX && MIX_ON_CORE1
            /* The menu's wavplayer is the other DI-queue producer, and
             * menu actions may touch sound state — park the mixer. */
            mix_c1_park();
#endif
            int r = showSettingsMenu(true);
            if (r == 3) {
#if 0
                if ((clock_get_hz(clk_sys) / 1000) > EMULATOR_CLOCKFREQ_KHZ)
                {
                    /* Quit game. Instead of returning to main()'s in-place
                     * teardown + menu re-entry (S9xDeinit*, core1 park/resume,
                     * wav-player reset, screen-mode switch) — a fragile sequence
                     * that intermittently core1-hardfaults at the 504 MHz OC
                     * margin (undefined-instruction fetch glitch during the
                     * transition) — flush the battery save and hard-reboot to a
                     * clean boot + fresh ROM menu. The mixer is already parked;
                     * watchdog_reboot resets both cores and all peripherals, so
                     * there is no teardown to get wrong. */
                    snes_save_sram();
                    watchdog_reboot(0, 0, 0);
                    while (true)
                        tight_loop_contents(); /* not reached */
                }
#endif
                return;
            }
            if (r == 5) {
                /* Reset game. Do it while the mixer is still parked —
                 * S9xReset reinitializes the APU/DSP state core1 mixes
                 * from. playback_rate is untouched, so audio survives. */
                S9xReset();
            }
            /* Repaint border in case the menu touched the framebuffer. */
#if HSTX
            memset(fb, 0, 320 * 240 * sizeof(uint16_t));
#endif
#if HSTX && MIX_ON_CORE1
            mix_c1_resume();
#endif
            paceFrame(true);
        }

#if HSTX && RENDER_TO_FB
        /* Overscan bit flipped ScreenHeight 224<->239: re-anchor the
         * centered copy-out window and repaint the border. (A flip
         * mid-rendered-frame is contained by the EndY clamp in gfx.c
         * until this catches it.) */
        if (PPU.ScreenHeight != last_screen_h) {
            last_screen_h = PPU.ScreenHeight;
            s9x_port_anchor_screen();
            memset(fb, 0, 320 * 240 * sizeof(uint16_t));
        }
#endif

        IPPU.RenderThisFrame = (skipFrames == 0);

#if HSTX && BLIT_ON_CORE1
        /* S9xMainLoop overwrites GFX.Screen on rendered frames; the
         * previous blit must have consumed it first. With frameskip on
         * the blit finished two frames ago and this never spins. */
        if (IPPU.RenderThisFrame) blit_wait_done();
#endif

#if PROFILE_BUCKETS
        /* Re-apply the A/B toggle each frame: bypass may have been
         * defeated by DMA / port writes since last frame. */
        dbg_apply_apu_bypass(g_prof_bypass_apu);
        bool rendered_this_frame = IPPU.RenderThisFrame;
        uint32_t t2 = time_us_32();
#endif
        S9xMainLoop();
#if PROFILE_BUCKETS
        uint32_t t3 = time_us_32();
        {
            uint32_t d = t3 - t2;
            if (rendered_this_frame) { prof_us_main_r += d; prof_frames_r++; }
            else                     { prof_us_main_s += d; prof_frames_s++; }
        }
#endif

#if HSTX && RENDER_TO_FB
        /* The strip renderer already copied the finished frame into the
         * framebuffer window chunk by chunk — just stamp the FPS digits
         * on top. */
        if (IPPU.RenderThisFrame && settings.flags.displayFrameRate)
            draw_fps_overlay(s9x_port_fb_window, 320);
#elif HSTX
        /* Blit private PSRAM screen → HSTX SRAM framebuffer, centered.
         * 256*224*2 = 112 KB per NTSC frame (~2.5 ms of PSRAM reads). */
        if (IPPU.RenderThisFrame) {
            /* Stamp the FPS digits into the source frame before the blit so
             * they ride along with it (incl. the core1 offload) — no extra
             * sync, no single-buffered-scanout timing hazard. */
            if (settings.flags.displayFrameRate)
                draw_fps_overlay(g_snes_private_screen, SNES_WIDTH);
#if BLIT_ON_CORE1
            /* Hand the copy to core1's idle loop; core0 moves straight on
             * to emulating the next frame. */
            blit_submit(g_snes_private_screen,
                        fb + marginTop * 320 + marginLeft, snes_h);
#else
            const uint16_t * __restrict src = g_snes_private_screen;
            uint16_t       * __restrict dst = fb + marginTop * 320 + marginLeft;
            for (int y = 0; y < snes_h; y++) {
                memcpy(dst, src, SNES_WIDTH * sizeof(uint16_t));
                src += SNES_WIDTH;
                dst += 320;
            }
#endif
        }
#endif
#if PROFILE_BUCKETS
        uint32_t t4 = time_us_32(); prof_us_blit += (t4 - t3);
#endif

        if (skipFrames == 0) {
            /* frameSkip=true → skip frames to buy back frame budget. The
             * 256x224 blit + the snes9x renderer (RenderScreen/RenderLine/
             * Draw*) is the single biggest non-CPU cost. Super FX games
             * lean hardest on it, so they render 1 frame of every 3
             * (skip 2); all other games render every other frame (skip 1). */
            skipFrames = settings.flags.frameSkip ? (Settings.SuperFX ? 2 : 1) : 0;
        } else {
            skipFrames--;
        }

#if !(HSTX && MIX_ON_CORE1)
        pump_audio();
#endif
#if PROFILE_BUCKETS
        uint32_t t5 = time_us_32(); prof_us_pump += (t5 - t4);
        if (!g_prof_bypass_pace)
#endif
        paceFrame(false);
#if PROFILE_BUCKETS
        uint32_t t6 = time_us_32(); prof_us_pace += (t6 - t5);
#endif
        frame++;

#if HSTX
        /* Audio health tracking (always on — dropouts were reported in
         * release builds): min DI-queue level per second, sampled once
         * per frame. Underrun ground truth comes from the queue itself.
         * Only meaningful when audio routes to HDMI — with external I2S
         * audio the DI queue receives nothing and underruns by design. */
#if EXT_AUDIO_IS_ENABLED
        if (!audio_route_to_ext())
#endif
        {
            uint32_t lvl = hstx_di_queue_get_level();
            if (lvl < audio_min_level) audio_min_level = lvl;
        }
#endif

#if PROFILE_BUCKETS
        /* 4-state cycle, ~5 s per state (starts at state 0 so the first
         * boot handshakes settle before the first flip):
         *   0: baseline (APU on,  pace on)
         *   1: no APU   (APU off, pace on)
         *   2: no pace  (APU on,  pace off) — raw emulator fps
         *   3: no both  (APU off, pace off) */
        {
            static uint64_t ab_t0_us = 0;
            static uint8_t  ab_state = 0;
            uint64_t nowab = Frens::time_us();
            if (ab_t0_us == 0) ab_t0_us = nowab;
            if (nowab - ab_t0_us >= 5000000) {
                ab_state = (ab_state + 1) & 3;
                g_prof_bypass_apu  = (ab_state & 1) != 0;
                g_prof_bypass_pace = (ab_state & 2) != 0;
                ab_t0_us = nowab;
            }
        }
#endif

        /* Once-per-second window: drive the on-screen FPS overlay (g_fps) and
         * the audio-health readout below. The frame count over ~1 s is the
         * windowed average the overlay wants — immune to soft-pacing/frameskip
         * flicker. */
        static uint64_t fps_t0_us = 0;
        static uint32_t fps_f0 = 0;
        uint64_t now = Frens::time_us();
        if (fps_t0_us == 0) { fps_t0_us = now; fps_f0 = frame; }
        else if (now - fps_t0_us >= 1000000) {
            uint32_t delta = frame - fps_f0;
            g_fps = delta;
#if PROFILE_BUCKETS
            uint32_t d  = delta ? delta : 1;
            uint32_t dr = prof_frames_r ? prof_frames_r : 1;
            uint32_t ds = prof_frames_s ? prof_frames_s : 1;
            printf("fps=%lu PAL=%d skip=%d bypAPU=%d bypPACE=%d "
                   "us/frm host=%lu mainR=%lu(x%lu) mainS=%lu(x%lu) "
                   "blit=%lu pump=%lu pace=%lu sfx=%lu(x%lu)\n",
                   (unsigned long)delta, (int)Settings.PAL, settings.flags.frameSkip,
                   (int)g_prof_bypass_apu, (int)g_prof_bypass_pace,
                   (unsigned long)(prof_us_host_tick / d),
                   (unsigned long)(prof_us_main_r / dr), (unsigned long)prof_frames_r,
                   (unsigned long)(prof_us_main_s / ds), (unsigned long)prof_frames_s,
                   (unsigned long)(prof_us_blit / d),
                   (unsigned long)(prof_us_pump / d),
                   (unsigned long)(prof_us_pace / d),
                   (unsigned long)(g_prof_us_sfx / d),
                   (unsigned long)(g_prof_sfx_runs / d));
            prof_us_host_tick = prof_us_main_r = prof_us_main_s =
                prof_us_blit = prof_us_pump = prof_us_pace = 0;
            prof_frames_r = prof_frames_s = 0;
            g_prof_us_sfx = g_prof_sfx_runs = 0;
#endif
#if HSTX
#if EXT_AUDIO_IS_ENABLED
            if (!audio_route_to_ext())
#endif
            {
                uint32_t ur = hstx_di_queue_get_underrun_count();
                /* Only chatter when audio health is abnormal. minlvl is
                 * DI packets (4 samples each); watermark is 200. */
                if (ur != audio_underruns_last || audio_min_level < 20) {
                    printf("audio: underruns+%lu minlvl=%lu resyncs=%d\n",
                           (unsigned long)(ur - audio_underruns_last),
                           (unsigned long)audio_min_level,
                           get_video_output_resync_count());
                }
                audio_underruns_last = ur;
                audio_min_level = UINT32_MAX;
            }
#endif
            fps_t0_us = now;
            fps_f0 = frame;
        }
    }
}

int main()
{
    romName = selectedRom;
    ErrorMessage[0] = selectedRom[0] = 0;
    // Set min/max CPU freq and voltage limits for this board for overclocking. 
    // For 504 Mhz, 1.7V seems the minimum stable voltage.  Should run fine at 1.6 or 1.65 Volt, but
    // may cause hardfaults on heavy scenes. 1.7V is the safe limit for 504 Mhz. 
    // BUT CAN CAUSE DAMAGE !!!!!!
    //1.6V is the safe limit for 378 Mhz.
    Frens::setOverclockLimits(EMULATOR_CLOCKFREQ_KHZ,  EMULATOR_MAX_CLOCKFREQ_KHZ, 
                              vreg_voltage::VREG_VOLTAGE_1_60,vreg_voltage::VREG_VOLTAGE_1_70);
   
     Frens::FlashParams *flashParams;
    // assign flashParams to point to flash location

    flashParams = (Frens::FlashParams *)FLASHPARAM_ADDRESS;
    vreg_voltage voltage = vreg_voltage::VREG_VOLTAGE_1_60;
    if ( Frens::validateFlashParams(*flashParams) ) {
        CPUFreqKHz = flashParams->cpuFreqKHz;
        voltage = flashParams->voltage;
    }
   
    Frens::setClocksAndStartStdio(CPUFreqKHz, voltage);
    Frens::dumpHeapStats("startup");

    printf("==========================================================================================\n");
    printf("pico_snesPlus (snes9x core)\n");
    printf("Build: %s %s\n", __DATE__, __TIME__);
    printf("CPU freq: %d kHz\n", clock_get_hz(clk_sys) / 1000);
#if HSTX
    printf("HSTX freq: %d kHz\n", clock_get_hz(clk_hstx) / 1000);
#endif
    printf("Stack size: %d bytes\n", PICO_STACK_SIZE);
    printf("==========================================================================================\n");

    FrensSettings::initSettings(FrensSettings::emulators::SNES);

#if HSTX && (BLIT_ON_CORE1 || MIX_ON_CORE1)
    /* Register BEFORE initAll launches core1: background_task isn't
     * volatile in video_output.c, so core1's loop may legally cache it —
     * registering first guarantees visibility. The task no-ops until the
     * first blit_submit() / mix_c1_resume(). */
#if MIX_ON_CORE1
    port_sound_lock_init();
#endif
    video_output_set_background_task(core1_background_task);
#endif

    /* Framebuffer mode + 1024-byte audio buffer on RP2350. */
    isFatalError = !Frens::initAll(selectedRom, CPUFreqKHz, 0, 0,
                                   AUDIOBUFFERSIZE, false, true);
    Frens::dumpHeapStats("after-initAll");

#if HSTX
    /* Override the 44.1 kHz default that hstx_init() in pico_shared
     * hardcodes. Reconfigures the ACR N/CTS values and the DI queue's
     * samples-per-line accumulator to match SNES_AUDIO_HZ. */
    pico_hdmi_set_audio_sample_rate(SNES_AUDIO_HZ);
#endif

    Frens::applyScreenMode(settings.screenMode);

    g_settings_visibility = g_settings_visibility_snes;
    g_available_screen_modes = g_available_screen_modes_snes;

    /* Skip the splash when we got here via the quit-game reboot
     * (watchdog_reboot in run_emulator) — it should feel like a snappy return
     * to the ROM menu, not a fresh power-on. A cold/power-on boot still shows
     * it (watchdog_caused_reboot() is false then). */
    bool showSplash = !watchdog_caused_reboot();

    while (true) {
        if (selectedRom[0] == 0) {
            const char *romExtensions = ".smc .sfc";
            menu("Pico-snes9x+", ErrorMessage, isFatalError, showSplash,
                 romExtensions, selectedRom);
            showSplash = false;
            printf("Selected ROM: %s\n", selectedRom);
            Frens::dumpHeapStats("after-menu");
        }

        /* The framework's menu already copied the ROM into PSRAM and
         * stored the pointer in ROM_FILE_ADDR. We just need its size. */
        FIL *fil = (FIL *)Frens::f_malloc(sizeof(FIL));
        size_t romsize = 0;
        if (f_open(fil, selectedRom, FA_READ) == FR_OK) {
            romsize = f_size(fil);
            f_close(fil);
        }
        Frens::f_free(fil);
        if (!ROM_FILE_ADDR || !romsize) {
            strcpy(ErrorMessage, "ROM load failed");
            selectedRom[0] = 0;
            continue;
        }

        ErrorMessage[0] = 0;

        /* Allocate snes9x's hot working set AFTER the menu has finished —
         * the menu itself needs ~tens of KB of SRAM for its screen buffer
         * and dialog state; trying to keep both alive at once OOMs.
         * Allocate APU (single 64 KB block) FIRST while the libc heap is
         * least fragmented; smaller allocations from S9xInitMemory then
         * fill the gaps around it. */
        snes9x_setup_settings();
        if (!S9xInitAPU())        { strcpy(ErrorMessage, "APU init failed");    selectedRom[0] = 0; continue; }
        Frens::dumpHeapStats("after-APU");
        if (!S9xInitMemory())     { strcpy(ErrorMessage, "Memory init failed"); S9xDeinitAPU(); selectedRom[0] = 0; continue; }
        Frens::dumpHeapStats("after-Memory");
        if (!S9xInitSound(0, 0))  { strcpy(ErrorMessage, "Sound init failed");  S9xDeinitAPU(); S9xDeinitMemory(); selectedRom[0] = 0; continue; }
        /* S9xInitSound leaves so.playback_rate = 0; without this the
         * mixer's channel freq table stays uninitialized and S9xMixSamples
         * returns silence. This is why audio was completely missing — the
         * call has to happen after S9xInitSound and before S9xReset (which
         * S9xInitMemory eventually triggers via LoadROM). */
        S9xSetPlaybackRate(SNES_AUDIO_HZ);
        Frens::dumpHeapStats("after-Sound");

        if (!snes9x_load_rom_from_psram(ROM_FILE_ADDR, romsize)) {
            S9xDeinitSound();
            S9xDeinitAPU();
            S9xDeinitMemory();
            selectedRom[0] = 0;
            continue;
        }
        Frens::dumpHeapStats("after-LoadROM");

        /* Restore battery SRAM from SD before the session starts. LoadROM's
         * S9xReset does not clear Memory.SRAM, so the loaded save survives. */
        snes_load_sram();

        run_emulator();

        /* Flush battery SRAM back to SD before S9xDeinitMemory frees it. */
        snes_save_sram();

        /* Return to menu: tear down all snes9x state so the menu has room. */
        S9xDeinitGFX();
        S9xDeinitDisplay();
        S9xDeinitSound();
        S9xDeinitAPU();
        S9xDeinitMemory();
        Frens::dumpHeapStats("after-deinit");

        selectedRom[0] = 0;
    }
    return 0;
}
