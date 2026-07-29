#include "odroid_system.h"

#include <rg_audio.h>
#include <rg_display.h>
#include <rg_input.h>
#include <rg_storage.h>
#include <rg_gui.h>

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

// ---------------------------------------------------------------------------
// Battery
// ---------------------------------------------------------------------------

void odroid_input_battery_level_init(void)
{
    // rg_input_init already brought the battery driver up, if the board has one.
}

odroid_battery_state_t odroid_input_read_battery(void)
{
    rg_battery_t b = rg_input_read_battery();
    return (odroid_battery_state_t){
        .millivolts = (int)(b.volts * 1000),
        .percentage = (int)b.level,
    };
}

void odroid_input_battery_level_read(odroid_battery_state_t *out)
{
    if (out)
        *out = odroid_input_read_battery();
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

int odroid_display_get_backlight(void)
{
    // The fork counts in seven steps, retro-go in percent. Round-trip through the same
    // mapping so get(set(x)) == x.
    int percent = rg_display_get_backlight();
    return (percent * (ODROID_BACKLIGHT_LEVEL_COUNT - 1) + 50) / 100;
}

void odroid_display_set_backlight(int level)
{
    if (level < 0)
        level = 0;
    if (level >= ODROID_BACKLIGHT_LEVEL_COUNT)
        level = ODROID_BACKLIGHT_LEVEL_COUNT - 1;
    rg_display_set_backlight(level * 100 / (ODROID_BACKLIGHT_LEVEL_COUNT - 1));
}

odroid_display_filter_t odroid_display_get_filter_mode(void)
{
    switch (rg_display_get_filter())
    {
    case RG_DISPLAY_FILTER_HORIZ: return ODROID_DISPLAY_FILTER_LINEAR_X;
    case RG_DISPLAY_FILTER_VERT:  return ODROID_DISPLAY_FILTER_LINEAR_Y;
    case RG_DISPLAY_FILTER_BOTH:  return ODROID_DISPLAY_FILTER_LINEAR_XY;
    default:                      return ODROID_DISPLAY_FILTER_OFF;
    }
}

void odroid_display_write_rect(short left, short top, short width, short height, short stride,
                               const uint16_t *buffer)
{
    rg_display_write_rect(left, top, width, height, stride, buffer, 0);
}

void odroid_display_force_refresh(void)
{
    rg_display_force_redraw();
}

void odroid_display_show_hourglass(void)
{
    rg_gui_draw_hourglass();
}

void odroid_display_lock(void)
{
    // The Game & Watch had callers serialise access to a frame buffer they wrote directly.
    // Retro-Go owns its buffers and serialises inside rg_display, so the lock has nothing
    // left to protect. Kept as a no-op so porting layers need no edits.
}

void odroid_display_unlock(void)
{
}

int odroid_overlay_draw_text(uint16_t x_pos, uint16_t y_pos, uint16_t width, const char *text,
                             uint16_t color, uint16_t color_bg)
{
    rg_rect_t r = rg_gui_draw_text(x_pos, y_pos, width, text, color, color_bg, 0);
    return r.height;
}

void odroid_overlay_draw_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    rg_gui_draw_rect(x, y, width, height, 0, color, color);
}

void odroid_overlay_draw_battery(int x_pos, int y_pos, int unused)
{
    (void)x_pos, (void)y_pos, (void)unused;
    // Retro-Go draws the battery as part of its own status bar, at a position it chooses.
    rg_gui_draw_status_bars();
}

void odroid_overlay_draw_logo(int x_pos, int y_pos, int logo_id, uint16_t color)
{
    (void)x_pos, (void)y_pos, (void)logo_id, (void)color;
    // The fork ships its own logo bitmaps. Nothing is drawn here on purpose -- see the note
    // about not shipping artwork we do not own.
}

void odroid_overlay_clock(int x_pos, int y_pos)
{
    (void)x_pos, (void)y_pos;
    rg_gui_draw_status_bars();
}

void odroid_overlay_alert(const char *text)
{
    rg_gui_alert(NULL, text);
}

