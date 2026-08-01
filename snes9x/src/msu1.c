/* MSU-1 audio/data expansion — core-side state machine. See msu1.h. */

#include "msu1.h"

#if ENABLE_MSU1

#include <stdio.h>
#include <string.h>

#include "port_alloc.h"

uint8_t g_msu1_active = 0;

/* Both cores touch the ring, so every publish/observe pair needs a fence.
 * __atomic_thread_fence is a GCC builtin available on both targets: it emits
 * DMB on the Cortex-M33 and compiles to a pure compiler barrier on x86. */
#define MSU1_BARRIER() __atomic_thread_fence(__ATOMIC_SEQ_CST)

#define MSU1_RING_MASK (MSU1_RING_BYTES - 1u)
#define MSU1_DATA_WIN_MASK (MSU1_DATA_WIN_BYTES - 1u)

/* Below this, a refill is not worth the SD round trip; we top up next frame. */
#define MSU1_MIN_READ_BYTES 512u

/* Guards a malformed loop point that lands at or past EOF from spinning the
 * refill loop forever. */
#define MSU1_MAX_LOOPS_PER_PUMP 2

/* Status register bits ($2000 read). */
#define MSU1_ST_DATA_BUSY     0x80
#define MSU1_ST_AUDIO_BUSY    0x40
#define MSU1_ST_AUDIO_REPEAT  0x20
#define MSU1_ST_AUDIO_PLAYING 0x10
#define MSU1_ST_TRACK_MISSING 0x08

typedef struct
{
   /* --- register file ------------------------------------------------- */
   uint32_t seek_latch;      /* $2000-$2003 write assembly, LE */
   uint16_t track_latch;     /* $2004-$2005 write assembly, LE */
   uint8_t  volume;          /* $2006 */

   /* --- audio state ---------------------------------------------------- */
   /* audio_busy is set by the CPU (core0) on a track change and cleared by
    * msu1_pump once the ring is prefilled. While it is set core1 does not
    * touch the ring at all — neither rd nor the data — which is what lets
    * core0 rewind wr and refill without a handshake. */
   volatile uint8_t audio_busy;
   volatile uint8_t audio_playing;
   volatile uint8_t audio_repeat;
   uint8_t  track_missing;
   uint8_t  track_open;
   uint8_t  want_open;       /* a track change is pending the next pump */
   uint8_t  eof;             /* file exhausted; drain the ring, then stop */
   uint16_t want_track;
   uint16_t track_cur;
   int32_t  missing_reported;  /* last track we complained about, -1 = none */
   uint32_t loop_byte;       /* byte offset of the loop point in the .pcm */

   /* --- PCM ring, PSRAM ------------------------------------------------ */
   /* Free-running byte counters, masked on use. core0 owns wr, core1 owns
    * rd. Both advance in multiples of 4, so any masked offset is 4-byte
    * aligned and the mixer can read the ring as int16 pairs. */
   uint8_t *ring;
   volatile uint32_t wr;
   volatile uint32_t rd;

   /* --- data track sliding window, PSRAM -------------------------------- */
   uint8_t *dwin;
   uint32_t dwin_base;       /* file offset of dwin[0] */
   uint32_t dwin_len;        /* valid bytes in dwin */
   uint32_t data_size;
   uint32_t data_pos;        /* $2001 read pointer */

   /* --- data-track DMA staging, PSRAM ----------------------------------- */
   /* Retained across transfers rather than allocated per DMA: an FMV that
    * DMAs the data port every frame would otherwise churn the same lwmem
    * arena the ROM lives in. Grow-only; released in msu1_deinit. */
   uint8_t *dma_buf;
   uint32_t dma_cap;

   uint8_t  parked;

   /* --- instrumentation ------------------------------------------------- */
   uint32_t last_pump_us;
   uint32_t st_reads;
   uint32_t st_bytes;
   uint32_t st_us_total;
   uint32_t st_us_max;
   uint32_t st_open_us_max;
   uint32_t st_data_reads;
   uint32_t st_data_bytes;
   volatile uint32_t st_underruns;
} msu1_state_t;

