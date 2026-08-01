//============================================================================
// main.c - Retro-Go frontend for the potator (Watara Supervision) core.
//
// The potator core emulates the Watara Supervision handheld: a 6502-based
// console with a 160x160 greyscale LCD. The core renders RGB565 directly into
// a back buffer we provide, and emits unsigned 8-bit stereo audio. This file
// is the glue between that core and the retro-go framework (display, input,
// audio, save state), modelled on gbsp/main/main.c and the STM32H7 reference.
//============================================================================

#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "supervision.h"

// The Watara Supervision runs at ~50 FPS (real hardware is 50.81). The core
// generates audio at SV_SAMPLE_RATE (48000Hz), so one frame is exactly 960
// stereo sample pairs. Keeping these derived from the core's own constants
// guarantees audio stays pitch-correct regardless of the target clock.
#define SV_FPS                  50
#define AUDIO_SAMPLE_RATE       SV_SAMPLE_RATE
#define AUDIO_SAMPLES_PER_FRAME (AUDIO_SAMPLE_RATE / SV_FPS)

// supervision_save_state() writes its own length; the STM32 reference uses
// 24741 bytes. We round up generously and rely on the returned size for I/O.
#define STATE_BUFFER_SIZE       (32 * 1024)

// Scale the core's U8 samples (0..45 per channel, 0 == silence) into the
// signed 16-bit range retro-go expects. 32767/45 ~= 728; 700 leaves a little
// headroom so summed channels cannot clip.
#define SV_AUDIO_SCALE          700

static rg_app_t *app;
static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;

// Interleaved U8 stereo stream filled by the core (L,R,L,R,...).
static uint8_t audio_stream[AUDIO_SAMPLES_PER_FRAME * 2];

// --- LCD colour palette option -------------------------------------------
// The Supervision LCD was monochrome; the core ships several colour profiles
// (Default/Amber/Green/Blue/BGB/Wataroo). We expose them in the options menu.
static const char *palette_names[SV_COLOR_SCHEME_COUNT] = {
    "Default", "Amber", "Green", "Blue", "BGB", "Wataroo"
};

static rg_gui_event_t palette_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    int scheme = (int)rg_settings_get_number(NS_APP, "SvPalette", SV_COLOR_SCHEME_DEFAULT);
    int max = SV_COLOR_SCHEME_COUNT - 1;

    if (event == RG_DIALOG_PREV) scheme = scheme > 0 ? scheme - 1 : max;
    if (event == RG_DIALOG_NEXT) scheme = scheme < max ? scheme + 1 : 0;

    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        rg_settings_set_number(NS_APP, "SvPalette", scheme);
        rg_settings_commit();
        supervision_set_color_scheme((int8_t)scheme);
    }

    strcpy(option->value, palette_names[scheme]);
    return RG_DIALOG_VOID;
}

static void options_handler(rg_gui_option_t *dest)
{
    int end = 0;
    while (dest[end].label || dest[end].value || dest[end].arg || dest[end].flags || dest[end].update_cb)
        end++;
    dest[end] = (rg_gui_option_t){0, _("Palette"), "-", RG_DIALOG_FLAG_NORMAL, &palette_cb};
    dest[end + 1] = (rg_gui_option_t)RG_DIALOG_END;
}

// --- Save / load state ----------------------------------------------------
static bool save_state_handler(const char *filename)
{
    uint8_t *buffer = (uint8_t *)rg_alloc(STATE_BUFFER_SIZE, MEM_SLOW);
    if (!buffer)
        return false;

    int size = supervision_save_state(buffer);
    bool ok = false;
    if (size > 0)
    {
        FILE *f = fopen(filename, "wb");
        if (f)
        {
            ok = (fwrite(buffer, 1, size, f) == (size_t)size);
            fclose(f);
        }
    }
    free(buffer);
    return ok;
}

static bool load_state_handler(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > STATE_BUFFER_SIZE)
    {
        fclose(f);
        return false;
    }

    uint8_t *buffer = (uint8_t *)rg_alloc(STATE_BUFFER_SIZE, MEM_SLOW);
    if (!buffer)
    {
        fclose(f);
        return false;
    }

    bool ok = (fread(buffer, 1, size, f) == (size_t)size);
    fclose(f);

    // Guard against foreign/corrupt files: the core would otherwise memcpy an
    // untrusted blob straight into emulator RAM. Require the WSV1 magic.
    if (ok && (size < 4 || memcmp(buffer, "WSV1", 4) != 0))
        ok = false;
    if (ok)
        ok = (supervision_load_state(buffer) > 0);

    free(buffer);
    return ok;
}

static bool reset_handler(bool hard)
{
    supervision_reset();
    return true;
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(currentUpdate, 0);
}

