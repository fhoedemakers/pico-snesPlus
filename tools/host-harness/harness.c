/* Host-side test harness for the pico_snesPlus vendored snes9x core.
 *
 * Boots a ROM exactly the way main.cpp does on the RP2350 and dumps
 * rendered frames as PPM images, so render bugs can be reproduced and
 * bisected on a desktop machine in seconds instead of on hardware.
 * Used to find and fix the DKC "Nintendo presents" mode-5 strip-seam bug.
 *
 * Build variants (see build.sh):
 *   fb1_nolut — RENDER_TO_FB=1: strip-renderer mimic (16-row strips +
 *               copy-out into a 320x240 framebuffer), the device render flow
 *   fb0_nolut — RENDER_TO_FB=0: classic full-frame render, device color math
 *   fb0_lut   — RENDER_TO_FB=0 with the upstream ZERO-LUT color math
 *
 * Byte-comparing PPMs between variants isolates a bug's layer:
 *   fb1 vs fb0 differs  -> strip renderer;  fb0_nolut vs fb0_lut differs
 *   -> LUT-free color math.
 *
 * Env: TRACE_FROM=<frame> logs strip chunk ranges (fb1) and PPU registers
 * per frame from that frame on.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "snes9x.h"
#include "memmap.h"
#include "apu.h"
#include "gfx.h"
#include "ppu.h"
#include "soundux.h"
#include "display.h"
#include "port_alloc.h"

/* ---- allocator: both tiers are plain malloc on the host -------------- */
void *port_alloc_sram(size_t bytes)  { return malloc(bytes); }
void *port_alloc_psram(size_t bytes) { return malloc(bytes); }
void  port_alloc_free(void *p)       { free(p); }

/* ---- MSU-1 backend: stdio stand-in for msu1_port.cpp's FatFs ---------- */
#if ENABLE_MSU1
#include <time.h>
#include "msu1.h"

static char  msu_base[1024];   /* rom path with the extension stripped */
static FILE *msu_data_fp;
static FILE *msu_track_fp;

/* Virtual wall clock, advanced one frame period per emulated frame.
 *
 * msu1.c sizes each refill from elapsed real time, because audio is consumed
 * in real time while msu1_pump runs once per video frame. The harness
 * deliberately runs *faster* than real time, so a true clock would report
 * microseconds between pumps and starve the ring — with a virtual clock the
 * core sees exactly the interval it would see on hardware. FPS=<n> models a
 * game running below 60, which is the case that starves a fixed-size budget
 * (Zelda's intro FMV at 40 fps).
 *
 * Consequence: the us figures in the MSU1: line are meaningless here; read
 * cost can only be measured on the device. */
static uint32_t msu_vclock_us;
static uint32_t msu_frame_us = 1000000u / 60u;

uint32_t msu1_backend_now_us(void)
{
    return msu_vclock_us;
}

static FILE *msu_open(const char *suffix, long *size_out)
{
    char path[1200];
    snprintf(path, sizeof path, "%s%s", msu_base, suffix);
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (size_out) {
        fseek(fp, 0, SEEK_END);
        *size_out = ftell(fp);
        fseek(fp, 0, SEEK_SET);
    }
    return fp;
}

bool msu1_backend_available(void)
{
    long sz;
    FILE *fp = msu_open(".msu", &sz);
    if (!fp) fp = msu_open("-1.pcm", &sz);
    if (!fp) return false;
    fclose(fp);
    return true;
}

uint32_t msu1_backend_open_data(void)
{
    long sz = 0;
    if (msu_data_fp) { fclose(msu_data_fp); msu_data_fp = NULL; }
    msu_data_fp = msu_open(".msu", &sz);
    return msu_data_fp ? (uint32_t)sz : 0u;
}

uint32_t msu1_backend_read_data(uint8_t *dst, uint32_t offset, uint32_t bytes)
{
    if (!msu_data_fp) return 0;
    if (fseek(msu_data_fp, (long)offset, SEEK_SET) != 0) return 0;
    return (uint32_t)fread(dst, 1, bytes, msu_data_fp);
}