static msu1_state_t msu;

/* ------------------------------------------------------------------------ */
/* lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

static void msu1_reset_state(void)
{
   /* Buffer ownership survives a state reset — only msu1_deinit frees. */
   uint8_t *ring = msu.ring;
   uint8_t *dwin = msu.dwin;
   uint8_t *dma  = msu.dma_buf;
   uint32_t cap  = msu.dma_cap;
   memset(&msu, 0, sizeof(msu));
   msu.ring      = ring;
   msu.dwin      = dwin;
   msu.dma_buf   = dma;
   msu.dma_cap   = cap;
   msu.dwin_base = 0xffffffffu;   /* no window loaded */
   msu.missing_reported = -1;
}

bool msu1_init(void)
{
   msu1_deinit();

   if (!msu1_backend_available())
      return false;

   msu1_reset_state();

   /* PSRAM only. port_alloc_psram returns NULL rather than panicking
    * (PICO_MALLOC_PANIC=0), so a pack we cannot afford degrades to "no
    * MSU-1" instead of taking the board down. */
   msu.ring = (uint8_t *)port_alloc_psram(MSU1_RING_BYTES);
   msu.dwin = (uint8_t *)port_alloc_psram(MSU1_DATA_WIN_BYTES);
   if (!msu.ring || !msu.dwin)
   {
      printf("MSU1: out of PSRAM for %u B ring + %u B window - disabled\n",
             (unsigned)MSU1_RING_BYTES, (unsigned)MSU1_DATA_WIN_BYTES);
      port_alloc_free(msu.ring);
      port_alloc_free(msu.dwin);
      msu.ring = NULL;
      msu.dwin = NULL;
      msu1_backend_close();
      return false;
   }

   msu.data_size = msu1_backend_open_data();

   MSU1_BARRIER();
   g_msu1_active = 1;
   printf("MSU1: active (data track %u B, ring %u B, window %u B, all PSRAM)\n",
          (unsigned)msu.data_size, (unsigned)MSU1_RING_BYTES,
          (unsigned)MSU1_DATA_WIN_BYTES);
   return true;
}

void msu1_deinit(void)
{
   g_msu1_active = 0;
   MSU1_BARRIER();
   msu1_backend_close();
   port_alloc_free(msu.ring);
   port_alloc_free(msu.dwin);
   port_alloc_free(msu.dma_buf);
   msu.ring    = NULL;
   msu.dwin    = NULL;
   msu.dma_buf = NULL;
   msu.dma_cap = 0;
   msu1_reset_state();
}

void msu1_reset(void)
{
   if (!g_msu1_active)
      return;

   msu.audio_playing = 0;
   msu.audio_busy    = 0;
   msu.audio_repeat  = 0;
   msu.track_missing = 0;
   msu.track_open    = 0;
   msu.want_open     = 0;
   msu.eof           = 0;
   msu.seek_latch    = 0;
   msu.track_latch   = 0;
   msu.volume        = 0;
   msu.data_pos      = 0;

   /* Safe to drop the buffered audio without the AUDIO_BUSY handshake:
    * audio_playing is already clear, so core1 is not consuming. Anything it
    * had in flight is caught by the fill<0 self-heal in msu1_mix. */
   MSU1_BARRIER();
   msu.wr = msu.rd;
}

void msu1_park(void)
{
   msu.parked = 1;
   MSU1_BARRIER();
}

void msu1_resume(void)
{
   /* Re-anchor the budget clock: the menu may have held us for minutes, and
    * the first pump back should ask for one frame's worth, not for all of
    * it. The ring kept its contents while parked. */
   msu.last_pump_us = msu1_backend_now_us();
   MSU1_BARRIER();
   msu.parked = 0;
}

