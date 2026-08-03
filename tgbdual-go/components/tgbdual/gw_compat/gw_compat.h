#ifndef GW_COMPAT_H
#define GW_COMPAT_H
/* tgbdual-go G&W allocator shims for ESP32-P4 retro-go.
 * heap_alloc_mem / itc_calloc / heap_itc_alloc are the G&W tree's memory
 * functions. On ESP32 the heap is unified and PSRAM handles large blocks,
 * so we route everything to the standard C allocators. heap_itc_alloc is a
 * no-op (it controls ITC vs AHB placement on STM32, which we don't have). */
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void *heap_alloc_mem(size_t size) { return malloc(size); }
static inline void *itc_calloc(size_t n, size_t size) { return calloc(n, size); }
static inline void heap_itc_alloc(int prefer_itc) { (void)prefer_itc; }

#ifdef __cplusplus
}
#endif

#endif
