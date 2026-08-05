# Build gnuboy with the module layout expected by espressif/elf_loader.
# These macros must remain after project(); before that point they are stubs.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gnuboy_module)
include(elf_loader)
project_elf(gnuboy_module)
