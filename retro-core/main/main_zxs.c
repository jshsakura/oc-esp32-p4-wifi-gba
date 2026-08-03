/*
 * ZX Spectrum 48K for retro-go.
 *
 * Drives the floooh/chips core's own API (the same core the game-and-watch reference
 * port at Core/Src/porting/zxs uses): one frame is zx_exec() for one PAL frame's worth
 * of microseconds (19968us = 69888 T-states @ 3.5MHz), the picture is read out of the
 * core's framebuffer as 4-bit palette indices, and the sound comes out of the core's
 * float audio callback as [-1,1] samples which this front-end widens to signed 16-bit.
 *
 * The framebuffer is 512 wide (ZX_FRAMEBUFFER_WIDTH) with only the first 320 columns of
 * each row decoded (ZX_DISPLAY_WIDTH); the real Spectrum screen is the 256x192 content
 * at (32,32) inside that. This port crops to the content -- both the content and this
 * target's screen are 4:3, so the border is dropped for a clean fit, the same decision
 * the reference port made.
 */
#include "shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chips_common.h"
// Path-qualified rather than bare "z80.h"/"mem.h": folded into retro-core, this
// file is compiled with smsplus's and neopop's include dirs on the same search
// path, and both expose a same-named header of their own (smsplus/cpu/z80.h,
// neopop/mem.h) that would otherwise shadow chips's, the same ambiguity o2em's
// cpu.h hit against gnuboy's.
#include "../components/zxs/src/z80.h"
#include "beeper.h"
#include "ay38910.h"
#include "kbd.h"
#include "../components/zxs/src/mem.h"
#include "clk.h"
#include "zx.h"

/* ZX_FRAMEBUFFER_WIDTH is 512 (the decode stride); the visible content is the 256x192
 * screen at column/row 32 inside it. */
#define ZX_FB_STRIDE    512
#define ZX_CONTENT_LEFT 32
#define ZX_CONTENT_TOP  32
#define ZX_CONTENT_W    256
#define ZX_CONTENT_H    192

/* One PAL frame: 312 scanlines * 224 T-states / 3.5MHz = 69888 T-states = 19968us. */
#define ZX_FRAME_US     19968
#define ZX_FPS          50
#define ZX_SAMPLE_RATE  22050
#define ZX_SAMPLES_PER_FRAME  (ZX_SAMPLE_RATE / ZX_FPS)  /* 441 */

#define RGB565(r, g, b) ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

static rg_app_t *app;
/* Frame surfaces in PSRAM, not internal RAM.
 *
 * This core was a standalone app, where MEM_FAST was free -- it was the only
 * emulator in the binary. Folded into retro-core it is one of twenty, and there
 * are 42KB of internal RAM left across all of them. Asking for more does not
 * fail loudly: rg_alloc warns "CAPS not fully met", hands back PSRAM anyway, and
 * the mismatch surfaces later as an assert in heap_caps_free. */
static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;

static zx_t zx;
static uint16_t zx_pal565[16];

/* The core emits float samples ([-1,1]) via its audio callback during zx_exec, in
 * batches of desc.audio.num_samples. Accumulate them as int16; after the frame, one
 * frame's worth is handed to rg_audio_submit and any over-production carries into the
 * next frame so the 128-batch callback and the 441-per-frame consumer never click. */
#define ZX_SND_CAP  (ZX_SAMPLES_PER_FRAME + 256)
static int16_t zx_snd[ZX_SND_CAP];
static int zx_snd_w;

static void audio_cb(const float *samples, int num_samples, void *user_data)
{
    (void)user_data;
    for (int i = 0; i < num_samples && zx_snd_w < ZX_SND_CAP; i++)
    {
        float v = samples[i];
        if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
        zx_snd[zx_snd_w++] = (int16_t)(v * 22000.0f);
    }
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

/* Dump the live zx_t straight to disk. It is a static at a fixed address, so all its
 * internal self-pointers (mem page table -> zx.ram/rom) and the audio callback pointer
 * stay valid after reload within the same firmware build -- the same trick the
 * reference port uses, and it avoids the chips snapshot API's large scratch buffer. */
static bool save_state_handler(const char *filename)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp) return false;
    bool ok = fwrite(&zx, sizeof(zx), 1, fp) == 1;
    fclose(fp);
    return ok;
}

static bool load_state_handler(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) return false;
    bool ok = fread(&zx, sizeof(zx), 1, fp) == 1;
    fclose(fp);
    return ok;
}

static bool reset_handler(bool hard)
{
    /* Re-init from the ROM keeps the audio callback and machine type; a cold zx_reset
     * of the live struct would lose the callback pointer set at startup. */
    zx_desc_t desc = {0};
    desc.type = ZX_TYPE_48K;
    desc.joystick_type = ZX_JOYSTICKTYPE_KEMPSTON;
    desc.audio.callback.func = audio_cb;
    desc.audio.num_samples = 128;
    desc.audio.sample_rate = ZX_SAMPLE_RATE;
    desc.audio.beeper_volume = 0.5f;
    desc.audio.ay_volume = 0.5f;
    zx_init(&zx, &desc);
    return true;
}

static void event_handler(int event, void *arg)
{
    (void)arg;
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(currentUpdate, 0);
}

