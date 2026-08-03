//============================================================================
// main.c - Retro-Go frontend for the FCEUmm NES/Famicom core.
//
// FCEUmm is a mature NES emulator with broad iNES mapper support (300+ boards),
// built-in FDS disk support, NSF player, and save-state compatibility. This
// file replaces the nofrendo bridge (retro-core/main/main_nes.c) with a
// stand-alone app that gives the core its own internal-RAM budget — retro-core's
// sram_low (183 KB, already 176 KB full) could not absorb fceumm's larger
// .iram0.text footprint alongside SNES9x, gnuboy, handy, and SMS.
//
// The bridge is modelled on the STM32 Game-and-Watch port
// (Core/Src/porting/nes_fceu/main_nes_fceu.c) for the FCEUmm-specific boot,
// audio, and FDS logic, and on supervision-go/main/main.c for the retro-go
// framework side (surfaces, audio submit, input, save-state handlers).
//============================================================================

#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fceu.h"
#include "fceu-types.h"
#include "fceu-state.h"
#include "fceu-cart.h"
#include "fds.h"
#include "driver.h"
#include "video.h"
#include "input.h"
#include "general.h"

// The core's save-state API (FCEUSS_Save_Mem / FCEUSS_Load_Mem) does not touch
// files directly: it serialises into a flat byte buffer handed to it through
// memstream_set_buffer(). We expose that buffer, run the core, then persist the
// resulting bytes to the retro-go save file ourselves. The header lives in the
// submodule's libretro-common tree.
#include <streams/memory_stream.h>

// NES NTSC runs at 60 Hz. FCEUmm's audio rate defaults to 48000 Hz (44100 on
// some configs); we pass 48000 to rg_system_init and the framework resamples
// to the hardware DAC. At 60 fps that is 800 samples per frame.
#define AUDIO_SAMPLE_RATE       48000

// The core writes one 256x240 indexed frame into XBuf every vblank, then we
// look each index up in palette565[] to produce the RGB565 surface retro-go's
// display queue expects.
#define NES_WIDTH               256
#define NES_HEIGHT              240

// --- Globals consumed by the FCEUmm core ------------------------------------
// These match the externs / callbacks the core expects from its platform
// driver. XBuf (the indexed framebuffer) is owned and allocated by the core
// itself in video.c via FCEU_malloc when TARGET_GNW is not defined, so we only
// pull in its declaration through video.h — defining it here caused a multiple
// definition at link time.

// Overclocking globals (read by the PPU to decide how many extra scanlines to
// render). We keep them at the "no overclock" defaults.
unsigned overclock_enabled    = 0;
unsigned overclocked          = 0;
unsigned skip_7bit_overclocking = 1;
unsigned totalscanlines       = 240;
unsigned normal_scanlines     = 240;
unsigned extrascanlines       = 0;
unsigned vblankscanlines      = 0;

// Region and duty-cycle globals the core declares extern in fceu.h but leaves
// for the frontend to own. dendy = 0 keeps NTSC timing; the FCEUI_LoadGame path
// in fceu.c flips it for Dendy-region ROMs. swapDuty = 0 leaves the APU duty
// bits alone (some pirate boards need this set, but the default is off).
unsigned dendy   = 0;
unsigned swapDuty = 0;

// Player 1 joystick byte, set before each FCEUI_Emulate call.
static uint32_t fceu_joystick = 0;

// RGB565 palette LUT filled by FCEUD_SetPalette. The core calls this for every
// palette entry it changes (typically all 64 + mirroring at boot, then
// per-frame changes for games that swap palettes mid-frame).
static uint16_t palette565[256];

static rg_app_t *app;
static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;

// --- FCEUD_* platform callbacks ---------------------------------------------
// The core declares these as extern; we provide them here. They are the only
// OS-level touchpoints the core has beyond malloc/fopen.

void FCEUD_SetPalette(uint8 index, uint8 r, uint8 g, uint8 b)
{
    palette565[index] = ((uint16_t)(r >> 3) << 11) |
                        ((uint16_t)(g >> 2) << 5)  |
                        (uint16_t)(b >> 3);
}

