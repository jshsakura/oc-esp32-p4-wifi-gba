#ifndef NES_MEMORY_STREAM_H
#define NES_MEMORY_STREAM_H
/*
 * fceumm-go compatibility shim for the ESP32-P4 retro-go port.
 *
 * The core's TARGET_GNW path (fceu-endian.h) includes <nes_memory_stream.h>
 * instead of the libretro-common <streams/memory_stream.h>. The G&W tree
 * provides its own version of this header; we provide one here with the same
 * API so the save-state serialiser (fceu-state.c) links without pulling in
 * the full libretro-common tree (which conflicts with driver.h's local
 * enum retro_log_level definition under TARGET_GNW).
 *
 * The API matches RetroArch's streams/memory_stream.h exactly.
 */
#include <stdint.h>
#include <stddef.h>

typedef struct memstream memstream_t;

memstream_t *memstream_open(unsigned writing);
void         memstream_close(memstream_t *stream);
uint64_t     memstream_read(memstream_t *stream, void *data, uint64_t bytes);
uint64_t     memstream_write(memstream_t *stream, const void *data, uint64_t bytes);
int          memstream_getc(memstream_t *stream);
void         memstream_putc(memstream_t *stream, int c);
char        *memstream_gets(memstream_t *stream, char *buffer, size_t len);
uint64_t     memstream_pos(memstream_t *stream);
void         memstream_rewind(memstream_t *stream);
int64_t      memstream_seek(memstream_t *stream, int64_t offset, int whence);
void         memstream_set_buffer(uint8_t *buffer, uint64_t size);
uint64_t     memstream_get_last_size(void);
uint64_t     memstream_get_ptr(memstream_t *stream);

#endif /* NES_MEMORY_STREAM_H */
