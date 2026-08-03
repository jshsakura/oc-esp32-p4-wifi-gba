/*
 * TamaLIB hardware-abstraction types for retro-go / ESP32-P4.
 *
 * Filled in from external/tamalib/hal_types.h.template (every port has to provide this file;
 * it is deliberately not part of the upstream tree). u64_t is added on top of the template:
 * tamalib_cpu.h's state_t uses it for tick_counter and the timers, but the template only
 * goes up to u32_t. The game-and-watch reference port hit the same gap and fixed it the
 * same way in its own copy of this file.
 */
#ifndef _HAL_TYPES_H_
#define _HAL_TYPES_H_

#include <stdint.h>

typedef uint8_t  bool_t;
typedef uint8_t  u4_t;
typedef uint8_t  u5_t;
typedef uint8_t  u8_t;
typedef uint16_t u12_t;
typedef uint16_t u13_t;
typedef uint32_t u32_t;
typedef uint64_t u64_t;

#endif /* _HAL_TYPES_H_ */
