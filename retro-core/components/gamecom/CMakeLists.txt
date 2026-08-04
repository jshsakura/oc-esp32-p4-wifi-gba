# Tiger Game.com -- Sharp SM8500/SM8521 core, a freestanding C port of MAME's Tiger
# Game.com machine (gamecom_m.cpp / gamecom_v.cpp / gamecom.cpp) plus the SM8500 CPU
# core (src/devices/cpu/sm8500), with the MAME opcode table (sm85ops.h) reused verbatim.
#
# Unlike prosystem-go and tamalib-go, this core has no upstream git repository: it is a
# hand-written, self-contained port that the game-and-watch reference port
# (Core/Src/porting/gamecom) vendors directly. So the five translation units live here,
# in the component, rather than behind a symlink into external/. Our front-end (main.c)
# drives the core's own API -- gamecom_init(), gamecom_run_frame(), gamecom_set_input_state()
# -- which is the same trade the other cores here made.
file(GLOB GAMECOM_SRCS RELATIVE ${CMAKE_CURRENT_LIST_DIR} "*.c")

set(COMPONENT_SRCS ${GAMECOM_SRCS})
set(COMPONENT_ADD_INCLUDEDIRS ".")
set(COMPONENT_REQUIRES "retro-go")
set(COMPONENT_ADD_LDFRAGMENTS "linker.lf")
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
