#include "shared.h"


void app_main(void)
{
    rg_app_t *app = rg_system_init(AUDIO_SAMPLE_RATE, NULL, NULL);

    RG_LOGI("configNs=%s", app->configNs);

    if (strcmp(app->configNs, "gbc") == 0 || strcmp(app->configNs, "gb") == 0)
        gbc_main();
    else if (strcmp(app->configNs, "nes") == 0)
        nes_main();
    else if (strcmp(app->configNs, "pce") == 0)
        pce_main();
    else if (strcmp(app->configNs, "sms") == 0)
        sms_main();
    else if (strcmp(app->configNs, "gg") == 0)
        sms_main();
    else if (strcmp(app->configNs, "col") == 0)
        sms_main();
    /* SG-1000 is smsplus too, and main_sms.c already picks console 5 off the
     * .sg extension -- only this line was missing. The launcher has offered the
     * tab all along (applications.c: "Sega SG-1000" -> retro-core), so choosing
     * it fell through to the exit below and bounced straight back to the
     * launcher. An unknown configNs leaves silently; it does not warn. */
    else if (strcmp(app->configNs, "sg1") == 0)
        sms_main();
    else if (strcmp(app->configNs, "gw") == 0)
        gw_main();
    else if (strcmp(app->configNs, "snes") == 0)
        snes_main();
    else if (strcmp(app->configNs, "a78") == 0)
        a78_main();
    else if (strcmp(app->configNs, "supervision") == 0)
        supervision_main();
    else if (strcmp(app->configNs, "poke") == 0)
        poke_main();
    else if (strcmp(app->configNs, "videopac") == 0)
        videopac_main();
    else if (strcmp(app->configNs, "wsc") == 0)
        wsc_main();
    else if (strcmp(app->configNs, "a26") == 0)
        a26_main();
    else if (strcmp(app->configNs, "ngp") == 0)
        ngp_main();
    else if (strcmp(app->configNs, "zxs") == 0)
        zxs_main();
    else if (strcmp(app->configNs, "gamecom") == 0)
        gamecom_main();
    else if (strcmp(app->configNs, "vb") == 0)
        vb_main();
#ifndef __TINYC__
    else if (strcmp(app->configNs, "lnx") == 0)
        lynx_main();
#endif
    else
    {
        /* Say so. This fallthrough is why sg1 was invisible: a namespace nobody
         * dispatches lands in the launcher, which looks like a working boot from
         * the outside -- it draws, it runs at a few hundred fps, and the only
         * tell is a tab that bounces back when you pick it. It is also what the
         * bench harness measured for ten systems before it learned to check the
         * boot banner. Twenty systems dispatch from one binary here; a typo in a
         * namespace should not be silent. */
        RG_LOGW("No core for configNs='%s' -- falling back to the launcher. "
                "If this is a system, it is missing from the dispatch above.",
                app->configNs);
        launcher_main();
    }

    RG_PANIC("Never reached");
}
