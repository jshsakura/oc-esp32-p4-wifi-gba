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
        launcher_main();

    RG_PANIC("Never reached");
}
