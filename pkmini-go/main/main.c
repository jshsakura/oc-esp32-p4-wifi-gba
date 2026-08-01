//============================================================================
// main.c - Retro-Go frontend for the PokeMini (Pokemon Mini) core.
//
// The Pokemon Mini is a handheld with a 96x64 LCD running at 72 FPS, powered
// by a custom MinxCPU 8-bit processor. The core (JustBurn's PokeMini) renders
// RGB565 via a pluggable video spec and emits signed 16-bit mono audio through
// a PWM-style generator. This file is the glue between that core and the
// retro-go framework (display, input, audio, save state), modelled on
// supervision-go/main/main.c and the STM32H7 reference (main_pkmini).
//
// We build the core with TARGET_GNW, which makes it use rg_storage.h (already
// part of retro-go) for file-based save states and EEPROM, and strips out the
// 32-bit renderers we don't need. The native 96x64 (1x1) video spec is used;
// retro-go's display layer handles all upscaling.
//============================================================================

#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "PokeMini.h"
#include "Hardware.h"
#include "MinxIO.h"
#include "MinxAudio.h"
#include "Video.h"
#include "Video_x1.h"

// The Pokemon Mini LCD refreshes at 72 FPS. Audio is generated at 44100 Hz, so
// one frame is 44100/72 = 612.5 samples. We alternate 612/613 via an
// accumulator to stay pitch-correct over time.
#define PKMINI_FPS          72
#define PKMINI_SAMPLE_RATE  44100
#define PKMINI_WIDTH        96
#define PKMINI_HEIGHT       64

// Maximum mono samples extracted in a single frame (ceil(44100/72) = 613).
#define PKMINI_AUDIO_MAX    613

// PWM sound FIFO depth passed to PokeMini_Create. Must be a power of two;
// 4096 entries gives enough headroom for the ~613 samples consumed per frame.
#define PMSOUNDBUFF         (2048 * 2)

static rg_app_t *app;
static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;

// Mono S16 samples pulled from the core each frame; duplicated into L/R below.
static int16_t audio_samples[PKMINI_AUDIO_MAX];

// EEPROM (cartridge save) path, built once at startup from the ROM path.
static char eeprom_path[RG_PATH_MAX];

// --- Palette option -------------------------------------------------------
// The Pokemon Mini LCD was monochrome; the core ships 14 colour profiles.
static const char *palette_names[] = {
    "Default", "Old", "B&W", "Green", "Inv. Green",
    "Red", "Inv. Red", "Blue LCD", "LED Backlight", "Girl Power",
    "Blue", "Inv. Blue", "Sepia", "Inv. B&W"
};
#define PALETTE_COUNT (int)(sizeof(palette_names) / sizeof(palette_names[0]))

static rg_gui_event_t palette_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    int pal = (int)rg_settings_get_number(NS_APP, "PkPalette", 0);
    int max = PALETTE_COUNT - 1;

    if (event == RG_DIALOG_PREV) pal = pal > 0 ? pal - 1 : max;
    if (event == RG_DIALOG_NEXT) pal = pal < max ? pal + 1 : 0;

    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        rg_settings_set_number(NS_APP, "PkPalette", pal);
        rg_settings_commit();
        PokeMini_VideoPalette_Index(pal, NULL, CommandLine.lcdcontrast, CommandLine.lcdbright);
        PokeMini_ApplyChanges();
    }

    strcpy(option->value, palette_names[pal]);
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
// Under TARGET_GNW the core reads/writes save states directly to/from a file,
// so the handlers are thin wrappers. We also flush the EEPROM (cartridge
// save) whenever a state is saved, so the auto-save timer covers both.
static bool save_state_handler(const char *filename)
{
    if (PokeMini_EEPROMWritten)
        PokeMini_SaveEEPROMFile(eeprom_path);
    return PokeMini_SaveSSStream(filename, 0);
}

static bool load_state_handler(const char *filename)
{
    return PokeMini_LoadSSStream(filename, 0);
}

