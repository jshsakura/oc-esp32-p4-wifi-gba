cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smsplus_module)
include(elf_loader)
project_elf(smsplus_module)

add_custom_command(
    TARGET elf POST_BUILD
    COMMAND ${PYTHON} ${CMAKE_SOURCE_DIR}/../../generate_symbols.py
            -m ${CMAKE_BINARY_DIR}/smsplus_module.app.elf
            -oc ${CMAKE_BINARY_DIR}/smsplus_host_symbols.c
            -oi ${CMAKE_SOURCE_DIR}/../smsplus_module.imports.txt
            -t smsplus_host_symbols
    COMMENT "Generating smsplus import contract"
)
