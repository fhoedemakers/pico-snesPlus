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

/* ---- main --------------------------------------------------------------- */
int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr,
            "usage: %s <rom> <outdir> <tag> <maxframe> [dumpstep] [dumpfrom]\n"
            "env:   TRACE_FROM=<frame>  trace strip chunks + PPU regs\n"
            "       MOUSE=1            attach a scripted SNES Mouse (circling cursor)\n"
            "       MOUSE_CLICK=<f>    hold left button frames [f,f+10)\n",
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
        if (frame >= dumpfrom && (frame - dumpfrom) % dumpstep == 0)
            dump_frame(outdir, tag, frame);
    }
    printf("done: %u frames\n", maxframe + 1);
    return 0;
}
