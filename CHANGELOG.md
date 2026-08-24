# CHANGELOG

**v0.4** is a maintenance release. It updates the shared menu and support code, fixing a black screen on **DVI-only monitors** and making start-up steadier. The emulator core is unchanged.

# General Info

[Binaries for each configuration are at the end of this page](#downloads___).

[See the Supported hardware and SD card setup sections in the README for how to install and wire up your board.](https://github.com/fhoedemakers/pico-snesPlus#supported-hardware)

> [!IMPORTANT]
> An **RP2350** board with **8 MB PSRAM** is required. The original RP2040 (Pico 1), and RP2350 boards without PSRAM, are not supported.

> [!WARNING]
> **The optional 504 MHz overclock in the settings menu is not advised. Leave it off.**
>
> It gains very little — the bottleneck is PSRAM bandwidth, not the CPU clock, so most games run at essentially the same speed as at the default 378 MHz. It raises the core voltage, makes the chip run considerably hotter, and can overheat, destabilise or permanently damage the RP2350 and the board it is on. It is off by default and exists for experimenting only. Enabling it is entirely at your own risk; the author accepts no responsibility for any damage.

# v0.4

A maintenance release. It brings the shared menu and support code up to date; the emulator itself is unchanged. Upgrading is only a matter of flashing the new `.uf2` — your settings, saves and save states on the SD card are untouched.

## Fixes

- **DVI-only monitors show a picture again.** With **Display Mode** set to DVI, some older screens that accept DVI but not HDMI stayed black. They work again.
- **Steadier start-up.** The board lets its power settle before switching to the higher clock speed.

# v0.3

Updates the shared menu and support code (`pico_shared`). The emulator core is unchanged. Everything listed under [v0.2](#v02) and [v0.1](#v01) still applies.

## Recently played games

The menu keeps a list of the last 20 games started, most recent first. It is opened with **X** in the ROM browser, or with the **Recently played** entry at the top of the settings menu. That entry is present only when the settings menu is opened from the ROM browser, not from a running game; it is the route for pads without an X button, such as a NES pad on the GPIO port.

In the list, **A** starts the highlighted game, **Select** removes an entry after a confirmation, **Start** shows its metadata and box art, and **B** closes the list. Entries are added automatically when a game is started; starting a game already in the list moves it to the top. The list closes after a minute without input.

The list is stored as plain text in `/recent_SNES.txt` in the root of the SD card, one game per line. It survives a reboot and can be read, edited or deleted on a PC. A missing or damaged file is treated as an empty list; no other settings are affected. Each emulator running under [pico-bootLoader](https://github.com/fhoedemakers/pico-bootLoader) keeps its own list. An entry whose ROM is no longer on the card is reported as missing instead of started, and can be removed with Select.

See [Recently played games](https://github.com/fhoedemakers/pico-snesPlus#recently-played-games) in the README.

## Overclocking

The 504 MHz overclock option is now documented as not advised; see the warning above and the [Overclocking section](https://github.com/fhoedemakers/pico-snesPlus#overclocking) in the README. The default clock of 378 MHz is unchanged.

## The PicoNES PCB

Unchanged since [v0.2](#v02): design **v2.6** is required, and the gerber archive `pico_nesPCB_v2.6.zip` is attached to this release as before. The README chapter describing it is now called [Custom PCB](https://github.com/fhoedemakers/pico-snesPlus#custom-pcb) and lists the applicable design in a table; the PicoNES Mini and PicoNES Micro remain not applicable to this emulator, since they are built around Waveshare boards without PSRAM.

## Other changes

- **Menu, SNES pad on the GPIO port**: the four face buttons are now read by label instead of positionally, so A chooses, B goes back and X opens the recently played list, as on USB and Wii Classic pads. NES pads are unchanged.
- **Controller test screen**: reports the detected pad type (NES or SNES) for a pad on the GPIO port and names its buttons accordingly, and shows the raw port word.
- **Fixed the settings menu discarding its result** after a screen opened from it was closed, which made starting a game from the recently played list return to the browser without effect, and the controller test start the screensaver on exit.
- **Fixed "Reset to defaults"** carrying over a stale value of the obsolete standalone scanline toggle instead of restoring the off state.
- Reduced stack use in the menu: the ROM browser no longer keeps a 592-byte `FIL` on the 3 KB core 0 stack.

# v0.2

Everything listed under [v0.1](#v01) still applies — the features and the known limitations. This release adds MSU-1 soundtrack support, can be started from the pico-bootLoader boot menu, and documents the PicoNES PCB.

## MSU-1 soundtracks

MSU-1 is the homebrew expansion chip used by the CD-quality soundtrack patches that exist for many games — Zelda: A Link to the Past, Chrono Trigger, Aladdin and others. Those patches now play their music.

Give each MSU-1 game its own subfolder on the SD card, and put the patched ROM, its `.msu` file and its `-<n>.pcm` tracks in it, all sharing the same base name. A pack carries dozens of tracks, so keeping it in a folder of its own is the difference between a tidy card and an unusable one:

```
/roms/SNES/Zelda MSU-1/alttp_msu.sfc
/roms/SNES/Zelda MSU-1/alttp_msu.msu
/roms/SNES/Zelda MSU-1/alttp_msu-1.pcm
/roms/SNES/Zelda MSU-1/alttp_msu-2.pcm   ...
```

Nothing else needs to be configured — the emulator picks the pack up when it loads the ROM.

Worth knowing:

- **The music is streamed from the SD card while you play.** It is the only thing that reads the card during a game, so a slow or worn card can cost frame rate or make the music stutter. A decent card is the fix.
- **Packs with video (the "Deluxe" ones) are heavier.** Zelda's intro movie streams its video through the card as well, which drops that sequence to about 40 fps. The music itself stays clean, and ordinary music-only packs are far cheaper.
- **ROMs without a pack are unaffected** — nothing extra is loaded and the card is not touched.
- MSU-1 packs are large, often several GB, so plan card space accordingly.

See the [MSU-1 section in the README](https://github.com/fhoedemakers/pico-snesPlus#msu-1) for the details.

## Starting from the bootloader

pico-snesPlus can now be launched from [pico-bootLoader](https://github.com/fhoedemakers/pico-bootLoader), a boot menu for RP2350 boards that keeps several emulators (and a Doom port) on one SD card. At power-on you pick one from an on-screen menu; the bootloader flashes it and starts it, and a reset or power cycle always returns to the menu — no reconnecting the board to a PC to switch systems. From inside the emulator, Select + Start → *Return to emulator selection* goes back.

The bootloader ships its own copy of this emulator in the SD-card archive on its releases page, so nothing here needs to be downloaded for that. If you do not use the bootloader, nothing changes: flash the `.uf2` for your board as before.

## The PicoNES PCB

The README has a [PicoNES PCB](https://github.com/fhoedemakers/pico-snesPlus#picones-pcb) chapter covering the community PCB design that turns the HW_CONFIG 2 breadboard build into a finished little console: the parts it needs, how the board is mounted, which binary to flash and the matching 3D-printed case. The gerber archive `pico_nesPCB_v2.6.zip` is attached to this release.

**PCB design v2.6 is what makes this work.** The design gained through-holes, so a [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107) fitted with male headers can be plugged in instead of soldering a board flat. That matters more here than in the sister projects: this emulator needs PSRAM, so the Pimoroni Pico Plus 2 is the only board on that PCB that can run it at all. On v2.1 and older designs the board has to lie flat, which the SP/CE connector on its back prevents — an older PicoNES board built around a Pico 2 cannot be reused for pico-snesPlus. That is why **v2.1 is no longer offered here**: it is not suitable for this emulator. No separate binary is needed: flash the same `picosnesPlus_AdafruitDVISD_pico2_arm.uf2` as the breadboard build. When the board is mounted on headers, print the **latest** top cover from Thingiverse; the older ones assume a board soldered flat and leave no room for the USB cable.

The two smaller designs from pico-infonesPlus, the PicoNES Mini and PicoNES Micro, are **not** usable with this emulator either — they are built around Waveshare boards without PSRAM — so their gerbers are not attached.

The GPIO controller ports on the PCB (and on a breadboard build) use NES connectors but speak the SNES protocol and clock all 16 bits, so a real SNES pad maps 1:1. [snestonescontroller.md](https://github.com/fhoedemakers/pico-snesPlus/blob/main/snestonescontroller.md) describes the SNES-to-NES adapter cable you need to make for it.

## Other changes

- **The README opens with the list of hardware configurations it runs on**, so you no longer have to scroll to find out whether your board is supported. Each entry links to [Supported hardware](https://github.com/fhoedemakers/pico-snesPlus#supported-hardware) for the binary, and to the PCB design where one applies.
- The PCB design files moved to the shared `pico_shared` repository, and the stale `PCB/` copy was removed from this one — it still advertised a gerber it did not contain.
- Fixed object file extension handling in the CMake configuration.
- MSU-1 support was developed with the help of [Anthropic Claude](https://www.anthropic.com/claude), like the rest of the port.

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
| 2 | Pimoroni Pico Plus 2, on a breadboard or on the PicoNES PCB | [picosnesPlus_AdafruitDVISD_pico2_arm.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/picosnesPlus_AdafruitDVISD_pico2_arm.uf2) |
| 8 | Adafruit Fruit Jam | [picosnesPlus_AdafruitFruitJam_arm_piousb.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/picosnesPlus_AdafruitFruitJam_arm_piousb.uf2) |
| 13 | Murmulator M2 | [picosnesPlus_MurmulatorM2_arm.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/picosnesPlus_MurmulatorM2_arm.uf2)  |
| 14 | Adafruit Feather RP2350 with TLV320DAC3100 | [picosnesPlus_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/picosnesPlus_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2) |

## Other downloads

- Metadata: [SNESMetadata.zip](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/SNESMetadata.zip)
- PicoNES PCB gerbers: [pico_nesPCB_v2.6.zip](https://github.com/fhoedemakers/pico-snesPlus/releases/latest/download/pico_nesPCB_v2.6.zip) — upload as-is to a PCB manufacturer. See [PicoNES PCB](https://github.com/fhoedemakers/pico-snesPlus#picones-pcb) in the README.

Extract the zip file to the root folder of the SD card. Select a game in the menu and press START to show more information and box art. Works for most official released games. The screensaver shows floating random cover art.

