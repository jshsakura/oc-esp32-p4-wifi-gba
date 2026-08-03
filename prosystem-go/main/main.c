/*
 * Atari 7800 (ProSystem) for retro-go.
 *
 * Drives the core's own API rather than its bundled libretro front-end: one
 * frame is prosystem_ExecuteFrame(), the picture comes out of maria_surface as
 * 8-bit palette indices, and the sound comes out of tia_buffer as unsigned 8-bit
 * mono. Both are handed over without a per-pixel conversion -- the surface is
 * paletted (RG_PIXEL_PAL565_BE) so the display path expands it, which is the
 * same arrangement the Master System and PC Engine cores use here.
 */
#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ProSystem.h"
#include "Cartridge.h"
#include "Maria.h"
#include "Palette.h"
#include "Tia.h"
#include "Region.h"
#include "Database.h"
#include "Bios.h"

/* maria_surface holds the whole display area; the visible window is a slice of
 * it. Both are fixed by the core (Maria.c: displayArea {0,16,319,258},
 * visibleArea {0,26,319,248}), so the geometry is constant. */
#define A78_STRIDE  320
#define A78_WIDTH   320
#define A78_HEIGHT  223
#define A78_OFFSET  ((26 - 16) * A78_STRIDE)

/* prosystem's own rate: two samples per scanline, once per field. NTSC is
 * 60 * 262 * 2 = 31440 Hz, which is what libretro.c reports as sample_rate. */
#define A78_SAMPLE_RATE  31440
#define A78_MAX_SAMPLES  1024

static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;
static rg_app_t *app;

static uint8_t a78_input[17];

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

static bool save_state_handler(const char *filename)
{
    /* prosystem_Save() writes into a caller buffer and has no way to report the
     * size it needed, so the buffer has to be comfortably larger than the state
     * (RAM, cart bank state, the CPU and the two sound chips). */
    size_t cap = 64 * 1024;
    char *buffer = rg_alloc(cap, MEM_SLOW);
    if (!buffer)
        return false;
    bool ok = prosystem_Save(buffer, false);
    if (ok)
    {
        FILE *fp = fopen(filename, "wb");
        ok = fp && fwrite(buffer, 1, cap, fp) == cap;
        if (fp)
            fclose(fp);
    }
    free(buffer);
    return ok;
}

static bool load_state_handler(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
        return false;
    size_t cap = 64 * 1024;
    char *buffer = rg_alloc(cap, MEM_SLOW);
    if (!buffer)
    {
        fclose(fp);
        return false;
    }
    bool ok = fread(buffer, 1, cap, fp) == cap && prosystem_Load(buffer);
    fclose(fp);
    free(buffer);
    return ok;
}

static bool reset_handler(bool hard)
{
    prosystem_Reset();
    return true;
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(currentUpdate, 0);
}

/* The core takes a flat array of "is this input active", indexed by its own
 * Equates.h ordering for player 1. Only the seven the 7800 pad has are driven. */
static void update_input(void)
{
    uint32_t joystick = rg_input_read_gamepad();
    memset(a78_input, 0, sizeof(a78_input));
    a78_input[3] = (joystick & RG_KEY_RIGHT) ? 1 : 0;
    a78_input[2] = (joystick & RG_KEY_LEFT) ? 1 : 0;
    a78_input[1] = (joystick & RG_KEY_DOWN) ? 1 : 0;
    a78_input[0] = (joystick & RG_KEY_UP) ? 1 : 0;
    a78_input[4] = (joystick & RG_KEY_B) ? 1 : 0;
    a78_input[5] = (joystick & RG_KEY_A) ? 1 : 0;
    a78_input[6] = (joystick & RG_KEY_START) ? 1 : 0;   /* console reset */
    a78_input[7] = (joystick & RG_KEY_SELECT) ? 1 : 0;  /* console select */
}

/* palette_data is 256 RGB triplets. Build the surface palette once per load:
 * the 7800 palette is fixed for the run, so nothing here belongs in the frame
 * loop. Big-endian 565 to match RG_PIXEL_PAL565_BE. */
static void build_palette(rg_surface_t *surface)
{
    for (int i = 0; i < 256; i++)
    {
        uint8_t r = palette_data[i * 3 + 0];
        uint8_t g = palette_data[i * 3 + 1];
        uint8_t b = palette_data[i * 3 + 2];
        uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        surface->palette[i] = (c << 8) | (c >> 8);
    }
}

static uint8_t *load_rom(const char *path, uint32_t *out_size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *data = size > 0 ? (uint8_t *)rg_alloc(size, MEM_SLOW) : NULL;
    if (!data || fread(data, 1, size, fp) != (size_t)size)
    {
        free(data);
        data = NULL;
    }
    fclose(fp);
    *out_size = data ? (uint32_t)size : 0;
    return data;
}

void app_main(void)
{
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
    };

    app = rg_system_init(A78_SAMPLE_RATE, &handlers, NULL);

    /* Two buffers on purpose: rg_display_submit() reads the surface in place on
     * another task through a one-deep blocking queue, so a single buffer both
     * tears and stalls the emulator on every submit. */
    updates[0] = rg_surface_create(A78_WIDTH, A78_HEIGHT, RG_PIXEL_PAL565_BE, MEM_FAST);
    updates[1] = rg_surface_create(A78_WIDTH, A78_HEIGHT, RG_PIXEL_PAL565_BE, MEM_FAST);
    currentUpdate = updates[0];

    database_Initialize();

    uint32_t rom_size = 0;
    uint8_t *rom_data = load_rom(app->romPath, &rom_size);
    if (!rom_data || !cartridge_Load(false, rom_data, rom_size))
        rg_system_rom_load_failed(_("Could not load the game file."));

    /* The 7800 BIOS is optional: without it the core boots the cart directly,
     * which is what most emulators do and what nearly every game expects. */
    bios_Load(RG_BASE_PATH_BIOS "/7800.rom");

    region_Reset();
    prosystem_Reset();

    build_palette(updates[0]);
    build_palette(updates[1]);

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

        update_input();
        prosystem_ExecuteFrame(a78_input);

        if (drawFrame)
        {
            /* One row per scanline, and the core's stride already matches the
             * surface's, so the visible window is a straight copy of a slice. */
            memcpy(currentUpdate->data, maria_surface + A78_OFFSET,
                   (size_t)A78_STRIDE * A78_HEIGHT);
            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
        }

        /* tia_buffer is unsigned 8-bit mono, tia_size samples for this field.
         * Centre it and widen to signed 16-bit stereo. */
        static rg_audio_frame_t mixbuf[A78_MAX_SAMPLES];
        uint32_t samples = tia_size < A78_MAX_SAMPLES ? tia_size : A78_MAX_SAMPLES;
        for (uint32_t i = 0; i < samples; i++)
        {
            int16_t s = (int16_t)((int)tia_buffer[i] - 128) << 8;
            mixbuf[i].left = s;
            mixbuf[i].right = s;
        }

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit(mixbuf, samples);

        if (skipFrames == 0)
            skipFrames = app->frameskip;
        else if (skipFrames > 0)
            skipFrames--;
    }
}
