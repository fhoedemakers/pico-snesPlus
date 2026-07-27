/* This file is part of Snes9x. See LICENSE file. */
/* SuperFX (GSU) emulator, vendored from the CATSFC (ndssfc) Snes9x C line. */

#ifndef _FXEMU_H_
#define _FXEMU_H_ 1

#include "snes9x.h"

/* The FxInfo_s structure, the link between the FxEmulator and the Snes Emulator */
typedef struct
{
   uint8_t*  pvRegisters; /* 768 bytes located in the memory at address 0x3000 */
   uint32_t  nRamBanks;   /* Number of 64kb-banks in GSU-RAM/BackupRAM (banks 0x70-0x73) */
   uint8_t*  pvRam;       /* Pointer to GSU-RAM */
   uint32_t  nRomBanks;   /* Number of 32kb-banks in Cart-ROM */
   uint8_t*  pvRom;       /* Pointer to Cart-ROM */
   /* Max GSU instructions to execute per scanline, derived from the GSU-1
    * clock and the video timing in S9xResetSuperFX (cpu.c). 0 means "run to
    * STOP", the unbudgeted behaviour this port shipped with. Doubled at the
    * call site when CLSR bit 0 selects the 21.48 MHz GSU-2 clock. */
   uint32_t  speedPerLine;
} FxInit_s;

/* Reset the FxChip */
extern void FxReset(FxInit_s* psFxInfo);

/* Execute until the next stop instruction */
extern int32_t FxEmulate(uint32_t nInstructions);

/* Put the GSU back in the post-STOP state. Needed on any CPU-initiated
 * start/abort now that a budget can suspend a render mid-flight. */
extern void FxAbortSession(void);

/* Write access to the cache */
extern void FxFlushCache(void); /* Called when the G flag in SFR is set to zero */

/* SCBR write seen.  We need to update our cached screen pointers */
extern void fx_dirtySCBR(void);

/* Update RamBankReg and RAM Bank pointer */
extern void fx_updateRamBank(uint8_t Byte);

extern void fx_computeScreenPointers(void);
#endif
