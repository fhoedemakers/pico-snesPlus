# pico_snesPlus

**pico_snesPlus** is a Super Nintendo Entertainment System (SNES) emulator for RP2350-based microcontroller boards with PSRAM. It loads ROMs from an SD card through an on-screen menu and outputs video and audio over HDMI. The emulator core is a C-only derivative of [Snes9x](https://github.com/snes9xgit/snes9x) (the ndssfc/CATSFC lineage), adapted to the RP2350 and its 8 MB of PSRAM.

It is a sister project of these emulators, with which it shares its menu, display, and controller framework:

- NES: [pico-infonesPlus](https://github.com/fhoedemakers/pico-infonesPlus)
- Sega Master System / Game Gear: [pico-smsplus](https://github.com/fhoedemakers/pico-smsplus)
- Game Boy / Game Boy Color: [pico-peanutGB](https://github.com/fhoedemakers/pico-peanutGB)
- Sega Mega Drive / Genesis: [pico-genesisPlus](https://github.com/fhoedemakers/pico-genesisPlus)

***

## Status and limitations

Please read this section before using the emulator. SNES emulation is demanding for this class of hardware, and this project has real limitations:

- **Games with special chips in the cartridge are not supported.** ROMs that require Super FX, SA-1, S-DD1, C4, DSP-1 to DSP-4, SPC7110, OBC1, or S-RTC are detected at load time and refused with a message. This excludes a significant part of the SNES library, including titles such as Star Fox, Yoshi's Island, Super Mario Kart, Super Mario RPG, Kirby Super Star, and Mega Man X2/X3.
- **NTSC (60 Hz) games do not run at a solid 60 fps.** Frame skipping (one rendered frame out of three) is enabled by default and can be turned off in the settings menu, at the cost of speed. **PAL (50 Hz) games run better** and generally come closer to full speed.
- **Audio is functional but has occasional dropouts** when emulation cannot keep up with real-time sample playback.
- **Battery saves are persisted, but save states are not.** In-game saves that a cartridge writes to its battery-backed SRAM are stored on the SD card under `/SAVES/SNES/`. The save is written when you quit the game to the ROM menu (Select + Start → Quit game), so **quit to the menu before powering off** to keep your progress — pulling power mid-game loses everything since the last quit. There is no separate save-state feature. Games that use password systems are unaffected.
- Development and testing take place primarily on the Adafruit Fruit Jam. The other supported boards receive less testing.

***

## Warning

To reach the performance described above, the RP2350 is overclocked to 378 MHz with the core voltage raised to 1.60 V (the default is 1.10 V). This is a significant overclock and overvolt that may reduce the lifespan of the chip.

Use this software at your own risk. I am not responsible in any way for damage to your board and/or connected peripherals caused by using this software, nor for damage caused by incorrect wiring or voltages.

***

## Supported hardware

An RP2350 board with 8 MB of PSRAM is required. Only the four hardware configurations below are supported; other configurations known from the sister projects will not build, because the build refuses configurations without RP2350 and PSRAM.

| HW_CONFIG | Hardware | Binary |
| --- | --- | --- |
| 2 | Breadboard with [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107), [Adafruit DVI Breakout](https://www.adafruit.com/product/4984), and [Adafruit Micro-SD breakout](https://www.adafruit.com/product/254) | [pico_snesPlus_AdafruitDVISD_pico2_arm.uf2](https://github.com/fhoedemakers/pico_snesPlus/releases/latest/download/pico_snesPlus_AdafruitDVISD_pico2_arm.uf2) |
| 8 | [Adafruit Fruit Jam](https://www.adafruit.com/product/6200) (primary development and test board) | [pico_snesPlus_AdafruitFruitJam_arm_piousb.uf2](https://github.com/fhoedemakers/pico_snesPlus/releases/latest/download/pico_snesPlus_AdafruitFruitJam_arm_piousb.uf2) |
| 13 | [Murmulator M2](https://murmulator.ru) | [pico_snesPlus_MurmulatorM2_arm.uf2](https://github.com/fhoedemakers/pico_snesPlus/releases/latest/download/pico_snesPlus_MurmulatorM2_arm.uf2) |
| 14 | [Adafruit Feather RP2350 with HSTX Port](https://www.adafruit.com/product/6130) with TLV320DAC3100 I2S DAC and microSD breakout | [pico_snesPlus_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2](https://github.com/fhoedemakers/pico_snesPlus/releases/latest/download/pico_snesPlus_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2) |

Notes per configuration:

- **HW_CONFIG 2**: a plain Raspberry Pi Pico 2 does not work — it has no PSRAM. The Pimoroni Pico Plus 2 (with onboard PSRAM) is required.
- **HW_CONFIG 8**: no additional hardware is required apart from a game controller. Audio is output through the monitor and the built-in speaker or headphone jack.
- **HW_CONFIG 14**: the Feather RP2350 is sold in two variants: [with 8 MB PSRAM onboard](https://www.adafruit.com/product/6130) and [without PSRAM](https://www.adafruit.com/product/6000). On the variant without PSRAM, a PSRAM chip must be soldered onto the board separately.

For wiring and assembly instructions, see the setup sections of the [pico-infonesPlus README](https://github.com/fhoedemakers/pico-infonesPlus#setup). Flashing works the same for every board: hold BOOTSEL while connecting the board over USB, then copy the `.uf2` file onto the USB drive that appears.

***

## SD card setup

1. Format a microSD card as FAT32 (recommended) or exFAT.
2. Copy SNES ROM files you legally own onto the card, preferably into `/roms/SNES`. Subdirectories are supported. ROMs must have the `.smc` or `.sfc` extension.
3. Insert the card into the SD card slot and power on the device.
4. Select a game in the on-screen menu to start it.

In-game battery saves are written to the SD card under `/SAVES/SNES/` (created automatically) when you quit a game to the ROM menu, so quit to the menu before powering off. There are no save states; see [Status and limitations](#status-and-limitations).

***

## Controllers

Every supported controller delivers the full SNES button set (B, Y, Select, Start, d-pad, A, X, L, R), laid out to match the SNES pad positions (bottom=B, right=A, top=X, left=Y):

| Controller | SNES mapping |
| --- | --- |
| XInput (Xbox One/360/Series, 8BitDo in X-mode) | Positional: A→B, B→A, X→Y, Y→X; LB/RB→L/R; Back→Select; Guide opens the menu |
| DualShock 4 / DualSense | Cross→B, Circle→A, Square→Y, Triangle→X; L1 or L2→L, R1 or R2→R; Share/touchpad→Select, Options→Start |
| MantaPad (AliExpress SNES USB pad, 081f:e401 / 0810:e501) | 1:1 by label — SNES mode is active at connect, no Y-press needed |
| Wii Classic / SNES-Classic-mini pad (I2C port) | 1:1 by label, including L/R (ZL/ZR also act as L/R) |
| SNES controller on the GPIO NES port (boards that have one) | 1:1 by label — the port clocks all 16 bits |
| NES controller on the GPIO NES port | Positional: A→B, B→Y (jump/run); Select/Start/d-pad 1:1 |
| USB keyboard | Z=B, X=A, C=X, V=Y, Q=L, W=R, A=Select, S=Start, arrows=d-pad |
| PS Classic | Cross→B, Circle→A, Square→Y, Triangle→X (shoulders not mapped yet) |

Two players: a second USB pad is player 2. When a USB pad is connected, the GPIO NES/SNES pad and the Wii Classic pad act as player 2; without one they are player 1.

The settings menu contains a controller test screen that shows which button the emulator receives for each press.

See the [pico-infonesPlus README](https://github.com/fhoedemakers/pico-infonesPlus#gamecontroller-support) for general controller notes and troubleshooting.

***

## Menu and in-game controls

In the menu:

- **Up/Down**: previous/next item, **Left/Right**: previous/next page.
- **A**: open folder / start the selected game.
- **B**: back to the parent folder.
- **Start**: show game metadata and box art, when metadata files are present on the card (same format as the sister projects).
- **Select**: open the settings menu.

In game:

- **Select + Start** opens the settings menu. From there you can quit to the ROM menu (which writes the cartridge's battery save to the SD card), reset the game, or change settings: screen mode (8:7 or 1:1, with or without scanlines), frame rate display, audio on/off, frame skip, rapid-fire on A/B, font colors, the controller test screen, and board-specific options such as speaker volume and the NeoPixel VU meter on the Fruit Jam. Settings are remembered across restarts.

***

## Building from source

Build on Linux (a Raspberry Pi also works) or on Windows under WSL, with the [Pico SDK](https://github.com/raspberrypi/pico-sdk) version 2.x or later installed and `PICO_SDK_PATH` set. Two additional requirements:

- The TinyUSB submodule of the Pico SDK must be on the latest master branch (`cd $PICO_SDK_PATH/lib/tinyusb && git checkout master && git pull`).
- Configurations 8 and 14 use PIO USB for a second USB port and need [Pico-PIO-USB](https://github.com/sekigon-gonnoc/Pico-PIO-USB), with `PICO_PIO_USB_PATH` pointing to the cloned repository.

Then:

```bash
git clone https://github.com/fhoedemakers/pico_snesPlus.git
cd pico_snesPlus
git submodule update --init --recursive
./bld.sh -c2 -2    # HW_CONFIG 2:  Pimoroni Pico Plus 2 breadboard
./bld.sh -c8       # HW_CONFIG 8:  Adafruit Fruit Jam
./bld.sh -c13      # HW_CONFIG 13: Murmulator M2
./bld.sh -c14      # HW_CONFIG 14: Adafruit Feather RP2350
```

Run `./bld.sh -h` for all options. The resulting `.uf2` file is placed in the `releases/` folder; flash it by holding BOOTSEL while connecting the board and copying the file onto the USB drive that appears.

***

## Acknowledgements

- The [Snes9x](https://github.com/snes9xgit/snes9x) authors, and the maintainers of the ndssfc/CATSFC line of C ports on which the bundled core is based.
- The menu, HDMI driver, PSRAM allocator, and controller code in [pico_shared](https://github.com/fhoedemakers/pico_shared) are shared with the sister projects listed at the top of this README.

## License

The Snes9x-derived emulator core in `snes9x/` is covered by its own license; see [snes9x/LICENSE](snes9x/LICENSE). The remainder of this project is licensed under the GNU General Public License v3.0; see [LICENSE](LICENSE).
