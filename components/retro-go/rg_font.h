#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "rg_gui.h"

#define FONT_HEADER_SIZE 24
#define RG_FONT_MAX_EXTERNAL 16

typedef struct {
    char path[64];
    char name[16];
    uint8_t height;
    bool loaded;
    rg_font_t *font_data;
} rg_external_font_t;

void rg_font_init(void);
void rg_font_deinit(void);
bool rg_font_load_from_memory(const uint8_t *buffer, size_t size);
// Load one .font file. rg_font_init() already sweeps the fonts folder on the card, so this
// is only needed to add one from somewhere else.
bool rg_font_load_from_file(const char *path);
const rg_font_t *rg_font_get(int index);
int rg_font_get_count(void);
const char *rg_font_get_name(int index);
void rg_font_free_external(void);
const rg_font_t *rg_font_find_by_name(const char *name);