static bool reset_handler(bool hard)
{
    PokeMini_Reset(hard ? 1 : 0);
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
    else if (event == RG_EVENT_SHUTDOWN)
    {
        // Last-chance EEPROM flush on power-off / exit.
        if (PokeMini_EEPROMWritten)
            PokeMini_SaveEEPROMFile(eeprom_path);
    }
}

// --- Input ----------------------------------------------------------------
// Pokemon Mini keys: A, B, C (menu/confirm), D-pad, and a shock sensor.
// We map the device's Start to the PM's C button and Select to the shock
// sensor; MENU/OPTION are intercepted earlier by the framework for menus.
static void handle_input(void)
{
    uint32_t pad = rg_input_read_gamepad();
    MinxIO_Keypad(MINX_KEY_A,     (pad & RG_KEY_A)      ? 1 : 0);
    MinxIO_Keypad(MINX_KEY_B,     (pad & RG_KEY_B)      ? 1 : 0);
    MinxIO_Keypad(MINX_KEY_C,     (pad & RG_KEY_START)  ? 1 : 0);
    MinxIO_Keypad(MINX_KEY_SHOCK, (pad & RG_KEY_SELECT) ? 1 : 0);
    MinxIO_Keypad(MINX_KEY_UP,    (pad & RG_KEY_UP)     ? 1 : 0);
    MinxIO_Keypad(MINX_KEY_DOWN,  (pad & RG_KEY_DOWN)   ? 1 : 0);
    MinxIO_Keypad(MINX_KEY_LEFT,  (pad & RG_KEY_LEFT)   ? 1 : 0);
    MinxIO_Keypad(MINX_KEY_RIGHT, (pad & RG_KEY_RIGHT)  ? 1 : 0);
}

