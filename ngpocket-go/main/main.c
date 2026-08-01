#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neopop_compat.h"
#include "neopop.h"
#include "flash.h"

/*
 * Neo Geo Pocket Color emulator (NeoPop) ported to retro-go on ESP32-P4.
 *
 * Architecture:
 *   - The NeoPop core calls system_VBL() once per emulated frame (from inside
 *     interrupt.c::updateTimers). system_VBL() converts the core's 12-bit
 *     indexed framebuffer (cfb, format X4B4G4R4) into an RGB565 surface and
 *     raises a flag consumed by the main loop.
 *   - The main loop runs emulate() until that flag is raised, then submits the
 *     frame to the display and a chunk of mono audio to the audio subsystem.
 *   - Battery saves (flash) live next to the ROM as a .sav file and are loaded
 *     by rom_loaded()->flash_read() and flushed via flash_commit().
 */

// NGP runs at ~59.95 fps. We target 60 for the audio buffer math.
#define NGP_FPS_APPROX       60
#define AUDIO_SAMPLE_RATE    22050
#define AUDIO_BUFFER_LENGTH  (AUDIO_SAMPLE_RATE / NGP_FPS_APPROX + 1)

// NGPC joystick register, active-low is NOT used here: NeoPop expects bits SET
// when a key is held (see System_SDL/system_input.c, joy_mask[]).
#define JOYPORT_ADDR         0x6F82
#define NGP_JOY_UP           0x01
#define NGP_JOY_DOWN         0x02
#define NGP_JOY_LEFT         0x04
#define NGP_JOY_RIGHT        0x08
#define NGP_JOY_A            0x10
#define NGP_JOY_B            0x20
#define NGP_JOY_OPTION       0x40

static rg_app_t *app;
static rg_surface_t *update;

// Path of the battery-save (flash) file, set before rom_loaded() runs.
static char *flash_path;

// 12-bit NGP colour index (B4G4R4 packed low) -> RGB565, precomputed once.
static uint16_t rgb565_lut[16 * 16 * 16];

// Raised by system_VBL() to signal the main loop that a full frame is drawn.
static volatile bool frame_ready;

// Owned by the core (extern in neopop.h), defined here.
_u8 system_frameskip_key;

// Mono scratch buffer filled by the core's sound_update().
static _u16 chip_buffer[AUDIO_BUFFER_LENGTH];

// ---------------------------------------------------------------------------
// Colour conversion
// ---------------------------------------------------------------------------

static void rgb565_lut_init(void)
{
    // cfb pixel format is X4B4G4R4: bits 0-3 = R, 4-7 = G, 8-11 = B.
    // The low 12 bits form the index into this table (see System_SDL rgb_lookup).
    for (int i = 0; i < 16 * 16 * 16; i++)
    {
        int r4 = i & 0xF;
        int g4 = (i >> 4) & 0xF;
        int b4 = (i >> 8) & 0xF;
        // Expand 4-bit channels to 5/6/5 bits, replicating the high bits.
        int r5 = (r4 << 1) | (r4 >> 3);
        int g6 = (g4 << 2) | (g4 >> 2);
        int b5 = (b4 << 1) | (b4 >> 3);
        rgb565_lut[i] = (r5 << 11) | (g6 << 5) | b5;
    }
}

// ---------------------------------------------------------------------------
// ROM / flash / state IO callbacks required by the core
// ---------------------------------------------------------------------------

static int load_rom_file(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
        return -1;

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len <= 0)
    {
        fclose(fp);
        return -1;
    }

    rom.data = (_u8 *)malloc(len);
    if (!rom.data)
    {
        fclose(fp);
        return -1;
    }

    if (fread(rom.data, 1, len, fp) != (size_t)len)
    {
        free(rom.data);
        rom.data = NULL;
        fclose(fp);
        return -1;
    }

    rom.length = (_u32)len;
    fclose(fp);
    return 0;
}

BOOL system_io_rom_read(char *filename, _u8 *buffer, _u32 bufferLength)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
        return FALSE;
    size_t n = fread(buffer, 1, bufferLength, fp);
    fclose(fp);
    return n > 0 ? TRUE : FALSE;
}

BOOL system_io_flash_read(_u8 *buffer, _u32 bufferLength)
{
    if (!flash_path)
        return FALSE;
    FILE *fp = fopen(flash_path, "rb");
    if (!fp)
        return FALSE; // No save yet -- silent failure, as the core expects.
    size_t n = fread(buffer, 1, bufferLength, fp);
    fclose(fp);
    return (n == bufferLength) ? TRUE : FALSE;
}

BOOL system_io_flash_write(_u8 *buffer, _u32 bufferLength)
{
    if (!flash_path || bufferLength == 0)
        return FALSE;
    FILE *fp = fopen(flash_path, "wb");
    if (!fp)
        return FALSE;
    size_t n = fwrite(buffer, 1, bufferLength, fp);
    fclose(fp);
    return (n == bufferLength) ? TRUE : FALSE;
}

BOOL system_io_state_read(char *filename, _u8 *buffer, _u32 bufferLength)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
        return FALSE;
    size_t n = fread(buffer, 1, bufferLength, fp);
    fclose(fp);
    return (n == bufferLength) ? TRUE : FALSE;
}

BOOL system_io_state_write(char *filename, _u8 *buffer, _u32 bufferLength)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp)
        return FALSE;
    size_t n = fwrite(buffer, 1, bufferLength, fp);
    fclose(fp);
    return (n == bufferLength) ? TRUE : FALSE;
}