/* ------------------------------------------------------------------------ */
/* data track                                                                */
/* ------------------------------------------------------------------------ */

/* Makes the sliding window cover `pos`, reloading it if needed. Returns the
 * number of valid bytes available at `pos`, or 0 at EOF / on a read error.
 * This is the only path that can touch SD from inside the CPU interpreter;
 * it costs one blocking read per MSU1_DATA_WIN_BYTES consumed. Most
 * music-replacement packs ship an empty .msu and never come here at all. */
static uint32_t msu1_data_window(uint32_t pos, uint32_t *idx_out)
{
   uint32_t idx;

   if (pos >= msu.data_size)
      return 0;

   if (pos < msu.dwin_base || pos >= msu.dwin_base + msu.dwin_len)
   {
      uint32_t base = pos & ~MSU1_DATA_WIN_MASK;
      msu.dwin_len  = msu1_backend_read_data(msu.dwin, base, MSU1_DATA_WIN_BYTES);
      msu.dwin_base = base;
      msu.st_data_reads++;
      msu.st_data_bytes += msu.dwin_len;
      if (msu.dwin_len == 0)
         return 0;
   }

   idx = pos - msu.dwin_base;
   if (idx >= msu.dwin_len)
      return 0;

   *idx_out = idx;
   return msu.dwin_len - idx;
}

static uint8_t msu1_data_byte(void)
{
   uint32_t pos = msu.data_pos;
   uint32_t idx = 0;

   msu.data_pos = pos + 1;
   return msu1_data_window(pos, &idx) ? msu.dwin[idx] : 0u;
}

void msu1_data_read_block(uint8_t *dst, uint32_t count)
{
   /* Copied a window-run at a time rather than byte by byte: the MSU-1 FMV
    * packs pull tens of KB per frame through here, and per-byte bounds
    * checks on that are pure overhead inside the DMA path. */
   while (count)
   {
      uint32_t idx = 0;
      uint32_t avail = msu1_data_window(msu.data_pos, &idx);
      uint32_t n;

      if (avail == 0)
      {
         /* Past EOF or unreadable: the port reads as zero, and the pointer
          * still advances exactly as `count` single reads would. */
         memset(dst, 0, count);
         msu.data_pos += count;
         return;
      }

      n = avail < count ? avail : count;
      memcpy(dst, msu.dwin + idx, n);
      dst          += n;
      count        -= n;
      msu.data_pos += n;
   }
}

uint8_t *msu1_dma_stage(uint8_t abank, uint16_t aaddress, uint32_t count,
                        bool in_sa1_dma)
{
   uint8_t *stage;

   /* MSU-1 is only visible at $2000-$2007 of banks $00-$3F / $80-$BF. */
   if (in_sa1_dma || count == 0 || (abank & 0x40) != 0
       || aaddress < 0x2000 || aaddress > 0x2007)
      return NULL;

   if (msu.dma_cap < count)
   {
      port_alloc_free(msu.dma_buf);
      msu.dma_buf = (uint8_t *)port_alloc_psram(count);
      msu.dma_cap = msu.dma_buf ? count : 0u;
   }
   stage = msu.dma_buf;
   if (!stage)
      return NULL;

   if ((aaddress & 7) == 1)
      msu1_data_read_block(stage, count);
   else
   {
      uint32_t i;
      for (i = 0; i < count; i++)
         stage[i] = msu1_read_port(aaddress);
   }
   return stage;
}

/* ------------------------------------------------------------------------ */
/* register file                                                             */
/* ------------------------------------------------------------------------ */

