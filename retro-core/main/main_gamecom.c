/*
 * Tiger Game.com for retro-go.
 *
 * Drives the core's own API (the same core the game-and-watch reference port at
 * Core/Src/porting/gamecom uses): one frame is gamecom_run_frame(), the picture is the
 * core's 200x160 framebuffer of 5-entry palette indices, and the sound is the core's
 * mono int16 mix (SG0/SG1 wavetable channels + DAC) which this front-end widens to
 * stereo. The surface is paletted (RG_PIXEL_PAL565_BE) so the display path expands it,
 * the same arrangement the Atari 7800 and PC Engine cores use here.
 *
 * The Game.com PDA menu and many in-game prompts are stylus-only, and this device has
 * no touchscreen. Without a bridge the inserted cartridge never launches: after the boot
 * animation the PDA menu becomes interactive around frame ~400, so this port holds a tap
 * on the CARTRIDGE icon through that window, matching the reference port's launch
 * sequence. The buttons cover all four game.com action buttons (A/B/C/D).
 */
#include "shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gamecom_core.h"

#define GC_SAMPLE_RATE  22050
#define GC_FPS          60
#define GC_SAMPLES_PER_FRAME  (GC_SAMPLE_RATE / GC_FPS)  /* 367 */

static rg_app_t *app;
static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;

/* BIOS/cart buffers and their lengths, kept for the app's whole lifetime: gamecom_init
 * holds the pointers (it does not copy), and reset re-runs gamecom_init with them. */
static uint8_t *gc_irom, *gc_krom, *gc_cart;
static uint32_t gc_isz, gc_ksz, gc_csz;

/* Auto-launch window: hold a tap on the CARTRIDGE icon through this frame range so the
 * cart enters without a touchscreen (matches the reference port's launch sequence). */
#define GC_LAUNCH_BEGIN 400
#define GC_LAUNCH_END   520

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

static int rw_write(void *ctx, void *data, uint32_t len)
{
    return fwrite(data, 1, len, (FILE *)ctx) == len;
}
static int rw_read(void *ctx, void *data, uint32_t len)
{
    return fread(data, 1, len, (FILE *)ctx) == len;
}

static bool save_state_handler(const char *filename)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp) return false;
    int ok = gamecom_state_rw(rw_write, fp, 1);
    fclose(fp);
    return ok != 0;
}

static bool load_state_handler(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) return false;
    int ok = gamecom_state_rw(rw_read, fp, 0);
    fclose(fp);
    return ok != 0;
}

static bool reset_handler(bool hard)
{
    /* The core has no separate soft-reset: gamecom_init is the reset, re-running it from
     * the same BIOS/cart pointers the app loaded at startup. */
    return gamecom_init(gc_irom, (int)gc_isz, gc_krom, (int)gc_ksz, gc_cart, (int)gc_csz) == 0;
}

/* The 8KB battery-backed cartridge NVRAM (0xE000-0xFFFF) is game.com's "internal
 * memory" save (high scores, PDA data). Persist it across sessions like a real
 * game.com's battery: load right after gamecom_init() zeroes it, flush on shutdown. */
static void nvram_load(void)
{
    char *path = rg_emu_get_path(RG_PATH_SAVE_SRAM, app->romPath);
    if (!path) return;
    FILE *fp = fopen(path, "rb");
    if (fp)
    {
        uint32_t n;
        uint8_t *nv = gamecom_nvram(&n);
        if (fread(nv, 1, n, fp) != n) { /* short/empty .sav: keep the init zeroes */ }
        fclose(fp);
    }
    free(path);
}

static void nvram_save(void)
{
    char *path = rg_emu_get_path(RG_PATH_SAVE_SRAM, app->romPath);
    if (!path) return;
    FILE *fp = fopen(path, "wb");
    if (fp)
    {
        uint32_t n;
        uint8_t *nv = gamecom_nvram(&n);
        fwrite(nv, 1, n, fp);
        fclose(fp);
    }
    free(path);
}

static void event_handler(int event, void *arg)
{
    (void)arg;
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(currentUpdate, 0);
    else if (event == RG_EVENT_SHUTDOWN)
        nvram_save();
}

/* palette_data is 5 RGB triplets set by gamecom_init. Build the surface palette once
 * per load: the game.com palette is fixed for the run. Big-endian 565 to match
 * RG_PIXEL_PAL565_BE; indices above 4 (rare, edge cases) clamp to the brightest entry. */
static void build_palette(rg_surface_t *surface)
{
    for (int i = 0; i < 256; i++)
    {
        int idx = i <= 4 ? i : 4;
        uint8_t r = gamecom_palette[idx][0];
        uint8_t g = gamecom_palette[idx][1];
        uint8_t b = gamecom_palette[idx][2];
        uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        surface->palette[i] = (c << 8) | (c >> 8);
    }
}

