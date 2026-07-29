#include "odroid_system.h"

#include <rg_audio.h>
#include <rg_display.h>
#include <rg_input.h>
#include <rg_storage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

odroid_gamepad_state_t odroid_input_read_gamepad(void)
{
    uint32_t keys = rg_input_read_gamepad();
    odroid_gamepad_state_t out = {0};

    out.values[ODROID_INPUT_UP] = (keys & RG_KEY_UP) ? 1 : 0;
    out.values[ODROID_INPUT_RIGHT] = (keys & RG_KEY_RIGHT) ? 1 : 0;
    out.values[ODROID_INPUT_DOWN] = (keys & RG_KEY_DOWN) ? 1 : 0;
    out.values[ODROID_INPUT_LEFT] = (keys & RG_KEY_LEFT) ? 1 : 0;
    out.values[ODROID_INPUT_SELECT] = (keys & RG_KEY_SELECT) ? 1 : 0;
    out.values[ODROID_INPUT_START] = (keys & RG_KEY_START) ? 1 : 0;
    out.values[ODROID_INPUT_A] = (keys & RG_KEY_A) ? 1 : 0;
    out.values[ODROID_INPUT_B] = (keys & RG_KEY_B) ? 1 : 0;
    out.values[ODROID_INPUT_X] = (keys & RG_KEY_X) ? 1 : 0;
    out.values[ODROID_INPUT_Y] = (keys & RG_KEY_Y) ? 1 : 0;
    out.values[ODROID_INPUT_MENU] = (keys & RG_KEY_MENU) ? 1 : 0;
    // The Game & Watch had a dedicated volume button. Nothing here does, and the wheel is an
    // analogue input rather than a key, so this stays clear.
    out.values[ODROID_INPUT_VOLUME] = 0;
    out.values[ODROID_INPUT_ANY] = keys ? 1 : 0;

    return out;
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

odroid_display_scaling_t odroid_display_get_scaling_mode(void)
{
    switch (rg_display_get_scaling())
    {
    case RG_DISPLAY_SCALING_OFF:
        return ODROID_DISPLAY_SCALING_OFF;
    case RG_DISPLAY_SCALING_FIT:
        return ODROID_DISPLAY_SCALING_FIT;
    case RG_DISPLAY_SCALING_FULL:
        return ODROID_DISPLAY_SCALING_FULL;
    default:
        return ODROID_DISPLAY_SCALING_FIT;
    }
}

void odroid_display_set_scaling_mode(odroid_display_scaling_t mode)
{
    switch (mode)
    {
    case ODROID_DISPLAY_SCALING_OFF:
        rg_display_set_scaling(RG_DISPLAY_SCALING_OFF);
        break;
    case ODROID_DISPLAY_SCALING_FULL:
        rg_display_set_scaling(RG_DISPLAY_SCALING_FULL);
        break;
    default:
        rg_display_set_scaling(RG_DISPLAY_SCALING_FIT);
        break;
    }
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

void odroid_audio_mute(bool mute)
{
    rg_audio_set_mute(mute);
}

int odroid_audio_volume_get(void)
{
    return rg_audio_get_volume();
}

void odroid_audio_volume_set(int volume)
{
    rg_audio_set_volume(volume);
}

int odroid_audio_sample_rate_get(void)
{
    return rg_audio_get_sample_rate();
}

// ---------------------------------------------------------------------------
// System and paths
// ---------------------------------------------------------------------------

void odroid_system_init(int appId, int sampleRate)
{
    // The fork identifies apps by a numeric APPID and looks the rest up from a table. Here
    // each emulator is its own esp-idf application with its own partition, so the identity
    // is already established by the time this runs and the id is only of historical
    // interest.
    (void)appId;
    rg_system_init(sampleRate, NULL, NULL);
}

void odroid_system_emu_init(void *load_cb, void *save_cb, void *reset_cb)
{
    // Retro-Go takes these as part of the rg_handlers_t passed to rg_system_init rather than
    // as a separate registration step. A core that calls this has already been through
    // odroid_system_init, so the honest thing is to make the porting layer pass them there;
    // flagging it is better than accepting them and never calling them.
    (void)load_cb, (void)save_cb, (void)reset_cb;
    RG_LOGW("odroid_system_emu_init: pass handlers to rg_system_init instead; these are ignored");
}

bool odroid_system_emu_load_state(int slot)
{
    return rg_emu_load_state(slot);
}

bool odroid_system_emu_save_state(int slot)
{
    return rg_emu_save_state(slot);
}

char *odroid_system_get_path(odroid_path_type_t type, const char *romPath)
{
    rg_path_type_t mapped;
    switch (type)
    {
    case ODROID_PATH_SAVE_STATE:
        mapped = RG_PATH_SAVE_STATE;
        break;
    case ODROID_PATH_SAVE_SRAM:
        mapped = RG_PATH_SAVE_SRAM;
        break;
    case ODROID_PATH_SCREENSHOT:
        mapped = RG_PATH_SCREENSHOT;
        break;
    case ODROID_PATH_ROM_FILE:
        mapped = RG_PATH_ROM_FILE;
        break;
    default:
        mapped = RG_PATH_SAVE_STATE;
        break;
    }
    return rg_emu_get_path(mapped, romPath);
}

void odroid_system_sleep(void)
{
    rg_system_sleep();
}

void odroid_system_switch_app(int app)
{
    (void)app;
    // The fork switches between apps inside one binary by id. Here they are separate
    // partitions, and the only destination a core ever wants is the launcher.
    rg_system_exit();
}

// ---------------------------------------------------------------------------
// Flash caching, which this chip does not need
// ---------------------------------------------------------------------------

static void *read_whole_file(const char *filename, size_t *size_out)
{
    if (size_out)
        *size_out = 0;

    FILE *fp = fopen(filename, "rb");
    if (!fp)
    {
        RG_LOGE("Could not open '%s'", filename);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len <= 0)
    {
        fclose(fp);
        return NULL;
    }

    // PSRAM: these files are ROMs and asset blobs, far too large for internal RAM and not
    // latency critical.
    // MEM_NOPANIC because a missing or oversized asset file is a thing to report, not
    // a thing to die on -- rg_alloc panics by default.
    void *buf = rg_alloc((size_t)len, MEM_SLOW | MEM_NOPANIC);
    if (!buf)
    {
        RG_LOGE("Out of memory reading '%s' (%ld bytes)", filename, len);
        fclose(fp);
        return NULL;
    }

    size_t got = fread(buf, 1, (size_t)len, fp);
    fclose(fp);

    if (got != (size_t)len)
    {
        RG_LOGE("Short read on '%s': %u of %ld", filename, (unsigned)got, len);
        free(buf);
        return NULL;
    }

    if (size_out)
        *size_out = got;
    return buf;
}

void *odroid_overlay_cache_file_in_flash(const char *filename, size_t *size_out)
{
    // On the Game & Watch this wrote the file into external flash and returned a
    // memory-mapped pointer, because 724KB of RAM could not hold it. With 32MB of PSRAM
    // the copy is unnecessary and the file just goes into memory.
    return read_whole_file(filename, size_out);
}

void *odroid_overlay_cache_file_in_ram(const char *filename, size_t *size_out)
{
    return read_whole_file(filename, size_out);
}
