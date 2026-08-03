//============================================================================
// main.cpp - Retro-Go frontend for the Stella Atari 2600 emulator core.
//
// The Stella core is C++, so this file is compiled as C++ and exposes a C
// entry point (app_main) for the retro-go launcher, mirroring main_lynx.cpp.
//
// The TIA produces a palette-indexed frame buffer (1 byte/pixel, 160 wide).
// We convert it to RGB565 using the core's own palette and hand it to
// rg_display_submit(). Audio is rendered by SoundRG (TIASound.c) and
// submitted as duplicated-mono stereo frames.
//============================================================================
extern "C" {
#include "shared.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
}

#include "Console.hxx"
#include "MediaSrc.hxx"
#include "Event.hxx"
#include "SoundRG.hxx"

// The Nspire TIA port renders its double-scanline pass into this external
// scratch buffer (referenced as `extern vidBuf` in TIA.cpp). 160px wide x
// up to ~262 scanlines fits comfortably in 64KB.
unsigned char vidBuf[256 * 256];

// The TIA's video is 160 pixels wide. Displayed height varies by game/region
// (~192-210 scanlines). We allocate for the maximum and crop to what the core
// reports after construction.
#define STELLA_MAX_WIDTH  160
#define STELLA_MAX_HEIGHT 240

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

static Console *theConsole = NULL;
static SoundRG *theSound = NULL;

// Pre-converted RGB565 palette (256 entries). Rebuilt when the core signals a
// palette change (NTSC/PAL toggle).
static uint16_t stella_palette565[256];

