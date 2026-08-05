# Build gnuboy with the module layout expected by espressif/elf_loader.
# These macros must remain after project(); before that point they are stubs.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gnuboy_module)
include(elf_loader)
project_elf(gnuboy_module)

# Post-build symbol contract & table generation rule (Lane C)
add_custom_command(
    TARGET elf POST_BUILD
    COMMAND ${PYTHON} ${CMAKE_SOURCE_DIR}/../../generate_symbols.py
            -m ${CMAKE_BINARY_DIR}/gnuboy_module.app.elf
            -oc ${CMAKE_SOURCE_DIR}/../gbhost_symbols.c
            -oi ${CMAKE_SOURCE_DIR}/../gnuboy_module.imports.txt
            -t gb_host_symbols
    COMMENT "[Lane C] Generating symbol contract artifact and C symbol table"
)
