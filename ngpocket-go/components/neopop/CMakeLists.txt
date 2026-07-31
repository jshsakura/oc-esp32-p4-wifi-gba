set(COMPONENT_SRCDIRS ". TLCS-900h z80")
set(COMPONENT_ADD_INCLUDEDIRS ". TLCS-900h z80")
set(COMPONENT_REQUIRES "retro-go")
register_component()
rg_setup_compile_options(
    -O2
    -DLSB_FIRST
    -include neopop_compat.h
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-unused-function
    -Wno-implicit-fallthrough
    -Wno-format
    -Wno-format-security
    -Wno-format-truncation
    -Wno-stringop-truncation
    -Wno-pointer-sign
    -Wno-discarded-qualifiers
    -Wno-sequence-point
    -Wno-maybe-uninitialized
)
# z80/dasm.c is a standalone disassembler tool with its own main(); keep it out.
set_source_files_properties(z80/dasm.c PROPERTIES HEADER_FILE_ONLY TRUE)