// ---------------------------------------------------------------------------
// Misc system callbacks
// ---------------------------------------------------------------------------

void __cdecl system_message(char *vaMessage, ...)
{
    char message[1024];
    va_list vl;
    va_start(vl, vaMessage);
    vsnprintf(message, sizeof(message), vaMessage, vl);
    va_end(vl);
    RG_LOGI("NeoPop: %s", message);
}

char *system_get_string(STRINGS string_id)
{
    switch (string_id)
    {
    case IDS_BADFLASH: return (char *)"Invalid flash data";
    case IDS_BADSTATE: return (char *)"Invalid / corrupt state file";
    case IDS_WRONGROM: return (char *)"State is for a different ROM";
    case IDS_POWER:    return (char *)"Power";
    default:           return (char *)"";
    }
}

BOOL system_comms_read(_u8 *buffer)  { (void)buffer; return FALSE; }
BOOL system_comms_poll(_u8 *buffer)  { (void)buffer; return FALSE; }
void system_comms_write(_u8 data)    { (void)data; }

void system_sound_chipreset(void)
{
    sound_init(AUDIO_SAMPLE_RATE);
}

void system_sound_silence(void)
{
    // Nothing to do: retro-go's audio layer handles muting.
}

// ---------------------------------------------------------------------------
// Per-frame display callback (invoked by the core at vertical blank)
// ---------------------------------------------------------------------------

void system_VBL(void)
{
    uint16_t *dest = (uint16_t *)update->data;
    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        // cfb rows are spaced SCREEN_WIDTH (160) apart, not 256.
        const _u16 *src = cfb + (y * SCREEN_WIDTH);
        for (int x = 0; x < SCREEN_WIDTH; x++)
            dest[x] = rgb565_lut[src[x] & 0xFFF];
        dest += SCREEN_WIDTH;
    }
    frame_ready = true;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

static void update_joystick(uint32_t joystick)
{
    _u8 joy = 0;
    if (joystick & RG_KEY_UP)     joy |= NGP_JOY_UP;
    if (joystick & RG_KEY_DOWN)   joy |= NGP_JOY_DOWN;
    if (joystick & RG_KEY_LEFT)   joy |= NGP_JOY_LEFT;
    if (joystick & RG_KEY_RIGHT)  joy |= NGP_JOY_RIGHT;
    if (joystick & RG_KEY_A)      joy |= NGP_JOY_A;
    if (joystick & RG_KEY_B)      joy |= NGP_JOY_B;
    if (joystick & RG_KEY_START)  joy |= NGP_JOY_OPTION;
    ram[JOYPORT_ADDR] = joy;
}

// ---------------------------------------------------------------------------
// retro-go handlers
// ---------------------------------------------------------------------------

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(update, filename, width, height);
}

static bool save_state_handler(const char *filename)
{
    return state_store(filename) ? true : false;
}

static bool load_state_handler(const char *filename)
{
    return state_restore(filename) ? true : false;
}

static bool reset_handler(bool hard)
{
    reset();
    return true;
}

static void event_handler(int event, void *arg)
{
    (void)arg;
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(update, 0);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

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

    rgb565_lut_init();

    update = rg_surface_create(SCREEN_WIDTH, SCREEN_HEIGHT, RG_PIXEL_565_LE, MEM_FAST);
    if (!update)
        RG_PANIC("Could not create display surface");

    // Core configuration
    language_english = TRUE;
    system_colour = COLOURMODE_AUTO;
    system_frameskip_key = 1;
    mute = FALSE;

    flash_path = rg_emu_get_path(RG_PATH_SAVE_SRAM, app->romPath);

    // Install the HLE BIOS (self-contained, always succeeds)
    bios_install();
    // Initialise sound chips for our sample rate
    sound_init(AUDIO_SAMPLE_RATE);

    // Load the ROM
    if (load_rom_file(app->romPath) != 0)
        RG_PANIC("Could not load the game file.");

    // rom_loaded() extracts the header, applies per-game hacks and loads flash
    rom_loaded();
    reset();

    RG_LOGI("NeoPop: loaded '%s' (%u bytes)", rom.name, (unsigned)rom.length);

    while (true)
    {
        rg_audio_sample_t mixbuffer[AUDIO_BUFFER_LENGTH];
        uint32_t joystick = rg_input_read_gamepad();

        if (joystick & (RG_KEY_MENU | RG_KEY_OPTION))
        {
            // Persist battery saves before entering a menu.
            flash_commit();
            if (joystick & RG_KEY_MENU)
                rg_gui_game_menu();
            else
                rg_gui_options_menu();
        }

        int64_t start_time = rg_system_timer();

        update_joystick(joystick);

        // Emulate until the core signals a completed frame via system_VBL().
        frame_ready = false;
        while (!frame_ready)
            emulate();

        rg_display_submit(update, 0);

        // Generate one frame worth of mono audio and hand it to retro-go.
        int samples = AUDIO_BUFFER_LENGTH;
        sound_update(chip_buffer, samples * (int)sizeof(_u16));
        for (int i = 0; i < samples; i++)
        {
            // NeoPop chip output is 0..0x7FFF (silence = 0); casting straight
            // to int16_t keeps that range valid for the signed audio path.
            int16_t s = (int16_t)chip_buffer[i];
            mixbuffer[i].left = s;
            mixbuffer[i].right = s;
        }

        rg_system_tick(rg_system_timer() - start_time);
        rg_audio_submit(mixbuffer, samples);
    }

    RG_PANIC("ngpocket-go Ended");
}
