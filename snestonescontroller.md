# How to create an adapter cable so you can use an SNES controller on your NES.

Source: "Using Super NES controllers with a NES" [snes_to_nes_controller.txt](http://www.neshq.com/hardmods/snes_to_nes_controller.txt)
by Mark Knibbs (rev. 0.1, 5 Apr 1998), reproduced here and reformatted as
Markdown.

<img width="640" alt="SNES controller adapter cable for NES consoles" src="https://github.com/user-attachments/assets/56f9152f-ed77-4be6-b7eb-3425b5c73a47" />

---

## Revision History

| Version | Date      | Changes        |
| ------- | --------- | -------------- |
| 0.1     | 5-Apr-98  | First release. |

---

## Introduction

The Super NES has different controllers to the NES. Compared to the original
NES ones, the Super NES controllers are more rounded and have four extra
buttons. If you are used to the Super NES controller, you may well prefer it
to the NES one. If only you could use the more comfortable SNES controller to
play games on the NES...

Well, you can. Super NES controllers use a different type of connector than
NES controllers, so you cannot simply plug a Super NES controller directly
into a NES. However, apart from this the Super NES controller is backwardly
compatible with the NES. By making a simple adapter, you can connect your SNES
controller to the NES.

When using the SNES controller to play NES games, the D-pad, Start and Select
buttons function as you would expect:

| SNES button    | NES equivalent      |
| -------------- | ------------------- |
| D-pad          | D-pad               |
| Start / Select | Start / Select      |
| Y              | B                   |
| B              | A                   |
| A, X, L, R     | *no function*       |

---

## Adapter Options

There are several ways to make an adapter. Some possibilities:

- Buy controller extension cables for both the NES and SNES. Cut each in half
  and connect the NES controller plug lead to the SNES controller socket lead.
  The advantage of this is that you don't need to modify your controller.

- If you have an old broken NES controller, you can cut the cable off (or
  open up the controller and desolder the cable from inside), and use that
  along with a cut-up SNES controller extension lead.

- If you have a spare SNES controller, you could remove the cable and replace
  it with one which has a NES connector on. The disadvantage of this is that
  you cannot use the SNES controller with a SNES any more, and it is specific
  to a single SNES controller.

You get the idea. Below I will describe how to make an adapter using two
controller extension cables. These should be available cheaply in video game
shops. The procedure if you are doing it another way should be very similar.

---

## Some Basics

You need to know which pins go where on the NES and SNES controllers, so
examine the diagrams below. The pin number is next to the corresponding pin.

**NES controller connector pin definitions:**

```text
        ___
       |   \
     1 | O  \
       |     \
     2 | O  O | 5
       |      |
     3 | O  O | 6
       |      |
     4 | O  O | 7
       +------+
```

**Super NES controller connector pin definitions:**

```text
        ______________________
       |            |         \
       | O  O  O  O | O  O  O  |
       |____________|_________/
         1  2  3  4   5  6  7
```

---

## Which Pins to Connect with Which

> **See the note about wire colours below!**

| SNES pin | Wire colour\* | Function   | NES pin | Wire colour\* |
| :------: | ------------- | ---------- | :-----: | ------------- |
| 1        | white         | Vcc (+5V)  | 5       | white         |
| 2        | yellow        | CUP        | 2       | red           |
| 3        | orange        | OUT0       | 3       | orange        |
| 4        | red           | D0         | 4       | yellow        |
| 7        | brown         | GND (0V)   | 1       | green         |

\* the wire colours in the table are only applicable to original Nintendo
controller cables. If you are modifying extension cables, the colours will be
different. Basically: **ignore the colours in the table, just pay attention to
the pin numbers.**

SNES pins 5 & 6, and NES pins 6 & 7 are not used by standard controllers. NES
pins 6 & 7 are definitely used by the NES Zapper, and may also be used by e.g.
the Power Pad or Power Glove. The Super NES Super Scope uses SNES pin 6.

There is no need to connect these remaining two pins, as no SNES peripheral
will function as a NES Zapper, for example.

---

## How to make the Adapter

> This procedure may require some soldering, and is quite fiddly.

1. Cut both the NES controller extension and the SNES controller extension
   cables in half. Put the halves with a NES socket and a SNES plug at the end
   to one side; they are not needed.

2. Using a sharp knife such as a craft knife, carefully strip the outer cover
   from the ends of the NES plug lead and SNES socket lead. (The outer cover
   is usually black or grey.) Two centimetres should be sufficient. Be careful
   not to damage the multi-coloured wires inside.

3. Carefully strip the end centimetre or so of insulation from each of the
   seven wires which were just exposed. To stop the ends from getting frayed,
   it is best to twist the exposed metal and tin it with solder after
   stripping the insulation from each one. Do the same thing for the other
   cable; there are 14 wires to strip and tin in total.

4. Now you need to determine which wire goes to which pin. You will need to
   use a multimeter or continuity checker for this. Connect one terminal of
   the multimeter to a pin of the connector, and in turn touch the other
   terminal to each of the bared wires. For the NES connector, you will
   probably need to insert a piece of ridid wire into each pin to make
   contact; touch the terminal to the other end of this wire.

   Make a note of which wire colour corresponds to each pin, for both NES and
   SNES connector cables. Work out which colours to connect to which, by
   referring to the table in the [Which Pins to Connect with Which](#which-pins-to-connect-with-which)
   section above.

5. Now simply connect the right wires. It is best to solder them together, and
   use insulating tape around each joint. Alternatively, if you have stripped
   enough insulation from each wire, you can try twisting the two ends of each
   pair of wires together. However the connections made like this will not be
   as reliable, and I do not recommend it. Insulate the ends of the remaining
   two wires on each side.

   A better solution is to buy some male and female connectors, for example
   9-pin D connectors. If you put a connector on each end of the four cables
   (including the cables which you put to one side in step 1), you can swap
   them over if you ever want to use a plain SNES or NES extension lead in
   future.

6. Double check that you have connected the correct wires. You can verify this
   with a multimeter. Now you can plug the adapter into your NES, and a SNES
   controller into the other end of the adapter. If everything has gone okay,
   you should be able to use the SNES controller to play NES games.
