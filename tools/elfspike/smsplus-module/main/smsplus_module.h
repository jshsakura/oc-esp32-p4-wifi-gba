#pragma once

#include "smsplus.h"

#define SMSPLUS_MODULE_ABI_VERSION 1

typedef struct
{
    unsigned abi_version;

    smsplus_t *smsplus;
    sms_t *sms;
    coleco_t *coleco;
    snd_t *snd;

    void (*reset_config)(void);
    int (*load_rom)(void *, int, int);
    int (*load_rom_file)(const char *);
    void (*poweron)(void);
    void (*poweroff)(void);
    void (*reset)(void);
    void (*shutdown)(void);
    void (*frame)(int);
    int (*save_state)(void *);
    void (*load_state)(void *);
    void (*palette_sync)(int);
    bool (*copy_palette)(uint16 *);
} smsplus_module_api_t;
