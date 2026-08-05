set(CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../retro-core/components/pce-go")
set(RETRO_GO_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../components/retro-go")

file(GLOB CORE_SRCS "${CORE_DIR}/*.c")

idf_component_register(
    SRCS "pce_go_module.c" ${CORE_SRCS}
    INCLUDE_DIRS "." "${CORE_DIR}" "${RETRO_GO_DIR}"
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -DRG_TARGET_ESP32P4=1
    -DRETRO_GO=1
    -fjump-tables
    -ftree-switch-conversion
    -Wno-sequence-point
    -Wno-unused
    -Wno-error=format
)
