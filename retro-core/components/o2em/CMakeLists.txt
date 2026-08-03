# O2EM — Magnavox Odyssey2 / Philips Videopac+ emulator core.
#
# The upstream tree is the git submodule at external/o2em-go, reached through
# the symlinks in this directory (src, allegrowrapper, libretro-common) so that
# every path below stays relative to the component, which is what ESP-IDF
# expects and what keeps the file list readable — the same trick fake08-core
# uses for external/fake-08.
#
# Compile switches:
#   -D__LIBRETRO__  : makes o2em_keyboard.c define the key[] array itself and
#                     read the joystick through our joystick_data[][] instead of
#                     calling the Allegro keyboard API (install_keyboard etc.),
#                     and drops save_highscore's file backend.
#
# We deliberately do NOT define TARGET_GNW here. That switch would route
# init_display through static uint8_t bmp_buff[85000] / bmpcache_buff[85000],
# which on ESP32-P4 land in .bss / internal SRAM and overflow sram_low (the
# 183 KB bank that also holds the IRAM hot loops). Without TARGET_GNW the core
# mallocs those buffers through create_bitmap(), and ESP-IDF's PSRAM allocator
# (CONFIG_SPIRAM_USE_MALLOC, threshold 16 KB) puts every one of those 85 KB
# allocations straight into PSRAM — no internal-RAM cost, no linker fragment,
# no sdkconfig overlay. The trade-off is retro_blit writing into our mbmp
# intermediate instead of a direct hook, but that is one 153 KB memcpy we can
# well afford at 60 fps.
#
# The vkeyb on-screen keypad sources are deliberately excluded: the keypad is
# an optional follow-up and nothing in the core's emulation path references
# their symbols, so leaving them out keeps the binary smaller and avoids
# pulling in UI dependencies this port does not have yet.

set(O2EM_CORE_SRCS
    src/o2em_audio.c
    src/o2em_cpu.c
    src/o2em_cset.c
    src/o2em_keyboard.c
    src/o2em_score.c
    src/o2em_table.c
    src/o2em_vdc.c
    src/o2em_vmachine.c
    src/o2em_voice.c
    src/o2em_vpp.c
    src/o2em_vpp_cset.c
    allegrowrapper/wrapalleg.c
)

set(COMPONENT_SRCS "${O2EM_CORE_SRCS}")
set(COMPONENT_ADD_INCLUDEDIRS
    "src"
    "allegrowrapper"
    "libretro-common/include"
)
set(COMPONENT_REQUIRES "retro-go")
set(COMPONENT_ADD_LDFRAGMENTS "linker.lf")
register_component()

rg_setup_compile_options(
    -O2
    -D__LIBRETRO__
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-unused-value
    -Wno-sign-compare
    -Wno-parentheses
    -Wno-implicit-fallthrough
    -Wno-missing-field-initializers
    -Wno-format
    -Wno-pointer-to-int-cast
    -Wno-int-to-pointer-cast
)
