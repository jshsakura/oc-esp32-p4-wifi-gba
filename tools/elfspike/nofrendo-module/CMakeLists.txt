cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nofrendo_module)
include(elf_loader)
project_elf(nofrendo_module)

add_custom_command(
    TARGET elf POST_BUILD
    COMMAND ${PYTHON} ${CMAKE_SOURCE_DIR}/../../generate_symbols.py
            -m ${CMAKE_BINARY_DIR}/nofrendo_module.app.elf
            -oc ${CMAKE_BINARY_DIR}/nofrendo_host_symbols.c
            -oi ${CMAKE_SOURCE_DIR}/../nofrendo_module.imports.txt
            -t nofrendo_host_symbols
    COMMENT "Generating nofrendo import contract"
)
