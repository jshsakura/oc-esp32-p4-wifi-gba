/* shared.h - oswan build glue for retro-go.
 * Provides the path macros and globals that WSFileio.c / ws_fileio.c expect
 * from the original oswan/SDL build. On retro-go, ROM loading and save-state
 * paths are managed by the front-end, so these are stubs. */
#ifndef SHARED_H_
#define SHARED_H_

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 512
#endif

/* WS.c's Interrupt() calls graphics_paint() at vblank. The front-end (main.c)
 * provides the implementation (a no-op — we blit FrameBuffer in the app loop). */
void graphics_paint(void);

/* oswan's WsCreate/WsLoadEeprom use these to build save paths. On retro-go
 * the front-end drives save/load via WsSaveStateToFile/WsLoadStateFromFile
 * (FILE*-based), so these are never used at runtime. Kept as empty strings to
 * satisfy the snprintf calls without producing bogus paths. */
#define PATH_DIRECTORY  ""
#define SAVE_DIRECTORY  ""
#define EXTENSION       ""

/* WsLoadEeprom/WsSaveEeprom use strrchr(gameName,'/'). The front-end sets
 * this to the ROM path before calling WsInit(). */
extern char gameName[512];

#endif /* SHARED_H_ */
