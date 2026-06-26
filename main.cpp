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
 * via PICO_SNESPLUS_HSTX in port.h). */

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
#include "settings.h"
#include "menu.h"
#include "menu_settings.h"
#include "gamepad.h"
#include "nespad.h"
#include "wiipad.h"

#if HSTX
#include "hstx.h"
#endif

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

/* port glue — snes9x's render target, 256-wide RGB555 in PSRAM. */
extern uint16_t *g_snes_private_screen;

#define EMULATOR_CLOCKFREQ_KHZ 378000   /* RP2350 overclock — 378 MHz */
#define AUDIOBUFFERSIZE 1024
/* Dropped from 44100 to 32000 to cut SPC700 sample-synthesis work by
 * ~27%. 32 kHz is in the HDMI ACR lookup table (N=4096, CTS=25200) so
 * the HDMI receiver still clock-recovers correctly. We also have to
 * re-call pico_hdmi_set_audio_sample_rate() after Frens::initAll
 * because hstx_init() in pico_shared hardcodes 44100. */
#define SNES_AUDIO_HZ 32000

bool isFatalError = false;
char *romName = nullptr;
char selectedRom[FF_MAX_LFN] = {0};
/* ErrorMessage[] is owned by the framework (FrensHelpers.cpp); declared
 * extern in FrensHelpers.h. */

static uint32_t CPUFreqKHz = EMULATOR_CLOCKFREQ_KHZ;

/* SNES menu visibility — same shape as NES, drop FDS/DMG/border options
 * that don't apply. Non-const because settings might toggle entries later. */
int8_t g_settings_visibility_snes[MOPT_COUNT] = {
    0,                  /* MOPT_EXIT_GAME — set 1 at runtime when in-game */
    0,                  /* MOPT_RESET_GAME — set 1 at runtime when in-game */
    BOOTLOADER_BUILD,   /* MOPT_REBOOT_TO_LOADER */
    0,                  /* MOPT_SAVE_RESTORE_STATE — deferred */
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
    0,                  /* MOPT_OVERCLOCK — already overclocked by default here */
    1,                  /* MOPT_ENTER_BOOTSEL_MODE */
};

static const uint8_t g_available_screen_modes_snes[] = {
    1,  /* SCANLINE_8_7 */
    1,  /* NOSCANLINE_8_7 */
    1,  /* SCANLINE_1_1 */
    1,  /* NOSCANLINE_1_1 */
};

/* -------------------------------------------------------------------------
 * Audio pump — pull stereo samples from snes9x and push into the
 * framework's HDMI Data Island queue (or external I2S if enabled). */
