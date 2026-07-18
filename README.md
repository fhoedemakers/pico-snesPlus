# pico-snesPlus

**pico-snesPlus** is a Super Nintendo Entertainment System (SNES) emulator for RP2350-based microcontroller boards with PSRAM. It loads ROMs from an SD card through an on-screen menu and outputs video and audio over HDMI. The emulator core is a C-only derivative of [Snes9x](https://github.com/snes9xgit/snes9x) (the ndssfc/CATSFC lineage), adapted to the RP2350 and its 8 MB of PSRAM.

It is a sister project of these emulators, with which it shares its menu, display, and controller framework:

- NES: [pico-infonesPlus](https://github.com/fhoedemakers/pico-infonesPlus)
- Sega Master System / Game Gear: [pico-smsplus](https://github.com/fhoedemakers/pico-smsplus)
- Game Boy / Game Boy Color: [pico-peanutGB](https://github.com/fhoedemakers/pico-peanutGB)
- Sega Mega Drive / Genesis: [pico-genesisPlus](https://github.com/fhoedemakers/pico-genesisPlus)

See [CHANGELOG.md](CHANGELOG.md) for release notes and per-board download links.

***

## Status and limitations

> [!IMPORTANT]
> This version is not perfect. The emulator pushes the RP2350 to its limits: occasional screen artifacts can appear, especially in scrolling levels, and sound is not always flawless. Performance can also vary from game to game. Expect rough edges.

Please read this section before using the emulator. SNES emulation is demanding for this class of hardware; much of the library runs well, but there are real limitations:

- **Most cartridge expansion chips are emulated, but not all.** DSP-1 to DSP-4, Super FX, C4, OBC1, SA-1 and S-RTC games run;  S-DD1, and SPC7110 games are refused at load time with a message. Super FX speed varies a lot per game. See [Expansion chips](#expansion-chips) for the full picture.
- **Games generally run at full speed (60 fps).** Demanding Super FX titles are the main exception; see [Expansion chips](#expansion-chips).
- **Frame skipping is still enabled by default.** Most games render every other frame; demanding Super FX titles render one frame in three. Turning it off in the settings menu renders every frame, which looks considerably smoother; many games still hold full speed, but some slow down — try it per game, and leave it on for the heaviest titles.
- **Battery saves are persisted** In-game saves that a cartridge writes to its battery-backed SRAM are stored on the SD card under `/SAVES/SNES/`. The save is written when you quit the game to the ROM menu (Select + Start → Quit game), so **quit to the menu before powering off** to keep your progress — pulling power mid-game loses everything since the last quit. There is no separate save-state feature. Games that use password systems are unaffected.
- Development and testing take place primarily on the Adafruit Fruit Jam. The other supported boards need still to be tested. Especially the Murmulator M2 seems to have some issues and may bootloop.

***

## Expansion chips

Many SNES cartridges carry an extra chip that the console itself does not have. These are emulated:

| Chip | Status | Example games |
| --- | --- | --- |
| DSP-1 / DSP-1B | Emulated | Super Mario Kart, Pilotwings |
| DSP-2 / DSP-3 / DSP-4 | Emulated | Dungeon Master, SD Gundam GX, Top Gear 3000 |
| Super FX (GSU-1 / GSU-2) | Emulated, **speed varies — see below** | Star Fox, Yoshi's Island, Stunt Race FX, Doom |
| C4 | Emulated | Mega Man X2, Mega Man X3 |
| SA-1 | Emulated | Super Mario RPG, Kirby Super Star, Kirby's Dream Land 3 |
| OBC1 | Emulated | Metal Combat: Falcon's Revenge |
| S-RTC | Emulated | Dai Kaijuu Monogatari II |

These are **not** emulated. Such ROMs are detected at load time and refused with a message:

| Chip | Example games |
| --- | --- |
| S-DD1 | Star Ocean, Street Fighter Alpha 2 |
| SPC7110 | Far East of Eden Zero, Momotarou Dentetsu Happy |

Two more chips, SETA (ST010/ST011) and BS-X, are also unimplemented but are not detected, so those carts load and then run without the chip rather than being refused. Expect them to misbehave.

### A note on Super FX speed

The Super FX chip is emulated correctly, but **whether a game is playable depends on how hard it leans on the chip**:

- **Yoshi's Island (GSU-2) runs decently** and is the good case. It uses the chip mostly for sprite and effect work on top of ordinary PPU rendering.
- **Star Fox renders correctly but is too slow to play**, landing around 30 fps rather than 60. It draws its entire 3D world through the chip, and the GSU accounts for roughly a third of all emulation time.
- Other Super FX games are not tested.

The bottleneck is PSRAM bandwidth, not the CPU clock, so the optional overclock described below does not help these games — Star Fox runs at the same speed at 504 MHz as it did at 378 MHz. Other Super FX titles fall somewhere between these two cases; try them and see.

***

## Overclocking

By default the RP2350 is overclocked to 378 MHz for this emulator. This clock gives stable performance across the tested games.

On HW_CONFIG 2 (Pimoroni Pico Plus 2 breadboard) and HW_CONFIG 8 (Adafruit Fruit Jam), the settings menu has an optional overclock that raises the clock to 504 MHz; it is not offered on the Murmulator M2 (13) or the Feather RP2350 (14). **Enabling it can lead to instabilities and crashes**, and the performance gain is minimal: the real bottleneck is PSRAM bandwidth, not the CPU clock, so most games run at essentially the same speed at 504 MHz as they do at 378 MHz. Only enable it if you want to experiment, and expect reduced stability.

Use this software at your own risk. I am not responsible in any way for damage to your board and/or connected peripherals caused by using this software, nor for damage caused by incorrect wiring or voltages.

***

## Supported hardware

An RP2350 board with 8 MB of PSRAM is required. Only the four hardware configurations below are supported; other configurations known from the sister projects will not build, because the build refuses configurations without RP2350 and PSRAM.

| HW_CONFIG | Hardware | Binary |
| --- | --- | --- |
| 2 | Breadboard with [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107), [Adafruit DVI Breakout](https://www.adafruit.com/product/4984), and [Adafruit Micro-SD breakout](https://www.adafruit.com/product/254) | [pico_snesPlus_AdafruitDVISD_pico2_arm.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/pico_snesPlus_AdafruitDVISD_pico2_arm.uf2) |
| 8 | [Adafruit Fruit Jam](https://www.adafruit.com/product/6200) (primary development and test board) | [pico_snesPlus_AdafruitFruitJam_arm_piousb.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/pico_snesPlus_AdafruitFruitJam_arm_piousb.uf2) |
| 13 | [Murmulator M2](https://murmulator.ru) | [pico_snesPlus_MurmulatorM2_arm.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/pico_snesPlus_MurmulatorM2_arm.uf2) **NOTE** may bootloop |
| 14 | [Adafruit Feather RP2350 with HSTX Port](https://www.adafruit.com/product/6130) with TLV320DAC3100 I2S DAC and microSD breakout | [pico_snesPlus_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/pico_snesPlus_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2) |

Notes per configuration:

- **HW_CONFIG 2**: a plain Raspberry Pi Pico 2 does not work — it has no PSRAM. The Pimoroni Pico Plus 2 (with onboard PSRAM) is required.
- **HW_CONFIG 8**: no additional hardware is required apart from a game controller. Audio is output through the monitor and the built-in speaker or headphone jack.
- **HW_CONFIG 13**: the Murmulator M2 was tested by [DynaMight1124](https://github.com/DynaMight1124). On their device, the emulator started bootlooping after a few minutes of play; the cause is not yet known. Your mileage may vary.
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
| USB mouse | Emulates the SNES Mouse for Mario Paint — see [SNES Mouse](#snes-mouse-usb-mouse) below |

Two players: a second USB pad is player 2. When a USB pad is connected, the GPIO NES/SNES pad and the Wii Classic pad act as player 2; without one they are player 1.

The settings menu contains a controller test screen that shows which button the emulator receives for each press.

See the [pico-infonesPlus README](https://github.com/fhoedemakers/pico-infonesPlus#gamecontroller-support) for general controller notes and troubleshooting.

### SNES Mouse (USB mouse)

Plug in any USB mouse and it becomes a [SNES Mouse](https://en.wikipedia.org/wiki/Super_NES_Mouse) — no setting to change. Start Mario Paint and the hand cursor follows the mouse; left and right buttons map 1:1. Games without mouse support simply ignore it.

How it works, and what to expect:

- **The mouse occupies controller port 1 while it is plugged in**, exactly like connecting the real peripheral to the console's first controller socket (which is where Mario Paint expects it). The game ignores player 1's pad for as long as the mouse is connected — unplug the mouse and the pad is player 1 again. Both directions work mid-game, no reset needed.
- **Player 2 and the menu are unaffected.** A second pad keeps working, and Select + Start on any pad still opens the settings menu, mouse plugged in or not.
- **Motion is passed through as relative movement**, the way the real mouse reports it: the emulator hands the game the raw mouse deltas (halved once, because modern optical mice are far finer than the ~50 dpi original) and the game moves its own cursor — so edge behavior, cursor limits and any in-game speed settings behave exactly as on original hardware. Movement is capped at the real mouse's maximum of 63 counts per frame. To change the sensitivity, adjust `SNES_MOUSE_SENS_DIV` in `snes9x/src/port_glue.cpp` and rebuild.

***

## Metadata

The emulator can display box art and a short text description for each ROM when a metadata pack is present on the SD card. With the pack installed, pressing **START** on a ROM in the file browser displays its metadata; the screensaver also shows random box art.

A metadata pack can be downloaded from the [releases page](https://github.com/fhoedemakers/pico-snesPlus/releases) and extracted to the root of the SD card. It is installed under:

```
/metadata/SNES/
├── images/   (box art, named by ROM CRC32)
└── descr/    (text descriptions, named by ROM CRC32)
```

<img width="1920" height="1080" alt="Screenshot 2026-07-18 13-40-02" src="https://github.com/user-attachments/assets/4e0a064f-6fd7-48c9-971e-4cc5fc72aefe" />


***


## Menu and in-game controls

In the menu:

- **Up/Down**: previous/next item, **Left/Right**: previous/next page.
- **A**: open folder / start the selected game.
- **B**: back to the parent folder.
- **Start**: show game metadata and box art.
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
git clone https://github.com/fhoedemakers/pico-snesPlus.git
cd pico-snesPlus
git submodule update --init --recursive
./bld.sh -c2 -2    # HW_CONFIG 2:  Pimoroni Pico Plus 2 breadboard
./bld.sh -c8       # HW_CONFIG 8:  Adafruit Fruit Jam
./bld.sh -c13      # HW_CONFIG 13: Murmulator M2
./bld.sh -c14      # HW_CONFIG 14: Adafruit Feather RP2350
```

Run `./bld.sh -h` for all options. The resulting `.uf2` file is placed in the `releases/` folder; flash it by holding BOOTSEL while connecting the board and copying the file onto the USB drive that appears.

### Host-side render test harness

The bundled snes9x core also compiles natively on Linux. [tools/host-harness](tools/host-harness) wraps it in a small test harness that boots a ROM through the same initialization sequence the RP2350 firmware uses and dumps rendered frames as PPM images — rendering bugs can be reproduced and bisected on a desktop machine without flashing a board. Three build variants (strip renderer vs. classic full-frame, device vs. upstream color math) let a byte-compare of the output pinpoint which layer a bug lives in. See [tools/host-harness/README.md](tools/host-harness/README.md) for usage.

***

## Acknowledgements

- The [Snes9x](https://github.com/snes9xgit/snes9x) authors, and the maintainers of the ndssfc/CATSFC line of C ports on which the bundled core is based.
- The menu, HDMI driver, PSRAM allocator, and controller code in [pico_shared](https://github.com/fhoedemakers/pico_shared) are shared with the sister projects listed at the top of this README.

## Use of AI

The port of the Snes9x core to the RP2350, the coprocessor work (Super FX, DSP, SA-1, C4, OBC1, S-RTC), and the performance and stability tuning were developed with the help of [Anthropic Claude](https://www.anthropic.com/claude) (Opus 4.7, Opus 4.8 and Fable).

## License

The Snes9x-derived emulator core in `snes9x/` is covered by its own license; see [snes9x/LICENSE](snes9x/LICENSE). The remainder of this project is licensed under the GNU General Public License v3.0; see [LICENSE](LICENSE).