uint8_t msu1_read_port(uint16_t address)
{
   switch (address & 7)
   {
   case 0:
   {
      uint8_t status = MSU1_REVISION & 7;
      if (msu.audio_busy)    status |= MSU1_ST_AUDIO_BUSY;
      if (msu.audio_repeat)  status |= MSU1_ST_AUDIO_REPEAT;
      if (msu.audio_playing) status |= MSU1_ST_AUDIO_PLAYING;
      if (msu.track_missing) status |= MSU1_ST_TRACK_MISSING;
      /* DATA_BUSY is never set: the window reader is synchronous, so the
       * data port is always ready by the time the CPU can observe it. */
      return status;
   }
   case 1: return msu1_data_byte();
   case 2: return 'S';
   case 3: return '-';
   case 4: return 'M';
   case 5: return 'S';
   case 6: return 'U';
   default: return '1';
   }
}

void msu1_write_port(uint16_t address, uint8_t byte)
{
   switch (address & 7)
   {
   case 0:
      msu.seek_latch = (msu.seek_latch & 0xffffff00u) | ((uint32_t)byte);
      break;
   case 1:
      msu.seek_latch = (msu.seek_latch & 0xffff00ffu) | ((uint32_t)byte << 8);
      break;
   case 2:
      msu.seek_latch = (msu.seek_latch & 0xff00ffffu) | ((uint32_t)byte << 16);
      break;
   case 3:
      /* The MSB write commits the seek. */
      msu.seek_latch = (msu.seek_latch & 0x00ffffffu) | ((uint32_t)byte << 24);
      msu.data_pos   = msu.seek_latch;
      break;

   case 4:
      msu.track_latch = (uint16_t)((msu.track_latch & 0xff00u) | byte);
      break;
   case 5:
      /* The MSB write selects the track. Per spec this stops playback and
       * clears repeat; the actual f_open is deferred to msu1_pump so that a
       * directory walk over a 30-60 file pack never happens inside the CPU
       * interpreter. AUDIO_BUSY covers the gap, which is exactly what the
       * bit is for on real hardware. */
      msu.track_latch    = (uint16_t)((msu.track_latch & 0x00ffu) | ((uint16_t)byte << 8));
      msu.audio_playing  = 0;
      msu.audio_repeat   = 0;
      msu.track_missing  = 0;
      msu.want_track     = msu.track_latch;
      msu.want_open      = 1;
      MSU1_BARRIER();
      msu.audio_busy     = 1;
      break;

   case 6:
      msu.volume = byte;
      break;

   default:
      /* $2007 control. Ignored while the track is still loading — real
       * hardware behaves the same way, so every published MSU-1 patch polls
       * AUDIO_BUSY before writing here. */
      if (msu.audio_busy || msu.track_missing || !msu.track_open)
         break;
      msu.audio_repeat  = (byte & 2) ? 1 : 0;
      msu.audio_playing = (byte & 1) ? 1 : 0;
      break;
   }
}

/* ------------------------------------------------------------------------ */
/* streaming (core0)                                                         */
/* ------------------------------------------------------------------------ */

static void msu1_note_read(uint32_t t0, uint32_t bytes)
{
   uint32_t us = msu1_backend_now_us() - t0;
   msu.st_reads++;
   msu.st_bytes += bytes;
   msu.st_us_total += us;
   if (us > msu.st_us_max)
      msu.st_us_max = us;
}

/* Bytes to read this pump, derived from how much wall-clock time actually
 * elapsed since the previous one.
 *
 * msu1_pump runs once per *emulated video frame*, but PCM is consumed in
 * *real time*. Any fixed per-call budget is therefore only correct at one
 * frame rate: 4096 B is comfortable at 60 fps (2949 B needed) and starves at
 * 40 fps (4410 B needed), which is what Zelda's intro FMV runs at — it
 * streams video through the data port while the music plays. Measuring the
 * interval instead makes the refill rate independent of the frame rate, and
 * a full ring is what caps it in steady state. */