bool msu1_backend_open_track(uint16_t track)
{
    char suffix[32];
    if (msu_track_fp) { fclose(msu_track_fp); msu_track_fp = NULL; }
    snprintf(suffix, sizeof suffix, "-%u.pcm", (unsigned)track);
    msu_track_fp = msu_open(suffix, NULL);
    return msu_track_fp != NULL;
}

uint32_t msu1_backend_read_track(uint8_t *dst, uint32_t bytes)
{
    if (!msu_track_fp) return 0;
    return (uint32_t)fread(dst, 1, bytes, msu_track_fp);
}

bool msu1_backend_seek_track(uint32_t offset)
{
    if (!msu_track_fp) return false;
    return fseek(msu_track_fp, (long)offset, SEEK_SET) == 0;
}

void msu1_backend_close(void)
{
    if (msu_data_fp)  { fclose(msu_data_fp);  msu_data_fp  = NULL; }
    if (msu_track_fp) { fclose(msu_track_fp); msu_track_fp = NULL; }
}
#endif /* ENABLE_MSU1 */

/* ---- input / peripheral stubs ---------------------------------------- */
uint32_t S9xReadJoypad(int32_t port) { (void)port; return 0; }

/* Scripted SNES Mouse (env MOUSE=1): circles the cursor around screen
 * center (so deltas keep flowing however long the run) and holds the left
 * button for 10 frames from MOUSE_CLICK on — exercises the core mouse path
 * (Mario Paint) that port_glue.cpp feeds from the USB HID mouse on device.
 * Returns false (no mouse) when MOUSE is unset, keeping existing A/B
 * render runs unchanged. */
#include <math.h>
static int      mouse_enabled;
static uint32_t mouse_click = UINT32_MAX;
static uint32_t mouse_frame;

bool S9xReadMousePosition(int32_t p, int32_t *x, int32_t *y, uint32_t *b)
{
    if (!mouse_enabled || p != 0) return false;
    double a = (double)mouse_frame * 0.05;
    *x = SNES_WIDTH  / 2 + (int32_t)(64.0 * cos(a));
    *y = SNES_HEIGHT / 2 + (int32_t)(48.0 * sin(a));
    *b = (mouse_frame >= mouse_click && mouse_frame < mouse_click + 10) ? 1 : 0;
    return true;
}
bool S9xReadSuperScopePosition(int32_t *x, int32_t *y, uint32_t *b)
{ (void)x; (void)y; (void)b; return false; }
bool JustifierOffscreen(void) { return true; }
void JustifierButtons(uint32_t *b) { (void)b; }
void S9xToggleSoundChannel(int32_t c) { (void)c; }

/* ---- display ---------------------------------------------------------- */
#define FB_WIDTH  320
#define FB_HEIGHT 240
static uint16_t host_fb[FB_WIDTH * FB_HEIGHT];

#if RENDER_TO_FB
/* Mimic of port_glue.cpp's strip renderer — keep in sync with it. */
#define STRIP_GUARD_ROWS (S9X_STRIP_ROWS + 1)

static uint8_t *strip_screen;
static uint8_t *strip_sub;
static uint8_t *strip_z;
static uint8_t *strip_subz;

int s9x_port_max_endy = SNES_HEIGHT - 1;
uint16_t *s9x_port_fb_window = NULL;
uint8_t *s9x_port_objonline = NULL;
void (*s9x_port_strip_top_hook)(uint16_t *strip, int stride,
                                int block_start, int block_end) = NULL;

void s9x_port_anchor_screen(void)
{
    int h = PPU.ScreenHeight ? PPU.ScreenHeight
                             : (Settings.PAL ? SNES_HEIGHT_EXTENDED : SNES_HEIGHT);
    const int marginTop  = (FB_HEIGHT - h) / 2;
    const int marginLeft = (FB_WIDTH - SNES_WIDTH) / 2;
    s9x_port_fb_window = host_fb + marginTop * FB_WIDTH + marginLeft;
    s9x_port_max_endy = FB_HEIGHT - marginTop - 1;
}

