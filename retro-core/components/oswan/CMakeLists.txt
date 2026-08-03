# oswan (Bandai WonderSwan / WSC) emulator core.
#
# Build flags:
#   NOSDL_FB        - FrameBuffer is a fixed uint16_t[240*144] array (no SDL).
#   SOUND_EMULATION - enable sound IO register handling in WS.c
#   SOUND_ON        - enable apuWaveSet() calls and apuInit/apuEnd
#   GNW_WSWAN       - skip #include <SDL/SDL.h> in WSApu.c
#
# IMPORTANT: ws_fileio.c and WSFileio.c define the SAME symbols (WsCreate,
# ws_create_from_flash, WsSaveStateToFile, etc.). Only ws_fileio.c is compiled
# — it is the enhanced version with bank mirroring, APU savestate, and
# MemDummy persistence. WSFileio.c is the older upstream duplicate.
set(COMPONENT_SRCS
    "WS.c"
    "WSRender.c"
    "WSApu.c"
    "ws_fileio.c"
    "nec.c"
)
set(COMPONENT_ADD_INCLUDEDIRS "." "cpu")
set(COMPONENT_REQUIRES "retro-go")
set(COMPONENT_ADD_LDFRAGMENTS "linker.lf")
register_component()
rg_setup_compile_options(
    -O2
    -DNOSDL_FB
    -DSOUND_EMULATION
    -DSOUND_ON
    -DGNW_WSWAN
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-unused-value
    -Wno-sign-compare
    -Wno-parentheses
    -Wno-implicit-fallthrough
    -Wno-missing-field-initializers
    -Wno-pointer-to-int-cast
    -Wno-int-to-pointer-cast
    -Wno-format
)
