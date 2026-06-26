add_library(snes9x_core INTERFACE)

target_sources(snes9x_core INTERFACE
    src/apu.c
    src/c4.c
    src/c4emu.c
    src/clip.c
    src/cpu.c
    src/cpuexec.c
    src/cpuops.c
    src/dma.c
    src/dsp.c
    src/getset.c
    src/gfx.c
    src/globals.c
    src/memmap.c
    src/obc1.c
    src/ppu.c
    src/soundux.c
    src/spc700.c
    src/srtc.c
    src/tile.c

    src/port_alloc.cpp
    src/port_glue.cpp
)

target_include_directories(snes9x_core INTERFACE src)

target_compile_definitions(snes9x_core INTERFACE
    RIGHTSHIFT_IS_SAR
    FAST_LSB_WORD_ACCESS
    NO_ZERO_LUT
    PICO_SNESPLUS_HSTX
)

target_compile_options(snes9x_core INTERFACE
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-array-bounds
    -Wno-implicit-fallthrough
    -O2
)