void s9x_port_strip_repoint(uint32_t row)
{
    GFX.Screen     = strip_screen - (size_t)row * SNES_WIDTH * 2;
    GFX.SubScreen  = strip_sub    - (size_t)row * SNES_WIDTH * 2;
    GFX.ZBuffer    = strip_z      - (size_t)row * SNES_WIDTH;
    GFX.SubZBuffer = strip_subz   - (size_t)row * SNES_WIDTH;
}

static int trace_blocks;

void s9x_port_strip_copyout(uint32_t start_row, uint32_t end_row)
{
    if (trace_blocks)
        fprintf(stderr, "  chunk [%u..%u]\n", start_row, end_row);
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

bool S9xInitDisplay(void)
{
    GFX.Pitch  = SNES_WIDTH * 2;
    GFX.ZPitch = SNES_WIDTH;
    s9x_port_anchor_screen();

    strip_screen = malloc((size_t)STRIP_GUARD_ROWS * SNES_WIDTH * 2);
    strip_sub    = malloc((size_t)STRIP_GUARD_ROWS * SNES_WIDTH * 2);
    strip_z      = malloc((size_t)STRIP_GUARD_ROWS * SNES_WIDTH);
    strip_subz   = malloc((size_t)STRIP_GUARD_ROWS * SNES_WIDTH);
    s9x_port_objonline = malloc((size_t)SNES_HEIGHT_EXTENDED * 128);

    GFX.Screen     = strip_screen;
    GFX.SubScreen  = strip_sub;
    GFX.ZBuffer    = strip_z;
    GFX.SubZBuffer = strip_subz;
    return strip_screen && strip_sub && strip_z && strip_subz && s9x_port_objonline;
}

void S9xDeinitDisplay(void) { }
#else
static int trace_blocks; /* unused on this path; keeps main() simple */

bool S9xInitDisplay(void)
{
    const size_t pitch_bytes = (size_t)SNES_WIDTH * 2;
    const size_t z_stride    = pitch_bytes >> 1;

    GFX.Pitch  = pitch_bytes;
    GFX.ZPitch = z_stride;
    GFX.Screen     = malloc(pitch_bytes * SNES_HEIGHT_EXTENDED);
    GFX.SubScreen  = malloc(pitch_bytes * SNES_HEIGHT_EXTENDED);
    GFX.ZBuffer    = malloc(z_stride    * SNES_HEIGHT_EXTENDED);
    GFX.SubZBuffer = malloc(z_stride    * SNES_HEIGHT_EXTENDED);
    return GFX.Screen && GFX.SubScreen && GFX.ZBuffer && GFX.SubZBuffer;
}

void S9xDeinitDisplay(void) { }
#endif

/* ---- frame dump -------------------------------------------------------- */
/* RGB555 (PICO_SNESPLUS_HSTX pixel format): R at bit 10, G at 5, B at 0. */
static void write_ppm(const char *path, const uint16_t *px,
                      int w, int h, int pitch_px)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t p = px[y * pitch_px + x];
            uint8_t rgb[3] = {
                (uint8_t)(((p >> 10) & 31) << 3 | ((p >> 12) & 7)),
                (uint8_t)(((p >>  5) & 31) << 3 | ((p >>  7) & 7)),
                (uint8_t)(( p        & 31) << 3 | ((p >>  2) & 7)),
            };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

static void dump_frame(const char *dir, const char *tag, uint32_t frame)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s_f%05u.ppm", dir, tag, frame);
#if RENDER_TO_FB
    write_ppm(path, host_fb, FB_WIDTH, FB_HEIGHT, FB_WIDTH);
#else
    write_ppm(path, (const uint16_t *)GFX.Screen,
              IPPU.RenderedScreenWidth ? (int)IPPU.RenderedScreenWidth : SNES_WIDTH,
              PPU.ScreenHeight ? PPU.ScreenHeight : SNES_HEIGHT,
              GFX.Pitch / 2);
#endif
}

/* ---- scripted MSU-1 driver --------------------------------------------- */
#if ENABLE_MSU1
/* Stands in for the game's MSU-1 driver: select a track, wait out
 * AUDIO_BUSY, then set volume and press play — the sequence qwertymodo's
 * msu1.asm patch uses. Driven through S9xGetPPU/S9xSetPPU on purpose, so the
 * ppu.c register traps are exercised and not just the state machine. */
