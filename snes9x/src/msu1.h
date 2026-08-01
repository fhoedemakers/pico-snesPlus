/* MSU-1 audio/data expansion for the pico-snesPlus port.
 *
 * MSU-1 is a homebrew SNES coprocessor: eight registers at $2000-$2007, a
 * byte-addressed "data track" (romname.msu) and up to 65535 PCM audio tracks
 * (romname-<n>.pcm). The PCM format is 44100 Hz signed 16-bit stereo LE,
 * which is exactly what this port's audio chain already runs end to end
 * (SNES_AUDIO_HZ in main.cpp), so no resampling is involved.
 *
 * Three actors, deliberately separated so SD I/O never lands on core1 and
 * never lands inside the CPU interpreter's hot path:
 *
 *   core0, once per frame     core0, inside S9xMainLoop    core1, mixer
 *   ---------------------     -------------------------    ------------
 *   msu1_pump()               msu1_read_port()             msu1_mix()
 *     deferred f_open           register state only          reads PCM ring
 *     f_read -> PCM ring        (SD only for the rare        sums + clamps
 *                                data-track window miss)
 *
 * All buffers live in PSRAM and are allocated only when a pack is actually
 * found next to the ROM; SRAM holds nothing but the small state struct.
 *
 * This file is core-side and portable: everything that touches a filesystem
 * goes through the msu1_backend_* hooks, implemented by msu1_port.cpp on the
 * device and by tools/host-harness/harness.c on the desktop. */

#ifndef PICO_SNESPLUS_MSU1_H
#define PICO_SNESPLUS_MSU1_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PCM ring, PSRAM. Must be a power of two. 64 KB == 371 ms of audio, which
 * is deep enough to absorb a loop-point seek (FF_USE_FASTSEEK is off, so a
 * backward seek re-walks the cluster chain) without an audible gap. */
#ifndef MSU1_RING_BYTES
#define MSU1_RING_BYTES 65536u
#endif

/* Data-track sliding window, PSRAM. Must be a power of two. */
#ifndef MSU1_DATA_WIN_BYTES
#define MSU1_DATA_WIN_BYTES 4096u
#endif

/* Bytes of PCM consumed per second of wall-clock time. */
#define MSU1_BYTES_PER_SEC (44100u * 4u)

/* Hard ceiling on one pump's refill. The budget itself is derived from
 * elapsed wall time (see msu1_pump_budget), not from a fixed per-call
 * constant: msu1_pump runs once per *emulated video frame* while audio is
 * consumed in *real time*, so a game that drops to 40 fps still needs
 * 176.4 KB/s — 4410 B per pump rather than the 2949 B a 60 fps frame needs.
 * A fixed 4 KB cap starved exactly that case (Zelda's intro FMV, which also
 * streams video through the data port) and underran continuously.
 *
 * This ceiling only binds below ~21 fps; it exists to bound the worst
 * single blocking f_read, which at a measured 1.3 MB/s is ~9 ms. */
#ifndef MSU1_PUMP_MAX_BYTES
#define MSU1_PUMP_MAX_BYTES 12288u
#endif

/* How hard to claw back a deficit, in percent of what was consumed since
 * the previous pump. 150 = read 1.5x the consumption rate until the ring is
 * full again; the ring being full is what caps it in steady state. */
#ifndef MSU1_CATCHUP_PERCENT
#define MSU1_CATCHUP_PERCENT 150u
#endif

/* Larger ceiling while AUDIO_BUSY is asserted — the game is spinning on the
 * status bit, so there is no frame work to protect. */
#ifndef MSU1_PREFILL_MAX_BYTES
#define MSU1_PREFILL_MAX_BYTES 8192u
#endif

/* Ring fill required before AUDIO_BUSY is released after a track change. */
#ifndef MSU1_PREFILL_BYTES
#define MSU1_PREFILL_BYTES 16384u
#endif

/* Reported in status bits 2-0. Revision 1 is what SD2SNES reports and what
 * every published MSU-1 patch is written against. */
#ifndef MSU1_REVISION
#define MSU1_REVISION 1
#endif

/* Header of a .pcm track: "MSU1" + uint32 LE loop point, in stereo frames. */
#define MSU1_PCM_HEADER_BYTES 8u

/* Non-zero once a pack has been detected and the buffers allocated. Tested
 * from the ppu.c / dma.c hot paths, so it is a plain byte, not a function. */
extern uint8_t g_msu1_active;

/* ---- lifecycle (core0) ------------------------------------------------- */

/* Detect a pack for the loaded ROM and allocate. Safe to call when no pack
 * exists (leaves g_msu1_active at 0 and allocates nothing) and safe to call
 * when allocation fails. Returns true only if MSU-1 is now active. */
bool msu1_init(void);
void msu1_deinit(void);

/* Stop playback and clear the register file, keeping the pack open — for an
 * in-game reset, which puts the cart back at its reset vector. */
