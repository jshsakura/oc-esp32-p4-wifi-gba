/*
 * coreloader — one app, no cores.
 *
 * Every other app in this tree links an emulator into its firmware image, which
 * is why adding a system has always meant finding a partition. This one does
 * not: cores live at /sd/retro-go/cores/<name>.elf, and the launcher's choice
 * arrives the same way it does for retro-core, through app->configNs.
 *
 * The difference from retro-core is what is NOT in the binary. retro-core holds
 * twenty systems and pays for all twenty on every boot -- flash for the code,
 * internal RAM for the statics, and a linker fragment per core to keep the
 * second cost survivable. Here the system that is not running is a file nobody
 * opened.
 *
 * Measured on device, against the same cores linked into retro-core:
 *
 *     Game Boy (gnuboy)    2,925 us/frame   linked: 3,002
 *     NES (nofrendo)       2,304 us/frame   linked: 57.4 fps
 *
 * Loading costs nothing per frame. Relocation is 6-9ms once per launch, next to
 * a ROM read that costs more.
 *
 * Adding a system here is: build the core with elf_loader's project_elf(), write
 * a driver like gbhost.c, put the .elf on the card. No partition, no image
 * repack, no internal RAM budget to renegotiate.
 */
#include <rg_system.h>
#include <string.h>

void gbhost_run(void);
void neshost_run(void);

void app_main(void)
{
    rg_app_t *app = rg_system_init(32000, NULL, NULL);

    RG_LOGI("coreloader: configNs=%s", app->configNs);

    if (strcmp(app->configNs, "nes") == 0)
        neshost_run();
    else if (strcmp(app->configNs, "gb") == 0 || strcmp(app->configNs, "gbc") == 0)
        gbhost_run();
    else
    {
        /* Say so rather than falling through quietly. retro-core's silent
         * fallthrough to the launcher is exactly what hid SG-1000 being
         * unwired for as long as it was. */
        RG_LOGE("coreloader: no driver for configNs='%s'", app->configNs);
        rg_system_rom_load_failed(_("This system has no core installed."));
    }

    RG_PANIC("Core returned");
}
