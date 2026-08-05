#pragma once

#include "gnuboy.h"

#define GNUBOY_MODULE_ABI_VERSION 1

/*
 * The entry point returns this table through argv[0]. Keeping the public core
 * calls in one reachable object also prevents project_elf()'s gc-sections from
 * turning a successfully linked module into an empty shell.
 */
typedef struct
{
    unsigned abi_version;

    int (*init)(int, gb_audio_fmt_t, gb_video_fmt_t, gb_video_cb_t *, gb_audio_cb_t *);
    int (*load_bios)(const byte *, size_t);
    int (*load_bios_file)(const char *);
    void (*free_bios)(void);
    int (*load_rom)(const byte *, size_t);
    int (*load_rom_file)(const char *);
    void (*free_rom)(void);
    void (*reset)(bool);
    void (*run)(bool);
    bool (*sram_dirty)(void);
    void (*load_bank)(int);
    void (*set_pad)(int);
    void (*set_framebuffer)(void *);
    void (*set_soundbuffer)(void *, size_t);
    void (*get_time)(int *, int *, int *, int *);
    void (*set_time)(int, int, int, int);
    int (*get_hwtype)(void);
    void (*set_hwtype)(gb_hwtype_t);
    int (*get_palette)(void);
    void (*set_palette)(gb_palette_t);
    int (*load_sram)(const char *);
    int (*save_sram)(const char *, bool);
    int (*load_state)(const char *);
    int (*save_state)(const char *);
} gnuboy_module_api_t;
