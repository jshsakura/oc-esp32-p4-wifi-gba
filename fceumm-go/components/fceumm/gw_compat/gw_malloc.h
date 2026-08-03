#ifndef GW_MALLOC_H
#define GW_MALLOC_H
/*
 * fceumm-go compatibility shim for the ESP32-P4 retro-go port.
 *
 * The core's TARGET_GNW path expects the Game-and-Watch tree's allocators:
 * itc_calloc / itc_malloc (ITC RAM) and ahb_malloc / ahb_calloc (AHB SRAM).
 * On ESP32-P4 the heap is unified and the PSRAM allocator handles large
 * blocks, so we route every G&W allocator to the standard C functions.
 * FCEU_gfree/FCEU_free are no-ops in the TARGET_GNW path (the G&W never frees
 * emulation memory), and we keep that behaviour: the buffers live for the
 * whole game session and are reclaimed on app exit.
 */
#include <stdlib.h>

#define itc_calloc(nmemb, size)  calloc((nmemb), (size))
#define itc_malloc(size)         malloc((size))
#define ahb_malloc(size)         malloc((size))
#define ahb_calloc(nmemb, size)  calloc((nmemb), (size))

#endif /* GW_MALLOC_H */
