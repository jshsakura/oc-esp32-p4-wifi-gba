#ifndef GW_MALLOC_H
#define GW_MALLOC_H

#include <stdlib.h>

// The PokeMini core's TARGET_GNW path allocates LCD buffers, EEPROM, palette
// tables and the audio FIFO through itc_malloc() and checks the result against
// 0xffffffff (the Game-And-Watch platform's out-of-memory sentinel). We map
// itc_malloc to the standard heap and translate a NULL return into that same
// sentinel so the core's existing error checks work unchanged.
static inline void *itc_malloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr)
        return (void *)0xffffffff;
    return ptr;
}

#endif
