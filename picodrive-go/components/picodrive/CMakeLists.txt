# PicoDrive -- Mega Drive / Sega CD / 32X, from the jshsakura/picodrive gnw-port fork.
#
# The fork is at external/picodrive, reached through the "src" symlink so every
# path here stays relative to the component.
#
# We deliberately do NOT define GNW_32X_CORE.
#
# That switch is the Game & Watch build's configuration, and among other things
# it compiles the whole Sega CD subsystem out -- PicoCdCheck() returns -1
# unconditionally, cdd_unload() is skipped, and pico/cd/ is not in the source
# list at all (see the #ifdef in pico/media.c). It was the right trade there:
# that device could not fit Sega CD's resident RAM anyway, so carrying the code
# was pure cost. It is the wrong trade here, where PSRAM is 32MB, and the
# guards are clean enough that leaving the define out simply restores upstream's
# own CD path rather than needing anything unpicked.
#
# Sega CD first, 32X later: 32X was removed from the Game & Watch firmware on
# measurement, not on principle -- a frame cost ~24,000,000 cycles against a
# 5,197,920 budget. This part is faster and has a 2D-DMA the M7 did not, but 4.6x
# is not a gap that closes by itself, so it gets its own measurement before it
# gets a launcher entry.
set(PICO_SRCS
    "src/pico/cart.c"
    "src/pico/memory.c"
    "src/pico/draw.c"
    "src/pico/draw2.c"
    "src/pico/sek.c"
    "src/pico/videoport.c"
    "src/pico/media.c"
    "src/pico/pico.c"
    "src/pico/misc.c"
    "src/pico/patch.c"
    "src/pico/z80if.c"
    "src/pico/eeprom.c"
    "src/pico/state.c"
    "src/pico/sound/sound.c"
    "src/pico/sound/mix.c"
    "src/pico/sound/sn76496.c"
    "src/pico/sound/ym2612.c"
    "src/pico/sound/resampler.c"
    # YM2413 is the Master System's FM add-on, not anything a Mega CD needs -- but
    # sound.c includes its header unconditionally, so it comes along. The fork left
    # pico/sound/emu2413 empty because it is a git submodule
    # (digital-sound-antiques/emu2413) that was never initialised; it is cloned in
    # place rather than stubbed, because a stub here would silently mute a system
    # we do intend to support.
    # ym2413.h uses picodrive's own short type names (s16/s32) without pulling in
    # pico_types.h, so it only compiles from inside the core where pico_int.h has
    # already been included. Forcing that header in keeps the file honest without
    # editing upstream.
    "src/pico/sound/ym2413.c"
    "src/pico/sound/emu2413/emu2413.c"
)

# Sega Pico (the toy computer, not the emulator). Not a system we expose, but
# videoport.c and sek.c call into it unconditionally -- PicoPicoIrqAck and its
# neighbours are reached through the shared VDP and 68000 paths rather than
# behind a PAHW test, so leaving these out is a link error, not a smaller build.
# Cartridge hardware: the mappers and add-on chips that live on the cart itself
# (J-Cart, Realtec, the SF-00x protections, SPI EEPROM). cart.c dispatches to all
# of them by name from its built-in table, so they link or nothing does.
#
# SVP (Virtua Racing's DSP) is left out on purpose: its accelerated path is ARM
# assembly (carthw/svp/stub_arm.S) and the C interpreter is a system of its own to
# get right. One game, and it can be added once there is something to measure.
# 32X. Compiled in, but not offered in the launcher yet.
#
# It is here because it cannot cleanly be left out -- draw.c and memory.c reach
# 32X entry points (FinalizeLine32xRGB555, p32x_pwm_update) from the shared paths
# rather than behind a PAHW test, so excluding the directory is a link error.
# Since it has to link anyway, it may as well be measurable.
#
# It is NOT in the launcher because the Game & Watch port removed 32X on
# measurement: a frame cost ~24,000,000 cycles against a 5,197,920 budget, 4.6x
# over. This part is faster and has a 2D-DMA that one did not, but 4.6x does not
# close by itself. It gets an entry when it has a number, not before.
set(PICO_32X_SRCS
    "src/pico/32x/32x.c"
    "src/pico/32x/draw.c"
    "src/pico/32x/memory.c"
    "src/pico/32x/pwm.c"
    "src/pico/32x/sh2soc.c"
    "src/cpu/sh2/sh2.c"
    # mame/sh2.c is a body, not a translation unit -- sh2pico.c is the wrapper that
    # includes it with the right typedefs. Compiling it directly gives a page of
    # "unknown type name sh2_state".
    "src/cpu/sh2/mame/sh2pico.c"
)

