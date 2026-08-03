/*
 * Sega CD (and Mega Drive) for retro-go, on PicoDrive.
 *
 * The Mega Drive already has a core here -- gwenesis -- and this is not meant to
 * replace it. It exists for the add-on hardware gwenesis does not emulate: Sega
 * CD now, 32X once it has been measured. A cart loaded here runs as plain Mega
 * Drive because PicoLoadMedia() decides that for itself from the file, which
 * makes plain-MD the natural fallback rather than a second implementation to
 * keep in step.
 *
 * The Game & Watch port of this same fork compiled the CD subsystem out (its
 * GNW_32X_CORE define) because that device could not fit Sega CD's resident RAM.
 * That reason does not survive the move to 32MB of PSRAM, and the fork's guards
 * are clean, so the component simply leaves the define unset -- see its
 * CMakeLists for the whole of it.
 */
#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* pico.h declares things with picodrive's short type names (s16/s32) but does not
 * pull in the header that defines them -- inside the core that is covered by
 * pico_int.h, which a front-end has no business including. */
#include "pico/pico_types.h"
#include "pico/pico.h"

/* Mega Drive output is up to 320x240; the H32 modes are narrower and PicoDrive
 * writes them into the same buffer, so the surface is sized for the widest case
 * and the visible width is set per frame from the emulator. */
#define MD_WIDTH   320
#define MD_HEIGHT  240

#define MD_SAMPLE_RATE  44100
#define MD_MAX_SAMPLES  (MD_SAMPLE_RATE / 50 + 16)   /* PAL is the longer field */

static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;
static rg_app_t *app;

static short sndBuffer[MD_MAX_SAMPLES * 2];
static int sndFrameLen;

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

static bool reset_handler(bool hard)
{
    PicoReset();
    return true;
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(currentUpdate, 0);
}

/* PicoDrive asks for its sound to be taken once per frame rather than handing it
 * over, so this only records how much there is; the frame loop submits it. */
static void write_sound(int len)
{
    sndFrameLen = len / (int)sizeof(short) / 2;
}

/* Pad format is MXYZ SACB RLDU, one bit per button, active high. */
static uint16_t read_input(void)
{
    uint32_t joystick = rg_input_read_gamepad();
    uint16_t pad = 0;
    if (joystick & RG_KEY_UP)     pad |= 1 << 0;
    if (joystick & RG_KEY_DOWN)   pad |= 1 << 1;
    if (joystick & RG_KEY_LEFT)   pad |= 1 << 2;
    if (joystick & RG_KEY_RIGHT)  pad |= 1 << 3;
    if (joystick & RG_KEY_B)      pad |= 1 << 4;   /* B */
    if (joystick & RG_KEY_A)      pad |= 1 << 5;   /* C */
    if (joystick & RG_KEY_X)      pad |= 1 << 6;   /* A */
    if (joystick & RG_KEY_START)  pad |= 1 << 7;
    return pad;
}

/* Sega CD needs a region BIOS; a cart does not. PicoLoadMedia() asks for one
 * only when it has decided the image is a CD, which is why this is a callback
 * rather than something loaded up front. */
static const char *get_bios_filename(int *region, const char *cd_fname)
{
    static char path[RG_PATH_MAX + 1];
    const char *name = "bios_CD_U.bin";
    if (region)
    {
        if (*region == 8)
            name = "bios_CD_E.bin";
        else if (*region == 1 || *region == 2)
            name = "bios_CD_J.bin";
    }
    snprintf(path, sizeof(path), "%s/%s", RG_BASE_PATH_BIOS, name);
    return path;
}

void app_main(void)
{
    const rg_handlers_t handlers = {
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
    };

    app = rg_system_init(MD_SAMPLE_RATE, &handlers, NULL);

    /* Two buffers: rg_display_submit() reads the surface in place on the display
     * task, through a queue one deep with a blocking send, so a single buffer
     * both tears and stalls the emulator on every frame. */
    updates[0] = rg_surface_create(MD_WIDTH, MD_HEIGHT, RG_PIXEL_565_LE, MEM_FAST);
    updates[1] = rg_surface_create(MD_WIDTH, MD_HEIGHT, RG_PIXEL_565_LE, MEM_FAST);
    currentUpdate = updates[0];

    PicoInit();

    PicoIn.opt = POPT_EN_STEREO | POPT_EN_FM | POPT_EN_PSG | POPT_EN_Z80
               | POPT_EN_MCD_PCM | POPT_EN_MCD_CDDA | POPT_ACC_SPRITES;
    PicoIn.sndRate = MD_SAMPLE_RATE;
    PicoIn.sndOut = sndBuffer;
    PicoIn.writeSound = &write_sound;

    PicoDrawSetOutFormat(PDF_RGB555, 0);
    PicoDrawSetOutBuf(currentUpdate->data, MD_WIDTH * 2);

    enum media_type_e media = PicoLoadMedia(app->romPath, NULL, 0, NULL, &get_bios_filename, NULL, NULL);
    switch (media)
    {
    case PM_BAD_CD_NO_BIOS:
        /* Worth its own message: the image is fine and the emulator is fine, the
         * user simply has no BIOS on the card, and "could not load" would send
         * them looking at the wrong thing. */
        rg_system_rom_load_failed(_("Sega CD BIOS not found in /bios"));
        break;
    case PM_MD_CART:
    case PM_CD:
    case PM_MARK3:
    case PM_PICO:
        break;
    default:
        rg_system_rom_load_failed(_("Could not load the game file."));
        break;
    }

    PicoLoopPrepare();
    PicoPower();
    PicoReset();

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

        PicoIn.pad[0] = read_input();
        PicoIn.skipFrame = drawFrame ? 0 : 1;

        sndFrameLen = 0;
        PicoFrame();

        if (drawFrame)
        {
            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
            /* The renderer writes wherever it was last pointed, so the swap has
             * to be told about -- otherwise both frames land in one buffer. */
            PicoDrawSetOutBuf(currentUpdate->data, MD_WIDTH * 2);
        }

        rg_system_tick(rg_system_timer() - startTime);

        if (sndFrameLen > 0)
            rg_audio_submit((rg_audio_frame_t *)sndBuffer, sndFrameLen);

        if (skipFrames == 0)
            skipFrames = app->frameskip;
        else if (skipFrames > 0)
            skipFrames--;
    }
}
