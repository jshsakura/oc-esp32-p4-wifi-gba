# Caprice32 -- Amstrad CPC emulator (sylverb/caprice32-go fork).
#
# The upstream tree is at external/caprice32-go, reached through the "src"
# symlink so every path here stays relative to the component.
#
# cap32/*.c is the emulator core: Z80, CRTC, Gate Array, PSG, FDC, tape and
# the built-in CPC OS/BASIC/AMSDOS ROM images (rom/*.h -- there is nothing to
# load from disk, unlike ProSystem's optional BIOS). It reaches out to a
# handful of externs (retro_getScreenPtr(), retro_malloc(), retro_computer_cfg,
# ...) that our own main.c implements, the same trade prosystem-go makes with
# libretro-common's file streams.
#
# The fork also carries libretro/libretro-core.c: a full RetroArch core with
# its own video/audio buffer scheme, on-screen keyboard and status bar, and
# disk-control interface. None of that is built -- it is a second front-end
# that would fight our own display/audio/input plumbing for the same job, and
# our main.c drives cap32/*.c directly instead.
#
# libretro/gfx/video16bpp.c is the one file pulled in from that tree anyway:
# CPC.rgb2color and CPC.video_set_palette_antialias are core-side colour
# conversion (wired up by cap32.c itself in video_set_style() and the Gate
# Array ink-select handler), not front-end glue, so it belongs here.
file(GLOB CAPRICE32_SRCS RELATIVE ${CMAKE_CURRENT_LIST_DIR} "src/cap32/*.c")

set(CAPRICE32_GFX_SRCS
    "src/libretro/gfx/video16bpp.c"
    # video_set_style() switches on CPC.scr_bpp between the 16bpp and 32bpp
    # colour paths at compile time (it's one function, not two) even though we
    # only ever select 16 -- rgb2color_24bpp()/video_set_palette_antialias_24bpp()
    # still have to exist for the other case to link. Same category of file as
    # video16bpp.c above, so it comes along for the same reason.
    "src/libretro/gfx/video24bpp.c"
)

set(COMPONENT_SRCS ${CAPRICE32_SRCS} ${CAPRICE32_GFX_SRCS})

# "src" itself is on the path because cap32.c reaches for its one front-end
# header with a "libretro/gfx/video.h" style include; "src/libretro" is on
# it too because video16bpp.c in turn reaches for "assets/assets.h" relative
# to the libretro/ directory it lives under.
#
# "src/libretro-common/include" is the one genuinely unfortunate one: cap32.c
# unconditionally does `#include "libretro.h"` (the real RetroArch core ABI
# header, ~4000 lines) even though nothing in cap32.c actually uses anything
# it declares -- grep finds zero RETRO_*/struct retro_* references, just a
# commented-out RETROK_F9. It costs us nothing to build (header-only, no .c
# file, no symbols added to the binary) so pulling it in is simpler and more
# honest than patching out a no-op include in vendored code.
set(COMPONENT_ADD_INCLUDEDIRS "src" "src/cap32" "src/libretro" "src/libretro/gfx" "src/libretro-common/include")
set(COMPONENT_REQUIRES "retro-go")
register_component()

rg_setup_compile_options(
    -O2
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-sign-compare
    -Wno-unused-value
    -Wno-implicit-fallthrough
    -Wno-missing-field-initializers
    -Wno-maybe-uninitialized
)
