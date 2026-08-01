# red-viper (Nintendo Virtual Boy) emulator core.
#
# Build flags:
#   GNW_VB_DEVICE    - embedded device path: interpreter-only (no ARM DRC),
#                      single-player RAM (338KB not 676KB), pointer-based ROM
#                      (no 16MB malloc), no zlib dependency in replay.c.
#   VB_LEFT_EYE_ONLY - render left eye only (VB_RENDER_EYES=1 in video_soft.cpp)
#
# The ARM DRC (drc_core.h, drc_alloc.h, arm_codegen.h, arm_emit.h, arm_types.h)
# is automatically disabled on RISC-V: DRC_AVAILABLE is false because the check
# in drc_core.h requires __ARM_ARCH >= 6 && __arm__. The DRC implementation
# source files (drc_core.c, drc_alloc.c) do not exist in this tree — only
# declarations remain, and they are never referenced at link time.
#
# Excluded source files:
#   video.c       - 3DS GL renderer dispatcher (needs citro3d, video_hard)
#   video_hard.c  - 3DS GPU renderer (needs citro3d, linearAlloc)
#   vb_gui.c      - 3DS GUI (needs citro3d, inih file I/O); we provide our own
#                   frontend in main/main.cpp
#
# C++ flags: video_soft.cpp requires -fno-rtti -fno-exceptions.
set(COMPONENT_SRCS
    "interpreter.c"
    "patches.c"
    "replay.c"
    "rom_db.c"
    "v810_cpu.c"
    "v810_ins.c"
    "v810_mem.c"
    "vb_set.c"
    "vb_sound.c"
    "video_common.c"
    "video_soft.cpp"
)
set(COMPONENT_ADD_INCLUDEDIRS ".")
set(COMPONENT_REQUIRES "retro-go")
register_component()
rg_setup_compile_options(
    -O2
    -DGNW_VB_DEVICE
    -DVB_LEFT_EYE_ONLY
    -DDEBUGLEVEL=0
    -fno-rtti
    -fno-exceptions
    -Wno-error=format
    -Wno-format
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-unused-function
    -Wno-unused-value
    -Wno-sign-compare
    -Wno-parentheses
    -Wno-maybe-uninitialized
    -Wno-implicit-fallthrough
    -Wno-missing-field-initializers
    -Wno-pointer-to-int-cast
    -Wno-int-to-pointer-cast
)