static int     msu_on;             /* MSU-1 initialised for this run */
static int     msu_track;          /* >0: harness drives it; 0: the game does */
static int     msu_vol = 255;
static int     msu_repeat = 1;
static int     msu_stage;
static int16_t msu_mixbuf[(44100 / 10 + 1) * 2];   /* down to FPS=10 */
static FILE   *msu_audio_out;
static int     msu_peak;
static int     msu_peak_snes;

static void msu_check_identity(void)
{
    char id[7] = {0};
    for (int i = 0; i < 6; i++)
        id[i] = (char)S9xGetPPU((uint16_t)(0x2002 + i));
    uint8_t st = S9xGetPPU(0x2000);
    printf("MSU1 identity $2002-$2007 = \"%s\"  [%s]\n",
           id, strcmp(id, "S-MSU1") == 0 ? "ok" : "FAIL");
    printf("MSU1 status   $2000 = %02x  (revision %d)\n", st, st & 7);
}

/* Exercise the data port: seek via $2000-$2003, then read bytes out of
 * $2001. Prints what the CPU would see so it can be diffed against the
 * .msu file (e.g. `xxd -l 8 rom.msu`). */
static void msu_check_data_track(uint32_t offset, int count)
{
    printf("MSU1 data $2001 @%u:", offset);
    S9xSetPPU((uint8_t)(offset       & 0xff), 0x2000);
    S9xSetPPU((uint8_t)((offset >> 8) & 0xff), 0x2001);
    S9xSetPPU((uint8_t)((offset >> 16) & 0xff), 0x2002);
    S9xSetPPU((uint8_t)((offset >> 24) & 0xff), 0x2003);   /* commits the seek */
    for (int i = 0; i < count; i++)
        printf(" %02x", S9xGetPPU(0x2001));
    printf("\n");
}

static void msu_drive(uint32_t frame)
{
    uint8_t st;
    switch (msu_stage) {
    case 0:
        msu_check_identity();
        msu_check_data_track(0, 8);
        msu_check_data_track(256, 4);
        S9xSetPPU((uint8_t)(msu_track & 0xff), 0x2004);
        S9xSetPPU((uint8_t)(msu_track >> 8),   0x2005);
        printf("MSU1 f%u: selected track %d\n", frame, msu_track);
        msu_stage = 1;
        break;
    case 1:
        st = S9xGetPPU(0x2000);
        if (st & 0x08) {
            printf("MSU1 f%u: TRACK_MISSING (status %02x)\n", frame, st);
            msu_stage = 3;
            break;
        }
        if (st & 0x40)
            break;                        /* still AUDIO_BUSY — keep polling */
        S9xSetPPU((uint8_t)msu_vol, 0x2006);
        S9xSetPPU((uint8_t)(msu_repeat ? 3 : 1), 0x2007);
        printf("MSU1 f%u: AUDIO_BUSY cleared, vol=%d, play%s (status %02x)\n",
               frame, msu_vol, msu_repeat ? "+repeat" : "", S9xGetPPU(0x2000));
        msu_stage = 2;
        break;
    default:
        break;
    }
}

/* Pull one video frame's worth of audio the way core1_mix_task does on
 * device — at the *real-time* rate, which is what makes a below-60 fps run
 * consume more per frame than it refills if the budget is mis-sized.
 * Peaks are taken before and after msu1_mix so the MSU contribution can be
 * told apart from the SNES DSP's own output. */
static void msu_mix_frame(void)
{
    const int frames = (int)((uint64_t)44100 * msu_frame_us / 1000000u);
    S9xMixSamples(msu_mixbuf, frames * 2);
    for (int i = 0; i < frames * 2; i++) {
        int v = msu_mixbuf[i] < 0 ? -msu_mixbuf[i] : msu_mixbuf[i];
        if (v > msu_peak_snes) msu_peak_snes = v;
    }
    msu1_mix(msu_mixbuf, frames);
    for (int i = 0; i < frames * 2; i++) {
        int v = msu_mixbuf[i] < 0 ? -msu_mixbuf[i] : msu_mixbuf[i];
        if (v > msu_peak) msu_peak = v;
    }
    if (msu_audio_out)
        fwrite(msu_mixbuf, sizeof(int16_t), (size_t)frames * 2, msu_audio_out);
}
#endif /* ENABLE_MSU1 */

