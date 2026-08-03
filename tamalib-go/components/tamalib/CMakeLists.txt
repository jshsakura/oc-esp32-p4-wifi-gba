# TamaLib -- Tamagotchi P1 (E0C6S46) emulation core (BenjaminSoelberg fork, tuned
# for game-and-watch-retro-go).
#
# The upstream tree is at external/tamalib, reached through the "src" symlink so every
# path here stays relative to the component, the same trade prosystem-go's component makes.
#
# Only three translation units exist upstream and all three are needed: tamalib.c is the
# init/step entry points, tamalib_cpu.c is the CPU core itself, and tamalib_hw.c -- despite
# the name -- is not a platform HAL. It is fixed glue, built into the library, that turns the
# CPU's raw SEG/COM writes into the hal_t callbacks a front-end implements (matrix pixel,
# icon, buzzer period/enable). Our front-end (main.c) supplies the actual hal_t.
#
# hal_types.h is a template upstream (hal_types.h.template) that every port is expected to
# fill in; ours lives here rather than inside external/tamalib so the pristine clone is never
# edited. It also has to add u64_t, which tamalib_cpu.h uses but the template never defines --
# the same gap the game-and-watch reference port had to fill in its own copy.
file(GLOB TAMALIB_SRCS RELATIVE ${CMAKE_CURRENT_LIST_DIR} "src/tamalib.c" "src/tamalib_cpu.c" "src/tamalib_hw.c")

set(COMPONENT_SRCS ${TAMALIB_SRCS})
set(COMPONENT_ADD_INCLUDEDIRS "." "src")
set(COMPONENT_REQUIRES "retro-go")
register_component()

rg_setup_compile_options(
    -O2
    -Wno-unused-variable
    -Wno-unused-function
)