// --- Input ----------------------------------------------------------------
// Bit layout expected by supervision_set_input() (bits 0-7):
//   Right, Left, Down, Up, B, A, Select, Start.
static uint8_t read_input(void)
{
    uint32_t pad = rg_input_read_gamepad();
    uint8_t state = 0;
    if (pad & RG_KEY_RIGHT)  state |= 0x01;
    if (pad & RG_KEY_LEFT)   state |= 0x02;
    if (pad & RG_KEY_DOWN)   state |= 0x04;
    if (pad & RG_KEY_UP)     state |= 0x08;
    if (pad & RG_KEY_B)      state |= 0x10; // fire2
    if (pad & RG_KEY_A)      state |= 0x20; // fire
    if (pad & RG_KEY_SELECT) state |= 0x40;
    if (pad & RG_KEY_START)  state |= 0x80;
    return state;
}

// --- ROM loading ----------------------------------------------------------
// supervision_load() only stores the ROM pointer, so the buffer must outlive
// the call; we keep it for the whole app lifetime (never freed).
static uint8_t *load_rom(const char *path, uint32_t *out_size)
{
    uint8_t *data = NULL;
    size_t size = 0;

    if (rg_extension_match(path, "zip"))
    {
        if (!rg_storage_unzip_file(path, NULL, (void **)&data, &size, 0))
            return NULL;
    }
    else
    {
        FILE *fp = fopen(path, "rb");
        if (!fp)
            return NULL;
        fseek(fp, 0, SEEK_END);
        size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        data = (uint8_t *)malloc(size);
        if (!data || fread(data, 1, size, fp) != size)
        {
            free(data);
            data = NULL;
        }
        fclose(fp);
    }

    *out_size = (uint32_t)size;
    return data;
}

extern void app_main(void);

void app_main(void)
{
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
        .options = &options_handler,
    };

    app = rg_system_init(AUDIO_SAMPLE_RATE, &handlers, NULL);

    // 160x160 RGB565, double buffered. The core writes one row per SV_W
    // pixels, which matches the surface stride.
    updates[0] = rg_surface_create(SV_W, SV_H, RG_PIXEL_565_LE, MEM_FAST);
    updates[1] = rg_surface_create(SV_W, SV_H, RG_PIXEL_565_LE, MEM_FAST);
    currentUpdate = updates[0];

    supervision_init();

    uint32_t rom_size = 0;
    uint8_t *rom_data = load_rom(app->romPath, &rom_size);
    // supervision_load() rejects ROMs whose size is not a multiple of 16KB
    // (or is empty). A bad/unsupported file must never crash the device: we
    // tell the user and return to the launcher.
    if (!rom_data || !supervision_load(rom_data, rom_size))
    {
        rg_gui_alert(_("Error"), _("Could not load the game file."));
        rg_system_exit();
    }

    int scheme = (int)rg_settings_get_number(NS_APP, "SvPalette", SV_COLOR_SCHEME_DEFAULT);
    supervision_set_color_scheme((int8_t)scheme);

    if (app->bootFlags & RG_BOOT_RESUME)
        rg_emu_load_state(app->saveSlot);

    long skipFrames = 0;

    while (1)
    {
        uint32_t joystick = rg_input_read_gamepad();

        if (joystick & (RG_KEY_MENU | RG_KEY_OPTION))
        {
            if (joystick & RG_KEY_MENU)
                rg_gui_game_menu();
            else
                rg_gui_options_menu();
        }

        int64_t startTime = rg_system_timer();
        bool drawFrame = !skipFrames;

        supervision_set_input(read_input());

        // Run one frame: 6502 + timers, then render 160 scanlines into the
        // current surface's pixel buffer.
        supervision_exec_ex((uint16_t *)currentUpdate->data, SV_W);

        if (drawFrame)
        {
            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
        }

        // Audio: the core fills audio_stream with AUDIO_SAMPLES_PER_FRAME U8
        // stereo pairs; expand each channel to signed 16-bit for retro-go.
        rg_audio_frame_t mixbuf[AUDIO_SAMPLES_PER_FRAME];
        supervision_update_sound(audio_stream, AUDIO_SAMPLES_PER_FRAME * 2);
        for (int i = 0; i < AUDIO_SAMPLES_PER_FRAME; i++)
        {
            mixbuf[i].left  = (int16_t)(audio_stream[2 * i + 0] * SV_AUDIO_SCALE);
            mixbuf[i].right = (int16_t)(audio_stream[2 * i + 1] * SV_AUDIO_SCALE);
        }

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit(mixbuf, AUDIO_SAMPLES_PER_FRAME);

        if (skipFrames == 0)
            skipFrames = app->frameskip;
        else if (skipFrames > 0)
            skipFrames--;
    }
}