void FCEUD_PrintError(char *s)  { RG_LOGE("fceumm: %s", s); }
void FCEUD_Message(char *s)     { RG_LOGI("fceumm: %s", s); }
void FCEUD_DispMessage(enum retro_log_level level, unsigned duration, const char *str)
{
    (void)duration;
    if (level >= RETRO_LOG_WARN)
        RG_LOGW("fceumm: %s", str);
    else
        RG_LOGI("fceumm: %s", str);
}

// --- Save / load state ------------------------------------------------------
// FCEUmm's public API only exposes FCEUSS_Save_Mem / FCEUSS_Load_Mem, which
// serialise into a buffer registered via memstream_set_buffer() rather than
// into a FILE *. We hand the core a heap buffer, let it fill/drain it, then do
// the file I/O ourselves. 256 KiB is comfortably above the ~200 KiB the worst
// MMC5 / FDS state chunks reach.
#define SAVESTATE_MAX_SIZE (256 * 1024)

static bool save_state_handler(const char *filename)
{
    uint8_t *buf = (uint8_t *)malloc(SAVESTATE_MAX_SIZE);
    if (!buf)
        return false;

    memstream_set_buffer(buf, SAVESTATE_MAX_SIZE);
    FCEUSS_Save_Mem();
    uint64_t size = memstream_get_last_size();

    FILE *fp = fopen(filename, "wb");
    bool ok = false;
    if (fp)
    {
        ok = (fwrite(buf, 1, (size_t)size, fp) == (size_t)size);
        fclose(fp);
    }
    free(buf);
    return ok;
}

static bool load_state_handler(const char *filename)
{
    uint8_t *buf = (uint8_t *)malloc(SAVESTATE_MAX_SIZE);
    if (!buf)
        return false;

    bool ok = false;
    FILE *fp = fopen(filename, "rb");
    if (fp)
    {
        size_t size = fread(buf, 1, SAVESTATE_MAX_SIZE, fp);
        fclose(fp);
        memstream_set_buffer(buf, size);
        FCEUSS_Load_Mem();
        ok = true;
    }
    free(buf);
    return ok;
}

static bool reset_handler(bool hard)
{
    FCEUI_ResetNES();
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

// --- Options menu -----------------------------------------------------------
static rg_gui_event_t overscan_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    static bool crop = false;
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
        crop = !crop;
    strcpy(option->value, crop ? _("On") : _("Off"));
    return RG_DIALOG_VOID;
}

static rg_gui_event_t sprite_limit_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    static bool disabled = false;
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        disabled = !disabled;
        FCEUI_DisableSpriteLimitation(disabled);
    }
    strcpy(option->value, disabled ? _("Unlimited") : _("Normal"));
    return RG_DIALOG_VOID;
}

static void options_handler(rg_gui_option_t *dest)
{
    int end = 0;
    while (dest[end].label || dest[end].value || dest[end].arg || dest[end].flags || dest[end].update_cb)
        end++;
    dest[end] = (rg_gui_option_t){0, _("Overscan"), "-", RG_DIALOG_FLAG_NORMAL, &overscan_cb};
    dest[end + 1] = (rg_gui_option_t){0, _("Sprite limit"), "-", RG_DIALOG_FLAG_NORMAL, &sprite_limit_cb};
    dest[end + 2] = (rg_gui_option_t)RG_DIALOG_END;
}