void msu1_reset(void);

/* Quiesce before the in-game menu takes the SD card, and restart after.
 *
 * ORDERING REQUIREMENT: msu1_park() must be called *after* the core1 mixer
 * has been parked and acked (mix_c1_park() in main.cpp), so that core1 is
 * provably outside msu1_mix() when core0 next touches the ring. Resume in
 * the mirror order: msu1_resume() before mix_c1_resume(). */
void msu1_park(void);
void msu1_resume(void);

/* ---- register file ($2000-$2007, core0 inside S9xMainLoop) ------------- */

uint8_t msu1_read_port(uint16_t address);
void    msu1_write_port(uint16_t address, uint8_t byte);

/* Bulk drain of the data port for the fixed-address $2001 DMA path in
 * dma.c. Advances the data-track pointer exactly as `count` single reads
 * of $2001 would. */
void msu1_data_read_block(uint8_t *dst, uint32_t count);

/* Staging buffer for that DMA. Returns NULL — meaning "not an MSU-1
 * transfer, carry on as before" — unless the A-bus address really is
 * $2000-$2007 in banks $00-$3F/$80-$BF; otherwise allocates `count` bytes of
 * PSRAM and fills it by draining the port. NULL is also the out-of-memory
 * answer, which degrades to the stock (wrong but harmless) FillRAM read.
 *
 * The address decode lives here rather than at the dma.c call site on
 * purpose: dma.c is one of the .time_critical files relocated into SRAM and
 * the SRAM heap runs ~1 KB free, so everything that can stay in flash does.
 * The caller only has to test g_msu1_active, which is free.
 *
 * The buffer is owned by msu1.c and retained between transfers (grow-only,
 * released in msu1_deinit) — the caller must NOT free it. An FMV pack DMAs
 * the data port every frame, and churning the lwmem arena the ROM lives in
 * at that rate is not worth it. */
uint8_t *msu1_dma_stage(uint8_t abank, uint16_t aaddress, uint32_t count,
                        bool in_sa1_dma);

/* ---- streaming (core0, once per frame) --------------------------------- */

/* Performs every SD access the audio path needs: the deferred track open
 * and the ring refill. Must be called from core0 only, and never while
 * parked. Cheap no-op when inactive, paused with a full ring, or parked. */
void msu1_pump(void);

/* ---- mixing (core1) ---------------------------------------------------- */

/* Sums `frames` stereo frames of MSU-1 audio into `buf` (interleaved L,R
 * s16), applying the $2006 volume and saturating. `buf` already holds the
 * SNES DSP mix, which S9xMixSamples has clipped to full scale, so the sum
 * genuinely can overflow and the clamp is not decorative. */
void msu1_mix(int16_t *buf, int frames);

/* ---- instrumentation --------------------------------------------------- */

/* A single blocking read costing more than this is worth complaining about
 * even if the ring absorbed it — roughly half an NTSC frame. */
#ifndef MSU1_SLOW_READ_US
#define MSU1_SLOW_READ_US 8000u
#endif

/* Print the SD/ring health line every second while a track plays, instead
 * of only when something looks wrong. For characterising a card. */
#ifndef MSU1_VERBOSE
#define MSU1_VERBOSE 0
#endif

/* One-line SD/ring health report, called from the once-per-second block in
 * run_emulator(). Silent unless the card is struggling (underruns, a read
 * slower than MSU1_SLOW_READ_US, or the ring below a quarter full while
 * playing) — or always, under MSU1_VERBOSE. `max` is the worst blocking
 * f_read in us, against a 16716 us frame budget. */
void msu1_stats_report(void);

/* ---- backend, supplied by the port ------------------------------------- */

/* Monotonic microseconds; only used for instrumentation. */
uint32_t msu1_backend_now_us(void);

/* True if <romname>.msu or <romname>-1.pcm exists next to the loaded ROM.
 * Called once, before anything is allocated. */
bool msu1_backend_available(void);

/* Open <romname>.msu and return its size in bytes; 0 if absent or empty.
 * A pack whose only marker is -1.pcm legitimately returns 0 here. */
uint32_t msu1_backend_open_data(void);

/* Random-access read from the data track. Returns bytes actually read. */
uint32_t msu1_backend_read_data(uint8_t *dst, uint32_t offset, uint32_t bytes);

/* Open <romname>-<track>.pcm, replacing any previously open track.
 * Returns false if the file does not exist (-> TRACK_MISSING). */
bool msu1_backend_open_track(uint16_t track);

/* Sequential read from the open track. Returns bytes actually read; a short
 * read means end of file. */
uint32_t msu1_backend_read_track(uint8_t *dst, uint32_t bytes);

/* Absolute seek within the open track. */
bool msu1_backend_seek_track(uint32_t offset);

/* Close both files and release any backend-owned memory. */
void msu1_backend_close(void);

#ifdef __cplusplus
}
#endif

#endif
