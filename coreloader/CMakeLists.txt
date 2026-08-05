cmake_minimum_required(VERSION 3.5)
# Carries no emulator core. Cores are ELF files on the SD card, loaded at launch.
set(COMPONENTS "main retro-go app_trace bootloader esptool_py")
set(RG_ENABLE_NETWORKING 0)
include(../base.cmake)
project(coreloader)
