/* This file is part of Snes9x. See LICENSE file. */

#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "dsp.h"
#include "cpuexec.h"
#include "apu.h"
#include "dma.h"
#include "srtc.h"
#include "obc1.h"
#include "fxemu.h"
#include "sa1.h"
#include "sa1.h"

extern FxInit_s SuperFX;

/* GSU-1 core clock: the SNES master clock / 2. Master is SNES_CLOCK_SPEED * 6
 * = 21.47727 MHz (snes9x.h), so the GSU-1 runs at 10.738636 MHz. CLSR bit 0
 * selects double that on a GSU-2; that doubling is applied at the call site in
 * S9xSuperFXExec, not here. */
#define GSU_CLOCK_HZ 10738636u
/* Average GSU instructions retired per 1000 core clocks (~2.4 clocks each,
 * averaged over cache hits, ROM-fetch stalls and plot). fx_run meters
 * instructions rather than clocks, so the budget has to be expressed in
 * instructions; 0.417 is the factor upstream snes9x uses for the same reason. */
#define GSU_INSN_PER_1000_CLK 417u

/* SUPERFX_SPEED_PERCENT scales the budget: 100 = real GSU-1 throughput,
 * 0 = no budget at all (run to STOP inside one scanline, which is what this
 * port did before the budget existed). Set from CMakeLists.txt. */
#ifndef SUPERFX_SPEED_PERCENT
#define SUPERFX_SPEED_PERCENT 100
#endif

void S9xResetSuperFX(void)
{
   /* Computed here rather than in S9xInitSuperFX because that runs from
    * SuperFXROMMap, before Settings.PAL and Memory.ROMFramesPerSecond are
    * determined — it would pin every PAL cart to NTSC timing. */
   uint32_t vmax = Settings.PAL ? SNES_MAX_PAL_VCOUNTER : SNES_MAX_NTSC_VCOUNTER;
   uint32_t fps  = Memory.ROMFramesPerSecond ? Memory.ROMFramesPerSecond
                                             : (Settings.PAL ? 50 : 60);

   SuperFX.speedPerLine =
      (uint32_t)(((uint64_t) GSU_CLOCK_HZ * GSU_INSN_PER_1000_CLK) / 1000u
                 / fps / vmax);
   SuperFX.speedPerLine = SuperFX.speedPerLine * SUPERFX_SPEED_PERCENT / 100u;

   FxReset(&SuperFX);
}

void S9xResetCPU()
{
   ICPU.Registers.PB = 0;
   ICPU.Registers.PC = S9xGetWord(0xfffc);
   ICPU.Registers.D.W = 0;
   ICPU.Registers.DB = 0;
   ICPU.Registers.SH = 1;
   ICPU.Registers.SL = 0xff;
   ICPU.Registers.XH = 0;
   ICPU.Registers.YH = 0;
   ICPU.Registers.P.W = 0;

   ICPU.ShiftedPB = 0;
   ICPU.ShiftedDB = 0;
   SetFlags(MemoryFlag | IndexFlag | IRQ | Emulation);
   ClearFlags(Decimal);

   CPU.Flags = CPU.Flags & (DEBUG_MODE_FLAG | TRACE_FLAG);
   CPU.BranchSkip = false;
   CPU.NMIActive = false;
   CPU.IRQActive = false;
   CPU.WaitingForInterrupt = false;
   CPU.InDMA = false;
   CPU.WhichEvent = HBLANK_START_EVENT;
   CPU.PC = NULL;
   CPU.PCBase = NULL;
   CPU.PCAtOpcodeStart = NULL;
   CPU.WaitAddress = NULL;
   CPU.WaitCounter = 1;
   CPU.Cycles = 188; /* This is the cycle count just after the jump to the Reset Vector. */
   CPU.NextEvent = Settings.HBlankStart;
   CPU.V_Counter = 0;
   CPU.MemSpeed = SLOW_ONE_CYCLE;
   CPU.MemSpeedx2 = SLOW_ONE_CYCLE * 2;
   CPU.SRAMModified = false;
   CPU.NMICycleCount = 0;
   CPU.IRQCycleCount = 0;
   S9xSetPCBase(ICPU.Registers.PC);

   ICPU.S9xOpcodes = S9xOpcodesE1;
   ICPU.CPUExecuting = true;

   S9xUnpackStatus();
}

static void CommonS9xReset()
{
   memset(Memory.FillRAM, 0, FILLRAM_SIZE);
   memset(Memory.VRAM, 0x00, VRAM_SIZE);

   S9xResetCPU();
   S9xResetSRTC();

   S9xResetDMA();
   S9xResetAPU();
   if (Settings.DSP)
      S9xResetDSP();
   /* After the FillRAM memset above: FxReset writes the GSU register space
    * into FillRAM[0x3000]; S9xResetPPU then preserves 0x3000-0x32ff. */
   if (Settings.SuperFX)
      S9xResetSuperFX();
   if (Settings.OBC1)
      ResetOBC1();
   if (Settings.SA1)
      S9xSA1Init();
   else
      /* Quiesce a stale SA-1 left running by a previously loaded SA-1 cart.
       * SA1.Executing (not Settings.SA1) is the S9xMainLoop gate (cpuexec.c),
       * and only S9xSA1Init resets it — which is skipped for non-SA-1 carts.
       * Without this, switching from e.g. Super Mario RPG to a plain LoROM
       * runs the SA-1 CPU against a torn-down SA1.Map and bus-faults. */
      SA1.Executing = false;
   if (Settings.C4)
      S9xInitC4();
}

void S9xReset()
{
   CommonS9xReset();
   S9xResetPPU();
   memset(Memory.RAM, 0x55, RAM_SIZE);
}

void S9xSoftReset()
{
   CommonS9xReset();
   S9xSoftResetPPU();
}
