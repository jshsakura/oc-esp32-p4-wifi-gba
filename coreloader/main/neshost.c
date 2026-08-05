/*
 * The same host, a different core.
 *
 * gbhost.c proved a core can be a file. This proves the HOST is not written
 * around one core: nofrendo arrives the same way, through the same loader, with
 * its own generated symbol table and its own API table, and the only
 * core-specific code is the twenty lines that drive it -- exactly the role
 * retro-core/main/main_nes.c plays for the linked build.
 *
 * That is the shape the product wants: one host, cores as files, a small driver
 * per system.
 */
#include <rg_system.h>
#include <rg_storage.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_elf.h"
#include "private/elf_symbol.h"

#include "nofrendo.h"
#include "nes/nes.h"
#include "nes/apu.h"
#include "nofrendo_module.h"

#define AUDIO_SAMPLE_RATE (32000)

extern const uint8_t nesmod_start[] asm("_binary_nofrendo_module_app_elf_start");
extern const uint8_t nesmod_end[]   asm("_binary_nofrendo_module_app_elf_end");

extern const struct esp_elfsym nes_host_symbols[];

static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;
static nes_t *nes;
static bool slowFrame = false;

static void blit_screen(uint8 *bmp)
{
    currentUpdate->width = NES_SCREEN_WIDTH;
    currentUpdate->height = NES_SCREEN_HEIGHT;
    currentUpdate->offset = 8;
    slowFrame = !rg_display_sync(false);
    rg_display_submit(currentUpdate, 0);
}

void neshost_run(void)
{
    size_t modsize = nesmod_end - nesmod_start;
    rg_app_t *app = rg_system_get_app();
    RG_LOGW("NESHOST: nofrendo module is %u bytes, ROM '%s'", (unsigned)modsize,
            app->romPath ?: "(none)");

    int rc = esp_elf_register_symbol(nes_host_symbols);
    if (rc != 0 && rc != -EEXIST)
    {
        RG_LOGE("NESHOST: symbol table rejected, rc=%d", rc);
        return;
    }

    /* Core from the card, seeded once from the embedded copy -- same as gbhost. */
    const char *core_path = RG_BASE_PATH "/cores/nofrendo.elf";
    const uint8_t *image = nesmod_start;
    void *filebuf = NULL;
    size_t filelen = 0;

    rg_storage_mkdir(RG_BASE_PATH "/cores");
    if (!rg_storage_exists(core_path))
    {
        RG_LOGW("NESHOST: seeding %s", core_path);
        rg_storage_write_file(core_path, nesmod_start, modsize, 0);
    }
    if (rg_storage_read_file(core_path, &filebuf, &filelen, 0) && filelen == modsize)
    {
        RG_LOGW("NESHOST: loaded core from %s (%u bytes)", core_path, (unsigned)filelen);
        image = (const uint8_t *)filebuf;
    }

    static esp_elf_t elf;
    if ((rc = esp_elf_init(&elf)) != 0)
    {
        RG_LOGE("NESHOST: esp_elf_init failed, rc=%d", rc);
        return;
    }

    int64_t t0 = rg_system_timer();
    if ((rc = esp_elf_relocate(&elf, image)) != 0)
    {
        RG_LOGE("NESHOST: relocate failed, rc=%d", rc);
        return;
    }
    RG_LOGW("NESHOST: relocated in %lld us", rg_system_timer() - t0);
    free(filebuf);

    nofrendo_module_api_t *api = NULL;
    char *argv[1] = {(char *)&api};
    if ((rc = esp_elf_request(&elf, 0, 1, argv)) != 0 || !api ||
        api->abi_version != NOFRENDO_MODULE_ABI_VERSION)
    {
        RG_LOGE("NESHOST: bad API table (rc=%d, %p)", rc, api);
        return;
    }
    RG_LOGW("NESHOST: API table at %p, abi %u -- core is live", api, api->abi_version);

    updates[0] = rg_surface_create(NES_SCREEN_PITCH, NES_SCREEN_HEIGHT, RG_PIXEL_PAL565_BE, MEM_SLOW);
    updates[1] = rg_surface_create(NES_SCREEN_PITCH, NES_SCREEN_HEIGHT, RG_PIXEL_PAL565_BE, MEM_SLOW);
    currentUpdate = updates[0];

    nes = api->nes_init(SYS_DETECT, AUDIO_SAMPLE_RATE, true, NULL);
    if (!nes)
    {
        RG_LOGE("NESHOST: nes_init failed");
        return;
    }
    api->nes_setvidbuf(currentUpdate->data);

    if (api->nes_loadfile(app->romPath) < 0)
    {
        RG_LOGE("NESHOST: core could not load the ROM");
        return;
    }
    nes->blit_func = blit_screen;

    /* main_nes.c does this twice before the loop and says it is needed for state
     * restoration; kept so the two are the same core doing the same thing. */
    api->nes_emulate(false);
    api->nes_emulate(false);
    RG_LOGW("NESHOST: running");

    /* Paced exactly like retro-core/main/main_nes.c. The audio submit is not
     * optional garnish -- that file says outright "Audio is used to pace
     * emulation", and without it the loop free-runs and reports BUSY 100%
     * whatever the core actually costs. Learned twice today: measure the loop
     * before you measure the thing. */
    int skipFrames = 0;

    while (1)
    {
        uint32_t joystick = rg_input_read_gamepad();
        int buttons = 0;
        if (joystick & RG_KEY_UP)     buttons |= NES_PAD_UP;
        if (joystick & RG_KEY_RIGHT)  buttons |= NES_PAD_RIGHT;
        if (joystick & RG_KEY_DOWN)   buttons |= NES_PAD_DOWN;
        if (joystick & RG_KEY_LEFT)   buttons |= NES_PAD_LEFT;
        if (joystick & RG_KEY_SELECT) buttons |= NES_PAD_SELECT;
        if (joystick & RG_KEY_START)  buttons |= NES_PAD_START;
        if (joystick & RG_KEY_A)      buttons |= NES_PAD_A;
        if (joystick & RG_KEY_B)      buttons |= NES_PAD_B;

        int64_t startTime = rg_system_timer();
        bool drawFrame = !skipFrames;

        if (drawFrame)
        {
            currentUpdate = updates[currentUpdate == updates[0]];
            api->nes_setvidbuf(currentUpdate->data);
        }
        api->input_update(0, buttons);
        api->nes_emulate(drawFrame);

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit((void *)nes->apu->buffer, nes->apu->samples_per_frame);

        if (skipFrames == 0)
        {
            int frameTime = 1000000 / (nes->refresh_rate * app->speed);
            int elapsed = rg_system_timer() - startTime;
            if (app->frameskip > 0)
                skipFrames = app->frameskip;
            else if (elapsed > frameTime + 1500)
                skipFrames = 1;
            else if (drawFrame && slowFrame)
                skipFrames = 1;
        }
        else if (skipFrames > 0)
        {
            skipFrames--;
        }
    }
}