/* ---- main --------------------------------------------------------------- */
int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr,
            "usage: %s <rom> <outdir> <tag> <maxframe> [dumpstep] [dumpfrom]\n"
            "env:   TRACE_FROM=<frame>  trace strip chunks + PPU regs\n"
            "       MOUSE=1            attach a scripted SNES Mouse (circling cursor)\n"
            "       MOUSE_CLICK=<f>    hold left button frames [f,f+10)\n"
            "       MSU=<track>        MSU-1 build only: play <rom>-<track>.pcm;\n"
            "                          MSU=0 initialises it and lets the ROM drive\n"
            "       MSU_VOL=<0-255>    MSU-1 volume  (default 255)\n"
            "       MSU_REPEAT=<0|1>   MSU-1 repeat  (default 1)\n"
            "       AUDIO_OUT=<path>   dump the mixed 44.1 kHz s16 stereo stream\n"
            "       FPS=<10-60>        model a game running below 60 fps\n",
            argv[0]);
        return 2;
    }
    const char *rompath = argv[1];
    const char *outdir  = argv[2];
    const char *tag     = argv[3];
    uint32_t maxframe   = (uint32_t)strtoul(argv[4], NULL, 0);
    uint32_t dumpstep   = argc > 5 ? (uint32_t)strtoul(argv[5], NULL, 0) : 1;
    uint32_t dumpfrom   = argc > 6 ? (uint32_t)strtoul(argv[6], NULL, 0) : 0;

    FILE *f = fopen(rompath, "rb");
    if (!f) { perror(rompath); return 1; }
    fseek(f, 0, SEEK_END);
    long romsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *rom = malloc((size_t)romsize + 0x200);
    if (fread(rom, 1, (size_t)romsize, f) != (size_t)romsize) {
        fprintf(stderr, "short read\n"); return 1;
    }
    fclose(f);

    /* Settings — identical to main.cpp snes9x_setup_settings(). */
    memset(&Settings, 0, sizeof(Settings));
    Settings.CyclesPercentage  = 100;
    Settings.H_Max             = SNES_CYCLES_PER_SCANLINE;
    Settings.HBlankStart       = (256 * Settings.H_Max) / SNES_HCOUNTER_MAX;
    Settings.FrameTimePAL      = 20000;
    Settings.FrameTimeNTSC     = 16667;
    Settings.ControllerOption  = SNES_JOYPAD;
    Settings.SoundPlaybackRate = 44100;
    Settings.SoundInputRate    = 44100;
    Settings.SoundBufferSize   = 1024;
    Settings.SoundMixInterval  = 0;
    Settings.InterpolatedSound = false;
    Settings.DisableSoundEcho  = false;
    Settings.Mute              = false;
    Settings.APUEnabled        = true;
    Settings.Shutdown          = true;

    const char *me = getenv("MOUSE");
    mouse_enabled = me && atoi(me);
    if (mouse_enabled) {
        const char *e;
        /* InitROM does MouseMaster = Mouse, so Mouse is the one to set. */
        Settings.Mouse = true;
        if ((e = getenv("MOUSE_CLICK"))) mouse_click = (uint32_t)strtoul(e, NULL, 0);
    }

    if (!S9xInitAPU())    { fprintf(stderr, "APU init failed\n");    return 1; }
    if (!S9xInitMemory()) { fprintf(stderr, "Memory init failed\n"); return 1; }
    if (!S9xInitSound(0, 0)) { fprintf(stderr, "Sound init failed\n"); return 1; }
    S9xSetPlaybackRate(44100);

    Memory.ROM           = rom;
    Memory.ROM_AllocSize = (uint32_t)romsize;
    Memory.ROM_Offset    = 0;
    if (!LoadROM(NULL))   { fprintf(stderr, "LoadROM failed\n"); return 1; }
    S9xReset();
    /* Reset always lands on SNES_JOYPAD; on device host_tick() flips the
     * live controller when a USB mouse is present — mimic that here. */
    if (mouse_enabled)
        IPPU.Controller = SNES_MOUSE;

    if (!S9xInitDisplay()) { fprintf(stderr, "Display init failed\n"); return 1; }
    if (!S9xInitGFX())     { fprintf(stderr, "GFX init failed\n");     return 1; }

    printf("ROM: %s  PAL=%d  size=%ld\n", Memory.ROMName, Settings.PAL, romsize);

