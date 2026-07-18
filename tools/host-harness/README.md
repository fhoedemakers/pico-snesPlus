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

Needs only a native `gcc`. Produces three binaries in this directory:

| binary      | meaning                                                        |
| ----------- | -------------------------------------------------------------- |
| `fb1_nolut` | strip renderer + device color math — **the device render flow** |
| `fb0_nolut` | classic full-frame render, device color math                   |
| `fb0_lut`   | classic full-frame render, upstream ZERO-LUT color math        |

Byte-comparing the PPM output between variants isolates a bug's layer:
`fb1` vs `fb0` differs → strip renderer; `fb0_nolut` vs `fb0_lut` differs →
the LUT-free color math (`NO_ZERO_LUT`).

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
