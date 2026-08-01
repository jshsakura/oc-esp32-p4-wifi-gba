#ifndef COMPAT_POSIX_STRING_H
#define COMPAT_POSIX_STRING_H

// The PokeMini core's CommandLine.c includes this header unconditionally. On
// the original libretro build it provides strcasecmp/strdup etc.; ESP-IDF's
// newlib already supplies all of those, so this is a no-op shim.
#endif
