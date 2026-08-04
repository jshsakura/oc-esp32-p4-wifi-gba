/*
 * The symbols this core expects its host to define.
 *
 * They live in the component rather than in main/ on purpose: main depends on
 * sm, so the linker meets libmain.a before the archive that needs these and
 * reports them undefined even when they are plainly present. Inside the archive
 * that uses them, the ordering question does not arise. (Learned the same way on
 * picodrive, one core over.)
 *
 * All three exist because this core is not only an emulator. It is also the
 * runtime for a Super Metroid decompilation, and the reference port runs that
 * native code alongside the emulated machine, switching between them. There is
 * no native game here -- the guest is a plain ROM -- so each of these takes the
 * plain-emulator branch of a decision the reference makes at runtime.
 */
#include <rg_system.h>

#include "snes/snes.h"
#include "snes/apu.h"

/* The fast line renderer, which is the one the ledger's PPU work went into.
 * The reference port sets this true and so do we; false selects the old
 * scanline path that nothing here wants. */
bool g_new_ppu = true;

/* Set by the decompiled game's own assertions when its state and the emulated
 * machine's disagree. With no native game running, nothing ever sets it -- but
 * cpu_infra reads it, so it has to exist. */
bool g_fail;

/* APU port writes ($2140-$2143, reached as B-bus $2140+).
 *
 * In the reference this is a fork in the road: with the native sound player
 * armed the write goes to that, otherwise to the emulated SPC700. Here there is
 * only ever the emulated one, so this is the straight path -- and it is worth
 * knowing this is exactly where an N-SPC HLE would be wired in, if the
 * feasibility question in docs/SNES_APU_HLE_FEASIBILITY.md is ever reopened.
 */
extern Snes *sm_get_snes(void);

void RtlApuWrite(uint32_t adr, uint8_t val)
{
    Snes *snes = sm_get_snes();
    if (snes)
        apu_cpuWrite(snes->apu, (uint16_t)(adr & 0xffff), val);
}

/* ---- the interpreter's two escape hatches ----------------------------------
 *
 * These went undefined the moment main started calling cpu_runOpcode(): until
 * then nothing referenced cpu_doOpcode, so --gc-sections dropped the whole
 * opcode body and the references with it. The build "succeeding" before was the
 * linker discarding the emulator, not the emulator being complete.
 *
 * Both are the decompilation's way back into native code. BRK is how the
 * reference hands an opcode to the rewritten game, and the RTS/RTL gate fires
 * only when cpu->spBreakpoint has been armed, which only sm_cpu_infra.c does.
 * With a plain ROM there is no native side to jump to, so both take the
 * emulate-it-normally branch -- the same stubs the reference port's generic
 * emulator uses (porting/snes/main_snes.c:67-68). */
int CpuOpcodeHook(uint32_t addr)
{
    (void)addr;
    return 0;   /* 0 = run the opcode as read */
}

bool HookedFunctionRts(int is_long)
{
    (void)is_long;
    return false;   /* no hooked function to return from */
}

/* ---- the static recompiler, which is not built -----------------------------
 *
 * rc_dispatch.c is deliberately absent (see CMakeLists): the ledger closes rc as
 * a dead road on the reference device -- 46 fps with spin-skip against rc's 44,
 * and 3.5 via XIP. But cpu_runOpcode consults g_rc_active on every opcode
 * regardless, so the flag and the table it guards still have to resolve.
 *
 * False here means the branch is never taken and the lookup is never reached,
 * which is exactly the state the reference port leaves a non-SMW ROM in. */
bool g_rc_active = false;
void (**g_rc_fns)(struct Cpu *) = NULL;

uint16_t rc_dispatch_lookup(uint8_t bank, uint16_t pc)
{
    (void)bank;
    (void)pc;
    return 0;
}
