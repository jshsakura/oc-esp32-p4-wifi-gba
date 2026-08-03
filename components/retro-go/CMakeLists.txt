set(COMPONENT_SRCDIRS ". drivers/audio fonts libs/netplay libs/lodepng")
set(COMPONENT_ADD_INCLUDEDIRS ". libs/netplay libs/lodepng")
if((IDF_VERSION_MAJOR EQUAL 5) AND (IDF_VERSION_MINOR GREATER 2)) # esp-idf 5.3+
    # esp_lcd is needed by the frame-buffer display drivers (agb_rgb, st7701). It was
    # missing, so both failed to compile the moment they were selected -- the SPI driver
    # was the only one anybody had built.
    # esp_mm is for esp_cache.h, which rg_psram_exec_test.c needs to make code it just wrote
    # as data visible to instruction fetch, and which the PPA blit needs for the same reason
    # in the other direction: the DMA reads memory, not our cache.
    # esp_driver_ppa is the ESP32-P4's 2D-DMA, used by that blit.
    set(COMPONENT_REQUIRES "spi_flash fatfs app_update json nvs_flash esp_adc esp_timer esp_psram esp_wifi esp_http_client esp_driver_ledc esp_lcd esp_mm esp_driver_ppa driver")
elseif(IDF_VERSION_MAJOR EQUAL 5) # esp-idf 5.0 - 5.2
    set(COMPONENT_REQUIRES "spi_flash fatfs app_update json nvs_flash esp_adc esp_timer esp_psram esp_wifi esp_http_client")
else() # esp-idf 4.1 - 4.4
    set(COMPONENT_REQUIRES "spi_flash fatfs app_update esp_adc_cal json nvs_flash esp_wifi esp_http_client")
endif()

# Keep the built-in Korean font in step with translations.h. It is a subset font, so a
# string using a character it does not carry draws as nothing at all -- silently. Done at
# configure time because the generated .c is compiled in this same pass, and listed as a
# configure dependency so editing the translations triggers it.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/translations.h")
execute_process(COMMAND ${CMAKE_COMMAND} -E env python3
                        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/regen_font.py")

register_component()

# Small size is preferred because of the small cache and most things here aren't performance sensitive!
# rg_display and rg_audio benefit from higher optimization (which of -O2 or -O3 is better depends...)

component_compile_options(
    -DLODEPNG_NO_COMPILE_ANCILLARY_CHUNKS
    -DLODEPNG_NO_COMPILE_ERROR_TEXT
    -Os
    -Wno-unused-function
)
set_source_files_properties(
    rg_audio.c rg_display.c rg_utils.c rg_surface.c libs/lodepng/lodepng.c
    PROPERTIES COMPILE_FLAGS
    -O2
)

if(RG_ENABLE_NETPLAY)
    component_compile_options(-DRG_ENABLE_NETWORKING -DRG_ENABLE_NETPLAY)
elseif(RG_ENABLE_NETWORKING)
    component_compile_options(-DRG_ENABLE_NETWORKING)
endif()

if(RG_ENABLE_PROFILING)
    component_compile_options(-DRG_ENABLE_PROFILING)
endif()

# Bench harness -- see the call site in rg_system.c. Both must be given; neither is set in
# any build we ship. It belongs here, next to the code that reads it: this component does
# not call rg_setup_compile_options(), so a definition added there never arrives, and the
# result is a hook that compiles away to nothing without a word of complaint.
if(RG_BENCH_PROFILE_APU)
    component_compile_options(-DRG_BENCH_PROFILE_APU=1)
endif()

if(RG_BENCH_ROM_DIR AND RG_BENCH_ROM_MATCH)
    component_compile_options(
        -DRG_BENCH_ROM_DIR="${RG_BENCH_ROM_DIR}"
        -DRG_BENCH_ROM_MATCH="${RG_BENCH_ROM_MATCH}"
    )
    message(STATUS "RG_BENCH: /sd/roms/${RG_BENCH_ROM_DIR} matching '${RG_BENCH_ROM_MATCH}'")
endif()

if(RG_PROJECT_VER)
    component_compile_options(-DRG_PROJECT_VER="${RG_PROJECT_VER}")
endif()

if(RG_BUILD_RELEASE)
    component_compile_options(-DRG_BUILD_RELEASE=1)
endif()

set(RG_BUILD_INFO "${RG_BUILD_TARGET}-${RG_BUILD_RELEASE} ${PROJECT_VER} SDK:${IDF_VER} ${IDF_TARGET}")
string(TIMESTAMP RG_BUILD_TIME "%s")

component_compile_options(-D${RG_BUILD_TARGET}=1)
component_compile_options(-DRG_PROJECT_APP="${RG_PROJECT_APP}")
component_compile_options(-DRG_BUILD_INFO="${RG_BUILD_INFO}")
component_compile_options(-DRG_BUILD_TIME=${RG_BUILD_TIME})

message(STATUS "RG_PROJECT_APP: ${RG_PROJECT_APP}")
message(STATUS "RG_PROJECT_VER: ${RG_PROJECT_VER}")
message(STATUS "RG_BUILD_INFO: ${RG_BUILD_INFO}")