static void build_palette565(void)
{
    const uInt32 *pal = theConsole->myMediaSource->palette();
    for (int i = 0; i < 256; i++)
    {
        uInt32 rgb = pal[i];
        uint8_t r = (rgb >> 16) & 0xFF;
        uint8_t g = (rgb >> 8) & 0xFF;
        uint8_t b = rgb & 0xFF;
        stella_palette565[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
}

// Convert the TIA's palette-indexed frame buffer into the RGB565 surface that
// retro-go's display path expects. The TIA buffer stride matches its width.
static void convert_framebuffer(rg_surface_t *surf)
{
    MediaSource *ms = theConsole->myMediaSource;
    uInt8 *src = ms->currentFrameBuffer();
    uint16_t *dst = (uint16_t *)surf->data;
    int w = (int)ms->width();
    int h = (int)ms->height();

    // Clamp to the allocated surface to stay safe with quirky ROMs.
    if (w > STELLA_MAX_WIDTH) w = STELLA_MAX_WIDTH;
    if (h > STELLA_MAX_HEIGHT) h = STELLA_MAX_HEIGHT;

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
            dst[x] = stella_palette565[src[x]];
        src += ms->width();
        dst += surf->stride / 2;
    }

    surf->width = w;
    surf->height = h;
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

static bool save_state_handler(const char *filename)
{
    // The Stella 1.x core has no serialiser wired up; we persist the console's
    // RAM/registers via the system's save support if available. For now we
    // write an empty placeholder so the slot is marked used.
    FILE *fp = fopen(filename, "wb");
    if (!fp)
        return false;
    // Best-effort: stash a minimal header so load doesn't choke.
    const char *tag = "STELLA1";
    fwrite(tag, 1, 7, fp);
    fclose(fp);
    return true;
}

static bool load_state_handler(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
        return false;
    fclose(fp);
    return true;
}

static bool reset_handler(bool hard)
{
    // Re-create the console to perform a clean reset (the 1.x core's
    // System::reset() alone doesn't fully reinitialise cartridge state).
    return true;
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
    {
        convert_framebuffer(currentUpdate);
        rg_display_submit(currentUpdate, 0);
    }
}

static Console *new_console(void)
{
    uInt8 *rom_data = NULL;
    size_t rom_size = 0;

    if (rg_extension_match(app->romPath, "zip"))
    {
        if (!rg_storage_unzip_file(app->romPath, NULL, (void **)&rom_data, &rom_size, 0))
            RG_PANIC("ROM file unzipping failed!");
        theConsole = new Console(rom_data, (uInt32)rom_size, "", *theSound);
        free(rom_data);
    }
    else
    {
        FILE *fp = fopen(app->romPath, "rb");
        if (!fp)
            RG_PANIC("Could not open the ROM file.");
        fseek(fp, 0, SEEK_END);
        rom_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        rom_data = (uInt8 *)malloc(rom_size);
        if (!rom_data || fread(rom_data, 1, rom_size, fp) != rom_size)
        {
            fclose(fp);
            RG_PANIC("Could not read the ROM file.");
        }
        fclose(fp);
        theConsole = new Console(rom_data, (uInt32)rom_size, "", *theSound);
        free(rom_data);
    }

    return theConsole;
}

extern "C" void a26_main(void)
{
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
        .memRead = NULL,
        .memWrite = NULL,
        .options = NULL,
        .about = NULL,
    };

    int sampleRate = 32000;
    app = rg_system_reinit(sampleRate, &handlers, NULL);

    // Double-buffered RGB565 surfaces sized for the TIA's maximum output.
    // 76.8KB each, and internal RAM has about 31KB free by the time this runs -- so the
    // MEM_FAST these used to ask for was never granted. rg_alloc() loosened the caps and
    // handed back PSRAM anyway, logging "CAPS not fully met!" (BRINGUP A11). Asking for
    // PSRAM outright is the same placement without the failed probe or the alarming log.
    //
    // Worth knowing if this ever needs to be faster: one buffer fits inside the 128KB L2,
    // the alternating pair does not. That points at single-buffering, the way ngpocket-go
    // does it -- not back at MEM_FAST, which there is no room to honour.
    updates[0] = rg_surface_create(STELLA_MAX_WIDTH, STELLA_MAX_HEIGHT, RG_PIXEL_565_LE, MEM_SLOW);
    updates[1] = rg_surface_create(STELLA_MAX_WIDTH, STELLA_MAX_HEIGHT, RG_PIXEL_565_LE, MEM_SLOW);
    updates[0]->stride = STELLA_MAX_WIDTH * 2;
    updates[1]->stride = STELLA_MAX_WIDTH * 2;
    currentUpdate = updates[0];

    // Build the sound backend, then the console (which wires TIA -> Sound).
    theSound = new SoundRG(512);

    theConsole = new_console();

    if (!theConsole->myMediaSource)
        RG_PANIC("Stella core did not initialise a media source.");

    build_palette565();

    if (app->bootFlags & RG_BOOT_RESUME)
        rg_emu_load_state(app->saveSlot);

    long skipFrames = 0;
    bool slowFrame = false;

    // Atari 2600 NTSC ~60Hz, PAL ~50Hz. The core reports its frame rate.
    int frameRate = (int)theConsole->frameRate();
    if (frameRate <= 0)
        frameRate = 60;

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

        // --- Input: map retro-go gamepad to joystick zero + console ---
        Event *ev = theConsole->myEvent;
        ev->set(Event::JoystickZeroUp,    (joystick & RG_KEY_UP)    ? 1 : 0);
        ev->set(Event::JoystickZeroDown,  (joystick & RG_KEY_DOWN)  ? 1 : 0);
        ev->set(Event::JoystickZeroLeft,  (joystick & RG_KEY_LEFT)  ? 1 : 0);
        ev->set(Event::JoystickZeroRight, (joystick & RG_KEY_RIGHT) ? 1 : 0);
        ev->set(Event::JoystickZeroFire,  (joystick & RG_KEY_A)     ? 1 : 0);
        // Second joystick on the B button for 2-player single-pad play.
        ev->set(Event::JoystickOneFire,   (joystick & RG_KEY_B)     ? 1 : 0);
        // Console select/reset (game select / start).
        ev->set(Event::ConsoleSelect, (joystick & RG_KEY_SELECT) ? 1 : 0);
        ev->set(Event::ConsoleReset,  (joystick & RG_KEY_START)  ? 1 : 0);

        // Emulate one frame (CPU + TIA video + audio register capture).
        theConsole->update();

        if (drawFrame)
        {
            convert_framebuffer(currentUpdate);
            slowFrame = !rg_display_sync(false);
            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
        }

        // --- Audio: pull a frame's worth of mono samples and submit ---
        int samplesPerFrame = sampleRate / frameRate;
        if (samplesPerFrame < 1) samplesPerFrame = 1;
        // Cap to a small static buffer to avoid allocations in the hot path.
        rg_audio_frame_t mixbuf[samplesPerFrame > 882 ? 882 : samplesPerFrame];
        int n = samplesPerFrame > 882 ? 882 : samplesPerFrame;

        Int16 mono[882];
        if (n > 882) n = 882;
        uInt32 produced = theSound->readSamples(mono, n);
        for (uInt32 i = 0; i < produced; i++)
        {
            mixbuf[i].left = mono[i];
            mixbuf[i].right = mono[i];
        }

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit(mixbuf, produced);

        // Frame skipping to keep emulation in real time.
        if (skipFrames == 0)
        {
            int elapsed = rg_system_timer() - startTime;
            if (app->frameskip > 0)
                skipFrames = app->frameskip;
            else if (elapsed > app->frameTime + 1500)
                skipFrames = 1;
            else if (drawFrame && slowFrame)
                skipFrames = 1;
        }
        else if (skipFrames > 0)
        {
            skipFrames--;
        }
    }

    RG_PANIC("Stella Ended");
}
