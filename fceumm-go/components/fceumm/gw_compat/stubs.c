/*
 * Link-time stubs for functions the FCEUmm core references but does not define
 * in the paths we compile (no TARGET_GNW, no FCEU_ENABLE_GAMEGENIE_ROM).
 *
 * We cannot patch the submodule, so these empty implementations live here and
 * are compiled into libfceumm.a alongside the core. Each one is a no-op or a
 * thin wrapper around free(), matching the documented contract of the real
 * function.
 */

#include <stdlib.h>

/* fceu-cart.h declares FCEU_OpenGenie / FCEU_CloseGenie only under
 * FCEU_ENABLE_GAMEGENIE_ROM, but fceu.c calls FCEU_CloseGenie() under
 * #ifndef TARGET_GNW and FCEU_OpenGenie() under if (FSettings.GameGenie).
 * Game Genie needs a Genie ROM image we do not ship, so we stub both.
 * FSettings.GameGenie defaults to 0, so OpenGenie is never reached at run
 * time — the stub exists purely to satisfy the linker. */
void FCEU_OpenGenie(void)  {}
void FCEU_CloseGenie(void) {}

/* OPLL_FCEU_delete is declared in boards/fceu-emu2413.h and called from
 * boards/vrc7.c (VRC7 mapper's Kill handler), but never defined in the
 * submodule. OPLL_FCEU_new() allocates via malloc, so free() is the correct
 * deallocator. */
typedef struct OPLL OPLL;
void OPLL_FCEU_delete(OPLL *opll)
{
    free(opll);
}

/* GetKeyboard is declared in boards/transformer.c and implemented in the
 * libretro frontend (src/drivers/libretro/libretro.c) which we do not compile.
 * It returns the current Famicom keyboard scan matrix; NULL means "no keyboard"
 * and the caller handles it gracefully (skips the key-scan loop). */
char *GetKeyboard(void)
{
    return NULL;
}
