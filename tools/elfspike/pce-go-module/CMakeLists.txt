cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pce_go_module)
include(elf_loader)
project_elf(pce_go_module)

add_custom_command(
    TARGET elf POST_BUILD
    COMMAND ${PYTHON} ${CMAKE_SOURCE_DIR}/../../generate_symbols.py
            -m ${CMAKE_BINARY_DIR}/pce_go_module.app.elf
            -oc ${CMAKE_BINARY_DIR}/pce_go_host_symbols.c
            -oi ${CMAKE_SOURCE_DIR}/../pce_go_module.imports.txt
            -t pce_go_host_symbols
    COMMENT "Generating pce-go import contract"
)