static uint8_t *load_file(const char *path, uint32_t *out_size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
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

/* BIOS files are mandatory (internal 4KB at 0x1000, external "kernel" PDA firmware up
 * to 256KB). User-supplied -- Tiger BIOS is copyrighted, like the other cores' BIOSes. */
static uint8_t *load_bios(const char *name, uint32_t want, uint32_t *out_size)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/gamecom/%s", RG_BASE_PATH_BIOS, name);
    uint8_t *p = load_file(path, out_size);
    if (!p || *out_size < want)
    {
        free(p);
        *out_size = 0;
        return NULL;
    }
    return p;
}

void gamecom_main(void)
{
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
    };

    app = rg_system_reinit(GC_SAMPLE_RATE, &handlers, NULL);

    /* Two buffers on purpose: rg_display_submit() reads the surface in place on another
     * task through a one-deep blocking queue, so a single buffer both tears and stalls
     * the emulator on every submit. */
    updates[0] = rg_surface_create(GAMECOM_W, GAMECOM_H, RG_PIXEL_PAL565_BE, MEM_FAST);
    updates[1] = rg_surface_create(GAMECOM_W, GAMECOM_H, RG_PIXEL_PAL565_BE, MEM_FAST);
    currentUpdate = updates[0];

    uint32_t isz = 0, ksz = 0, csz = 0;
    uint8_t *irom = load_bios("internal.bin", 0x1000, &isz);
    uint8_t *krom = load_bios("external.bin", 0x40000, &ksz);
    uint8_t *cart = load_file(app->romPath, &csz);

    if (!irom || !krom)
        rg_system_rom_load_failed(_("Missing Game.com BIOS."));

    if (gamecom_init(irom, (int)isz, krom, (int)ksz, cart, (int)csz) != 0)
        rg_system_rom_load_failed(_("Could not load the game file."));

    /* Stash for reset_handler (gamecom_init keeps these pointers, never copies). */
    gc_irom = irom; gc_isz = isz;
    gc_krom = krom; gc_ksz = ksz;
    gc_cart = cart; gc_csz = csz;

    build_palette(updates[0]);
    build_palette(updates[1]);

    nvram_load();  /* restore cartridge NVRAM after gamecom_init zeroed it */

    if (app->bootFlags & RG_BOOT_RESUME)
        rg_emu_load_state(app->saveSlot);

    long skipFrames = 0;
    int frame = 0;

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

        /* Buttons -> game.com ports (active low: 0 bit = pressed). The game.com has
         * four action buttons A/B/C/D; map all of them so games/menus needing C or D
         * can be operated (START->C, SELECT->D). */
        uint8_t in0 = 0xFF, in1 = 0xFF, in2 = 0xFF;
        if (joystick & RG_KEY_UP)     in0 &= ~GC_IN0_UP;
        if (joystick & RG_KEY_DOWN)   in0 &= ~GC_IN0_DOWN;
        if (joystick & RG_KEY_LEFT)   in0 &= ~GC_IN0_LEFT;
        if (joystick & RG_KEY_RIGHT)  in0 &= ~GC_IN0_RIGHT;
        if (joystick & RG_KEY_A)      in0 &= ~GC_IN0_A;
        if (joystick & RG_KEY_B)      in1 &= ~GC_IN1_B;
        if (joystick & RG_KEY_START)  in1 &= ~GC_IN1_C;
        if (joystick & RG_KEY_SELECT) in2 &= ~GC_IN2_D;
        gamecom_set_input_state(in0, in1, in2);

        /* Stylus bridge: auto-tap the CARTRIDGE icon during the launch window so the
         * cart enters without a touchscreen. */
        if (frame >= GC_LAUNCH_BEGIN && frame < GC_LAUNCH_END)
            gamecom_set_stylus(45, 60, 1);
        else
            gamecom_set_stylus(0, 0, 0);

        gamecom_run_frame();

        if (drawFrame)
        {
            memcpy(currentUpdate->data, gamecom_fb, (size_t)GAMECOM_W * GAMECOM_H);
            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
        }

        /* gamecom_audio_mix produces signed-16 mono; widen to signed-16 stereo. */
        static rg_audio_frame_t mixbuf[GC_SAMPLES_PER_FRAME];
        static int16_t mono[GC_SAMPLES_PER_FRAME];
        gamecom_audio_mix(mono, GC_SAMPLES_PER_FRAME, GC_SAMPLE_RATE);
        for (int i = 0; i < GC_SAMPLES_PER_FRAME; i++)
        {
            mixbuf[i].left = mono[i];
            mixbuf[i].right = mono[i];
        }

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit(mixbuf, GC_SAMPLES_PER_FRAME);

        if (skipFrames == 0)
            skipFrames = app->frameskip;
        else if (skipFrames > 0)
            skipFrames--;

        frame++;
    }
}
