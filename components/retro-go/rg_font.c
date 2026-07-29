#include "rg_system.h"
#include "rg_font.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#define TAG "FONT"

static rg_external_font_t external_fonts[RG_FONT_MAX_EXTERNAL];
static int external_font_count = 0;

// Fonts the firmware does not ship live here on the card. Korean is the reason this exists:
// covering Hangul means thousands of glyphs, which is a lot to bake into every build for a
// language most users will not pick -- and it would mean vendoring someone's typeface. On a
// chip with 32MB of flash and 32MB of PSRAM there is no reason not to read it at runtime and
// let the user choose the face.
#define RG_FONTS_PATH RG_STORAGE_ROOT "/retro-go/fonts"

bool rg_font_load_from_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // A header and nothing else is not a font; neither is something too big to be one.
    if (size <= FONT_HEADER_SIZE || size > (2 << 20))
    {
        RG_LOGE("'%s' is %ld bytes, which is not a plausible font", path, size);
        fclose(fp);
        return false;
    }

    uint8_t *buffer = rg_alloc((size_t)size, MEM_SLOW | MEM_NOPANIC);
    if (!buffer)
    {
        fclose(fp);
        return false;
    }

    size_t got = fread(buffer, 1, (size_t)size, fp);
    fclose(fp);

    bool ok = (got == (size_t)size) && rg_font_load_from_memory(buffer, got);
    free(buffer);

    if (ok)
        RG_LOGI("Loaded font from '%s'", path);
    return ok;
}

static void load_fonts_from_storage(void)
{
    DIR *dir = opendir(RG_FONTS_PATH);
    if (!dir)
        return; // No font folder is the normal case, not an error.

    struct dirent *entry;
    int found = 0;
    while ((entry = readdir(dir)) && external_font_count < RG_FONT_MAX_EXTERNAL)
    {
        const char *dot = strrchr(entry->d_name, '.');
        if (!dot || strcasecmp(dot, ".font") != 0)
            continue;

        char path[280];
        snprintf(path, sizeof(path), "%s/%s", RG_FONTS_PATH, entry->d_name);
        if (rg_font_load_from_file(path))
            found++;
    }
    closedir(dir);

    if (found)
        RG_LOGI("Loaded %d font(s) from %s", found, RG_FONTS_PATH);
}

void rg_font_init(void)
{
    memset(external_fonts, 0, sizeof(external_fonts));
    external_font_count = 0;
    load_fonts_from_storage();
    RG_LOGI("Font module initialized (%d external)", external_font_count);
}

void rg_font_deinit(void)
{
    rg_font_free_external();
}

static bool load_font_from_buffer(const uint8_t *buffer, size_t size)
{
    if (size < FONT_HEADER_SIZE)
    {
        RG_LOGE("Font data too small: %d bytes", (int)size);
        return false;
    }

    if (external_font_count >= RG_FONT_MAX_EXTERNAL)
    {
        RG_LOGE("Maximum external fonts reached");
        return false;
    }

    const uint8_t *ptr = buffer;

    char name[17];
    memcpy(name, ptr, 16);
    name[16] = '\0';
    ptr += 16;

    uint8_t type = *ptr++;
    uint8_t width = *ptr++;
    uint8_t height = *ptr++;
    ptr++;
    uint32_t chars = *(uint32_t*)ptr; ptr += 4;

    size_t data_size = size - FONT_HEADER_SIZE;

    rg_font_t *font = (rg_font_t *)rg_alloc(sizeof(rg_font_t) + data_size, MEM_SLOW);
    if (!font)
    {
        RG_LOGE("Failed to allocate font memory");
        return false;
    }

    strncpy(font->name, name, sizeof(font->name) - 1);
    font->name[sizeof(font->name) - 1] = '\0';
    font->type = type;
    font->width = width;
    font->height = height;
    font->chars = chars;
    memcpy(font->data, ptr, data_size);

    rg_external_font_t *ext = &external_fonts[external_font_count];
    snprintf(ext->path, sizeof(ext->path), "memory");
    strncpy(ext->name, font->name, sizeof(ext->name) - 1);
    ext->height = font->height;
    ext->font_data = font;
    ext->loaded = true;

    external_font_count++;

    RG_LOGI("Loaded font: %s (height=%d, chars=%d, size=%d bytes)",
            font->name, font->height, (int)font->chars, (int)data_size);

    return true;
}

bool rg_font_load_from_memory(const uint8_t *buffer, size_t size)
{
    if (!buffer || size == 0)
    {
        RG_LOGE("Invalid font buffer");
        return false;
    }

    return load_font_from_buffer(buffer, size);
}

const rg_font_t *rg_font_get(int index)
{
    if (index < 0)
        return NULL;

    if (index < external_font_count && external_fonts[index].loaded)
    {
        return external_fonts[index].font_data;
    }

    return NULL;
}

int rg_font_get_count(void)
{
    return external_font_count;
}

const char *rg_font_get_name(int index)
{
    if (index < 0 || index >= external_font_count)
        return NULL;

    return external_fonts[index].name;
}

void rg_font_free_external(void)
{
    for (int i = 0; i < external_font_count; i++)
    {
        if (external_fonts[i].font_data)
        {
            free(external_fonts[i].font_data);
            external_fonts[i].font_data = NULL;
            external_fonts[i].loaded = false;
        }
    }
    external_font_count = 0;
    RG_LOGI("Freed all external fonts");
}

const rg_font_t *rg_font_find_by_name(const char *name)
{
    if (!name)
        return NULL;

    for (int i = 0; i < external_font_count; i++)
    {
        if (external_fonts[i].loaded && 
            strncmp(external_fonts[i].name, name, sizeof(external_fonts[i].name)) == 0)
        {
            return external_fonts[i].font_data;
        }
    }
    return NULL;
}
