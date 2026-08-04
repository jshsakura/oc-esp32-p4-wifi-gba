/*
 * SNES for retro-go, on the `sm` core.
 *
 * This replaces nothing yet -- retro-core still carries snes9x, which arrived in
 * this repo's initial commit from the ximzi base. It exists because that core
 * runs the SNES at 26-28 fps here (36 ms against a 16.7 ms budget), and the
 * reason is not that snes9x is badly tuned: it is a different, heavier emulator.
 * The Game & Watch port reached 46 fps on the same games with THIS core, on a
 * 280 MHz single-core M7. The gap is the core, not the optimisation.
 *
 * There are two ports of this core in that repo and they are not
 * interchangeable. Core/Src/porting/sm/ runs a DECOMPILED Super Metroid next to
 * the emulator: the game is native code, so it drives the CPU itself and hands
 * the emulator interrupts. Core/Src/porting/snes/ is the generic emulator that
 * plays arbitrary ROMs, and it is the one the ledger's 46 fps belongs to. This
 * file follows the latter -- see the frame driver below, which is where the
 * distinction stops being academic.
 *
 * Two things are deliberately not carried over from that port:
 *
 *   TARGET_GNW is not defined. It switches the PPU to a per-line hand-off
 *   because that device had no room for a 256x224 staging buffer, and it makes
 *   snes_loadRom expect a memory-mapped cart. Neither constraint exists here --
 *   PSRAM is 32MB -- so the PPU renders straight into the frame surface through
 *   renderPitch, which is the arrangement the core was written for.
 *
 *   The thumb2/ directory is not built. Those are hand-written ARM assembly
 *   interpreters for the 65816 and the SPC700; on RISC-V there is nothing to
 *   run. The C interpreters they replace are still there and are what executes.
 *   spin_skip.c IS built -- it is portable C, and the ledger records it as the
 *   lever that beat static recompilation on that device (46 fps against rc's 44).
 */
#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "snes/snes.h"
#include "snes/ppu.h"
#include "snes/cpu.h"
#include "snes/dma.h"
#ifdef SNES_SPIN_SKIP
#include "snes/spin_skip.h"
#endif

#define SNES_W       256
#define SNES_H       224
/* The core's own rate. snes.c produces this many samples per field. */
#define SNES_RATE    32000
#define SNES_MAX_SAMPLES  (SNES_RATE / 50 + 32)

/* WRAM is the guest's 128KB. The core takes it from the caller rather than
 * allocating, so that a port can decide where it lives -- here, PSRAM. */
#define SNES_WRAM_SIZE  0x20000

static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;
static rg_app_t *app;
static Snes *snes;

/* The core's glue (components/sm/glue.c) needs the machine to route APU port
 * writes to, and it is linked before main -- so it asks rather than reaching. */
Snes *sm_get_snes(void) { return snes; }
static uint8_t *snes_wram;
static int16_t *sampleBuf;

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

static bool reset_handler(bool hard)
{
    snes_reset(snes, hard);
    return true;
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(currentUpdate, 0);
}

/* The core reads the pad through snes->input1->currentState. The layout is
 * LakeSnes' auto-joypad order and it counts up from bit 0, not down from bit 15:
 * 0=B 1=Y 2=Select 3=Start 4=Up 5=Down 6=Left 7=Right 8=A 9=X 10=L 11=R. */
static uint16_t read_input(void)
{
    uint32_t joystick = rg_input_read_gamepad();
    uint16_t pad = 0;
    if (joystick & RG_KEY_B)      pad |= 1u << 0;
    if (joystick & RG_KEY_Y)      pad |= 1u << 1;
    if (joystick & RG_KEY_SELECT) pad |= 1u << 2;
    if (joystick & RG_KEY_START)  pad |= 1u << 3;
    if (joystick & RG_KEY_UP)     pad |= 1u << 4;
    if (joystick & RG_KEY_DOWN)   pad |= 1u << 5;
    if (joystick & RG_KEY_LEFT)   pad |= 1u << 6;
    if (joystick & RG_KEY_RIGHT)  pad |= 1u << 7;
    if (joystick & RG_KEY_A)      pad |= 1u << 8;
    if (joystick & RG_KEY_X)      pad |= 1u << 9;
    if (joystick & RG_KEY_L)      pad |= 1u << 10;
    if (joystick & RG_KEY_R)      pad |= 1u << 11;
    return pad;
}

