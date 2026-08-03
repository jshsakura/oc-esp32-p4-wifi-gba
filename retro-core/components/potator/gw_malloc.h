#ifndef __GW_MALLOC_H__
#define __GW_MALLOC_H__

/*
 * Stub for the potator (Watara Supervision) core.
 *
 * The upstream STM32H7 port allocates the emulator's work RAM through
 * itc_malloc() (instruction-tightly-coupled memory on the STM32). The core
 * itself is plain portable C and only needs a heap allocation here, so on
 * retro-go / ESP32-P4 we simply back it with the standard C heap.
 */
#include <stdlib.h>

static inline void *itc_malloc(unsigned int size)
{
    return malloc(size);
}

#endif /* __GW_MALLOC_H__ */