// --- ROM loading ------------------------------------------------------------
static uint8_t *load_rom(const char *path, uint32_t *out_size)
{
    uint8_t *data = NULL;
    size_t size = 0;

    if (rg_extension_match(path, "zip"))
    {
        if (!rg_storage_unzip_file(path, NULL, (void **)&data, &size, RG_FILE_ALIGN_8KB))
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

// --- App entry point --------------------------------------------------------
extern void app_main(void);

void app_main(void)
{
    const rg_handlers_t handlers = {
        .loadState  = &load_state_handler,
        .saveState  = &save_state_handler,
        .reset      = &reset_handler,
        .screenshot = &screenshot_handler,
        .event      = &event_handler,
        .options    = &options_handler,
    };

    app = rg_system_init(AUDIO_SAMPLE_RATE, &handlers, NULL);

    // 256x240 RGB565, double buffered. The core allocates its own indexed
    // XBuf internally (video.c); after each frame we look those bytes up in
    // palette565 and write RGB565 into whichever surface we are about to submit.
    updates[0] = rg_surface_create(NES_WIDTH, NES_HEIGHT, RG_PIXEL_565_LE, MEM_FAST);
    updates[1] = rg_surface_create(NES_WIDTH, NES_HEIGHT, RG_PIXEL_565_LE, MEM_FAST);
    currentUpdate = updates[0];

    // FCEUmm's fds.c (LINUX_EMU path) opens "bios/nes/disksys.rom" with a
    // relative path. The SD card is mounted at /sd, so chdir there makes the
    // relative path resolve to /sd/bios/nes/disksys.rom.
    chdir("/sd");

    FCEUI_Initialize();

    uint32_t rom_size = 0;
    uint8_t *rom_data = load_rom(app->romPath, &rom_size);
    if (!rom_data)
        rg_system_rom_load_failed(_("Could not load the game file."));

    FCEUGI *gameInfo = FCEUI_LoadGame(app->romPath, rom_data, rom_size, NULL);
    if (!gameInfo)
        rg_system_rom_load_failed(_("Unsupported ROM or missing FDS BIOS."));

    PowerNES();
    FCEUI_SetInput(0, SI_GAMEPAD, &fceu_joystick, 0);
    FCEUI_Sound(AUDIO_SAMPLE_RATE);
    FCEUI_SetSoundVolume(150);

    if (app->bootFlags & RG_BOOT_RESUME)
        rg_emu_load_state(app->saveSlot);

    // PAL is a global the core sets during FCEUI_LoadGame based on the ROM's
    // region (FCEUGI.vidsys). PAL NES runs at 50 fps, NTSC at 60 fps.
    rg_system_set_tick_rate(PAL ? 50 : 60);

    int skipFrames = 0;

    while (1)
    {
        uint32_t pad = rg_input_read_gamepad();

        if (pad & (RG_KEY_MENU | RG_KEY_OPTION))
        {
            if (pad & RG_KEY_MENU)
                rg_gui_game_menu();
            else
                rg_gui_options_menu();
        }

        int64_t startTime = rg_system_timer();
        bool drawFrame = !skipFrames;

        // Map the physical gamepad to FCEUmm's JOY_* bits.
        fceu_joystick = 0;
        if (pad & RG_KEY_UP)     fceu_joystick |= JOY_UP;
        if (pad & RG_KEY_DOWN)   fceu_joystick |= JOY_DOWN;
        if (pad & RG_KEY_LEFT)   fceu_joystick |= JOY_LEFT;
        if (pad & RG_KEY_RIGHT)  fceu_joystick |= JOY_RIGHT;
        if (pad & RG_KEY_A)      fceu_joystick |= JOY_A;
        if (pad & RG_KEY_B)      fceu_joystick |= JOY_B;
        if (pad & RG_KEY_START)  fceu_joystick |= JOY_START;
        if (pad & RG_KEY_SELECT) fceu_joystick |= JOY_SELECT;

        // Run one frame. The core fills nes_framebuffer (via XBuf) and the
        // sound buffer. When drawFrame is false the core still emulates but
        // skips the video write.
        uint8_t *gfx;
        int32_t *sound;
        int32_t ssize;
        FCEUI_Emulate(&gfx, &sound, &ssize, !drawFrame);

        if (drawFrame && gfx)
        {
            // Convert the indexed framebuffer to RGB565. 256*240 = 61 440 LUT
            // lookups — a fraction of a millisecond at 360 MHz.
            uint16_t *dst = (uint16_t *)currentUpdate->data;
            for (int i = 0; i < NES_WIDTH * NES_HEIGHT; i++)
                dst[i] = palette565[gfx[i]];

            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
        }

        // Audio: FCEUmm produces int32 mono samples. Split to stereo S16.
        // The framework handles volume and resampling.
        if (ssize > 0)
        {
            rg_audio_frame_t mixbuf[ssize];
            for (int i = 0; i < ssize; i++)
            {
                int16_t s = (int16_t)(sound[i] >> 8);
                mixbuf[i].left  = s;
                mixbuf[i].right = s;
            }
            rg_audio_submit(mixbuf, ssize);
        }

        rg_system_tick(rg_system_timer() - startTime);

        if (skipFrames == 0)
            skipFrames = app->frameskip;
        else if (skipFrames > 0)
            skipFrames--;
    }
}
