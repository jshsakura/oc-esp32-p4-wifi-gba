# Potator (Watara Supervision) emulator core.
# Sources live in both this directory and the bundled m6502 CPU subdirectory.
set(COMPONENT_SRCDIRS "." "m6502")
set(COMPONENT_ADD_INCLUDEDIRS ".")
set(COMPONENT_REQUIRES "retro-go")
register_component()
rg_setup_compile_options(
    -O2
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-unused-value
    -Wno-sign-compare
    -Wno-parentheses
    -Wno-implicit-fallthrough
    -Wno-missing-field-initializers
)
