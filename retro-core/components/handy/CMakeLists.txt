set(COMPONENT_SRCDIRS ".")
set(COMPONENT_ADD_INCLUDEDIRS ".")
set(COMPONENT_REQUIRES "retro-go")
set(COMPONENT_ADD_LDFRAGMENTS "linker.lf")
register_component()
rg_setup_compile_options(
    -O2
    -fno-rtti
    -fno-exceptions
    -Wno-error=format
    -Wno-error=unused-value
    -Wno-format
)
