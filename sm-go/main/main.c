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

/* The core reads the pad through snes->input1->currentState, a SNES bit layout:
 * B Y Select Start Up Down Left Right A X L R, from bit 15 down. */
static uint16_t read_input(void)
{
    uint32_t joystick = rg_input_read_gamepad();
    uint16_t pad = 0;
    if (joystick & RG_KEY_B)      pad |= 0x8000;
    if (joystick & RG_KEY_Y)      pad |= 0x4000;
    if (joystick & RG_KEY_SELECT) pad |= 0x2000;
    if (joystick & RG_KEY_START)  pad |= 0x1000;
    if (joystick & RG_KEY_UP)     pad |= 0x0800;
    if (joystick & RG_KEY_DOWN)   pad |= 0x0400;
    if (joystick & RG_KEY_LEFT)   pad |= 0x0200;
    if (joystick & RG_KEY_RIGHT)  pad |= 0x0100;
    if (joystick & RG_KEY_A)      pad |= 0x0080;
    if (joystick & RG_KEY_X)      pad |= 0x0040;
    if (joystick & RG_KEY_L)      pad |= 0x0020;
    if (joystick & RG_KEY_R)      pad |= 0x0010;
    return pad;
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
        snes_runFrame(snes);

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
