#ifndef INI_H
#define INI_H

/*
 * Stub for inih/ini.h — vb_set.c includes it for ini_parse() (config file
 * loading). On device we never call loadFileOptions/loadGameOptions (those
 * are 3DS GUI paths), so a no-op stub that returns -1 (file not found) is
 * sufficient. This avoids pulling in the full inih library.
 */
static inline int ini_parse(const char *filename,
                            int (*handler)(void *, const char *, const char *, const char *),
                            void *user)
{
    (void)filename;
    (void)handler;
    (void)user;
    return -1;
}

#endif /* INI_H */