/* ---- the frame driver ------------------------------------------------------
 *
 * This core has no generic run loop of its own. `sm` is a Super Metroid
 * decompilation first and an emulator second: cpu_runOpcode() is called only
 * from sm_cpu_infra.c, which drives the CPU by running to known PCs
 * (RunCpuUntilPC) because the game it was built for is native code. snes.c keeps
 * cpuCyclesLeft/cpuMemOps/apuCatchupCycles but nothing steps them, and
 * snes_runFrame() is declared in snes.h and defined nowhere.
 *
 * So a frame cannot be driven by waiting for NMI, which is what this file did
 * before: snes_run_line() only raises nmiWanted if the GUEST has enabled NMI
 * (snes.c:266), and with no CPU running, no guest ever does. The board booted,
 * loaded the ROM and span in that loop forever at FPS:0.
 *
 * What follows is the reference port's generic driver, from
 * game-and-watch-retro-go-sd Core/Src/porting/snes/main_snes.c -- the `snes`
 * port, not the `sm` one this file was first modelled on. It interleaves the
 * interpreter with the dot clock: CPU cycles are charged against dots, DMA
 * stalls the CPU, and the APU is caught up in step, which is the accounting
 * snes_handle_pos_stuff() alone does not do.
 *
 * spin-skip is wired in behind SNES_SPIN_SKIP, but it was added only AFTER the
 * driver above was measured working on its own -- the ledger calls it the lever
 * that beat static recompilation, and a lever mixed into the first run that
 * works at all cannot be credited with anything. Its OFF arm is
 * -DSNES_SPIN_SKIP_DEFAULT=false, not an absent -DSNES_SPIN_SKIP. */

/* 32040 Hz * 32 SPC cycles per sample, over the master dots in a 60 Hz frame. */
static const double apuCyclesPerMaster = (32040.0 * 32) / (1364 * 262 * 60.0);

/* Dots until the next position that actually does something. Only 0, 512 and
 * 1024 do work, plus an H-timer IRQ if one is armed -- everything between is a
 * counter increment, and stepping it two dots at a time is most of a frame. */
static int dots_to_next_event(Snes *s)
{
    int h = s->hPos;
    if (h == 0 || h == 512 || h == 1024)
        return 0;
    if (s->hIrqEnabled && h == s->hTimer * 4)
        return 0;
    int next = 1362;
    if (h < 512)       next = 512;
    else if (h < 1024) next = 1024;
    if (s->hIrqEnabled)
    {
        int t = s->hTimer * 4;
        if (t > h && t < next)
            next = t;
    }
    return next - h;
}

static void apply_irq_match(Snes *s)
{
    if (!(s->hIrqEnabled || s->vIrqEnabled))
        return;
    if (s->vIrqEnabled && s->vPos != s->vTimer)
        return;
    if (s->hIrqEnabled && s->hPos != s->hTimer * 4)
        return;
    s->inIrq = true;
    s->cpu->irqWanted = true;
}

/* One interpreted opcode. cpuMemOps is what the memory accesses already charged,
 * so only the remainder becomes dot time.
 *
 * The spin learner is sampled BEFORE the call and told after: `dispatched`
 * records whether this opcode could have been diverted by an interrupt, which is
 * what makes an iteration impure. spin_engaged() is checked once so a parked
 * learner costs two branches rather than the whole pc24/register pack. */