int odroid_overlay_dialog(const char *header, odroid_dialog_choice_t *options, int selected)
{
    // The two option structs differ in field order and in how the terminator is spelled, so
    // this converts rather than casts. Bounded copy: a runaway list is a bug worth stopping
    // at rather than following off the end of memory.
    rg_gui_option_t converted[32];
    size_t n = 0;
    while (options && options[n].id != -1 && n < RG_COUNT(converted) - 1)
    {
        converted[n] = (rg_gui_option_t){
            .arg = options[n].id,
            .label = options[n].label,
            .value = options[n].value,
            .flags = options[n].enabled ? RG_DIALOG_FLAG_NORMAL : RG_DIALOG_FLAG_DISABLED,
            .update_cb = NULL, // callbacks take different types; the caller drives them
        };
        n++;
    }
    converted[n] = (rg_gui_option_t)RG_DIALOG_END;
    return (int)rg_gui_dialog(header, converted, selected);
}

int odroid_overlay_settings_menu(odroid_dialog_choice_t *extra_options, void_callback_t repaint,
                                 odroid_menu_flags_t flags)
{
    (void)extra_options, (void)flags;
    rg_gui_options_menu();
    if (repaint)
        repaint();
    return 0;
}

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------

int odroid_sdcard_read_file(const char *path, void *buf, size_t buf_size)
{
    // The fork reads into a buffer the caller already sized; rg_storage_read_file allocates.
    // Read into our own allocation and copy, so a file larger than the caller's buffer is
    // refused instead of overrunning it.
    void *data = NULL;
    size_t len = 0;
    if (!rg_storage_read_file(path, &data, &len, 0))
        return -1;
    if (len > buf_size)
    {
        RG_LOGE("'%s' is %u bytes, caller offered %u", path, (unsigned)len, (unsigned)buf_size);
        free(data);
        return -1;
    }
    memcpy(buf, data, len);
    free(data);
    return (int)len;
}

bool odroid_sdcard_mkdir(const char *dir)
{
    return rg_storage_mkdir(dir);
}

bool odroid_sdcard_unlink(const char *path)
{
    return rg_storage_delete(path);
}

// ---------------------------------------------------------------------------
// Odds and ends
// ---------------------------------------------------------------------------

void odroid_audio_init(int sample_rate)
{
    rg_audio_init(sample_rate);
}

void odroid_audio_submit_zero(void)
{
    static const rg_audio_frame_t silence[64] = {0};
    rg_audio_submit(silence, RG_COUNT(silence));
}

void odroid_audio_volume_change(void)
{
    // Step through in tens and wrap, which is what the fork's button did.
    int vol = rg_audio_get_volume() + 10;
    rg_audio_set_volume(vol > 100 ? 0 : vol);
}

void *odroid_system_get_app(void)
{
    return rg_system_get_app();
}

void odroid_system_led_set(int on)
{
    rg_system_set_led_color(on ? C_GREEN : C_BLACK);
}

int odroid_settings_cpu_oc_level_get(void)
{
    return rg_system_get_overclock();
}

int odroid_settings_turbo_buttons_get(void)
{
    return 0; // no turbo-button setting in retro-go
}

uint32_t odroid_button_turbos(void)
{
    return 0; // ditto: nothing is ever auto-fired
}

bool odroid_idle_timeout_expired(uint32_t idle_seconds)
{
    (void)idle_seconds;
    // Retro-Go tracks no idle timer, so nothing here ever decides the device should sleep.
    return false;
}

// ---------------------------------------------------------------------------
// The on-screen keyboard, which has no counterpart here
// ---------------------------------------------------------------------------

static void keyboard_unavailable(const char *what)
{
    static bool warned = false;
    if (!warned)
    {
        RG_LOGW("%s: the Game & Watch on-screen keyboard is not implemented on this platform. "
                "MSX and Amstrad will run but cannot be typed into.", what);
        warned = true;
    }
}

bool odroid_keyboard_init(void)
{
    keyboard_unavailable("odroid_keyboard_init");
    return false;
}

void odroid_keyboard_event_callback_set(void *cb)
{
    (void)cb;
    keyboard_unavailable("odroid_keyboard_event_callback_set");
}

void odroid_keyboard_leds_set(int leds)
{
    (void)leds;
}

void odroid_keyboard_state_get(odroid_key_state_t *out)
{
    if (out)
        memset(out, 0, sizeof(*out));
}

int odroid_keyboard_state_key_get(const odroid_key_state_t *state, int key)
{
    (void)state, (void)key;
    return ODROID_KEY_RELEASED;
}
