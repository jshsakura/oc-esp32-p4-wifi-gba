set(COMPONENT_SRCDIRS ".")
set(COMPONENT_ADD_INCLUDEDIRS ".")
set(COMPONENT_REQUIRES "retro-go" "pokemini")
register_component()
rg_setup_compile_options(
    -O2
    -DTARGET_GNW
    -Wno-unused-variable
)
