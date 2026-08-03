# ZX Spectrum 48K -- floooh/chips header-only core (the zx.h lineage), the same core
# the game-and-watch reference port (Core/Src/porting/zxs) uses.
#
# The upstream tree is vendored at external/chips, reached through the "src" symlink
# so every path here stays relative to the component, the same trade prosystem-go's
# and tamalib-go's components make. Only the ZX-relevant subset of chips is vendored
# (z80, beeper, ay38910, kbd, mem, clk, zx) -- the rest of chips is other systems.
#
# chips is header-only, so there is no libretro front-end to exclude (chips ships as a
# library, not a libretro core). One translation unit -- zxs_impl.c -- defines
# CHIPS_IMPL and includes the headers in dependency order, which compiles the whole
# core into this component. Our front-end (main.c) drives the core's own API
# directly -- zx_init(), zx_exec(), zx_quickload(), zx_joystick() -- which is the same
# trade the other cores here made.
file(GLOB ZXS_SRCS RELATIVE ${CMAKE_CURRENT_LIST_DIR} "*.c")

set(COMPONENT_SRCS ${ZXS_SRCS})
set(COMPONENT_ADD_INCLUDEDIRS "." "src")
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
)
