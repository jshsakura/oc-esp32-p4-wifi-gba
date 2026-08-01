//============================================================================
// main.c - Retro-Go frontend for the oswan (Bandai WonderSwan / WSC) core.
//
// The oswan core emulates the WonderSwan handheld: a NEC V30 CPU with a
// 224x144 colour LCD and 4-channel audio. The core renders RGB565 directly
// into its own FrameBuffer[240*144] (stride 240, 8px left margin) and emits
// audio into a ring buffer (sndbuffer). This file is the glue between that
// core and the retro-go framework (display, input, audio, save state),
// modelled on supervision-go/main/main.c and the STM32 reference.
//============================================================================

#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "WSHard.h"
#include "WS.h"
#include "WSApu.h"
#include "WSRender.h"
#include "WSFileio.h"

// ws_create_from_flash is defined in ws_fileio.c but not declared in any
// header. Forward-declare it here (the G&W reference does the same).
int ws_create_from_flash(const uint8_t *data, uint32_t size);

// WonderSwan native screen: 224x144 visible, rendered into FrameBuffer at row
// stride 240 with an 8-pixel left margin (see RefreshLine in WSRender.c).
#define WS_WIDTH            224
#define WS_HEIGHT           144
#define WS_STRIDE           240     // pixels per row in FrameBuffer
#define WS_XOFF             8       // left margin in pixels

// WonderSwan runs at ~75 FPS. Audio is generated at 44100 Hz, so one frame
// is 44100/75 = 588 stereo sample pairs.
#define WS_FPS              75
#define AUDIO_SAMPLE_RATE   44100
#define AUDIO_SAMPLES_PER_FRAME (AUDIO_SAMPLE_RATE / WS_FPS)

// The APU ring buffer (WSApu.c): must match the non-NATIVE_AUDIO layout.
#define WS_SND_RNGSIZE      (8 * 512)

static rg_app_t *app;

// The core's framebuffer is a global array (NOSDL_FB). We wrap it in a
// rg_surface_t with the correct stride/offset so rg_display_submit reads the
// 224 visible pixels per row without any per-frame copy.
// (FrameBuffer is declared in WSRender.h as uint16_t[240*144] when NOSDL_FB
// is defined, which we set in main/CMakeLists.txt.)

static rg_surface_t ws_surface = {
    .width = WS_WIDTH,
    .height = WS_HEIGHT,
    .stride = WS_STRIDE * 2,    // bytes per row (240 pixels * 2)
    .offset = WS_XOFF * 2,      // byte offset to first visible pixel (8 * 2)
    .format = RG_PIXEL_565_LE,
    .data = NULL,               // set in app_main after core init
};

// gameName is used by WsLoadEeprom/WsSaveEeprom to build the internal-EEPROM
// save path. Set to the ROM path so the filename is derivable.
char gameName[512];

// --- Core stubs -----------------------------------------------------------
// oswan's Interrupt() calls graphics_paint() at vblank (the SDL front-end blits
// there). We blit FrameBuffer ourselves in the app loop, so this is a no-op.
void graphics_paint(void) { }

// oswan's APU (WSApu.c) calls these at sample boundaries. The SDL backend
// would lock/unlock its audio thread; we drain the ring buffer directly in
// the app loop, so these are empty.
void Sound_APU_Start(void) { }
void Sound_APU_End(void)   { }
void Sound_APUClose(void)  { }
void Pause_Sound(void)     { }

// --- Input ----------------------------------------------------------------
// Bit layout expected by WS.c's Interrupt() (ButtonState):
//   Y1-4 = bits 0-3, X1-4 (D-pad) = bits 4-7, OPTION=8, START=9, A=10, B=11.
// We drive ONLY the X-pad (horizontal D-pad) as the main directional input.
uint32_t WsInputGetState(void)
{
    uint32_t pad = rg_input_read_gamepad();
    uint32_t s = 0;
    if (pad & RG_KEY_UP)     s |= 0x0010; // X1 up
    if (pad & RG_KEY_RIGHT)  s |= 0x0020; // X2 right
    if (pad & RG_KEY_DOWN)   s |= 0x0040; // X3 down
    if (pad & RG_KEY_LEFT)   s |= 0x0080; // X4 left
    if (pad & RG_KEY_A)      s |= 0x0400; // WS A
    if (pad & RG_KEY_B)      s |= 0x0800; // WS B
    if (pad & RG_KEY_START)  s |= 0x0200; // WS START
    if (pad & RG_KEY_SELECT) s |= 0x0100; // WS OPTION (TIME / SELECT)
    return s;
}

