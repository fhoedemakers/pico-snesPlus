#!/bin/bash
# Build the host-side snes9x test harness (three variants, see harness.c).
# Requires only a native gcc; run from anywhere.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(cd "$HERE/../../snes9x/src" && pwd)"

CORE="$SRC/apu.c $SRC/c4.c $SRC/c4emu.c $SRC/clip.c $SRC/cpu.c $SRC/cpuexec.c \
      $SRC/cpuops.c $SRC/dma.c $SRC/dsp.c $SRC/fxemu.c $SRC/fxinst.c \
      $SRC/getset.c $SRC/gfx.c $SRC/globals.c $SRC/memmap.c $SRC/obc1.c \
      $SRC/ppu.c $SRC/sa1.c $SRC/sa1cpu.c $SRC/soundux.c $SRC/spc700.c \
      $SRC/srtc.c $SRC/tile.c"

# Same core defines as snes9x/CMakeLists.txt; the -include flags supply
# headers the pico toolchain pulls in transitively.
COMMON="-O2 -g -fno-strict-aliasing -w -I$SRC -lm \
        -include stdint.h -include stddef.h \
        -DRIGHTSHIFT_IS_SAR -DFAST_LSB_WORD_ACCESS -DPICO_SNESPLUS_HSTX"

# Device config: strip renderer (the render flow that ships on hardware)
gcc -o "$HERE/fb1_nolut" "$HERE/harness.c" $CORE $COMMON -DNO_ZERO_LUT -DRENDER_TO_FB=1
# Device color math, classic full-frame path (isolates strip-renderer bugs)
gcc -o "$HERE/fb0_nolut" "$HERE/harness.c" $CORE $COMMON -DNO_ZERO_LUT -DRENDER_TO_FB=0
# Upstream ZERO-LUT color math, classic path (isolates color-math bugs)
gcc -o "$HERE/fb0_lut"   "$HERE/harness.c" $CORE $COMMON -DRENDER_TO_FB=0
echo "built: fb1_nolut fb0_nolut fb0_lut"
