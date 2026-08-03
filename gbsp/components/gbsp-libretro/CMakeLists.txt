set(COMPONENT_SRCDIRS "." "m4a")
set(COMPONENT_ADD_INCLUDEDIRS "." "m4a")
set(COMPONENT_REQUIRES "retro-go")
register_component()
rg_setup_compile_options(
    -O3
    -fomit-frame-pointer -ffast-math
    -DOVERCLOCK_60FPS
    -DROM_BUFFER_SIZE=2
    # M4A software-mixer HLE (m4a/). Nearly every commercial GBA cart links
    # Nintendo's M4A sound library, which mixes its PCM audio on the guest CPU;
    # that one block is 27-60% of the guest instructions the interpreter runs.
    # The block is byte-matched before it is trusted and charges every guest
    # cycle it would have cost, so only the host's work disappears.
    -DGBA_M4A_HLE
    -Wno-unused-variable
    -Wno-implicit-fallthrough
    -Wno-missing-field-initializers
    -Wno-error=unused-label
    -Wno-unused-label
)

# NOTE: -Os on m4a_hle.c was tried and does nothing -- the object is 131 KB at -O3 and
# 131 KB at -Os. It is a transliteration, one C statement per ARM instruction, so there
# are no loops to unroll and no -O3 bulk to give back. gbsp.bin is 1,407,760 bytes with
# the HLE against a 1280 KB partition, so the partition has to grow; an app that
# overflows is not rejected loudly, the bootloader falls through to another app
# (BRINGUP 0.3).
