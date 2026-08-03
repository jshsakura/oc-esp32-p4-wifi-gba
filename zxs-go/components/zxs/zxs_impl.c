/* Single translation unit that compiles the floooh/chips ZX Spectrum core
 * (header-only). zx.h does not self-include its dependencies, so they are included
 * here in dependency order, then CHIPS_IMPL pulls in every implementation. This is
 * the same arrangement the game-and-watch reference port's zxs_impl.c uses. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define CHIPS_IMPL
#define CHIPS_ASSERT(c) ((void)0)

#include "chips_common.h"
#include "z80.h"
#include "beeper.h"
#include "ay38910.h"
#include "kbd.h"
#include "mem.h"
#include "clk.h"
#include "zx.h"
