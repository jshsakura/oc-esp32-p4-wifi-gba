set(COMPONENT_SRCDIRS "src")
set(COMPONENT_ADD_INCLUDEDIRS "." "src")
set(COMPONENT_REQUIRES "retro-go")
set(COMPONENT_PRIV_REQUIRES "esp_driver_ppa")
set(COMPONENT_ADD_LDFRAGMENTS "linker.lf")
register_component()
rg_setup_compile_options(
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-array-bounds
    -Wno-implicit-fallthrough
    -DRIGHTSHIFT_IS_SAR
    -DFAST_LSB_WORD_ACCESS
    -DNO_ZERO_LUT
    -O3
    -ffunction-sections
    -fdata-sections
    -fomit-frame-pointer
)

# Bench only -- see the call site in src/cpuexec.c. Skips the SPC700/DSP so the work can be
# priced; the front-end's audio toggle does not reach that code.
if(RG_BENCH_PROFILE_APU)
    component_compile_options(-DRG_BENCH_PROFILE_APU=1)
endif()
