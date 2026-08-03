#ifndef RETRO_INLINE_H
#define RETRO_INLINE_H

// libretro-common shim: the only thing the PokeMini core uses from this
// header is the INLINE macro (for static INLINE functions in Video.h,
// PokeMini.h, etc.). ESP-IDF's GCC supports standard C inline directly.
#define INLINE inline

#endif
