# pico-snesPlus

**pico-snesPlus** is a Super Nintendo Entertainment System (SNES) emulator for RP2350-based microcontroller boards with PSRAM. It loads ROMs from an SD card through an on-screen menu and outputs video and audio over HDMI. The emulator core is a C-only derivative of [Snes9x](https://github.com/snes9xgit/snes9x) (the ndssfc/CATSFC lineage), adapted to the RP2350 and its 8 MB of PSRAM.

It is a sister project of these emulators, with which it shares its menu, display, and controller framework:

- NES: [pico-infonesPlus](https://github.com/fhoedemakers/pico-infonesPlus)
- Sega Master System / Game Gear: [pico-smsplus](https://github.com/fhoedemakers/pico-smsplus)
- Game Boy / Game Boy Color: [pico-peanutGB](https://github.com/fhoedemakers/pico-peanutGB)
- Sega Mega Drive / Genesis: [pico-genesisPlus](https://github.com/fhoedemakers/pico-genesisPlus)

It runs on four hardware configurations, each with its own ready-made binary — see [Supported hardware](#supported-hardware) for the download links:

- [Adafruit Fruit Jam](https://www.adafruit.com/product/6200) — the primary development and test board
- [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107) with an [Adafruit DVI breakout](https://www.adafruit.com/product/4984) and a microSD breakout, on a breadboard or on the [PicoNES PCB](#picones-pcb)
- [Murmulator M2](https://murmulator.ru)
- [Adafruit Feather RP2350 with HSTX Port](https://www.adafruit.com/product/6130) with a TLV320DAC3100 I2S DAC and a microSD breakout

All four are RP2350 boards with 8 MB of PSRAM, which this emulator requires: a plain Raspberry Pi Pico 2 has none and cannot be used, and configurations without PSRAM known from the sister projects will not build.

See [CHANGELOG.md](CHANGELOG.md) for release notes and per-board download links.

***

## Status and limitations

> [!NOTE]
> Squeezing a SNES into a microcontroller asks a lot of the RP2350, and most of the library plays back nicely: full speed, with sound. It is not a perfect emulator, though — now and then a graphical artifact shows up in a scrolling level or the audio hiccups, and how well a game runs varies from title to title. 

Worth knowing before you start. SNES emulation is demanding for this class of hardware, so there are some real limitations:

- **Most cartridge expansion chips are emulated, but not all.** DSP-1 to DSP-4, Super FX, C4, OBC1, SA-1, S-RTC and MSU-1 games run;  S-DD1, and SPC7110 games are refused at load time with a message. Super FX speed varies a lot per game. See [Expansion chips](#expansion-chips) for the full picture.
- **Games generally run at full speed (60 fps).** Demanding Super FX titles are the main exception; see [Expansion chips](#expansion-chips).
- **Frame skipping is still enabled by default.** Most games render every other frame; demanding Super FX titles render one frame in three. Turning it off in the settings menu renders every frame, which looks considerably smoother; many games still hold full speed, but some slow down — try it per game, and leave it on for the heaviest titles.
- **Battery saves are persisted** In-game saves that a cartridge writes to its battery-backed SRAM are stored on the SD card under `/SAVES/SNES/`. The save is written when you quit the game to the ROM menu (Select + Start → Quit game), so **quit to the menu before powering off** to keep your progress — pulling power mid-game loses everything since the last quit. There is no separate save-state feature. Games that use password systems are unaffected.
- Development and testing take place primarily on the Adafruit Fruit Jam. The other supported boards need still to be more thoroughly tested.

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

### MSU-1

MSU-1 is the homebrew expansion chip behind the CD-quality soundtrack patches (Zelda: A Link to the Past, Aladdin, Chrono Trigger and many others). It is emulated: put the patched ROM, its `.msu` data track and its `-<n>.pcm` audio tracks together and the music plays.

**Give each MSU-1 game its own subfolder.** A pack carries dozens of `.pcm` tracks, so dropping one in among your other ROMs makes the folder unusable. Subdirectories are supported by the menu, so a folder per game costs nothing:

```
/roms/SNES/Zelda MSU-1/alttp_msu.sfc
/roms/SNES/Zelda MSU-1/alttp_msu.msu
/roms/SNES/Zelda MSU-1/alttp_msu-1.pcm
/roms/SNES/Zelda MSU-1/alttp_msu-2.pcm   ...
```

The three parts must share a base name and sit in the same folder as each other; the folder name itself does not matter.

Things worth knowing:

- **The `.pcm` tracks are streamed from the SD card while the game runs** — a playing track needs a steady 176 KB/s, measured at roughly 14% of one CPU core on the Fruit Jam. This is the one part of the emulator that reads the card during gameplay, so a slow or worn card can cost frame rate or make the music stutter. A decent card is the fix. If the card cannot keep up, an `MSU1:` line appears on the serial console reporting the read cost and the underrun count; it stays quiet otherwise. Build with `-DMSU1_VERBOSE=ON` to get that line every second regardless, which is the way to measure what a particular card can do.
- **Packs with video (the "Deluxe" ones) are much heavier.** Zelda's intro FMV streams its video through the data track as well as the music — around 830 KB/s in total, which is more than half of what the SD card can deliver, and it drops that sequence to about 40 fps. The music itself stays clean; ordinary music-only packs cost only the 176 KB/s above.
- **Nothing is allocated and no card access happens unless a pack is present.** ROMs without one behave exactly as before.
- MSU-1 packs are large (often several GB), so plan card space accordingly.
- MSU-1 can be compiled out entirely with `-DENABLE_MSU1=OFF`.

### A note on Super FX speed

The Super FX chip is emulated correctly, but **whether a game is playable depends on how hard it leans on the chip**:

- **Yoshi's Island (GSU-2) runs decently** and is the good case. It uses the chip mostly for sprite and effect work on top of ordinary PPU rendering.
- **Star Fox renders correctly but is too slow to play**, landing around 30 fps rather than 60. It draws its entire 3D world through the chip, and the GSU accounts for roughly a third of all emulation time.
- Other Super FX games are not tested.

The bottleneck is PSRAM bandwidth, not the CPU clock, so the optional overclock described below does not help these games — Star Fox runs at the same speed at 504 MHz as it did at 378 MHz. Other Super FX titles fall somewhere between these two cases; try them and see.

***

## Overclocking

By default the RP2350 is overclocked to 378 MHz for this emulator. This clock gives stable performance across the tested games.

On HW_CONFIG 2 (Pimoroni Pico Plus 2 breadboard or PicoNES PCB) and HW_CONFIG 8 (Adafruit Fruit Jam), the settings menu has an optional overclock that raises the clock to 504 MHz; it is not offered on the Murmulator M2 (13) or the Feather RP2350 (14). **Enabling it can lead to instabilities and crashes**, and the performance gain is minimal: the real bottleneck is PSRAM bandwidth, not the CPU clock, so most games run at essentially the same speed at 504 MHz as they do at 378 MHz. Only enable it if you want to experiment, and expect reduced stability.

Use this software at your own risk. I am not responsible in any way for damage to your board and/or connected peripherals caused by using this software, nor for damage caused by incorrect wiring or voltages.

***

## Supported hardware

An RP2350 board with 8 MB of PSRAM is required. Only the four hardware configurations below are supported; other configurations known from the sister projects will not build, because the build refuses configurations without RP2350 and PSRAM.

| HW_CONFIG | Hardware | Binary |
| --- | --- | --- |
| 2 | [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107) with [Adafruit DVI Breakout](https://www.adafruit.com/product/4984) and a microSD breakout, on a breadboard or on the [PicoNES PCB](#picones-pcb) | [picosnesPlus_AdafruitDVISD_pico2_arm.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/picosnesPlus_AdafruitDVISD_pico2_arm.uf2) |
| 8 | [Adafruit Fruit Jam](https://www.adafruit.com/product/6200) (primary development and test board) | [picosnesPlus_AdafruitFruitJam_arm_piousb.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/picosnesPlus_AdafruitFruitJam_arm_piousb.uf2) |
| 13 | [Murmulator M2](https://murmulator.ru) | [picosnesPlus_MurmulatorM2_arm.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/picosnesPlus_MurmulatorM2_arm.uf2) |
| 14 | [Adafruit Feather RP2350 with HSTX Port](https://www.adafruit.com/product/6130) with TLV320DAC3100 I2S DAC and microSD breakout | [picosnesPlus_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/picosnesPlus_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2) |

Notes per configuration:

- **HW_CONFIG 2**: a plain Raspberry Pi Pico 2 does not work — it has no PSRAM. The Pimoroni Pico Plus 2 (with onboard PSRAM) is required. The [PicoNES PCB](#picones-pcb) is the tidy version of this configuration; it needs design v2.6 or later, which is the first that can host a Pimoroni Pico Plus 2. The two builds take different microSD breakouts: on a breadboard the [Adafruit Micro-SD breakout board+](https://www.adafruit.com/product/254), on the PCB the smaller [Adafruit Micro SD SPI or SDIO breakout](https://www.adafruit.com/product/4682), which is the footprint the board is laid out for.
- **HW_CONFIG 8**: no additional hardware is required apart from a game controller. Audio is output through the monitor and the built-in speaker or headphone jack.
- **HW_CONFIG 14**: the Feather RP2350 is sold in two variants: [with 8 MB PSRAM onboard](https://www.adafruit.com/product/6130) and [without PSRAM](https://www.adafruit.com/product/6000). On the variant without PSRAM, a PSRAM chip must be soldered onto the board separately.

For wiring and assembly instructions, see the setup sections of the [pico-infonesPlus README](https://github.com/fhoedemakers/pico-infonesPlus#setup); for the PCB version of HW_CONFIG 2, see [PicoNES PCB](#picones-pcb) below. Flashing works the same for every board: hold BOOTSEL while connecting the board over USB, then copy the `.uf2` file onto the USB drive that appears.

***

## PicoNES PCB

The PicoNES is a community PCB design by [@johnedgarpark](https://twitter.com/johnedgarpark) that turns the HW_CONFIG 2 breadboard build into a finished little console: it carries a Pico-format board, the DVI and microSD breakouts and up to two controller ports on a single board, with an optional 3D-printed case. Nothing changes in the firmware — it is simply a neater way to build hardware this emulator already supports, so you flash the same HW_CONFIG 2 binary and you are done. The current design is **v2.6**.

> [!IMPORTANT]
> For this emulator the PCB only works with a [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107), and that board needs design **v2.6 or later** plus male headers. A Raspberry Pi Pico 2 or Pico 2 W has no PSRAM and cannot run pico-snesPlus at all, so an older PicoNES board built around one of those cannot be reused here — see [Mounting the board](#mounting-the-board).

<img width="480" alt="Populated PCB with a Pico plugged into the through-holes" src="https://github.com/user-attachments/assets/2bbc846d-56b1-4528-9899-01bc9b32ce11" />

The gerber archive `pico_nesPCB_v2.6.zip` is attached to every [release](https://github.com/fhoedemakers/pico-snesPlus/releases/latest) of this project and also lives in [`pico_shared/PCB`](https://github.com/fhoedemakers/pico_shared/tree/main/PCB). Upload the zip as-is to a PCB manufacturer of your choice; [PCBWay](https://www.pcbway.com/) and JLCPCB are both good options.

The design comes from [pico-infonesPlus](https://github.com/fhoedemakers/pico-infonesPlus) and kept its NES-flavoured name, but there is nothing NES-specific about it — it is DVI, microSD and controller wiring, and this emulator runs on it just as well. Two smaller designs from that project, the PicoNES Mini and PicoNES Micro, are **not** usable here: they are built around Waveshare boards without PSRAM, which this emulator requires.

> [!NOTE]
> Sellers on AliExpress have copied the PicoNES design and sell ready-made boards. For questions about those, contact the seller.

### Mounting the board

Design v2.6 added through-holes, and that is what makes a Pimoroni Pico Plus 2 — and with it the PSRAM this emulator needs — an option at all:

| PCB design | Takes a Pimoroni Pico Plus 2? |
| --- | --- |
| v2.6 or later (through-holes) | Yes — with male headers soldered on, plugged into the through-holes |
| v2.1 and older | No — the board has to lie flat against the PCB, which the SP/CE connector on its back prevents |

> [!NOTE]
> Soldering skills are required. Solder every connection from the board to the PCB, including the ones on the short right-hand side — those are ground.

### What you need

- A [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107) with **male headers** soldered on ([these](https://a.co/d/dSNPuyo) fit), plugged into the through-holes of a v2.6 or later board.
- [Adafruit DVI Breakout Board — For HDMI Source Devices](https://www.adafruit.com/product/4984)
- [Adafruit Micro SD SPI or SDIO Card Breakout Board — 3V ONLY!](https://www.adafruit.com/product/4682) — note this is **not** the Micro-SD breakout board+ used in the breadboard build; the PCB is laid out for this smaller one.
- For controllers on the GPIO ports:
  * [one or two NES controller ports](https://www.zedlabz.com/products/controller-connector-port-for-nintendo-nes-console-7-pin-90-degree-replacement-2-pack-black-zedlabz)
  * [one or two SNES or NES controllers](https://www.amazon.com/s?k=SNES+controller)
- [Micro USB to OTG Y-cable](https://a.co/d/b9t11rl) if you want to use a USB game controller — it powers the board and connects the controller at the same time.
- Micro USB power supply.
- Optional: an on/off switch, such as [this one](https://www.kiwi-electronics.com/en/spdt-slide-switch-410?search=KW-2467).

Two controllers give you a two-player setup; a USB controller for player 1 and a controller in either GPIO port for player 2 works just as well. Audio on this configuration is carried over HDMI.

> [!NOTE]
> Although the sockets are NES connectors, they speak the SNES protocol as well and clock all 16 bits, so a real SNES pad maps 1:1 — see [Controllers](#controllers). The connectors differ, so a SNES pad needs a [SNES-to-NES adapter cable you make yourself](snestonescontroller.md) — one per socket. Ready-made cables exist but are hard to find, and some simply do not work as expected. A NES controller also works, with A→B and B→Y.

<img width="480" alt="Two-player setup with NES controllers" src="https://github.com/user-attachments/assets/d40ed98f-4632-4161-986a-732d35290fac" />

### Which binary to flash

`picosnesPlus_AdafruitDVISD_pico2_arm.uf2` — the same file as the breadboard build, since the PCB is the same hardware configuration. Unlike the sister projects there is no separate Pico 2 W image here: that board has no PSRAM and is not supported.

### 3D printed case

Gavin Knight ([DynaMight1124](https://github.com/DynaMight1124)) designed an NES-like enclosure for this PCB: [thingiverse.com/thing:6689537](https://www.thingiverse.com/thing:6689537). The v2.0 design has a base, a power-switch part and a choice of two top covers — one with a button that reaches the BOOTSEL button so firmware can be updated without opening the case, one without. Print the files that match the PCB version you own; Gavin's Thingiverse page has the details.

> [!IMPORTANT]
> Download the **latest** top cover. The Pimoroni Pico Plus 2 is always mounted on headers here, and headers raise the board — only the newest cover leaves room for the USB cable, the older ones assume a Pico soldered flat onto the PCB.

<img width="480" alt="Top cover with a button for BOOTSEL" src="https://github.com/user-attachments/assets/3c8f8990-51b9-4873-9054-64bb2cd6c300" />

For the full photo gallery and assembly detail, see the [PCB section of the pico-infonesPlus documentation](https://github.com/fhoedemakers/pico-infonesPlus#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2).

***

## SD card setup

1. Format a microSD card as FAT32 (recommended) or exFAT.
2. Copy SNES ROM files you legally own onto the card, preferably into `/roms/SNES`. Subdirectories are supported. ROMs must have the `.smc` or `.sfc` extension.
   - For an MSU-1 soundtrack patch, give the game its own subfolder and put the `.msu` and `-<n>.pcm` files in it alongside the ROM, sharing its base name. See [MSU-1](#msu-1).
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

The bundled snes9x core also compiles natively on Linux. [tools/host-harness](tools/host-harness) wraps it in a small test harness that boots a ROM through the same initialization sequence the RP2350 firmware uses and dumps rendered frames as PPM images — rendering bugs can be reproduced and bisected on a desktop machine without flashing a board. Three build variants (strip renderer vs. classic full-frame, device vs. upstream color math) let a byte-compare of the output pinpoint which layer a bug lives in. A fourth variant adds MSU-1 with a stdio backend, so a soundtrack pack can be played and the mixed audio dumped to a file without a board. See [tools/host-harness/README.md](tools/host-harness/README.md) for usage.

***

## Acknowledgements

- The [Snes9x](https://github.com/snes9xgit/snes9x) authors, and the maintainers of the ndssfc/CATSFC line of C ports on which the bundled core is based.
- The menu, HDMI driver, PSRAM allocator, and controller code in [pico_shared](https://github.com/fhoedemakers/pico_shared) are shared with the sister projects listed at the top of this README.
- Metadata, M2 testing and the 3D-printed case for the PicoNES PCB by [DynaMight1124](https://github.com/DynaMight1124)
- The [PicoNES PCB](#picones-pcb) was designed by **John Edgar Park** ([@johnedgarpark](https://twitter.com/johnedgarpark)).

## Use of AI

The port of the Snes9x core to the RP2350, the coprocessor work (Super FX, DSP, SA-1, C4, OBC1, S-RTC), and the performance and stability tuning were developed with the help of [Anthropic Claude](https://www.anthropic.com/claude) (Opus 4.7, Opus 4.8 and Fable).

## License

The Snes9x-derived emulator core in `snes9x/` is covered by its own license; see [snes9x/LICENSE](snes9x/LICENSE). The remainder of this project is licensed under the GNU General Public License v3.0; see [LICENSE](LICENSE).
