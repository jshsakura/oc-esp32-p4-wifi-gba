# TGB Dual — Game Boy / Game Boy Color / Super Game Boy emulator core.
#
# The upstream tree is the git submodule at external/tgbdual-go (sylverb's
# fork, pinned to the commit with SGB support), reached through symlinks so
# every path stays relative to the component.
#
# Sources compiled:
#   gb_core/*.cpp     — the emulator itself (CPU, LCD, APU, MBC, SGB, cheat)
#   gbr_interface/*.cpp — graphics resource interface (used by LCD/SGB)
#
# NOT compiled:
#   libretro/libretro.cpp — the libretro frontend; we provide our own in main.
#   libretro/dmy_renderer.cpp — the libretro renderer; we provide rg_renderer.
#   libretro-common/ — not needed (the core has no libretro-common deps).

set(TGBDUAL_CORE_SRCS
    gb_core/tgbdual_apu.cpp
    gb_core/tgbdual_cheat.cpp
    gb_core/tgbdual_cpu.cpp
    gb_core/tgbdual_gb.cpp
    gb_core/tgbdual_lcd.cpp
    gb_core/tgbdual_mbc.cpp
    gb_core/tgbdual_rom.cpp
    gb_core/tgbdual_sgb.cpp
    gbr_interface/gbr.cpp
)

set(COMPONENT_SRCS "${TGBDUAL_CORE_SRCS}")
set(COMPONENT_ADD_INCLUDEDIRS
    "."
    "gb_core"
    "gbr_interface"
    "gw_compat"
)
set(COMPONENT_REQUIRES "retro-go")
register_component()

rg_setup_compile_options(
    -O2
    -fno-exceptions
    -fno-rtti
    -include gw_compat.h
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-unused-value
    -Wno-sign-compare
    -Wno-parentheses
    -Wno-implicit-fallthrough
    -Wno-missing-field-initializers
    -Wno-format
    -Wno-pointer-to-int-cast
    -Wno-int-to-pointer-cast
    -Wno-array-bounds
    -Wno-enum-compare
    -Wno-narrowing
    -Wno-error=sequence-point
    -Wno-error=char-subscripts
    -Wno-error=address
    -Wno-error=maybe-uninitialized
    -Wno-error=stringop-overflow
    -Wno-error=implicit-function-declaration
)