static uint32_t msu1_pump_budget(void)
{
   uint32_t now = msu1_backend_now_us();
   uint32_t dt  = now - msu.last_pump_us;
   uint32_t need;

   msu.last_pump_us = now;

   if (dt > 1000000u)
      dt = 1000000u;   /* first pump, or just back from the menu */

   /* bytes/ms, scaled by the catch-up factor; kept in ms to stay in 32 bits */
   need = (dt * ((MSU1_BYTES_PER_SEC / 1000u) * MSU1_CATCHUP_PERCENT / 100u)) / 1000u;

   if (need < MSU1_MIN_READ_BYTES) need = MSU1_MIN_READ_BYTES;
   if (need > MSU1_PUMP_MAX_BYTES) need = MSU1_PUMP_MAX_BYTES;
   return need;
}

static uint32_t msu1_ring_fill(void)
{
   int32_t fill = (int32_t)(msu.wr - msu.rd);
   return fill > 0 ? (uint32_t)fill : 0u;
}

/* Reads at most `budget` bytes of PCM into the ring. Returns bytes added. */
static uint32_t msu1_refill(uint32_t budget)
{
   uint32_t added = 0;
   int loops = 0;

   while (budget >= MSU1_MIN_READ_BYTES && !msu.eof)
   {
      /* Leave one frame free so wr never lands exactly on rd, which would
       * be indistinguishable from "empty". */
      uint32_t space = MSU1_RING_BYTES - msu1_ring_fill();
      uint32_t want, off, seg, got, t0;
      bool at_eof;

      if (space < 4u + MSU1_MIN_READ_BYTES)
         break;
      space -= 4u;

      want = budget < space ? budget : space;
      /* Request whole stereo frames. The budget is time-derived and so is
       * rarely a multiple of 4; without this the alignment mask below would
       * shave the returned count under the requested one and every full
       * read would be misread as end-of-file. */
      want &= ~3u;
      if (want < MSU1_MIN_READ_BYTES)
         break;

      off = msu.wr & MSU1_RING_MASK;
      seg = MSU1_RING_BYTES - off;
      if (seg > want)
         seg = want;

      t0  = msu1_backend_now_us();
      got = msu1_backend_read_track(msu.ring + off, seg);
      msu1_note_read(t0, got);

      /* Decide EOF from the raw count, before any masking. */
      at_eof = (got < seg);

      got &= ~3u;   /* keep the ring 4-byte aligned; drops <=3 B at EOF */
      if (got)
      {
         MSU1_BARRIER();
         msu.wr += got;
         added  += got;
         budget -= got;
      }

      if (at_eof)
      {
         /* End of file. */
         if (!msu.audio_repeat || ++loops > MSU1_MAX_LOOPS_PER_PUMP)
         {
            msu.eof = 1;
            break;
         }
         if (!msu1_backend_seek_track(msu.loop_byte))
         {
            msu.eof = 1;
            break;
         }
      }
   }

   return added;
}

static void msu1_open_pending_track(void)
{
   uint8_t hdr[MSU1_PCM_HEADER_BYTES];
   uint32_t loop_frames;
   uint32_t t0;

   msu.want_open   = 0;
   msu.track_open  = 0;
   msu.eof         = 0;

   /* Drop whatever the previous track left buffered. Safe because
    * audio_busy is already set (msu1_write_port did that on the $2005
    * write), so core1 is out of the ring; a chunk that was already in
    * flight when busy went up may leave rd ahead of wr, and the mixer's own
    * fill<0 branch snaps it back. */
   MSU1_BARRIER();
   msu.wr = msu.rd;

   t0 = msu1_backend_now_us();

   if (!msu1_backend_open_track(msu.want_track))
   {
      msu.track_missing = 1;
      MSU1_BARRIER();
      msu.audio_busy = 0;
      /* Reported once per distinct track: selecting a track with no file is
       * a normal idiom for "stop the music" (Zelda's patch writes track 0),
       * so complaining every time would just be noise. */
      if (msu.missing_reported != (int32_t)msu.want_track)
      {
         msu.missing_reported = (int32_t)msu.want_track;
         printf("MSU1: track %u missing\n", (unsigned)msu.want_track);
      }
      return;
   }

   if (msu1_backend_read_track(hdr, MSU1_PCM_HEADER_BYTES) != MSU1_PCM_HEADER_BYTES
       || memcmp(hdr, "MSU1", 4) != 0)
   {
      msu.track_missing = 1;
      MSU1_BARRIER();
      msu.audio_busy = 0;
      printf("MSU1: track %u is not a valid .pcm\n", (unsigned)msu.want_track);
      return;
   }

   loop_frames = (uint32_t)hdr[4]
               | ((uint32_t)hdr[5] << 8)
               | ((uint32_t)hdr[6] << 16)
               | ((uint32_t)hdr[7] << 24);

   msu.loop_byte = MSU1_PCM_HEADER_BYTES + loop_frames * 4u;
   msu.track_cur = msu.want_track;
   msu.track_open = 1;

   {
      uint32_t us = msu1_backend_now_us() - t0;
      if (us > msu.st_open_us_max)
         msu.st_open_us_max = us;
   }
   /* audio_busy stays asserted until the prefill target is met. */
}

