set(COMPONENT_SRCDIRS ".")
set(COMPONENT_ADD_INCLUDEDIRS ".")
set(COMPONENT_ADD_LDFRAGMENTS "linker.lf")
register_component()
if(RG_BENCH_PROFILE_APU)
    component_compile_options(-DRG_BENCH_PROFILE_APU=1)
endif()

rg_setup_compile_options(-O2 -Wno-error=format)

# main_poke.c pulls in PokeMini.h, which branches on TARGET_GNW to choose the
# file-based save-state path (rg_storage.h) over the libretro memstream one.
# pkmini-go's own main component carried this define; folded into retro-core's
# shared main component, it has to be scoped to just this file so it doesn't
# leak into the other systems' translation units.
set_source_files_properties(main_poke.c PROPERTIES COMPILE_DEFINITIONS "TARGET_GNW")

# main_wsc.c includes WSRender.h directly for the FrameBuffer declaration,
# which is behind #ifndef NOSDL_FB -- wswan-go's own main component carried
# this define for the same reason. oswan's own -DNOSDL_FB (in its component
# CMakeLists) doesn't reach this file since they're separate compile units.
set_source_files_properties(main_wsc.c PROPERTIES COMPILE_DEFINITIONS "NOSDL_FB")
