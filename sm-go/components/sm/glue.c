/*
 * The three symbols this core expects its host to define.
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