// --- ROM loading ----------------------------------------------------------
// Load the ROM file (optionally from a zip) into a heap buffer. The buffer is
// kept for the entire app lifetime — the core holds PM_ROM and never frees it
// under TARGET_GNW.
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

    app = rg_system_init(PKMINI_SAMPLE_RATE, &handlers, NULL);

    // 96x64 RGB565, double buffered. The 1x1 video spec writes one row per
    // PKMINI_WIDTH pixels, matching the surface stride exactly.
    updates[0] = rg_surface_create(PKMINI_WIDTH, PKMINI_HEIGHT, RG_PIXEL_565_LE, MEM_FAST);
    updates[1] = rg_surface_create(PKMINI_WIDTH, PKMINI_HEIGHT, RG_PIXEL_565_LE, MEM_FAST);
    currentUpdate = updates[0];

    // --- Core initialisation (follows the STM32 reference ordering) -------
    CommandLineInit();

    // EEPROM / cartridge-save path follows the same convention as other cores.
    char *sram = rg_emu_get_path(RG_PATH_SAVE_SRAM, app->romPath);
    snprintf(eeprom_path, sizeof(eeprom_path), "%s", sram ? sram : "PokeMini.eep");
    free(sram);
    strncpy(CommandLine.eeprom_file, eeprom_path, sizeof(CommandLine.eeprom_file) - 1);
    CommandLine.eeprom_file[sizeof(CommandLine.eeprom_file) - 1] = '\0';

    // Sensible defaults: freebios HLE, piezo filter on, host RTC sync.
    CommandLine.forcefreebios = 0;
    CommandLine.eeprom_share  = 0;
    CommandLine.updatertc     = 2;
    CommandLine.palette       = (int)rg_settings_get_number(NS_APP, "PkPalette", 0);
    CommandLine.lcdfilter     = 1;  // Dot-matrix
    CommandLine.lcdmode       = 0;  // Analog
    CommandLine.piezofilter   = 1;  // ON
    CommandLine.lowpassfilter = 1;  // ON

    // Use the native 96x64 spec; retro-go scales to the panel.
    PokeMini_SetVideo((TPokeMini_VideoSpec *)&PokeMini_Video1x1, 16, 0, 0);

    // Create emulator (loads freebios, allocates subsystems + sound FIFO).
    PokeMini_Create(0, PMSOUNDBUFF);

    // BGR16 layout produces R<<11|G<<5|B which matches RG_PIXEL_565_LE.
    PokeMini_VideoPalette_Init(PokeMini_BGR16, 0);
    PokeMini_VideoPalette_Index(CommandLine.palette, NULL, CommandLine.lcdcontrast, CommandLine.lcdbright);
    PokeMini_ApplyChanges();
    MinxAudio_ChangeEngine(CommandLine.sound);

    // --- ROM load ---------------------------------------------------------
    uint32_t rom_size = 0;
    uint8_t *rom_data = load_rom(app->romPath, &rom_size);

    // Pokemon Mini ROMs are 8 KB - 2 MB. A bad/unsupported file must never
    // crash the device: tell the user and return to the launcher.
    if (!rom_data || rom_size <= 0x2100 || rom_size > 0x200000)
    {
        rg_gui_alert(_("Error"), _("Could not load the game file."));
        rg_system_exit();
    }

    // The core addresses ROM via PM_ROM[addr & PM_ROM_Mask], so the buffer
    // must be padded up to the next power of two and filled with 0xFF.
    PokeMini_FreeColorInfo();
    int rom_mask = GetMultiple2Mask((int)rom_size);
    int rom_padded = rom_mask + 1;
    if (rom_padded > (int)rom_size)
    {
        uint8_t *padded = (uint8_t *)malloc(rom_padded);
        if (!padded)
        {
            rg_gui_alert(_("Error"), _("Out of memory."));
            rg_system_exit();
        }
        memcpy(padded, rom_data, rom_size);
        memset(padded + rom_size, 0xFF, rom_padded - rom_size);
        free(rom_data);
        rom_data = padded;
    }
    PM_ROM = rom_data;
    PM_ROM_Mask = rom_mask;
    PM_ROM_Size = rom_padded;
    NewMulticart();

    // --- EEPROM (cartridge save) -----------------------------------------
    MinxIO_FormatEEPROM();
    if (rg_storage_exists(eeprom_path))
        PokeMini_LoadEEPROMFile(eeprom_path);

    PokeMini_Reset(0);

    // Resume from the last used save slot if requested.
    if (app->bootFlags & RG_BOOT_RESUME)
        rg_emu_load_state(app->saveSlot);

    // --- Main loop --------------------------------------------------------
    // 44100/72 = 612.5: accumulate the fractional remainder so we request 613
    // samples on roughly every other frame, keeping the long-term average
    // exactly 612.5 and the audio pitch correct.
    float audio_accum = 0.0f;
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

        handle_input();

        // Run one frame: MinxCPU + timers + PRC + LCD decay + audio FIFO fill.
        PokeMini_EmulateFrame();

        if (drawFrame)
        {
            // Blit the 96x64 LCD into the current surface (pitch = width).
            PokeMini_VideoBlit(currentUpdate->data, PKMINI_WIDTH);
            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
        }

        // Audio: pull the exact number of samples generated this frame.
        audio_accum += (float)PKMINI_SAMPLE_RATE / PKMINI_FPS;
        int num_samples = (int)audio_accum;
        audio_accum -= num_samples;
        if (num_samples > PKMINI_AUDIO_MAX)
            num_samples = PKMINI_AUDIO_MAX;

        MinxAudio_GetSamplesS16Ch(audio_samples, num_samples, 1);

        // Mono → stereo: duplicate the single channel into L/R.
        rg_audio_frame_t mixbuf[PKMINI_AUDIO_MAX];
        for (int i = 0; i < num_samples; i++)
        {
            mixbuf[i].left = audio_samples[i];
            mixbuf[i].right = audio_samples[i];
        }

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit(mixbuf, num_samples);

        if (skipFrames == 0)
            skipFrames = app->frameskip;
        else if (skipFrames > 0)
            skipFrames--;
    }
}
