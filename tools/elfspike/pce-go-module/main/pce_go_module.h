#pragma once

#include "pce-go.h"
#include "psg.h"

#define PCE_GO_MODULE_ABI_VERSION 1

typedef struct
{
    unsigned abi_version;

    int (*init)(int, bool);
    int (*load_card)(uint8_t *, size_t);
    int (*load_file)(const char *);
    void (*reset)(bool);
    void (*run)(void);
    void (*shutdown)(void);
    int (*load_state)(const char *);
    int (*save_state)(const char *);
    void *(*palette)(int);
    void (*psg_update)(int16_t *, size_t, uint32_t);
} pce_go_module_api_t;
