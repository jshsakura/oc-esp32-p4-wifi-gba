/*
 * A Game Boy that loads its emulator core at runtime.
 *
 * This is the whole runtime-loading design, end to end, on real hardware: no
 * gnuboy code is linked into this firmware. The core arrives as a relocatable
 * ELF, elf_loader places it and binds its imports against the symbol table
 * below, and the host drives it through a function table the module hands back.
 *
 * If this runs a game, the partition ceiling stops being a constraint on how
 * many systems the device can have -- cores become files, not firmware images.
 *
 * Built only when RG_ELF_SPIKE is defined; copied into an app's main/ to run.
 * The module blob is embedded rather than read from SD only because this
 * board's card is inside the case; esp_elf_relocate() takes a buffer either way,
 * so the loader path is identical.
 */
#include <rg_system.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_elf.h"
#include "private/elf_symbol.h"

#include "gnuboy.h"
#include "gnuboy_module.h"

#define AUDIO_SAMPLE_RATE   (32000)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 50 + 1)

extern const uint8_t gbmod_start[] asm("_binary_gnuboy_module_app_elf_start");
extern const uint8_t gbmod_end[]   asm("_binary_gnuboy_module_app_elf_end");

static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;

/* ---- the symbol table -------------------------------------------------------
 *
 * Everything the module leaves unresolved, and nothing else. gnuboy needs 18:
 * libc, four libgcc soft-double helpers, and exactly one framework call.
 *
 * elf_loader can supply libc itself via CONFIG_ELF_LOADER_LIBC_SYMBOLS, but the
 * table is written out in full deliberately -- a core's imports are a contract
 * with the host, and a contract that depends on which Kconfig options happened
 * to be set is not one. This is also the list that a real implementation would
 * generate with the component's tool/symbols.py.
 *
 * The libgcc entries are the tell that the module is genuinely separate code:
 * gnuboy's RTC arithmetic uses doubles, RISC-V has no hardware double here, so
 * the module calls into the host's soft-float routines.
 */
extern double __adddf3(double, double);
extern double __divdf3(double, double);
extern double __floatsidf(int);
extern int __fixdfsi(double);

static esp_elf_symbol_table_t gb_host_symbols[] = {
    { "memcpy",       (const void *)&memcpy },
    { "memset",       (const void *)&memset },
    { "malloc",       (const void *)&malloc },
    { "calloc",       (const void *)&calloc },
    { "free",         (const void *)&free },
    { "abort",        (const void *)&abort },
    { "rand",         (const void *)&rand },
    { "fopen",        (const void *)&fopen },
    { "fread",        (const void *)&fread },
    { "fwrite",       (const void *)&fwrite },
    { "fseek",        (const void *)&fseek },
    { "fclose",       (const void *)&fclose },
    { "feof",         (const void *)&feof },
    { "__adddf3",     (const void *)&__adddf3 },
    { "__divdf3",     (const void *)&__divdf3 },
    { "__floatsidf",  (const void *)&__floatsidf },
    { "__fixdfsi",    (const void *)&__fixdfsi },
    { "rg_system_log", (const void *)&rg_system_log },
    ESP_ELFSYM_END,
};

/* ---- the core's callbacks --------------------------------------------------
 * Called BY the loaded module, INTO this firmware. The reverse direction of the
 * symbol table, and the one that makes a core usable rather than merely
 * loadable. */
static void video_callback(void *buffer)
{
    rg_display_submit(currentUpdate, 0);
}

static void audio_callback(void *buffer, size_t length)
{
    rg_audio_submit(buffer, length >> 1);
}

void gbhost_run(void)
{
    size_t modsize = gbmod_end - gbmod_start;
    RG_LOGW("GBHOST: gnuboy module is %u bytes, ROM '%s'", (unsigned)modsize,
            rg_system_get_app()->romPath ?: "(none)");

    int rc = esp_elf_register_symbol(gb_host_symbols);
    if (rc != 0 && rc != -EEXIST)
    {
        RG_LOGE("GBHOST: symbol table rejected, rc=%d", rc);
        return;
    }

    static esp_elf_t elf;
    if ((rc = esp_elf_init(&elf)) != 0)
    {
        RG_LOGE("GBHOST: esp_elf_init failed, rc=%d", rc);
        return;
    }

    int64_t t0 = rg_system_timer();
    if ((rc = esp_elf_relocate(&elf, gbmod_start)) != 0)
    {
        RG_LOGE("GBHOST: relocate failed, rc=%d -- an unresolved import is the "
                "usual cause, check the table against readelf -r", rc);
        return;
    }
    RG_LOGW("GBHOST: relocated in %lld us", rg_system_timer() - t0);

    /* The module returns its API table through argv[0], which is also what keeps
     * gc-sections from stripping a core nobody appears to call. */
    gnuboy_module_api_t *api = NULL;
    char *argv[1] = {(char *)&api};
    if ((rc = esp_elf_request(&elf, 0, 1, argv)) != 0)
    {
        RG_LOGE("GBHOST: entry returned %d", rc);
        return;
    }
    if (!api || api->abi_version != GNUBOY_MODULE_ABI_VERSION)
    {
        RG_LOGE("GBHOST: bad API table (%p, abi %u, expected %u)", api,
                api ? api->abi_version : 0, GNUBOY_MODULE_ABI_VERSION);
        return;
    }
    RG_LOGW("GBHOST: API table at %p, abi %u -- core is live", api, api->abi_version);

    updates[0] = rg_surface_create(GB_WIDTH, GB_HEIGHT, RG_PIXEL_565_BE, MEM_SLOW);
    updates[1] = rg_surface_create(GB_WIDTH, GB_HEIGHT, RG_PIXEL_565_BE, MEM_SLOW);
    currentUpdate = updates[0];

    if (api->init(AUDIO_SAMPLE_RATE, GB_AUDIO_STEREO_S16, GB_PIXEL_565_BE,
                  &video_callback, &audio_callback) < 0)
    {
        RG_LOGE("GBHOST: core init failed");
        return;
    }
    api->set_framebuffer(currentUpdate->data);
    api->set_soundbuffer(malloc(AUDIO_BUFFER_LENGTH * 4), AUDIO_BUFFER_LENGTH);

    if (api->load_rom_file(rg_system_get_app()->romPath) < 0)
    {
        RG_LOGE("GBHOST: core could not load the ROM");
        return;
    }
    api->reset(true);
    RG_LOGW("GBHOST: running");

    while (1)
    {
        uint32_t joystick = rg_input_read_gamepad();
        int pad = 0;
        if (joystick & RG_KEY_UP)     pad |= GB_PAD_UP;
        if (joystick & RG_KEY_RIGHT)  pad |= GB_PAD_RIGHT;
        if (joystick & RG_KEY_DOWN)   pad |= GB_PAD_DOWN;
        if (joystick & RG_KEY_LEFT)   pad |= GB_PAD_LEFT;
        if (joystick & RG_KEY_SELECT) pad |= GB_PAD_SELECT;
        if (joystick & RG_KEY_START)  pad |= GB_PAD_START;
        if (joystick & RG_KEY_A)      pad |= GB_PAD_A;
        if (joystick & RG_KEY_B)      pad |= GB_PAD_B;
        api->set_pad(pad);

        int64_t startTime = rg_system_timer();

        currentUpdate = updates[currentUpdate == updates[0]];
        api->set_framebuffer(currentUpdate->data);
        api->run(true);

        rg_system_tick(rg_system_timer() - startTime);
    }
}
