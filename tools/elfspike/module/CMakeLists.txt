# Builds bench.c the way elf_loader expects a module to be built, rather than by
# hand. The macros must come AFTER project(): they are defined by the component,
# and project_include.cmake only installs no-op stubs until then.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(bench)
include(elf_loader)
project_elf(bench)
