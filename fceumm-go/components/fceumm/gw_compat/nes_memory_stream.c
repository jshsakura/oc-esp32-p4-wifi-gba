/*
 * nes_memory_stream.c — standalone memstream implementation for the ESP32-P4
 * fceumm-go port. This is a copy of the libretro-common streams/memory_stream.c
 * with the retro_common_api dependencies removed, so it compiles without the
 * libretro-common include tree.
 *
 * The API is used by fceumm's save-state serialiser (fceu-state.c): the caller
 * sets a static buffer via memstream_set_buffer(), then memstream_open() picks
 * it up (the buffer and size are one-shot globals).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nes_memory_stream.h"

static uint8_t *g_buffer         = NULL;
static uint64_t g_size            = 0;
static uint64_t last_file_size    = 0;

struct memstream
{
    uint64_t size;
    uint64_t ptr;
    uint64_t max_ptr;
    uint8_t *buf;
    unsigned writing;
};

void memstream_set_buffer(uint8_t *buffer, uint64_t size)
{
    g_buffer = buffer;
    g_size   = size;
}

uint64_t memstream_get_last_size(void)
{
    return last_file_size;
}

memstream_t *memstream_open(unsigned writing)
{
    memstream_t *stream;
    if (!g_buffer || !g_size)
        return NULL;

    stream = (memstream_t *)malloc(sizeof(*stream));
    if (!stream)
        return NULL;

    stream->buf     = g_buffer;
    stream->size    = g_size;
    stream->ptr     = 0;
    stream->max_ptr = 0;
    stream->writing = writing;

    g_buffer = NULL;
    g_size   = 0;

    return stream;
}

void memstream_close(memstream_t *stream)
{
    if (!stream)
        return;
    last_file_size = stream->writing ? stream->max_ptr : stream->size;
    free(stream);
}

uint64_t memstream_get_ptr(memstream_t *stream)
{
    return stream->ptr;
}

uint64_t memstream_read(memstream_t *stream, void *data, uint64_t bytes)
{
    uint64_t avail;
    if (!stream)
        return 0;
    avail = stream->size - stream->ptr;
    if (bytes > avail)
        bytes = avail;
    memcpy(data, stream->buf + stream->ptr, (size_t)bytes);
    stream->ptr += bytes;
    if (stream->ptr > stream->max_ptr)
        stream->max_ptr = stream->ptr;
    return bytes;
}

uint64_t memstream_write(memstream_t *stream, const void *data, uint64_t bytes)
{
    uint64_t avail;
    if (!stream)
        return 0;
    avail = stream->size - stream->ptr;
    if (bytes > avail)
        bytes = avail;
    memcpy(stream->buf + stream->ptr, data, (size_t)bytes);
    stream->ptr += bytes;
    if (stream->ptr > stream->max_ptr)
        stream->max_ptr = stream->ptr;
    return bytes;
}

int64_t memstream_seek(memstream_t *stream, int64_t offset, int whence)
{
    uint64_t ptr;
    switch (whence)
    {
        case SEEK_SET: ptr = offset; break;
        case SEEK_CUR: ptr = stream->ptr + offset; break;
        case SEEK_END: ptr = (stream->writing ? stream->max_ptr : stream->size) + offset; break;
        default: return -1;
    }
    if (ptr <= stream->size)
    {
        stream->ptr = ptr;
        return 0;
    }
    return -1;
}

void memstream_rewind(memstream_t *stream)
{
    memstream_seek(stream, 0L, SEEK_SET);
}

uint64_t memstream_pos(memstream_t *stream)
{
    return stream->ptr;
}

char *memstream_gets(memstream_t *stream, char *buffer, size_t len)
{
    (void)stream; (void)buffer; (void)len;
    return NULL;
}

int memstream_getc(memstream_t *stream)
{
    int ret;
    if (stream->ptr >= stream->size)
        return EOF;
    ret = stream->buf[stream->ptr++];
    if (stream->ptr > stream->max_ptr)
        stream->max_ptr = stream->ptr;
    return ret;
}

void memstream_putc(memstream_t *stream, int c)
{
    if (stream->ptr < stream->size)
        stream->buf[stream->ptr++] = c;
    if (stream->ptr > stream->max_ptr)
        stream->max_ptr = stream->ptr;
}
