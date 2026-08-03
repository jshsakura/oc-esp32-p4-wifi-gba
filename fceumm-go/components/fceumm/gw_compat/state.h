/*
 * Stub for the legacy "state.h" include path.
 *
 * unif.c (upstream, untouched submodule) is the only source file that still
 * #include "state.h"; every sibling (fceu.c, ppu.c, ines.c, video.c, ...)
 * was migrated to "fceu-state.h". Same rename-miss as cart.h. fceu-state.h
 * already declares every symbol unif.c uses (AddExState, ResetExState, plus
 * the SFORMAT type), so this stub just forwards. We cannot patch the
 * submodule directly.
 */
#ifndef FCEUMM_GW_COMPAT_STATE_H
#define FCEUMM_GW_COMPAT_STATE_H
#include "fceu-state.h"
#endif