// --- Save / load state ----------------------------------------------------
// oswan provides FILE*-based savestate functions that include a version header
// (WS_STATE_MAGIC) so corrupt/incompatible files are rejected on load.
static bool save_state_handler(const char *filename)
{
    FILE *f = fopen(filename, "wb");
    if (!f)
        return false;
    uint32_t err = WsSaveStateToFile(f);
    fclose(f);
    return err == 0;
}

static bool load_state_handler(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
        return false;
    uint32_t err = WsLoadStateFromFile(f);
    fclose(f);
    return err == 0;
}

static bool reset_handler(bool hard)
{
    WsReset();
    return true;
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(&ws_surface, filename, width, height);
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(&ws_surface, 0);
}

// --- ROM loading ----------------------------------------------------------
// ws_create_from_flash() takes a memory buffer and sets up the bank map,
// pointing ROMMap[] entries directly into the buffer (XIP-style). The buffer
// must outlive the call, so we keep it for the whole app lifetime.
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

// --- Audio drain ----------------------------------------------------------
// Drain the APU ring buffer (filled by apuWaveSet during WsRun) and resample
// this frame's samples to fill exactly AUDIO_SAMPLES_PER_FRAME output pairs.
// Linear interpolation avoids the nearest-neighbour aliasing.
static void ws_audio_submit(rg_audio_frame_t *mixbuf, int count)
{
    int32_t avail = wBuf - rBuf;
    if (avail < 0)
        avail += WS_SND_RNGSIZE;

    if (avail < 1)
    {
        memset(mixbuf, 0, count * sizeof(rg_audio_frame_t));
        return;
    }

    // Resample exactly `avail` source samples to `count` output samples.
    uint32_t step = ((uint32_t)avail << 16) / count;
    if (step == 0)
        step = 1;
    uint32_t pos = 0;

    for (int i = 0; i < count; i++)
    {
        uint32_t whole = pos >> 16;
        uint32_t frac  = pos & 0xFFFF;
        int32_t i0 = rBuf + (int32_t)whole;
        if (i0 >= WS_SND_RNGSIZE) i0 -= WS_SND_RNGSIZE;
        int32_t i1 = i0 + 1;
        if (i1 >= WS_SND_RNGSIZE) i1 -= WS_SND_RNGSIZE;
        // Left channel (lerp)
        int32_t s0 = sndbuffer[0][i0];
        int32_t s1 = sndbuffer[0][i1];
        mixbuf[i].left = (int16_t)(s0 + (((s1 - s0) * (int32_t)frac) >> 16));
        // Right channel (lerp)
        s0 = sndbuffer[1][i0];
        s1 = sndbuffer[1][i1];
        mixbuf[i].right = (int16_t)(s0 + (((s1 - s0) * (int32_t)frac) >> 16));
        pos += step;
    }

    // Consume all available samples (one frame of audio per frame of output).
    rBuf += avail;
    if (rBuf >= WS_SND_RNGSIZE)
        rBuf -= WS_SND_RNGSIZE;
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
    };

    app = rg_system_init(AUDIO_SAMPLE_RATE, &handlers, NULL);

    // Point the display surface at the core's framebuffer.
    ws_surface.data = FrameBuffer;

    // Set gameName for WsLoadEeprom (uses strrchr to extract the filename).
    snprintf(gameName, sizeof(gameName), "%s", app->romPath);

    // Initialise the core (loads internal EEPROM).
    WsInit();

    // Load the ROM into memory and pass it to the core.
    uint32_t rom_size = 0;
    uint8_t *rom_data = load_rom(app->romPath, &rom_size);
    // ws_create_from_flash rejects NULL/tiny ROMs and returns 0 on failure.
    // A bad/unsupported file must never crash the device: alert and exit.
    if (!rom_data || !ws_create_from_flash(rom_data, rom_size))
    {
        rg_gui_alert(_("Error"), _("Could not load the game file."));
        rg_system_exit();
    }

    WsReset();

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

        // Skip per-scanline rendering on frames we won't display, so the
        // emulator can keep pace (WsRun always renders otherwise).
        extern int ws_render_enabled;
        ws_render_enabled = drawFrame;

        // Run one frame: V30 CPU + interrupts + render scanlines.
        WsRun();

        if (drawFrame)
            rg_display_submit(&ws_surface, 0);

        // Audio: drain the APU ring buffer and resample to the output rate.
        rg_audio_frame_t mixbuf[AUDIO_SAMPLES_PER_FRAME];
        ws_audio_submit(mixbuf, AUDIO_SAMPLES_PER_FRAME);
        rg_audio_submit(mixbuf, AUDIO_SAMPLES_PER_FRAME);

        rg_system_tick(rg_system_timer() - startTime);

        if (skipFrames == 0)
            skipFrames = app->frameskip;
        else if (skipFrames > 0)
            skipFrames--;
    }
}
