set(GNUBOY_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../retro-core/components/gnuboy")
set(RETRO_GO_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../components/retro-go")

idf_component_register(
    SRCS
        "gnuboy_module.c"
        "${GNUBOY_DIR}/cpu.c"
        "${GNUBOY_DIR}/gnuboy.c"
        "${GNUBOY_DIR}/hw.c"
        "${GNUBOY_DIR}/lcd.c"
        "${GNUBOY_DIR}/sound.c"
    INCLUDE_DIRS
        "."
        "${GNUBOY_DIR}"
        "${RETRO_GO_DIR}"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -DRG_TARGET_ESP32P4=1
    -DRETRO_GO=1
    -fjump-tables
    -ftree-switch-conversion
)
