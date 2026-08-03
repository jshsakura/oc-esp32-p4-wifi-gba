/*
 * Stub for the legacy "cart.h" include path.
 *
 * unif.c (upstream, untouched submodule) is the only source file that still
 * #include "cart.h"; every sibling (ines.c, fds.c, ppu.c, fceu-cart.c, ...)
 * was migrated to "fceu-cart.h". The header was simply renamed upstream and
 * unif.c was missed. fceu-cart.h already declares every symbol unif.c uses
 * (CartInfo, SetupCartPRG/CHR/Mirroring mapping, ResetCartMapping, MI_H/MI_V),
 * so this stub just forwards. We cannot patch the submodule directly.
 */
#ifndef FCEUMM_GW_COMPAT_CART_H
#define FCEUMM_GW_COMPAT_CART_H
#include "fceu-cart.h"
#endif
