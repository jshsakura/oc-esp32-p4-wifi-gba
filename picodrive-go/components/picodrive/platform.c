/*
 * The platform layer PicoDrive expects a front-end to supply.
 *
 * PicoDrive is written to be embedded: it calls out for logging, for a video
 * mode change, for MP3 decoding, and for the 32X's late init, and expects the
 * host to define them. None of these are optional at link time -- cart.c, cd/,
 * carthw/ and the shared draw path reference them unconditionally, without a
 * PAHW test -- so they are all here, and the ones that are genuinely not
 * supported say so rather than pretending.
 */
#include <rg_system.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "pico/pico_types.h"
#include "pico/pico.h"

/* PicoDrive's logging. Routed to retro-go's so a cart that fails to load says
 * why in the same place everything else does. */
void lprintf(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    RG_LOGI("pico: %s", buf);
}

/* Called when the emulated machine changes resolution or region -- H32 vs H40,
 * 224 vs 240 lines. Nothing to do here: the surface is allocated at the widest
 * case and the display path scales whatever arrives, so a mode change needs no
 * reallocation. It still has to exist, because pico.c calls it directly. */
void emu_video_mode_change(int start_line, int line_count, int start_col, int col_count)
{
    (void)start_line;
    (void)line_count;
    (void)start_col;
    (void)col_count;
}

/* 32X late init. The 32X sources are compiled in (they cannot be left out -- the
 * shared draw path reaches into them), but nothing here offers 32X yet, so this
 * is the point where that would be wired up. */
void emu_32x_startup(void)
{
}

/* MP3-encoded CD audio.
 *
 * Some Sega CD rips replace the redbook tracks with MP3s, and PicoDrive supports
 * that by asking the host to decode them. We do not: there is no MP3 decoder in
 * this firmware, and adding one is its own piece of work with its own licensing
 * question. A .cue with real audio tracks plays; an MP3 rip runs with silence
 * where the music should be.
 *
 * The start hook returns void in PicoDrive's own declaration, so the only way to
 * report this is the log line -- silence that says why is debuggable, silence that
 * does not is a bug report with no content. */
void mp3_start_play(void *f, int pos)
{
    (void)f;
    (void)pos;
    RG_LOGW("MP3 CD audio is not supported; this track will be silent");
}

void mp3_update(s32 *buffer, int length, int stereo)
{
    (void)buffer;
    (void)length;
    (void)stereo;
}

int mp3_get_bitrate(void *f, int size)
{
    (void)f;
    (void)size;
    return -1;
}


/* Memory mapping.
 *
 * PicoDrive maps its larger allocations through these so a host with an MMU can
 * give it address space rather than heap -- the desktop ports use mmap(), the PS2
 * port its own allocator. Here it is plain heap: ESP-IDF's allocator puts blocks
 * this size in PSRAM, which is the same place an mmap would have landed them, so
 * there is nothing to gain from pretending otherwise.
 *
 * plat_mremap has to preserve contents up to the smaller of the two sizes, which
 * is what realloc already promises.
 */
void *plat_mmap(unsigned long addr, size_t size, int need_exec, int is_fixed)
{
    (void)addr;
    (void)need_exec;
    (void)is_fixed;
    return calloc(1, size);
}

void *plat_mremap(void *ptr, size_t oldsize, size_t newsize)
{
    (void)oldsize;
    return realloc(ptr, newsize);
}

void plat_munmap(void *ptr, size_t size)
{
    (void)size;
    free(ptr);
}
