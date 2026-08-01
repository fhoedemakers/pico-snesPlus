/* MSU-1 backend for the device build: FatFs on the SD card.
 *
 * The core state machine (snes9x/src/msu1.c) never names a file or touches a
 * filesystem; everything platform-specific lives here. tools/host-harness
 * supplies the same entry points backed by stdio so the register semantics
 * and the mixer can be regression-tested on desktop.
 *
 * A pack is "<rom path with the extension stripped>" plus:
 *   <base>.msu        data track (often empty, or absent entirely)
 *   <base>-<n>.pcm    audio track n, "MSU1" + uint32 loop point + s16le stereo
 *
 * Stack discipline (PICO_STACK_SIZE is 3072 on core0): FIL is ~550 B, FILINFO
 * ~276 B and FF_MAX_LFN is 255, so none of them may live on the stack — the
 * known failure mode is not a core0 fault but a core1 garbage-PC lockup. All
 * of it is one PSRAM block instead. */

#include "msu1.h"

#if ENABLE_MSU1

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "ff.h"

#include "FrensHelpers.h"

extern "C" {
#include "port_alloc.h"
}

/* Full path of the ROM currently loaded (main.cpp). */
extern char *romName;

namespace
{

struct Msu1Files
{
    FIL  data;
    FIL  track;
    bool data_open;
    bool track_open;
    char base[FF_MAX_LFN];   /* ROM path, extension stripped */
    char path[FF_MAX_LFN];   /* scratch for the file being opened */
};

Msu1Files *g_files = nullptr;

/* Lazily allocates the PSRAM block and derives <base> from romName.
 * Returns nullptr if there is no ROM or PSRAM is exhausted. */
Msu1Files *files()
{
    if (g_files) return g_files;
    if (!romName || !romName[0]) return nullptr;

    auto *f = (Msu1Files *)port_alloc_psram(sizeof(Msu1Files));
    if (!f) {
        printf("MSU1: out of PSRAM for %u B of file state\n",
               (unsigned)sizeof(Msu1Files));
        return nullptr;
    }
    memset(f, 0, sizeof(*f));

    strncpy(f->base, romName, sizeof(f->base) - 1);
    Frens::stripextensionfromfilename(f->base);

    g_files = f;
    return f;
}

/* Builds "<base><suffix>" into the scratch buffer. */
const char *sidecar(Msu1Files *f, const char *suffix)
{
    snprintf(f->path, sizeof(f->path), "%s%s", f->base, suffix);
    return f->path;
}

/* f_stat with the FILINFO in PSRAM rather than on the stack (Frens::fileExists
 * puts it on the stack, which we deliberately avoid here). */
bool stat_size(const char *path, FSIZE_t *size_out)
{
    auto *fno = (FILINFO *)port_alloc_psram(sizeof(FILINFO));
    if (!fno) return false;
    bool ok = (f_stat(path, fno) == FR_OK);
    if (ok && size_out) *size_out = fno->fsize;
    port_alloc_free(fno);
    return ok;
}

} /* namespace */

/* ------------------------------------------------------------------------ */

uint32_t msu1_backend_now_us(void)
{
    return time_us_32();
}

bool msu1_backend_available(void)
{
    Msu1Files *f = files();
    if (!f) return false;

    /* A data track alone is enough (some packs stream everything through
     * $2001), and so is -1.pcm alone (most music packs ship an empty or
     * absent .msu). */
    if (stat_size(sidecar(f, ".msu"), nullptr)) return true;
    if (stat_size(sidecar(f, "-1.pcm"), nullptr)) return true;
    return false;
}

uint32_t msu1_backend_open_data(void)
{
    Msu1Files *f = files();
    if (!f) return 0;

    if (f->data_open) {
        f_close(&f->data);
        f->data_open = false;
    }

    const char *path = sidecar(f, ".msu");
    FSIZE_t size = 0;
    if (!stat_size(path, &size) || size == 0)
        return 0;

    if (f_open(&f->data, path, FA_READ) != FR_OK) {
        printf("MSU1: cannot open data track %s\n", path);
        return 0;
    }
    f->data_open = true;
    return (uint32_t)size;
}

uint32_t msu1_backend_read_data(uint8_t *dst, uint32_t offset, uint32_t bytes)
{
    Msu1Files *f = g_files;
    if (!f || !f->data_open) return 0;

    if (f_lseek(&f->data, (FSIZE_t)offset) != FR_OK) return 0;

    UINT br = 0;
    if (f_read(&f->data, dst, (UINT)bytes, &br) != FR_OK) return 0;
    return (uint32_t)br;
}

bool msu1_backend_open_track(uint16_t track)
{
    Msu1Files *f = files();
    if (!f) return false;

    if (f->track_open) {
        f_close(&f->track);
        f->track_open = false;
    }

    char suffix[16];
    snprintf(suffix, sizeof(suffix), "-%u.pcm", (unsigned)track);

    const char *path = sidecar(f, suffix);
    if (f_open(&f->track, path, FA_READ) != FR_OK)
        return false;

    f->track_open = true;
    return true;
}

uint32_t msu1_backend_read_track(uint8_t *dst, uint32_t bytes)
{
    Msu1Files *f = g_files;
    if (!f || !f->track_open) return 0;

    UINT br = 0;
    if (f_read(&f->track, dst, (UINT)bytes, &br) != FR_OK) return 0;
    return (uint32_t)br;
}

bool msu1_backend_seek_track(uint32_t offset)
{
    Msu1Files *f = g_files;
    if (!f || !f->track_open) return false;

    /* FF_USE_FASTSEEK is 0, so a backward seek re-walks the cluster chain.
     * For a 30 MB .pcm on FAT32 with 32 KB clusters that is only a handful
     * of FAT sector reads, and the PCM ring covers it. */
    if (f_lseek(&f->track, (FSIZE_t)offset) != FR_OK) return false;
    return f_tell(&f->track) == (FSIZE_t)offset;
}

void msu1_backend_close(void)
{
    Msu1Files *f = g_files;
    if (!f) return;

    if (f->data_open)  f_close(&f->data);
    if (f->track_open) f_close(&f->track);

    g_files = nullptr;
    port_alloc_free(f);
}

#endif /* ENABLE_MSU1 */
