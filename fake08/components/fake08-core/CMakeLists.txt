# FAKE-08, an MIT-licensed PICO-8 player, built as a retro-go app.
#
# The upstream tree is the git submodule at external/fake-08, reached through the symlinks in
# this directory so that every path below stays relative to the component -- which is what
# ESP-IDF wants, and what keeps the file list readable.
#
# Sources are listed rather than globbed, on purpose:
#   * f8source/main.cpp is upstream's entry point for platforms that boot into a shell. Ours
#     is fake08/main/main.cpp, and having both would be two main()s.
#   * z8lua/ltests.c is Lua's internal test harness, not part of the library.
#   * z8lua/liolib.c is Lua's file I/O library, which nothing links to: neither z8lua's own
#     linit.c nor FAKE-08 ever calls luaopen_io, because a PICO-8 cart has no io. It is also
#     the one file that would not compile, since it formats lua_Number with "%g" and that
#     number is now a C++ class. Leaving it out is both smaller and honest; if a future
#     z8lua opens io, the link fails loudly rather than shipping broken formatting.
# The list otherwise matches upstream's own SOURCES_CXX for the libretro port, which is the
# closest thing they have to "the core with no operating system attached".

set(FAKE08_CORE_SRCS
    f8source/Audio.cpp
    f8source/Input.cpp
    f8source/cart.cpp
    f8source/emojiconversion.cpp
    f8source/filehelpers.cpp
    f8source/filter.cpp
    f8source/fontdata.cpp
    f8source/graphics.cpp
    f8source/hostCommonFunctions.cpp
    f8source/logger.cpp
    f8source/mathhelpers.cpp
    f8source/nibblehelpers.cpp
    f8source/picoluaapi.cpp
    f8source/printHelper.cpp
    f8source/stringToDataHelpers.cpp
    f8source/synth.cpp
    f8source/vm.cpp
    f8platform/RetroGoHost.cpp
    lodepng/lodepng.cpp
    miniz/miniz.c
    simpleini/ConvertUTF.c
)

set(Z8LUA_SRCS
    z8lua/eris.c z8lua/lapi.c z8lua/lauxlib.c z8lua/lbaselib.c z8lua/lbitlib.c
    z8lua/lcode.c z8lua/lcorolib.c z8lua/lctype.c z8lua/ldblib.c z8lua/ldebug.c
    z8lua/ldo.c z8lua/ldump.c z8lua/lfunc.c z8lua/lgc.c z8lua/linit.c
    z8lua/llex.c z8lua/lmem.c z8lua/loadlib.c z8lua/lobject.c
    z8lua/lopcodes.c z8lua/loslib.c z8lua/lparser.c z8lua/lpico8lib.c z8lua/lstate.c
    z8lua/lstring.c z8lua/lstrlib.c z8lua/ltable.c z8lua/ltablib.c z8lua/ltm.c
    z8lua/lundump.c z8lua/lvm.c z8lua/lzio.c
)

set(COMPONENT_SRCS ${FAKE08_CORE_SRCS} ${Z8LUA_SRCS})
set(COMPONENT_ADD_INCLUDEDIRS "f8source" "f8platform" "z8lua" "lodepng" "miniz" "simpleini")
set(COMPONENT_REQUIRES "retro-go")
register_component()

# Everything below has to come after register_component(): ESP-IDF runs this file once in
# script mode just to collect requirements, where these commands do not exist. In that pass
# register_component() returns early, so nothing here is reached.

# z8lua's files end in .c but are not C. luaconf.h makes Lua's number type z8::fix32, a C++
# class, so the whole library has to go through the C++ compiler -- which is exactly what
# upstream does, with "CC = $(CXX)" in every one of its Makefiles.
set_source_files_properties(${Z8LUA_SRCS} PROPERTIES LANGUAGE CXX)

# retro-go vendors lodepng too, and both copies end up in this app. The functions do not
# collide -- FAKE-08's copy is C++ so they are mangled, retro-go's is C so they are not -- but
# three file-scope globals do, because global variables keep their plain names either way.
#
# Renaming them in FAKE-08's copy is what is left. Sharing one decoder instead sounds tidier
# and is not: cart.cpp wants lodepng's C++ vector API and lodepng_error_text, retro-go builds
# neither (it compiles the C file with -DLODEPNG_NO_COMPILE_ERROR_TEXT), and bridging that
# means a shim whose symbol linkage has to be right in two directions at once. The duplicate
# costs tens of KB in a 3MB partition.
set_source_files_properties(lodepng/lodepng.cpp PROPERTIES COMPILE_DEFINITIONS
    "lodepng_default_decompress_settings=fake08_lodepng_default_decompress_settings;\
lodepng_default_compress_settings=fake08_lodepng_default_compress_settings;\
LODEPNG_VERSION_STRING=FAKE08_LODEPNG_VERSION_STRING")

rg_setup_compile_options(
    -O2
    # Upstream targets consoles with more slack than this one and builds warning-noisy but
    # clean. Silencing rather than patching keeps the submodule a fast-forward from upstream.
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-sign-compare
    -Wno-missing-field-initializers
    -Wno-implicit-fallthrough
    -Wno-char-subscripts
    # source/filter.cpp computes its biquad coefficients in a switch with no default, which
    # GCC cannot see is exhaustive. ESP-IDF turns that warning into an error.
    -Wno-maybe-uninitialized
    # On this toolchain uint32_t is unsigned long, not unsigned int, so z8lua's "%x" and "%d"
    # in lpico8lib.c (PICO-8's tostr/tonum) do not match their arguments' declared types --
    # while being the same width and passed identically, so the output is right. Fixed
    # properly upstream means PRIx32 everywhere; suppressed here to keep the submodule a
    # fast-forward from upstream, since the alternative is churn in a file we do not own.
    -Wno-format
    # lpico8lib.c:287 is a // comment continued with a backslash.
    -Wno-comment
)

# ESP-IDF v5.5 defaults to gnu++23, where std::lerp exists and collides with FAKE-08's own
# lerp() the moment <cmath> and "using namespace std" meet. Upstream is written against the
# older standard, so build it that way rather than renaming its functions.
#
# CXX only: the same flag on a .c file is "valid for C++ but not for C", which is a warning,
# and warnings are errors here.
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
