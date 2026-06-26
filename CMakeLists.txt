# pico_snesPlus — snes9x port for RP2350 + PSRAM, using the pico_shared
# emulator framework. First-target build: HSTX (RGB555) on HW_CONFIG=8
# (Adafruit Fruit Jam). PicoDVI/RGB444 path is a follow-up.

cmake_minimum_required(VERSION 3.13)

set(projectname pico_snesPlus)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Custom RELEASENODEBUG build type, mirroring pico-infonesPlus.
set(GCC_RELEASENODEBUG_FLAGS "-O2 -DNDEBUG")
set(CMAKE_ASM_FLAGS_RELEASENODEBUG "${GCC_RELEASENODEBUG_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASENODEBUG "${GCC_RELEASENODEBUG_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_RELEASENODEBUG   "${GCC_RELEASENODEBUG_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_RELEASENODEBUG "" CACHE STRING "" FORCE)
mark_as_advanced(
    CMAKE_CXX_FLAGS_RELEASENODEBUG CMAKE_ASM_FLAGS_RELEASENODEBUG
    CMAKE_C_FLAGS_RELEASENODEBUG CMAKE_EXE_LINKER_FLAGS_RELEASENODEBUG)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()
message("* Current build type is : ${CMAKE_BUILD_TYPE}")

# Hardware / board selection. RP2350+PSRAM+HSTX only for this build.
if(NOT HW_CONFIG)
    set(HW_CONFIG 8 CACHE STRING "Select the hardware configuration for your board")
endif()
if(NOT USE_HSTX)
    set(USE_HSTX 1 CACHE STRING "Use HSTX for display output")
endif()

include("pico_shared/BoardConfigs.cmake")

if(NOT PICO_BOARD)
    set(PICO_BOARD pico2 CACHE STRING "Board type")
endif()
if(PICO_PLATFORM STREQUAL "rp2040")
    message(FATAL_ERROR "pico_snesPlus requires RP2350 with PSRAM. Pick a HW_CONFIG that selects an RP2350 board (5, 8, 13, 14).")
endif()
if(NOT PSRAM_CS_PIN OR PSRAM_CS_PIN EQUAL 0)
    message(FATAL_ERROR "pico_snesPlus requires PSRAM. Selected board has no PSRAM_CS_PIN.")
endif()

add_definitions(-DNDEBUG)

# TinyUSB family support + Pico SDK init (same dance as pico-infonesPlus).
set(FAMILY rp2040)
include($ENV{PICO_SDK_PATH}/lib/tinyusb/hw/bsp/family_support.cmake)

if(ENABLE_PIO_USB EQUAL 0 AND TARGET tinyusb_common_base)
    get_target_property(_tu_defs tinyusb_common_base INTERFACE_COMPILE_DEFINITIONS)
    if(_tu_defs)
        list(REMOVE_ITEM _tu_defs "CFG_TUH_RPI_PIO_USB=1")
        set_target_properties(tinyusb_common_base PROPERTIES
            INTERFACE_COMPILE_DEFINITIONS "${_tu_defs}")
    endif()
endif()

project(${projectname} C CXX ASM)

pico_sdk_init()

if(ENABLE_PIO_USB EQUAL 1)
    check_and_add_pico_pio_usb_support()
endif()

add_executable(${projectname}
    main.cpp
    splash.cpp
    DefaultSS555.c
)

pico_set_program_name(${projectname} "${projectname}")
pico_set_program_version(${projectname} "0.1")

if(UART_ENABLED EQUAL 0)
    pico_enable_stdio_uart(${projectname} 0)
else()
    pico_enable_stdio_uart(${projectname} 1)
endif()
pico_enable_stdio_usb(${projectname} 0)

target_compile_definitions(${projectname} PRIVATE
    PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1
)

target_compile_options(${projectname} PRIVATE
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-cast-function-type
    -Wno-maybe-uninitialized
)

target_compile_definitions(${projectname} PRIVATE
    DVICONFIG=${DVICONFIG}
    SDCARD_PIN_CS=${SD_CS}
    SDCARD_PIN_SCK=${SD_SCK}
    SDCARD_PIN_MOSI=${SD_MOSI}
    SDCARD_PIN_MISO=${SD_MISO}
    SDCARD_PIO=pio1
    SDCARD_SPI=${SD_SPI}
    NES_PIN_CLK=${NES_CLK}
    NES_PIN_DATA=${NES_DATA}
    NES_PIN_LAT=${NES_LAT}
    NES_PIO=${NES_PIO}
    NES_PIN_CLK_1=${NES_CLK_1}
    NES_PIN_DATA_1=${NES_DATA_1}
    NES_PIN_LAT_1=${NES_LAT_1}
    NES_PIO_1=${NES_PIO_1}
    WII_PIN_SDA=${WII_SDA}
    WII_PIN_SCL=${WII_SCL}
    LED_GPIO_PIN=${LED_GPIO_PIN}
    HW_CONFIG=${HW_CONFIG}
    WIIPAD_I2C=${WIIPAD_I2C}
    USE_I2S_AUDIO=${USE_I2S_AUDIO}
    PICO_AUDIO_I2S_DATA_PIN=${PICO_AUDIO_I2S_DATA_PIN}
    PICO_AUDIO_I2S_CLOCK_PIN_BASE=${PICO_AUDIO_I2S_CLOCK_PIN_BASE}
    PICO_AUDIO_I2S_PIO=${PICO_AUDIO_I2S_PIO}
    PICO_AUDIO_I2S_DMA_IRQ=1
    PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED=${PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED}
    PICO_AUDIO_I2S_RESET_PIN=${PICO_AUDIO_I2S_RESET_PIN}
    PICO_AUDIO_I2S_INTERRUPT_PIN=${PICO_AUDIO_I2S_INTERRUPT_PIN}
    PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON=${PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON}
    CFG_TUH_RPI_PIO_USB=${ENABLE_PIO_USB}
    PIO_USB_USE_PIO=${PIO_USB_USE_PIO}
    PSRAM_CS_PIN=${PSRAM_CS_PIN}
    GPIOHSTXD0=${GPIOHSTXD0}
    GPIOHSTXD1=${GPIOHSTXD1}
    GPIOHSTXD2=${GPIOHSTXD2}
    GPIOHSTXCK=${GPIOHSTXCK}
    GPIOHSTXINVERTED=${GPIOHSTXINVERTED}
    ENABLE_VU_METER=${ENABLE_VU_METER}
    WIIPAD_DELAYED_START=${WIIPAD_DELAYED_START}
    ENABLEDVI=1
    DOUBLEFRAMEBUFFER=0
)

target_compile_definitions(${projectname} PRIVATE
    PICO_PIO_USE_GPIO_BASE=1
    PICO_STACK_SIZE=3072
    # Pico SDK's default _sbrk returns -1 if the padded request would
    # overshoot __StackLimit, even if the user-visible allocation would
    # have fit. Newlib's malloc pads sbrk requests when growing the
    # wilderness, so a 128 KB malloc can fail near the heap ceiling
    # despite there being room for the bytes themselves. With this set,
    # _sbrk caps at __StackLimit and returns whatever's available; newlib
    # carves the chunk into that partial extension and succeeds.
    PICO_USE_OPTIMISTIC_SBRK=1
)

if(DEFINED PICO_DEFAULT_UART_TX_PIN)
    target_compile_definitions(${projectname} PRIVATE
        PICO_DEFAULT_UART_TX_PIN=${PICO_DEFAULT_UART_TX_PIN}
        PICO_DEFAULT_UART=0
    )
endif()
if(DEFINED PICO_DEFAULT_UART_RX_PIN)
    target_compile_definitions(${projectname} PRIVATE
        PICO_DEFAULT_UART_RX_PIN=${PICO_DEFAULT_UART_RX_PIN}
    )
endif()
if(DEFINED PICO_DEFAULT_UART)
    target_compile_definitions(${projectname} PRIVATE
        PICO_DEFAULT_UART=${PICO_DEFAULT_UART}
    )
endif()

if(ENABLE_PIO_USB EQUAL 1)
    if(NOT PIO_DP_PLUS_PIN EQUAL -1)
        target_compile_definitions(${projectname} PRIVATE
            PICO_DEFAULT_PIO_USB_DP_PIN=${PIO_DP_PLUS_PIN}
        )
    endif()
    target_link_libraries(${projectname} PRIVATE tinyusb_pico_pio_usb)
endif()

target_link_libraries(${projectname} PRIVATE
    pico_stdlib
    pico_multicore
    hardware_dma
    hardware_pio
    hardware_i2c
    hardware_interp
    hardware_timer
    hardware_clocks
    hardware_pwm
    hardware_flash
    dvi
    util
    snes9x_core
    tinyusb_host
    tinyusb_board
    pico_shared
    xinput_host
)

if(PICO_BOARD STREQUAL "pico_w" OR PICO_BOARD STREQUAL "pico2_w")
    target_link_libraries(${projectname} PRIVATE pico_cyw43_arch_none)
endif()

target_include_directories(${projectname} PRIVATE
    pico_lib
    pico_shared
    snes9x/src
    tusb_xinput
    .
)

add_subdirectory(snes9x)
add_subdirectory(pico_lib/dvi)
add_subdirectory(pico_lib/util)
add_subdirectory(pico_shared)
add_subdirectory(tusb_xinput xinput_host)

target_link_options(${projectname} PRIVATE -Xlinker --print-memory-usage --data-sections)

# ----- snes9x hot-path SRAM placement -------------------------------------
# Strategy: hot snes9x C files have their entire .text renamed to
# .time_critical_<basename> so the Pico SDK linker script (*(.time_critical*))
# places them in SRAM. The linker auto-generates the flash->SRAM copy at boot.
#   1. -fno-function-sections coalesces every function in the file into a
#      single .text section. gc-sections then can't drop any one function —
#      since all of them are reachable through dispatch tables / pointers,
#      that's the desired behavior.
#   2. PRE_LINK objcopy renames that monolithic .text to .time_critical_<x>.
# Memory budget (HW_CONFIG=8, RP2350+PSRAM): all emulator buffers
# (Memory.RAM 128K, IAPU.RAM 64K, Memory.Map 16K, VRAM, SRAM, FillRAM,
# MapInfo, ScreenColors, TileCache, TileCached, bytes0x2000, GFX.Screen,
# GFX.SubScreen, GFX.Z*, sound LocalState, gfx LocalState) live in PSRAM.
# SRAM is reserved for static code (the hot snes9x files listed below
# plus cpuops.c) and the HSTX framebuffer (.bss).
set(SNES9X_SRAM_HOT_FILES
    cpuops.c    # 65816 opcode dispatch (every emulated CPU instr)
    spc700.c    # APUExecute — SPC700 interpreter (every audio cycle)
    gfx.c       # RenderScreen / RenderLine / DrawBackground
    tile.c      # DrawTile / DrawClippedTile / DrawLargePixel
    ppu.c       # S9xSetPPU / S9xGetPPU / S9xUpdateScreen
    memmap.c    # Memory map + Get/SetByte
    soundux.c   # S9xMixSamples / MixStereo
    dma.c       # S9xDoDMA / S9xDoHDMA
    clip.c      # ComputeClipWindows
    apu.c       # S9xSetAPUDSP and APU register access
    getset.c    # Get/Set helper inlines that didn't inline
    cpuexec.c   # S9xMainLoop / S9xDoHBlankProcessing
)
foreach(_src IN LISTS SNES9X_SRAM_HOT_FILES)
    get_filename_component(_stem "${_src}" NAME_WE)
    set_source_files_properties(snes9x/src/${_src} PROPERTIES
        COMPILE_FLAGS "-fno-function-sections")
    add_custom_command(TARGET ${projectname} PRE_LINK
        COMMAND ${CMAKE_OBJCOPY} --rename-section .text=.time_critical_${_stem}
                "${CMAKE_BINARY_DIR}/CMakeFiles/${projectname}.dir/snes9x/src/${_src}.o"
        COMMENT "Moving snes9x ${_src} to .time_critical (SRAM)"
        VERBATIM)
endforeach()

# -O3 on the inner-loop interpreters / rasterizers. Default snes9x build
# uses -O2 (snes9x/CMakeLists.txt). -O3 enables more aggressive inlining,
# loop unrolling, and IPA cloning, which pay off heavily on branch-heavy
# dispatch tables and per-pixel rasterizers. Code-size growth (~10-20 %)
# is absorbed by SRAM headroom.
set(SNES9X_O3_FILES
    cpuops.c    # 65816 dispatch
    spc700.c    # APUExecute
    ppu.c       # S9xSetPPU
    tile.c      # DrawTile / DrawClippedTile
)
foreach(_src IN LISTS SNES9X_O3_FILES)
    set_property(SOURCE snes9x/src/${_src} APPEND_STRING PROPERTY
        COMPILE_FLAGS " -O3")
endforeach()

if(BUILD_FOR_BOOTLOADER)
    include("pico_shared/BootPartition.cmake")
    if(DEFINED BUILD_FOR_BOOTLOADER_SLOT)
        frens_link_to_pinned_slot(${projectname} ${BUILD_FOR_BOOTLOADER_SLOT})
    else()
        frens_offset_for_bootloader(${projectname})
    endif()
endif()

pico_add_extra_outputs(${projectname})