static int run_one_opcode(Snes *s)
{
    Cpu *cpu = s->cpu;
#ifdef SNES_SPIN_SKIP
    const bool learn = spin_engaged();
    uint32_t pc24 = 0;
    int disp = 0;
    if (learn)
    {
        pc24 = ((uint32_t)cpu->k << 16) | cpu->pc;
        disp = (cpu->nmiWanted || (cpu->irqWanted && !cpu->i) || cpu->waiting) && !cpu->stopped;
    }
#endif
    s->cpuMemOps = 0;
    int cycles = cpu_runOpcode(cpu);
    s->cpuCyclesLeft += (cycles - s->cpuMemOps) * 6;
#ifdef SNES_SPIN_SKIP
    if (learn)
        spin_note_real(cpu, pc24, (uint8_t)s->cpuCyclesLeft, disp);
#endif
    return cycles;
}

static void cpu_tick(Snes *s)
{
    if (dma_cycle(s->dma))
        return;
    if (s->cpuCyclesLeft == 0)
        run_one_opcode(s);
    s->cpuCyclesLeft -= 2;
}

/* Advance `dots` master dots, running the CPU against them. */
static void run_dots(Snes *s, int dots)
{
    while (dots > 0)
    {
        if (s->dma->dmaBusy || s->dma->hdmaTimer > 0)
        {
            dma_cycle(s->dma);
            s->apuCatchupCycles += apuCyclesPerMaster * 2.0;
            s->hPos += 2;
            dots -= 2;
            continue;
        }
        bool started_dma = false;
        if (s->cpuCyclesLeft == 0)
        {
#ifdef SNES_SPIN_SKIP
            /* Replay: a learned pure wait-loop iteration is a semantic no-op, so
             * charge the recorded cycle pattern and step the pc along the ring
             * without running the interpreter. Only the NMI handler can change
             * the byte such a loop polls, and no handler can run inside a
             * run_dots span -- so within the span the loop provably cannot exit.
             * Anything that could divert control (pending interrupt, armed IRQ,
             * DMA, a pc that left the ring) drops out to the real interpreter.
             * Falls through to the shared bulk-consume below so hPos and the
             * apuCatchupCycles sequence stay identical to the real path. */
            Cpu *cpu = s->cpu;
            if (g_spin.on &&
                !cpu->nmiWanted && !cpu->irqWanted && !cpu->waiting && !cpu->stopped &&
                !s->hIrqEnabled &&
                !(s->vIrqEnabled && s->vPos == s->vTimer) &&
                (((uint32_t)cpu->k << 16) | cpu->pc) == g_spin.pc[g_spin.idx])
            {
                s->cpuCyclesLeft += g_spin.charge[g_spin.idx];
                g_spin.idx = (g_spin.idx + 1) % g_spin.len;
                cpu->k = (uint8_t)(g_spin.pc[g_spin.idx] >> 16);
                cpu->pc = (uint16_t)g_spin.pc[g_spin.idx];
                g_spin.ops_virtual++;
            }
            else
#endif
            {
                apply_irq_match(s);
                run_one_opcode(s);
                started_dma = s->dma->dmaBusy || s->dma->hdmaTimer > 0;
            }
        }
        /* Consume the opcode's remaining cycles in one step where nothing can
         * observe the difference; a DMA that just started must not be skipped
         * over, because it stalls the CPU from the very next cycle. */
        int step;
        if (s->cpuCyclesLeft >= 2 && !started_dma)
        {
            step = s->cpuCyclesLeft;
            if (step > dots)
                step = dots;
            step &= ~1;
            s->cpuCyclesLeft -= (uint8_t)step;
        }
        else
        {
            step = 2;
            s->cpuCyclesLeft -= 2;
        }
        s->apuCatchupCycles += apuCyclesPerMaster * step;
        s->hPos += step;
        dots -= step;
    }
}

/* One field: run until the dot clock wraps back to the top of frame. */
static void run_frame_events(Snes *s)
{
    for (;;)
    {
        s->apuCatchupCycles += apuCyclesPerMaster * 2.0;
        snes_handle_pos_stuff(s);
        cpu_tick(s);
        if (s->hPos == 0 && s->vPos == 0)
            break;
        run_dots(s, dots_to_next_event(s));
    }
    snes_catchupApu(s);
#ifdef SNES_SPIN_SKIP
    /* Auto-gate: a cart that ends an observation window having replayed almost
     * nothing parks the learner and drops its pattern, so a non-spinning game
     * pays the probe briefly rather than forever. No per-ROM list. */
    spin_frame_tick();
#endif
}