static void build_palette(void)
{
    chips_display_info_t di = zx_display_info(&zx);
    const uint32_t *pal = (const uint32_t *)di.palette.ptr;  /* 0xAABBGGRR */
    for (int i = 0; i < 16; i++)
    {
        uint32_t c = pal[i];
        zx_pal565[i] = RGB565(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
    }
}

/* The 48K machine ROM is mandatory -- without it the core has nothing to execute. */
static bool load_bios(zx_desc_t *desc)
{
    FILE *fp = fopen(RG_BASE_PATH_BIOS "/zxs/48.rom", "rb");
    if (!fp)
        return false;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    bool ok = false;
    if (size >= 0x4000)
    {
        uint8_t *rom = (uint8_t *)rg_alloc(0x4000, MEM_SLOW);
        if (rom && fread(rom, 1, 0x4000, fp) == 0x4000)
        {
            desc->roms.zx48k.ptr = rom;
            desc->roms.zx48k.size = 0x4000;
            ok = true;
        }
        else free(rom);
    }
    fclose(fp);
    return ok;
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

void zxs_main(void)
{
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
    };

    app = rg_system_reinit(ZX_SAMPLE_RATE, &handlers, NULL);

    /* Two buffers on purpose: rg_display_submit() reads the surface in place on another
     * task through a one-deep blocking queue, so a single buffer both tears and stalls
     * the emulator on every submit. */
    updates[0] = rg_surface_create(ZX_CONTENT_W, ZX_CONTENT_H, RG_PIXEL_565_LE, MEM_SLOW);
    updates[1] = rg_surface_create(ZX_CONTENT_W, ZX_CONTENT_H, RG_PIXEL_565_LE, MEM_SLOW);
    currentUpdate = updates[0];

    zx_desc_t desc = {0};
    desc.type = ZX_TYPE_48K;
    desc.joystick_type = ZX_JOYSTICKTYPE_KEMPSTON;
    desc.audio.callback.func = audio_cb;
    desc.audio.num_samples = 128;
    desc.audio.sample_rate = ZX_SAMPLE_RATE;
    desc.audio.beeper_volume = 0.5f;
    desc.audio.ay_volume = 0.5f;

    if (!load_bios(&desc))
        rg_system_rom_load_failed(_("Missing ZX Spectrum 48K BIOS."));

    zx_init(&zx, &desc);
    build_palette();

    /* Quickload the game (.z80/.sna/.tap/.tzx/...). zx_quickload parses the format and
     * drops the machine straight into it, read-only on the buffer. */
    uint32_t rom_size = 0;
    uint8_t *rom_data = load_file(app->romPath, &rom_size);
    if (rom_data)
    {
        chips_range_t range = { .ptr = rom_data, .size = rom_size };
        zx_quickload(&zx, range);
    }

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

        /* Kempston joystick: D-pad + A (fire). */
        uint8_t m = 0;
        if (joystick & RG_KEY_LEFT)  m |= ZX_JOYSTICK_LEFT;
        if (joystick & RG_KEY_RIGHT) m |= ZX_JOYSTICK_RIGHT;
        if (joystick & RG_KEY_UP)    m |= ZX_JOYSTICK_UP;
        if (joystick & RG_KEY_DOWN)  m |= ZX_JOYSTICK_DOWN;
        if (joystick & RG_KEY_A)     m |= ZX_JOYSTICK_BTN;
        zx_joystick(&zx, m);

        /* Most ZX games need a keyboard key to start or pick controls (e.g. "0=start",
         * "L=load"), which the Kempston pad cannot send. Map START/SELECT/B to the keys
         * the reference port defaulted to (Enter / Space / '0'), going straight to the
         * matrix via kbd_key_down/kbd_key_up so Space and the arrows aren't stolen by
         * zx's Kempston remap. */
        static const int gc_keys[3] = { 0x0D /*Enter*/, 0x20 /*Space*/, '0' };
        bool gc_pressed[3] = {
            (joystick & RG_KEY_START)  != 0,
            (joystick & RG_KEY_SELECT) != 0,
            (joystick & RG_KEY_B)      != 0,
        };
        for (int i = 0; i < 3; i++)
        {
            if (gc_pressed[i]) kbd_key_down(&zx.kbd, gc_keys[i]);
            else               kbd_key_up  (&zx.kbd, gc_keys[i]);
        }

        zx_snd_w = 0;
        zx_exec(&zx, ZX_FRAME_US);

        if (drawFrame)
        {
            uint16_t *dst = (uint16_t *)currentUpdate->data;
            for (int y = 0; y < ZX_CONTENT_H; y++)
            {
                const uint8_t *src = &zx.fb[(ZX_CONTENT_TOP + y) * ZX_FB_STRIDE + ZX_CONTENT_LEFT];
                uint16_t *row = dst + y * ZX_CONTENT_W;
                for (int x = 0; x < ZX_CONTENT_W; x++)
                    row[x] = zx_pal565[src[x] & 15];
            }
            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
        }

        /* Hand one frame's worth of audio to the system. Take ZX_SAMPLES_PER_FRAME out
         * of what the callback accumulated; if the core produced more, carry it forward,
         * if less, zero-fill so the stream never underruns. */
        static rg_audio_frame_t mixbuf[ZX_SAMPLES_PER_FRAME];
        for (int i = 0; i < ZX_SAMPLES_PER_FRAME; i++)
        {
            int16_t s = (i < zx_snd_w) ? zx_snd[i] : 0;
            mixbuf[i].left = s;
            mixbuf[i].right = s;
        }
        int rem = zx_snd_w - ZX_SAMPLES_PER_FRAME;
        if (rem > 0)
            memmove(zx_snd, zx_snd + ZX_SAMPLES_PER_FRAME, (size_t)rem * sizeof(int16_t));
        zx_snd_w = rem > 0 ? rem : 0;

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit(mixbuf, ZX_SAMPLES_PER_FRAME);

        if (skipFrames == 0)
            skipFrames = app->frameskip;
        else if (skipFrames > 0)
            skipFrames--;
    }
}
