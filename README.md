# pico_snesPlus

An experimental SNES emulator for the **Adafruit Fruit Jam**.

## What this is

A proof of concept that runs Super Nintendo games on the Fruit Jam,
with HDMI video and audio out of the board's built-in HDMI port, USB
controllers, and ROM loading from microSD. It is based on the
[snes9x](https://github.com/snes9xgit/snes9x) emulator core, ported to
the RP2350 microcontroller and its 8 MB of PSRAM.

## What this is not

This is **not a finished emulator** and not intended for general use:

- Most games do not run at full 60 fps. With frame skipping on you can
  expect somewhere in the mid-30s to low-40s depending on the game.
- Audio is functional but has occasional dropouts when emulation
  cannot keep up with real-time sample playback.
- Cartridges that need special chips inside them — Super FX, SA-1,
  DSP-1, SDD-1, C4, SPC7110, OBC1, S-RTC — are detected and politely
  refused at load time. So no Star Fox, Yoshi's Island, Mario Kart,
  Kirby Super Star, etc. That covers a large slice of the SNES library.
- It has only been tested on a single Adafruit Fruit Jam unit. Other
  units may run faster, slower, or not at all.

If you want a polished SNES experience on Pico hardware today, this
project is not the right starting point.

## Hardware required

- An Adafruit Fruit Jam
- A microSD card with SNES ROMs in `/roms/SNES/`
- An HDMI display
- A USB game controller (most XInput / standard HID controllers work)

## Building

Standard Pico SDK toolchain. From the project root:

```
git submodule update --init --recursive
mkdir build && cd build
cmake ..
make -j
```

Hold BOOTSEL on the Fruit Jam, plug it in, and drop the resulting
`pico_snesPlus.uf2` onto the USB drive that appears.

## Running

Power the Fruit Jam with the microSD card inserted. The on-screen
menu lists your `.smc` / `.sfc` files; pick one and press Start.

Inside a game, **Select + Start** on any connected pad opens the
settings menu.

## Controllers

Every supported controller delivers the full SNES button set
(B, Y, Select, Start, d-pad, A, X, L, R), laid out to match the SNES
pad positions (bottom=B, right=A, top=X, left=Y):

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

Two players: a second USB pad is player 2. When a USB pad is
connected, the GPIO NES/SNES pad and the Wii Classic pad act as
player 2; without one they are player 1.

## Acknowledgements

- The snes9x project — this port wraps a snes9x C core.
- The shared framework code under `pico_shared/` — menu, HDMI driver,
  PSRAM allocator and so on — is shared with sister projects like
  `pico-infonesPlus` and `pico-genesisPlus`.

## License

The snes9x core has its own license; see `snes9x/LICENSE`. The rest of
the project is covered by the top-level `LICENSE` file.
