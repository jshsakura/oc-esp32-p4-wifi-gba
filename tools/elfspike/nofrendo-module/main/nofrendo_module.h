#pragma once

#include "nofrendo.h"
#include "nes/input.h"
#include "nes/ppu.h"
#include "nes/rom.h"
#include "nes/state.h"

#define NOFRENDO_MODULE_ABI_VERSION 1

typedef struct
{
    unsigned abi_version;

    nes_t *(*nes_getptr)(void);
    nes_t *(*nes_init)(nes_type_t, int, bool, const char *);
    uint8 *(*nes_setvidbuf)(uint8 *);
    void (*nes_shutdown)(void);
    int (*nes_insertcart)(rom_t *);
    int (*nes_loadfile)(const char *);
    void (*nes_emulate)(bool);
    void (*nes_reset)(bool);

    rom_t *(*rom_loadmem)(uint8 *, size_t);
    int (*state_load)(const char *);
    int (*state_save)(const char *);
    void (*input_update)(int, int);
    void (*ppu_setopt)(ppu_option_t, int);
    int (*ppu_getopt)(ppu_option_t);
    void *(*buildpalette)(nespal_t, int);
} nofrendo_module_api_t;
