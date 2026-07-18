# CHANGELOG

First public release of pico-snesPlus.

# General Info

[Binaries for each configuration are at the end of this page](#downloads___).

[See the Supported hardware and SD card setup sections in the README for how to install and wire up your board.](https://github.com/fhoedemakers/pico-snesPlus#supported-hardware)

> [!IMPORTANT]
> An **RP2350** board with **8 MB PSRAM** is required. The original RP2040 (Pico 1), and RP2350 boards without PSRAM, are not supported.

# v0.1

First public release. There will be bugs. Please register an issue when you encounter one.

> [!IMPORTANT]
> This version is not perfect. The emulator pushes the RP2350 to its limits: occasional screen artifacts can appear, especially in scrolling levels, and sound is not always flawless. Performance can also vary from game to game. Expect rough edges.

## Features

**Cartridge ROMs**

- SNES ROMs (`.smc` / `.sfc`) are loaded directly from the SD card through an on-screen menu. Subdirectories are supported.
- Games generally run at full speed (60 fps).

**Expansion chips**

Many SNES cartridges carry an extra chip that the console itself does not have. These are emulated:

- **DSP-1 to DSP-4** — Super Mario Kart, Pilotwings, Top Gear 3000.
- **Super FX** — Star Fox, Yoshi's Island, Stunt Race FX.
- **C4** — Mega Man X2, Mega Man X3.
- **SA-1** — Super Mario RPG, Kirby Super Star, Kirby's Dream Land 3.
- **OBC1** — Metal Combat: Falcon's Revenge.
- **S-RTC** — Dai Kaijuu Monogatari II.

Super FX speed depends on how hard the game leans on the chip: Yoshi's Island plays well, while Star Fox renders correctly but runs at about half speed.

Star Ocean, Street Fighter Alpha 2 (S-DD1) and Far East of Eden Zero (SPC7110) use chips that are not supported; these games are refused with a message when you try to load them.

**Battery saves**

- In-game saves that a cartridge writes to its battery-backed memory are stored on the SD card under `/SAVES/SNES/`.
- The save is written when you quit a game to the menu (Select + Start → Quit game), so **quit to the menu before powering off** to keep your progress. There are no save states.

**Display**

- HDMI video output.
- 8:7 and 1:1 screen modes, optional scanlines, and an on-screen FPS overlay.
- On the Adafruit Fruit Jam the NeoPixel LEDs can act as a VU meter.

**Audio**

- Sound is played over HDMI, the audio jack, or an external I²S DAC, depending on the board.

**Controllers**

- USB controllers, including Xbox / XInput (and 8BitDo in X-mode), Sony DualShock 4 / DualSense, the AliExpress SNES USB pad, PlayStation Classic, and USB keyboards.
- Directly wired NES / SNES gamepads, and the Wii Classic / SNES-Classic-mini pad over I²C.
- Two-player play with a second USB controller.
- **SNES Mouse**: plug in any USB mouse and it becomes a SNES Mouse in controller port 1 — Mario Paint is fully playable, no configuration needed. While the mouse is connected it takes the place of player 1's pad, just like the real peripheral; unplug it and the pad is player 1 again. See the [README](https://github.com/fhoedemakers/pico-snesPlus#snes-mouse-usb-mouse) for details.
- A controller test screen in the settings menu shows which button the emulator receives for each press.

**Overclocking**

- The RP2350 runs at 378 MHz by default, which is stable across the tested games.
- An optional 504 MHz overclock can be enabled in the settings menu on HW_CONFIG 2 and 8 (Pimoroni Pico Plus 2 breadboard and Adafruit Fruit Jam); it is not offered on the Murmulator M2 or the Feather RP2350. It can cause instability and rarely improves speed, so it is off by default.

## Known limitations

- **Occasional screen artifacts and imperfect sound.** Glitches can show up, especially in scrolling levels, and audio is not always flawless. The emulator pushes the RP2350 to its limits, so performance also varies per game.
- **Frame skipping is on by default** (every other frame; one frame in three for Super FX games). Turn it off in the settings menu to render every frame for smoother motion; many games still hold full speed, but some slow down, so try it per game.
- Demanding Super FX games such as Star Fox run below full speed.
- The SETA (ST010 / ST011) and BS-X chips are not implemented and, unlike S-DD1 and SPC7110, are not detected — those games load but misbehave.
- The SNES hi-res modes 5 and 6 (512 pixels wide, used by very few games — e.g. the Donkey Kong Country "Nintendo presents" intro screen) are rendered at half horizontal resolution, so fine hi-res text can look thin or ragged.
- Development and testing take place mainly on the Adafruit Fruit Jam; the other supported boards still need testing.

## Use of AI

The port of the Snes9x core to the RP2350, the coprocessor work (Super FX, DSP, SA-1, C4, OBC1, S-RTC), and the performance and stability tuning were developed with the help of [Anthropic Claude](https://www.anthropic.com/claude) (Opus 4.7, Opus 4.8 and Fable).

<a name="downloads___"></a>
## Downloads by configuration

Only the four RP2350 + PSRAM configurations below are supported. For board-by-board wiring and which UF2 file to flash, see the [Supported hardware section in the README](https://github.com/fhoedemakers/pico-snesPlus#supported-hardware).

| HW_CONFIG | Board | Binary |
|:--|:--|:--|
| 2 | Breadboard with Pimoroni Pico Plus 2 | [pico_snesPlus_AdafruitDVISD_pico2_arm.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/pico_snesPlus_AdafruitDVISD_pico2_arm.uf2) |
| 8 | Adafruit Fruit Jam | [pico_snesPlus_AdafruitFruitJam_arm_piousb.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/pico_snesPlus_AdafruitFruitJam_arm_piousb.uf2) |
| 13 | Murmulator M2 | [pico_snesPlus_MurmulatorM2_arm.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/pico_snesPlus_MurmulatorM2_arm.uf2)  |
| 14 | Adafruit Feather RP2350 with TLV320DAC3100 | [pico_snesPlus_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/pico_snesPlus_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2) |

## Other downloads

- Metadata: [SNESMetadata.zip](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/SNESMetadata.zip)

Extract the zip file to the root folder of the SD card. Select a game in the menu and press START to show more information and box art. Works for most official released games. The screensaver shows floating random cover art.

