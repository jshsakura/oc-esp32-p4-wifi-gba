# GLM lane: two more systems, using the template that already exists

Goal: every system the Game & Watch port supported, plus 32X and Sega CD. Two of
the missing ones are yours. Take them one at a time and land each cleanly.

## Yours
1. **ZX Spectrum** — gnw has a working port at
   `/home/jshsakura/app/game-and-watch-retro-go-sd/Core/Src/porting/zxs/` (read
   only; do not modify that repo). Its core is `external/caprice32-go`'s sibling
   lineage — check gnw's `.gitmodules` for the exact upstream.
2. **Tiger Game.com** — same, `Core/Src/porting/gamecom/`.

## The template
`prosystem-go/` in this repo is a complete, building example, added today:
- `prosystem-go/CMakeLists.txt`
- `prosystem-go/components/prosystem/CMakeLists.txt` — globs the core through a
  relative `src` symlink into `external/`
- `prosystem-go/main/main.c` — drives the core's own API, NOT its libretro
  front-end (exclude that from the glob; a second front-end means two of
  everything)
- registration: `rg_tool.py` PROJECT_APPS + DEFAULT_APPS, and one
  `application(...)` line in `launcher/main/applications.c`

`tamalib-go/` is a second example, useful because that core needed a hand-written
HAL rather than a ready-made API.

## Non-negotiable
- **Double-buffer.** `updates[0]`/`updates[1]`, swap after every
  `rg_display_submit()`. That call reads the surface in place on another task
  through a queue one deep with a BLOCKING send (`rg_system.c`: `xQueueCreate(1)`
  / `xQueueSend(portMAX_DELAY)`). One buffer both tears and stalls the emulator
  every frame. Measured today on the GBA core: the display went from a real share
  of the frame to 0.1% once there were two.
- Build with `--target oc-gba-devkit`. The `rg_tool.py` default is `esp32p4` and
  that is NOT this device; a wrong-target build boots, reads a floating pin as a
  held button, and drops into recovery, which reads as "my change broke it".
- If it cannot build cleanly, STOP and say what blocked you, with the error. Do
  not stub things out to force a build.

## Do not touch
`gbsp/`, `gwenesis/`, `picodrive-go/`, `prosystem-go/`, `tamalib-go/`,
`components/retro-go/rg_input.c`, `components/retro-go/rg_system.c`. Those are in
flight elsewhere. `rg_tool.py` and `launcher/main/applications.c` are shared —
add your lines, do not reformat around them.
