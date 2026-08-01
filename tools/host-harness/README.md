# Host-side render test harness

Runs the vendored snes9x core from this repository natively on Linux and
dumps rendered frames as PPM images. Render bugs can be reproduced,
bisected and fixed on a desktop machine in seconds — no board, flashing or
capture hardware needed. It was built to find the DKC "Nintendo presents"
mode-5 strip-seam bug and is kept for future rendering work.

The harness boots the core through the exact same sequence `main.cpp` uses
on the RP2350 (same `Settings`, same init order, same `LoadROM(NULL)`
hand-off) and, in the `RENDER_TO_FB=1` variant, mimics the strip renderer
from `port_glue.cpp` faithfully: 16-row staging strips, `repoint`/`copyout`
per chunk, centered window in a 320x240 framebuffer. Keep that mimic in
sync when `port_glue.cpp` changes.

## Build

```bash
tools/host-harness/build.sh
```

Needs only a native `gcc`. Produces four binaries in this directory:

| binary      | meaning                                                        |
| ----------- | -------------------------------------------------------------- |
| `fb1_nolut` | strip renderer + device color math — **the device render flow** |
| `fb0_nolut` | classic full-frame render, device color math                   |
| `fb0_lut`   | classic full-frame render, upstream ZERO-LUT color math        |
| `msu1`      | device render flow + MSU-1 (`ENABLE_MSU1=1`), see below         |

Byte-comparing the PPM output between the first three isolates a bug's
layer: `fb1` vs `fb0` differs → strip renderer; `fb0_nolut` vs `fb0_lut`
differs → the LUT-free color math (`NO_ZERO_LUT`).

## Run

```bash
./fb1_nolut <rom.sfc> <outdir> <tag> <maxframe> [dumpstep] [dumpfrom]

# examples
mkdir -p out
./fb1_nolut dkc.sfc out dkc 1200 20      # frames 0-1200, dump every 20th
./fb1_nolut dkc.sfc out dkc 600 1 550    # dump every frame from 550 to 600
```

Frames are written as `<outdir>/<tag>_f00560.ppm` (RGB888 P6). No input is
fed to the joypads, so attract sequences and intros play by themselves.

`TRACE_FROM=<frame>` in the environment logs, from that frame on, every
strip-chunk row range (`fb1` only) and the PPU state per frame (BGMode,
$2130-$2133, TM/TS, screen height) to stderr — this is how a mid-frame
split or a screen-mode surprise shows up.

`MOUSE=1` attaches a scripted SNES Mouse (port 1, the port Mario Paint
requires): the cursor circles the screen center so motion never saturates
at an edge, and `MOUSE_CLICK=<frame>` holds the left button for 10 frames
from that frame on. This drives the same core mouse path that
`port_glue.cpp` feeds from a USB HID mouse on device. Quick check:
Mario Paint's title cursor follows the circle, and a click timed over a
title letter triggers its easter-egg animation.

```bash
MOUSE=1 MOUSE_CLICK=471 ./fb1_nolut mariopaint.sfc out mp 700 50 450
```

## MSU-1 (`./msu1`)

The `msu1` binary is the device render flow plus MSU-1, with a stdio backend
standing in for the FatFs one in `msu1_port.cpp`. It exercises the real
thing: the `$2000-$2007` traps in `ppu.c`, the PCM ring in `msu1.c`, the
core0 refill (`msu1_pump`) and the core1 mixer (`msu1_mix`), in the same
per-frame order the firmware runs them. Pack files are looked up next to the
ROM, exactly as on device (`<rom minus extension>.msu` / `-<n>.pcm`).

`MSU=0` initialises MSU-1 and lets **the ROM's own driver** work the
registers — the realistic end-to-end test:

```bash
MSU=0 AUDIO_OUT=/tmp/a.raw ./msu1 alttp_msu.sfc out z 1800 1000 9999
aplay -f S16_LE -r 44100 -c 2 /tmp/a.raw          # or: ffplay -f s16le -ar 44100 -ac 2
```

`MSU=<track>` instead has the harness drive it: check the identity string,
dump some data-track bytes through `$2001`, select the track, poll
`AUDIO_BUSY`, then set volume and press play. Use this to test a bare pack
against any ROM. `MSU_VOL=<0-255>` and `MSU_REPEAT=<0|1>` tune it.

```bash
MSU=1 MSU_REPEAT=1 AUDIO_OUT=/tmp/a.raw ./msu1 rom.sfc out t 300 1000 9999
```

`AUDIO_OUT=<path>` dumps the fully mixed stream (SNES DSP + MSU) as raw
44.1 kHz stereo s16le, one video frame at a time. The run ends with the
`MSU1:` health line and a peak comparison of SNES-only vs SNES+MSU, which is
what tells you the PCM actually reached the mix rather than the game just
being noisy on its own.

### Frame-rate modelling (`FPS=<10-60>`)

`msu1_pump` runs once per *video frame* but PCM is consumed in *real time*,
so a game running below 60 fps needs a bigger refill per frame — 4410 B at
40 fps versus 2949 B at 60. Getting that wrong does not show up at 60 fps at
all, and shows up below it as a permanently empty ring and continuous
underruns (this is real: Zelda's intro FMV runs at 40 fps and streams video
through the data port while the music plays).

`FPS=<n>` models it. The harness drives MSU-1 off a **virtual clock** that
advances one frame period per emulated frame, so the core sees exactly the
intervals it would see on hardware even though the harness runs far faster
than real time. Check `ring %` and `urun`:

```bash
MSU=0 FPS=40 ./msu1 alttp_msu.sfc out z 900 1000 9999   # want: ring ~93%, urun 0
```

Because the clock is virtual, the `avg`/`max`/`open` microsecond figures in
the `MSU1:` line are meaningless here — read cost can only be measured on
the device. `ring`, `urun`, `SD`/`rd` and `data` are all real.

A synthetic pack makes the checks exact — a known sine can be compared
sample-for-sample against `(src * volume) >> 8`, which catches ring-wrap and
loop-splice errors that are inaudible in a real soundtrack.

## Inspecting output

ffmpeg handles PPM everywhere:

```bash
# contact sheet to find the interesting frame
ffmpeg -pattern_type glob -i 'out/dkc_*.ppm' -vf tile=8x8 sheet.png

# zoom into a region (crop=w:h:x:y, then scale up with nearest neighbor)
ffmpeg -i out/dkc_f00560.ppm -vf "crop=120:80:190:160,scale=480:320:flags=neighbor" zoom.png
```

`cmp a.ppm b.ppm` byte-compares two dumps; identical files mean identical
rendering, which makes regression checks trivial (render the same frame
range before and after a core change and compare).

Note the geometry when comparing variants: `fb1` dumps the full 320x240
framebuffer with the SNES image centered (default NTSC window starts at
x=32, y=8); `fb0` dumps the native SNES resolution, which is 512 wide when
the frame used hi-res mode 5/6 or interlace (even pixels correspond to the
force-lores output).
