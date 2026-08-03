set(COMPONENT_SRCDIRS ".")
set(COMPONENT_ADD_INCLUDEDIRS ".")
register_component()
if(RG_BENCH_PROFILE_APU)
    component_compile_options(-DRG_BENCH_PROFILE_APU=1)
endif()

rg_setup_compile_options(-O2 -Wno-error=format)