#if ENABLE_MSU1
    {
        const char *e = getenv("MSU");
        if (e) {
            /* Same derivation as the device: strip at the last '.' of the
             * full path, then look for <base>.msu / <base>-<n>.pcm. */
            char *dot;
            strncpy(msu_base, rompath, sizeof msu_base - 1);
            dot = strrchr(msu_base, '.');
            if (dot) *dot = 0;
            msu_track = atoi(e);
            if ((e = getenv("MSU_VOL")))    msu_vol    = atoi(e);
            if ((e = getenv("MSU_REPEAT"))) msu_repeat = atoi(e);
            if ((e = getenv("FPS"))) {
                int fps = atoi(e);
                if (fps >= 10 && fps <= 60) msu_frame_us = 1000000u / (uint32_t)fps;
                printf("MSU1: modelling %u fps (%u us/frame)\n",
                       1000000u / msu_frame_us, msu_frame_us);
            }
            if (!msu1_init()) {
                fprintf(stderr, "MSU1: no pack for \"%s\" (.msu / -1.pcm)\n", msu_base);
            } else {
                msu_on = 1;
                if ((e = getenv("AUDIO_OUT"))) {
                    msu_audio_out = fopen(e, "wb");
                    if (!msu_audio_out) perror(e);
                }
                if (msu_track <= 0)
                    printf("MSU1: game-driven (MSU=0) — the ROM's own driver "
                           "writes $2000-$2007\n");
            }
        }
    }
#endif

    const char *tr = getenv("TRACE_FROM");
    uint32_t trace_from = tr ? (uint32_t)strtoul(tr, NULL, 0) : UINT32_MAX;

    for (uint32_t frame = 0; frame <= maxframe; frame++) {
        mouse_frame = frame;
        IPPU.RenderThisFrame = true;
        trace_blocks = (frame >= trace_from);
        if (trace_blocks) fprintf(stderr, "frame %u:\n", frame);
        S9xMainLoop();
        if (frame >= trace_from)
            fprintf(stderr,
                "  regs: BGMode=%d 2133=%02x 2130=%02x 2131=%02x "
                "TM=%02x TS=%02x 2105=%02x H=%d\n",
                PPU.BGMode, Memory.FillRAM[0x2133], Memory.FillRAM[0x2130],
                Memory.FillRAM[0x2131], Memory.FillRAM[0x212c],
                Memory.FillRAM[0x212d], Memory.FillRAM[0x2105],
                PPU.ScreenHeight);
#if ENABLE_MSU1
        if (msu_on) {
            msu_vclock_us += msu_frame_us;   /* one frame of wall time passed */
            if (msu_track > 0)
                msu_drive(frame);   /* stand in for the game's register writes */
            msu1_pump();            /* core0's end-of-frame SD refill */
            msu_mix_frame();        /* core1's mixer */
        }
#endif
        if (frame >= dumpfrom && (frame - dumpfrom) % dumpstep == 0)
            dump_frame(outdir, tag, frame);
    }
#if ENABLE_MSU1
    if (msu_on) {
        msu1_stats_report();
        printf("MSU1: peak SNES-only = %d, peak SNES+MSU = %d  [%s]\n",
               msu_peak_snes, msu_peak,
               msu_peak > msu_peak_snes + 256 ? "MSU audio present"
                                              : "no MSU contribution — check the pack");
        if (msu_audio_out) fclose(msu_audio_out);
        msu1_deinit();
    }
#endif
    printf("done: %u frames\n", maxframe + 1);
    return 0;
}