void msu1_pump(void)
{
   uint32_t budget;

   if (!g_msu1_active || msu.parked)
      return;

   if (msu.want_open)
      msu1_open_pending_track();

   if (!msu.track_open)
      return;

   if (msu.eof)
   {
      if (msu.audio_repeat)
      {
         /* Repeat can legitimately be switched on *after* we hit EOF: a
          * track shorter than the prefill target is read to completion
          * while AUDIO_BUSY is still asserted, i.e. before the game gets
          * to write $2007. Re-arm from the loop point rather than leaving
          * a short looping cue playing exactly once. */
         if (msu1_backend_seek_track(msu.loop_byte))
            msu.eof = 0;
      }
      else if (msu.audio_playing && msu1_ring_fill() == 0)
      {
         /* Clear AUDIO_PLAYING only once the tail has actually been
          * consumed — clearing it at file EOF would cut off up to a ring's
          * worth of audio. Then rewind, so a later play restarts the track
          * the way it does on hardware. */
         msu.audio_playing = 0;
         if (msu1_backend_seek_track(MSU1_PCM_HEADER_BYTES))
            msu.eof = 0;
      }
   }

   budget = msu1_pump_budget();
   if (msu.audio_busy && budget < MSU1_PREFILL_MAX_BYTES)
      budget = MSU1_PREFILL_MAX_BYTES;   /* the game is only spinning on the
                                          * status bit — nothing to protect */
   msu1_refill(budget);

   /* Release the game from its status poll once there is enough audio
    * buffered to survive the first few frames, or immediately if the whole
    * track is shorter than the prefill target. */
   if (msu.audio_busy && (msu1_ring_fill() >= MSU1_PREFILL_BYTES || msu.eof))
   {
      MSU1_BARRIER();
      msu.audio_busy = 0;
   }
}

/* ------------------------------------------------------------------------ */
/* mixing (core1)                                                            */
/* ------------------------------------------------------------------------ */

static void msu1_mix_seg(int16_t *dst, const int16_t *src, uint32_t frames, int vol)
{
   uint32_t i;
   for (i = 0; i < frames; i++)
   {
      int l = dst[0] + ((src[0] * vol) >> 8);
      int r = dst[1] + ((src[1] * vol) >> 8);
      if (l >  32767) l =  32767; else if (l < -32768) l = -32768;
      if (r >  32767) r =  32767; else if (r < -32768) r = -32768;
      dst[0] = (int16_t)l;
      dst[1] = (int16_t)r;
      dst += 2;
      src += 2;
   }
}

