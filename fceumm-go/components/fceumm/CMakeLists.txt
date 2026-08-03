# FCEUmm — NES / Famicom emulator core (sylverb/fceumm-go fork).
#
# The upstream tree is the git submodule at external/fceumm-go, reached through
# the "src" symlink so every path stays relative to the component.
#
# Compile switch:
#   -DLINUX_EMU  : populates the full iNES mapper table (every entry is guarded
#                  by #if defined(LINUX_EMU) || defined(NES_MAPPER_*)) and uses
#                  standard fopen() for the FDS BIOS. Without LINUX_EMU the
#                  bmap[] table is empty and the core expects a generated
#                  "build/mappers.h" pack we do not have.
#
# We deliberately do NOT define TARGET_GNW. That switch routes the core through
# the Game-and-Watch tree's bare-metal shims (gw_malloc.h, build/config.h,
# build/mappers.h, nes_memory_stream.h, rom_manager.h, itc_calloc/ahb_malloc
# allocators) which would each need a stub on ESP32. Without TARGET_GNW the
# core takes its standard code paths: standard malloc/free (which ESP-IDF's
# PSRAM allocator handles for large blocks), libretro-common headers (which
# are in the submodule at src/drivers/libretro/), and self-allocated XBuf.
# This is the same trade-off the videopac-go port made dropping TARGET_GNW.
#
# Mapper board sources are globbed. Files starting with "__" are internal
# helpers #included by other boards, not compiled standalone.

file(GLOB FCEUMM_CORE_SRCS RELATIVE ${CMAKE_CURRENT_LIST_DIR}
    "src/*.c"
    "src/input/*.c"
)
file(GLOB FCEUMM_BOARD_SRCS RELATIVE ${CMAKE_CURRENT_LIST_DIR}
    "src/boards/*.c"
)
list(FILTER FCEUMM_BOARD_SRCS EXCLUDE REGEX "/__[^/]*\\.c$")

# libretro-common support sources the core depends on at link time.
# memory_stream.c implements the memstream_* API that fceu-state.c uses to
# serialise save states into a flat buffer, and that our main.c save/load
# handlers use to shuttle that buffer to/from a file.
# file_path.c / file_path_io.c provide path_is_valid() used by palette.c.
# file_stream.c provides filestream_open/read/close used by palette.c and
# fceu-debug.c for palette and debug output files.
set(FCEUMM_LIBRETRO_SRCS
    "src/drivers/libretro/libretro-common/streams/memory_stream.c"
    "src/drivers/libretro/libretro-common/file/file_path.c"
    "src/drivers/libretro/libretro-common/file/file_path_io.c"
    "src/drivers/libretro/libretro-common/streams/file_stream.c"
    "src/drivers/libretro/libretro-common/string/stdstring.c"
    "src/drivers/libretro/libretro-common/vfs/vfs_implementation.c"
    "src/drivers/libretro/libretro-common/compat/compat_strl.c"
)

set(COMPONENT_SRCS ${FCEUMM_CORE_SRCS} ${FCEUMM_BOARD_SRCS} ${FCEUMM_LIBRETRO_SRCS} "gw_compat/stubs.c")
set(COMPONENT_ADD_INCLUDEDIRS
    "src"
    "gw_compat"
    "src/drivers/libretro/libretro-common/include"
)
set(COMPONENT_REQUIRES "retro-go")
register_component()

# Route fceumm's .bss into PSRAM via the ldgen linker fragment (see linker.lf
# for the full rationale). __ldgen_add_fragment_files is the ESP-IDF internal
# that registers a .lf with the linker script generator; we call it directly
# because register_component() predates the LDFRAGMENTS keyword.
__ldgen_add_fragment_files("${CMAKE_CURRENT_LIST_DIR}/linker.lf")

rg_setup_compile_options(
    -O2
    -DLINUX_EMU
    -DFCEU_VERSION_NUMERIC=9813
    # src/input/share.h includes ../input.h (which uses the DECLFR/DECLFW
    # macros) BEFORE ../fceu.h (which defines them). In the TARGET_GNW build
    # every file starts with build/config.h that pre-includes everything; we
    # have no such umbrella, so we force-include fceu.h to make the macros
    # visible from the first token. fceu.h's include guard makes the later
    # explicit #include in each .c a no-op.
    -include fceu.h
    # FCEUmm carries 300+ board source files, each exporting a handful of small
    # globals (init pointers, mapper flags). With ESP-IDF's default RISC-V
    # -msmall-data-limit=8 those land in .sdata/.sbss, which must sit within the
    # ±2 KiB range of the gp register. The mapper set alone blows past that, so
    # the linker (--enable-non-contiguous-regions) starts discarding .sbss from
    # the ESP-IDF system libraries and the link fails with ~1.7 MiB of discarded
    # sections. Forcing the limit to 0 routes every fceumm global into ordinary
    # .data/.bss (which can span the whole SRAM region), leaving the small-data
    # window for the framework's own globals. Measured: link goes from 1599
    # discarded-section errors to clean.
    -msmall-data-limit=0
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
    -Wno-array-bounds
    -Wno-enum-compare
    -Wno-error=implicit-function-declaration
    -Wno-error=sequence-point
    -Wno-error=char-subscripts
)
