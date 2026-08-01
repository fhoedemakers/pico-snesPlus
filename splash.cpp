#include "menu.h"
#include "FrensHelpers.h"
#include <cstring>

// called by menu.cpp
// shows emulator specific splash screen
static int fgcolorSplash = DEFAULT_FGCOLOR;
static int bgcolorSplash = DEFAULT_BGCOLOR;
void splash()
{
    char s[SCREEN_COLS + 1];  // SCREEN_COLS is the compile-time maximum
    const int cols = menuVisibleCols; // 40 or 80, whichever is on screen
    ClearScreen(bgcolorSplash);

    strcpy(s, "Pico-");
    int x = cols / 2 - strlen("Pico-SNES+") / 2;
    putText(x, 2, s, fgcolorSplash, bgcolorSplash);

    putText(x + 5, 2, "S", CRED, bgcolorSplash);
    putText(x + 6, 2, "N", CGREEN, bgcolorSplash);
    putText(x + 7, 2, "E", CBLUE, bgcolorSplash);
    putText(x + 8, 2, "S", CLIGHTBLUE, bgcolorSplash);
    putText(x + 9, 2, "+", fgcolorSplash, bgcolorSplash);

    strcpy(s, "SNES emulator for RP2350+PSRAM");
    putText(cols / 2 - strlen(s) / 2, 3, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "Snes9x Core");
    putText(cols / 2 - strlen(s) / 2, 5, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "snes9x.com / retro-go port");
    putText(cols / 2 - strlen(s) / 2, 6, s, CLIGHTBLUE, bgcolorSplash);
#if !HSTX
    strcpy(s, "Pico Port");
    putText(cols / 2 - strlen(s) / 2, 9, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "@shuichi_takano");
    putText(cols / 2 - strlen(s) / 2, 10, s, CLIGHTBLUE, bgcolorSplash);
#else
    
    strcpy(s, "HDMI Driver");
    putText(cols / 2 - strlen(s) / 2, 9, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "fliperama86");
    putText(cols / 2 - strlen(s) / 2, 10, s, CLIGHTBLUE, bgcolorSplash);
#endif
    strcpy(s, "Menu System & SD Card Support");
    putText(cols / 2 - strlen(s) / 2, 13, s, fgcolorSplash, bgcolorSplash);
    strcpy(s, "@frenskefrens");
    putText(cols / 2 - strlen(s) / 2, 14, s, CLIGHTBLUE, bgcolorSplash);

    strcpy(s, "NES/WII controller support");
    putText(cols / 2 - strlen(s) / 2, 17, s, fgcolorSplash, bgcolorSplash);

    strcpy(s, "@PaintYourDragon @adafruit");
    putText(cols / 2 - strlen(s) / 2, 18, s, CLIGHTBLUE, bgcolorSplash);

    // These three lines are left-aligned to a common edge rather than
    // individually centered. Anchor that edge to the widest of them (36 chars),
    // which reproduces the hand-placed columns 2 and 13 exactly at 40 columns.
    int blockCol = (cols - 36) / 2;
    if (blockCol < 0)
        blockCol = 0;

    strcpy(s, "PCB Design:");
    putText(blockCol, 21, s, fgcolorSplash, bgcolorSplash);

    strcpy(s, "@johnedgarpark DynaMight");
    putText(blockCol + 11, 21, s, CLIGHTBLUE, bgcolorSplash);

    strcpy(s, "3D Printed Case & artwork: DynaMight");
    putText(blockCol, 23, s, fgcolorSplash, bgcolorSplash);

    strcpy(s, "https://github.com/");
    putText(cols / 2 - strlen(s) / 2, 25, s, CLIGHTBLUE, bgcolorSplash);
    strcpy(s, "fhoedemakers/pico-snesPlus");
    putText(cols / 2 - strlen(s) / 2, 26, s, CLIGHTBLUE, bgcolorSplash);
}