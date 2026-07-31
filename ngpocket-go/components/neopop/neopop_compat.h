/*
 * neopop_compat.h -- platform compatibility shim for the NeoPop core.
 *
 * The NeoPop Core was written for Win32/SDL on desktop. When building for
 * ESP32-P4 (RISC-V / ESP-IDF) inside retro-go a few things need to be wired up:
 *
 *   - `__cdecl` is an MSVC calling-convention attribute. On GCC/clang it must
 *     expand to nothing.
 *   - Some core files reference `stricmp`/`strnicmp` (Win32 names); map them to
 *     the POSIX case-insensitive equivalents.
 *
 * This header is force-included into every core translation unit via the
 * `-include neopop_compat.h` compile flag set in the component CMakeLists.
 */
#ifndef NEOPOP_COMPAT_H
#define NEOPOP_COMPAT_H

/* MSVC calling convention attribute -> no-op on GCC/clang/RISC-V. */
#ifndef __cdecl
#define __cdecl
#endif

/* Win32 case-insensitive string compares -> POSIX equivalents. */
#ifdef __GNUC__
#ifndef stricmp
#define stricmp strcasecmp
#endif
#ifndef strnicmp
#define strnicmp strncasecmp
#endif
#endif

#endif /* NEOPOP_COMPAT_H */
