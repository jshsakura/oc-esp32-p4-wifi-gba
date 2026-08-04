set(COMPONENT_SRCDIRS ".")
set(COMPONENT_ADD_INCLUDEDIRS ".")
set(COMPONENT_REQUIRES "retro-go" "sm")
register_component()
# If SNES_SPIN_SKIP is ever turned back on, it has to be defined HERE as well as
# in the sm component: the purity hooks live in cpu.c, but the replay branch and
# the per-opcode notify live in the frame driver in main.c. Defining it on only
# one side buys the tax without the saving, silently.
#
# It is off today because it measured slower -- see the numbers in
# components/sm/CMakeLists.txt.
rg_setup_compile_options(-O2 -Wno-unused-variable)