# Master System. Not offered here either -- retro-core has its own SMS core -- but
# media.c and pico.c call PicoFrameMS/PicoPowerMS from the shared entry points, so
# it links or nothing does. Same shape as 32X and SVP below: PicoDrive's add-ons
# are reached through common paths rather than behind hardware tests, which makes
# "compile only what we offer" a link error rather than a smaller binary.
set(PICO_SMS_SRCS
    "src/pico/sms.c"
    "src/pico/mode4.c"
)

# SVP -- Virtua Racing's on-cart DSP. cart.c references PicoSVPInit from its
# hardware table. Only the C parts: compiler.c is an ARM recompiler and
# stub_arm.S is ARM assembly, neither of which can run on RISC-V. The ssp16
# interpreter is portable and is what will actually execute.
set(PICO_SVP_SRCS
    "src/pico/carthw/svp/svp.c"
    "src/pico/carthw/svp/ssp16.c"
    "src/pico/carthw/svp/memory.c"
)

# Zipped ROMs. cart.c calls openzip/closezip unconditionally.
set(PICO_ZIP_SRCS
    "src/unzip/unzip.c"
)

set(PICO_CARTHW_SRCS
    "src/pico/carthw/carthw.c"
    "src/pico/carthw/eeprom_spi.c"
)

set(PICO_PICO_SRCS
    "src/pico/pico/pico.c"
    "src/pico/pico/memory.c"
    "src/pico/pico/xpcm.c"
)

# The Sega CD subsystem. Upstream's own files, unmodified -- the fork only ever
# hid them behind the define above.
set(PICO_CD_SRCS
    "src/pico/cd/mcd.c"
    "src/pico/cd/memory.c"
    "src/pico/cd/sek.c"
    "src/pico/cd/cdc.c"
    "src/pico/cd/cdd.c"
    "src/pico/cd/cd_image.c"
    "src/pico/cd/cd_parse.c"
    "src/pico/cd/cell_map.c"
    "src/pico/cd/gfx.c"
    "src/pico/cd/gfx_dma.c"
    "src/pico/cd/misc.c"
    "src/pico/cd/pcm.c"
    "src/pico/cd/megasd.c"
)

# The 68000 is FAME/C, not the fork's gwenesis68k -- and that choice is forced by
# Sega CD, not preference.
#
# The gwenesis backend is faster (a const-table Musashi fork) and is what the
# Game & Watch build used, but it keeps ONE global CPU context. The fork says so
# itself, in pico_int.h next to the EMU_G68K block: "Sega CD sub-68k is NOT
# supported by this backend ... PAHW_MCD must not be used at runtime with
# EMU_G68K." A Mega CD has two 68000s, so that backend cannot run one at all.
#
# Of the two portable multi-context backends, FAME needs no code generation step
# (Musashi's m68kops.c has to be produced by m68kmake first, and the fork does
# not ship it), which is the whole of why it is this one and not that one.
#
# Z80 is cz80 for the same reason the fork chose it: DrZ80 is ARM assembly.
set(PICO_CPU_SRCS
    "src/cpu/fame/famec.c"
    "src/cpu/cz80/cz80.c"
)

# cart.c and state.c include <zlib.h> for zipped ROMs and compressed savestates.
# ESP-IDF has no zlib, so the fork's bundled copy comes along -- only the inflate
# side, since nothing here writes a zip.
set(PICO_ZLIB_SRCS
    "src/zlib/adler32.c"
    "src/zlib/crc32.c"
    "src/zlib/inffast.c"
    "src/zlib/inflate.c"
    "src/zlib/inftrees.c"
    "src/zlib/zutil.c"
)

# The platform layer PicoDrive expects its host to supply (lprintf, the video
# mode hook, the MP3 stubs). It lives in THIS component, not in main/, because
# main depends on picodrive: put it there and the linker meets libmain.a before
# the archive that needs those symbols, and reports them undefined even though
# they are plainly present in libmain.a. Inside the archive that uses them, the
# ordering question does not arise.
set(PICO_PLATFORM_SRCS "platform.c")

set(COMPONENT_SRCS ${PICO_SRCS} ${PICO_CD_SRCS} ${PICO_PICO_SRCS} ${PICO_CARTHW_SRCS} ${PICO_32X_SRCS} ${PICO_ZIP_SRCS} ${PICO_SMS_SRCS} ${PICO_SVP_SRCS} ${PICO_CPU_SRCS} ${PICO_ZLIB_SRCS} ${PICO_PLATFORM_SRCS})
set(COMPONENT_ADD_INCLUDEDIRS "src" "src/pico" "src/cpu" "src/zlib" "src/pico/sound" "src/unzip")
set(COMPONENT_REQUIRES "retro-go")
set(COMPONENT_ADD_LDFRAGMENTS "linker.lf")
register_component()

rg_setup_compile_options(
    -O2
    -DEMU_F68K
    -include
    pico_types.h
    -D_USE_CZ80
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-unused-value
    -Wno-sign-compare
    -Wno-implicit-fallthrough
    -Wno-missing-field-initializers
    -Wno-parentheses
)
