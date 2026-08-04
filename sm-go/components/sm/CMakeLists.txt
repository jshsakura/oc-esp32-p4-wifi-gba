# The `sm` SNES core (jshsakura/sm, gnw-port branch).
#
# The file list is the one the Game & Watch build uses, minus the parts that
# cannot cross to RISC-V:
#
#   thumb2/ -- hand-written ARM assembly interpreters for the 65816 and the
#   SPC700. There is nothing to run them on here. The C interpreters they replace
#   are still in cpu.c and spc.c and are what executes.
#
#   rc_dispatch.c -- the static recompiler's dispatch. The ledger closes rc as a
#   dead road on the reference device (46 fps with spin-skip against rc's 44, and
#   3.5 fps via XIP), so it is not carried over rather than ported and re-tested.
#
# spin_skip.c IS built. It is portable C and it is the lever the ledger records
# as beating static recompilation -- 56.6% of SMW's instructions live in spins.
set(SM_SRCS
    "src/snes/cart.c"
    "src/snes/cpu.c"
    "src/snes/dma.c"
    "src/snes/dsp.c"
    "src/snes/dsp1_hle.c"
    "src/snes/input.c"
    "src/snes/ppu.c"
    "src/snes/snes.c"
    "src/snes/snes_other.c"
    "src/snes/spc.c"
    "src/snes/spin_skip.c"
    "src/snes/apu.c"
)

set(COMPONENT_SRCS ${SM_SRCS} "glue.c")
set(COMPONENT_ADD_INCLUDEDIRS "src" "src/snes")
set(COMPONENT_REQUIRES "retro-go")
register_component()

# PPU_RGB565 makes the compositor emit 565 directly instead of the core's own
# 5-bit-per-channel format, which is what the display path here wants -- without
# it every frame would need a conversion pass between the renderer and the
# surface, which is exactly the staging copy renderPitch exists to avoid.
rg_setup_compile_options(
    -O2
    -DPPU_RGB565
    # SNES_SPIN_SKIP is deliberately NOT defined, and that is a measurement, not
    # an oversight. Defining it arms the purity hooks in cpu.c's cpu_read and
    # cpu_write -- the two hottest functions in the interpreter -- and on this
    # board that costs more than the replay saves. Measured on device against
    # /sd/roms/snes/snes.smc, A/B/A interleaved, us/frame:
    #
    #   not defined ................................. 32,986 / 33,043
    #   defined, learner on (auto-gate deciding) .... 34,296 / 34,234
    #   defined, learner gated off by DEFAULT=false . 34,296 / 34,358
    #
    # The last row is the informative one: gating the learner off recovers
    # nothing, so the cost is the compiled-in `if (g_spin.phase)` on every memory
    # access, not the learning. That is a per-access tax an in-order core pays in
    # full, and no runtime gate can refund it.
    #
    # The wiring stays behind the ifdefs (main.c) so this can be retried -- the
    # ledger measured 56.6% of SMW's opcodes inside spin loops on the reference
    # device, so a different ROM may well repay it. Re-measure, do not assume.
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-unused-value
    -Wno-sign-compare
    -Wno-implicit-fallthrough
    -Wno-missing-field-initializers
    -Wno-parentheses
    # This core is written against MSVC and 32-bit ARM as well as GCC, and this
    # toolchain is stricter than either: uint32_t is long unsigned here so %x
    # trips -Wformat, and the tree carries MSVC #pragma warning plus nested /*
    # in comments. All three are diagnostics about portability of the SOURCE,
    # not about the code being wrong, and none of them is ours to fix upstream.
    -Wno-format
    -Wno-unknown-pragmas
    -Wno-comment
)
