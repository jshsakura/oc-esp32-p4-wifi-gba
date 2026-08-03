# PokeMini (Pokemon Mini) emulator core.
#
# The upstream core is written for the libretro/GNW platforms. We build it
# with TARGET_GNW so it takes its file-based save-state path (which uses
# rg_storage.h, already provided by retro-go) and skips the 32-bit video
# renderers we don't need. The two shim headers in include/ satisfy the
# remaining GNW-specific includes (<gw_malloc.h>, <compat/posix_string.h>).
#
# Only the native 1x1 video blitter is compiled; Video_x2..x7 are omitted
# (they were never adapted for TARGET_GNW and we let retro-go handle scaling).
set(COMPONENT_SRCS
    "PokeMini.c"
    "Hardware.c"
    "PMCommon.c"
    "CommandLine.c"
    "MinxCPU.c"
    "MinxCPU_CE.c"
    "MinxCPU_CF.c"
    "MinxCPU_SP.c"
    "MinxCPU_XX.c"
    "MinxTimers.c"
    "MinxIO.c"
    "MinxIRQ.c"
    "MinxPRC.c"
    "MinxColorPRC.c"
    "MinxLCD.c"
    "MinxAudio.c"
    "Multicart.c"
    "Video.c"
    "Video_x1.c"
    "PokeMini_ColorPal.c"
    "freebios.c"
)
set(COMPONENT_ADD_INCLUDEDIRS "." "include")
set(COMPONENT_REQUIRES "retro-go")
set(COMPONENT_ADD_LDFRAGMENTS "linker.lf")
register_component()
rg_setup_compile_options(
    -O2
    -DTARGET_GNW
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-unused-value
    -Wno-sign-compare
    -Wno-parentheses
    -Wno-implicit-fallthrough
    -Wno-missing-field-initializers
    -Wno-pointer-to-int-cast
    -Wno-int-to-pointer-cast
)
