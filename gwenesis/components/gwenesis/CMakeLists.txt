set(COMPONENT_SRCDIRS "src/bus src/cpus/M68K src/cpus/Z80 src/io src/savestate src/sound src/vdp")
set(COMPONENT_ADD_INCLUDEDIRS ". src/bus src/cpus/M68K src/cpus/Z80 src/io src/savestate src/sound src/vdp")
set(COMPONENT_REQUIRES "retro-go")
register_component()

# Pin gwenesis's smaller hot objects (VDP, Z80 disassembly helper, both sound
# chips, cart I/O, savestate) into internal IRAM -- see linker.lf for the
# measurements and why the 68000 interpreter itself (m68kcpu.c, 247KB .text)
# doesn't fit in the available budget yet.
__ldgen_add_fragment_files("${CMAKE_CURRENT_LIST_DIR}/linker.lf")

rg_setup_compile_options(-O2 -Wno-incompatible-pointer-types)