void msu1_mix(int16_t *buf, int frames)
{
   uint32_t nbytes, rd, off, seg;
   int32_t avail;
   int vol;

   if (!g_msu1_active || frames <= 0)
      return;

   if (msu.audio_busy)
      return;   /* track change in flight: core0 owns the ring, hands off */

   if (!msu.audio_playing)
      return;   /* paused: keep the buffer, do not drain it */

   avail = (int32_t)(msu.wr - msu.rd);
   if (avail < 0)
   {
      /* core0 rewound wr behind us while we were mid-chunk on a track
       * change; resynchronise. This is the only place rd is repaired — the
       * AUDIO_BUSY branch above must NOT also glue rd to wr, or it would
       * eat the prefill it is waiting for and AUDIO_BUSY would never
       * clear (the game then spins on the status bit forever). */
      msu.rd = msu.wr;
      return;
   }
   if (avail == 0)
   {
      msu.st_underruns++;
      return;
   }

   nbytes = (uint32_t)frames * 4u;
   if ((uint32_t)avail < nbytes)
   {
      nbytes = (uint32_t)avail & ~3u;
      msu.st_underruns++;
      if (nbytes == 0)
         return;
   }

   vol = msu.volume;
   rd  = msu.rd;
   off = rd & MSU1_RING_MASK;
   seg = MSU1_RING_BYTES - off;
   if (seg > nbytes)
      seg = nbytes;

   msu1_mix_seg(buf, (const int16_t *)(const void *)(msu.ring + off), seg >> 2, vol);
   if (seg < nbytes)
      msu1_mix_seg(buf + (seg >> 1),
                   (const int16_t *)(const void *)msu.ring,
                   (nbytes - seg) >> 2, vol);

   MSU1_BARRIER();
   msu.rd = rd + nbytes;
}

/* ------------------------------------------------------------------------ */
/* instrumentation                                                           */
/* ------------------------------------------------------------------------ */

void msu1_stats_report(void)
{
   if (!g_msu1_active)
      return;

#if !MSU1_VERBOSE
   /* Quiet unless something is actually wrong — same rule the audio-health
    * line in run_emulator() follows. Build with -DMSU1_VERBOSE=1 (cmake:
    * -DMSU1_VERBOSE=ON) for a line every second while a track plays, which
    * is what you want when characterising a new SD card. */
   if (msu.st_underruns == 0
       && msu.st_us_max < MSU1_SLOW_READ_US
       && (!msu.audio_playing || msu1_ring_fill() > MSU1_RING_BYTES / 4u))
   {
      msu.st_reads = 0;
      msu.st_bytes = 0;
      msu.st_us_total = 0;
      msu.st_us_max = 0;
      msu.st_open_us_max = 0;
      msu.st_data_reads = 0;
      msu.st_data_bytes = 0;
      return;
   }
#endif

   printf("MSU1: trk %u %s vol %u | SD %u rd %u KB avg %u us max %u us"
          " | open max %u us | data %u rd %u KB | ring %u%% urun %u\n",
          (unsigned)msu.track_cur,
          msu.audio_playing ? "play" : (msu.audio_busy ? "busy" : "stop"),
          (unsigned)msu.volume,
          (unsigned)msu.st_reads,
          (unsigned)(msu.st_bytes >> 10),
          (unsigned)(msu.st_reads ? msu.st_us_total / msu.st_reads : 0),
          (unsigned)msu.st_us_max,
          (unsigned)msu.st_open_us_max,
          (unsigned)msu.st_data_reads,
          (unsigned)(msu.st_data_bytes >> 10),
          (unsigned)(msu1_ring_fill() * 100u / MSU1_RING_BYTES),
          (unsigned)msu.st_underruns);

   msu.st_reads = 0;
   msu.st_bytes = 0;
   msu.st_us_total = 0;
   msu.st_us_max = 0;
   msu.st_open_us_max = 0;
   msu.st_data_reads = 0;
   msu.st_data_bytes = 0;
   msu.st_underruns = 0;
}

#else  /* !ENABLE_MSU1 */

typedef int msu1_translation_unit_not_empty;

#endif