static uint8_t *load_rom(const char *path, uint32_t *out_size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *data = size > 0 ? (uint8_t *)rg_alloc(size, MEM_SLOW) : NULL;
    if (!data || fread(data, 1, size, fp) != (size_t)size)
    {
        free(data);
        data = NULL;
    }
    fclose(fp);
    *out_size = data ? (uint32_t)size : 0;
    return data;
}

/* Point the PPU at the surface we are about to fill. renderPitch is why this is
 * cheap: the renderer writes finished lines straight into the frame buffer, so
 * there is no staging copy between the emulator and the display. */
static void aim_ppu_at(rg_surface_t *surface)
{
    snes->ppu->renderBuffer = (uint8_t *)surface->data;
    snes->ppu->renderPitch = SNES_W * 2;
}

void app_main(void)
{
    const rg_handlers_t handlers = {
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
    };

    app = rg_system_init(SNES_RATE, &handlers, NULL);

    /* Two buffers: rg_display_submit() reads the surface in place on the display
     * task through a one-deep blocking queue, so a single buffer both tears and
     * stalls the emulator every frame. */
    updates[0] = rg_surface_create(SNES_W, SNES_H, RG_PIXEL_565_LE, MEM_FAST);
    updates[1] = rg_surface_create(SNES_W, SNES_H, RG_PIXEL_565_LE, MEM_FAST);
    currentUpdate = updates[0];

    snes_wram = rg_alloc(SNES_WRAM_SIZE, MEM_SLOW);
    sampleBuf = rg_alloc(SNES_MAX_SAMPLES * 2 * sizeof(int16_t), MEM_SLOW);
    if (!snes_wram || !sampleBuf)
        RG_PANIC("Out of memory for SNES state");

    snes = snes_init(snes_wram);
    if (!snes)
        RG_PANIC("snes_init failed");

    uint32_t rom_size = 0;
    uint8_t *rom_data = load_rom(app->romPath, &rom_size);
    if (!rom_data || !snes_loadRom(snes, rom_data, (int)rom_size))
        rg_system_rom_load_failed(_("Could not load the game file."));

    snes_setSamples(snes, sampleBuf, SNES_RATE / 60);
    aim_ppu_at(currentUpdate);
    snes_reset(snes, true);

    if (app->bootFlags & RG_BOOT_RESUME)
        rg_emu_load_state(app->saveSlot);

    long skipFrames = 0;

    while (1)
    {
        uint32_t joystick = rg_input_read_gamepad();

        if (joystick & (RG_KEY_MENU | RG_KEY_OPTION))
        {
            if (joystick & RG_KEY_MENU)
                rg_gui_game_menu();
            else
                rg_gui_options_menu();
        }

        int64_t startTime = rg_system_timer();
        bool drawFrame = !skipFrames;

        snes->input1->currentState = read_input();

        run_frame_events(snes);

        if (drawFrame)
        {
            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
            /* The renderer writes wherever it was last aimed, so the swap has to
             * be told -- otherwise both frames land in the same buffer. */
            aim_ppu_at(currentUpdate);
        }

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit((rg_audio_frame_t *)sampleBuf, SNES_RATE / 60);

        if (skipFrames == 0)
            skipFrames = app->frameskip;
        else if (skipFrames > 0)
            skipFrames--;
    }
}

/* The core aborts through this on an unrecoverable state -- a bad mapper, a
 * savestate it cannot read. It is declared NORETURN, so it must not return:
 * RG_PANIC ends in a crash handler that writes the trace and reboots, which is
 * the closest this firmware has to the reference port's behaviour. */
void Die(const char *error)
{
    RG_PANIC(error ?: "sm core aborted");
    for (;;) { }
}