static int audio_free_samples(void)
{
#if EXT_AUDIO_IS_ENABLED
    if (settings.flags.useExtAudio) {
        return audio_i2s_get_freebuffer_size();
    }
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

static int16_t mix_buf[512];  /* up to 256 stereo frames per pump call */

static void __not_in_flash_func(pump_audio)(void)
{
    if (!settings.flags.audioEnabled) return;

    int free_slots = audio_free_samples();
    if (free_slots <= 0) return;
    if (free_slots > 256) free_slots = 256;  /* cap to mix_buf */

    /* S9xMixSamples expects "count" = stereo*2 (number of int16 slots). */
    S9xMixSamples(mix_buf, free_slots * 2);

#if EXT_AUDIO_IS_ENABLED
    if (settings.flags.useExtAudio) {
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

/* -------------------------------------------------------------------------
 * Frame pacing — 60 Hz NTSC via PaceFrames60fps, 50 Hz PAL via sleep_until.
 * Same model as pico-infonesPlus's paceFrame(). */
static absolute_time_t pal_next_frame;
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
        Frens::PaceFrames60fps(init);
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
    (void)wiipad_read();
#endif
}

/* Input — check for menu trigger (SELECT+START combo). snes9x's joypad
 * read goes through S9xReadJoypad (in port_glue.cpp), called by
 * S9xUpdateJoypads from inside S9xMainLoop. */
static bool wantsMenu(void)
{
    auto &pad = io::getCurrentGamePadState(0);
    constexpr uint32_t combo = io::GamePadState::Button::SELECT |
                               io::GamePadState::Button::START;
    return (pad.buttons & combo) == combo;
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

    /* Reject special-chip ROMs explicitly — we don't emulate them. */
    if (Settings.SuperFX || Settings.SA1 || Settings.SDD1 || Settings.C4 ||
        Settings.DSP || Settings.SPC7110 || Settings.OBC1 || Settings.SRTC) {
        snprintf(ErrorMessage, ERRORMESSAGESIZE,
                 "Special chip ROMs not supported.");
        return false;
    }

    S9xReset();
    return true;
}

/* -------------------------------------------------------------------------
 * One emulator session — runs until user exits or requests reset. */
static void run_emulator(void)
{
    /* Compute centered placement of native SNES frame in 320x240 HSTX FB. */
    const int snes_h = Settings.PAL ? SNES_HEIGHT_EXTENDED : SNES_HEIGHT;
    const int marginTop = (240 - snes_h) / 2;
    const int marginLeft = (320 - SNES_WIDTH) / 2;

#if HSTX
    uint16_t * const fb = (uint16_t *)hstx_getframebuffer();
    /* Clear border once. SNES region gets overwritten every frame by the blit. */
    memset(fb, 0, 320 * 240 * sizeof(uint16_t));
#endif

    if (!S9xInitDisplay()) { snprintf(ErrorMessage, ERRORMESSAGESIZE, "Display init failed"); return; }
    Frens::dumpHeapStats("after-Display");
    if (!S9xInitGFX())     { snprintf(ErrorMessage, ERRORMESSAGESIZE, "GFX init failed");     return; }
    Frens::dumpHeapStats("after-GFX");

    paceFrame(true);

    uint32_t frame = 0;
    uint8_t  skipFrames = 0;
    while (true) {
        host_tick();

        if (wantsMenu()) {
            int r = showSettingsMenu(true);
            (void)r;
            /* Repaint border in case the menu touched the framebuffer. */
#if HSTX
            memset(fb, 0, 320 * 240 * sizeof(uint16_t));
#endif
            paceFrame(true);
        }

        IPPU.RenderThisFrame = (skipFrames == 0);

        S9xMainLoop();

#if HSTX
        /* Blit private PSRAM screen → HSTX SRAM framebuffer, centered.
         * 256*224*2 = 112 KB per NTSC frame, dominated by PSRAM read
         * bandwidth (~70 MB/s) — finishes well before HSTX scan reaches
         * the SNES region on the next frame, eliminating mid-write tearing. */
        if (IPPU.RenderThisFrame) {
            const uint16_t * __restrict src = g_snes_private_screen;
            uint16_t       * __restrict dst = fb + marginTop * 320 + marginLeft;
            for (int y = 0; y < snes_h; y++) {
                memcpy(dst, src, SNES_WIDTH * sizeof(uint16_t));
                src += SNES_WIDTH;
                dst += 320;
            }
        }
#endif

        if (skipFrames == 0) {
            /* frameSkip=true → render 1 frame of every 3. The 256x224
             * blit + the snes9x renderer (RenderScreen/RenderLine/Draw*)
             * is the single biggest non-CPU cost; cutting render rate
             * from 1/2 to 1/3 buys back substantial frame budget. */
            skipFrames = settings.flags.frameSkip ? 2 : 0;
        } else {
            skipFrames--;
        }

        pump_audio();
        paceFrame(false);
        frame++;

        /* UART FPS readout once per second (no on-screen overlay yet). */
        static uint64_t fps_t0_us = 0;
        static uint32_t fps_f0 = 0;
        uint64_t now = Frens::time_us();
        if (fps_t0_us == 0) { fps_t0_us = now; fps_f0 = frame; }
        else if (now - fps_t0_us >= 1000000) {
            uint32_t delta = frame - fps_f0;
            printf("fps=%lu PAL=%d skip=%d\n",
                   (unsigned long)delta, (int)Settings.PAL, settings.flags.frameSkip);
            fps_t0_us = now;
            fps_f0 = frame;
        }
    }
}

int main()
{
    romName = selectedRom;
    ErrorMessage[0] = selectedRom[0] = 0;

    /* 378 MHz needs ~1.30 V on RP2350; below that the PLL is unstable
     * and you get sporadic hangs in flash XIP and PSRAM reads. Higher
     * overclocks (432, 480, 504) were tried on this Fruit Jam board
     * and all hard-faulted under sustained emulator load — this chip
     * lot doesn't tolerate >378 MHz reliably even with relaxed QMI
     * timing for flash and PSRAM. The SRAM-code / PSRAM-buffer /
     * -O3 / IAPU+Memory.Map-in-SRAM wins are independent of the
     * overclock and remain in place. */
    Frens::setClocksAndStartStdio(CPUFreqKHz, VREG_VOLTAGE_1_30);
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

    bool showSplash = true;

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
        FIL fil;
        size_t romsize = 0;
        if (f_open(&fil, selectedRom, FA_READ) == FR_OK) {
            romsize = f_size(&fil);
            f_close(&fil);
        }

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

        run_emulator();

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
