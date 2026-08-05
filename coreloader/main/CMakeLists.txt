set(COMPONENT_SRCDIRS ".")
# Cores are read from /sd/retro-go/cores/. The embedded copies exist only to seed
# that directory on a device whose card cannot be reached -- this board's is
# sealed inside the case. A device that can be loaded from a PC needs neither.
set(COMPONENT_EMBED_FILES "gnuboy_module.app.elf" "nofrendo_module.app.elf")
# The core headers are read-only here: coreloader needs the types its drivers
# speak (gb_video_fmt_t, nes_t, ...), not the cores themselves.
set(COMPONENT_ADD_INCLUDEDIRS "."
    "../../retro-core/components/gnuboy"
    "../../retro-core/components/nofrendo"
    "../../retro-core/components/nofrendo/nes")
set(COMPONENT_REQUIRES "retro-go" "espressif__elf_loader")
register_component()
rg_setup_compile_options(-O2 -Wno-unused-variable)
