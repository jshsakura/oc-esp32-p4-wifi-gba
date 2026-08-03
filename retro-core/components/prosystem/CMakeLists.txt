# ProSystem -- Atari 7800 emulator core (sylverb/prosystem-go fork).
#
# The upstream tree is at external/prosystem-go, reached through the "src"
# symlink so every path here stays relative to the component.
#
# Only core/ is built. The fork also carries core/libretro.c and a
# libretro-common tree, and neither is used: libretro.c is a second front-end
# with its own video, audio and input plumbing, and linking it alongside ours
# would give the app two of everything and a duplicate main loop. Our front-end
# drives the core's own API directly -- prosystem_ExecuteFrame(), maria_surface,
# tia_buffer -- which is the same trade the other cores here made.
file(GLOB PROSYSTEM_SRCS RELATIVE ${CMAKE_CURRENT_LIST_DIR} "src/core/*.c")
list(FILTER PROSYSTEM_SRCS EXCLUDE REGEX "/libretro\\.c$")

# Bios.c and Cartridge.c read files through libretro's stream abstraction rather
# than stdio, so the four libretro-common translation units behind
# streams/file_stream.h come along. Nothing else from that tree is needed, and
# taking it wholesale would pull in networking, compression and a config system.
set(PROSYSTEM_LRC_SRCS
    "src/libretro-common/streams/file_stream.c"
    "src/libretro-common/streams/file_stream_transforms.c"
    "src/libretro-common/vfs/vfs_implementation.c"
    "src/libretro-common/compat/compat_strl.c"
    "src/libretro-common/compat/fopen_utf8.c"
)

set(COMPONENT_SRCS ${PROSYSTEM_SRCS} ${PROSYSTEM_LRC_SRCS})
set(COMPONENT_ADD_INCLUDEDIRS "src/core" "src/libretro-common/include")
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
