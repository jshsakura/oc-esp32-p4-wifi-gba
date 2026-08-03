/*
 * Amstrad CPC (Caprice32) for retro-go.
 *
 * cap32's sources are a libretro-flavoured core but not a libretro front end:
 * they reach out through a small set of externs -- retro_getScreenPtr(),
 * retro_malloc(), retro_computer_cfg, retro_video.bytes, retro_audio_mix_sample()
 * -- and otherwise runs its own emulation with no SDL and no RetroArch headers.
 * We implement those externs ourselves below and drive the core through
 * capmain()/caprice_retro_loop() directly, the same trade prosystem-go makes.
 * What we do NOT compile is libretro/libretro-core.c: that is a full second
 * front-end (its own video/audio buffer scheme, on-screen keyboard, status
 * bar, disk-control interface) that would fight our display/audio/input code
 * for the same job. See components/caprice32/CMakeLists.txt for the exact
 * source list and why video16bpp.c is the one file pulled in from libretro/.
 *
 * The CPC's BIOS (OS ROM, BASIC, AMSDOS) is compiled into the core itself
 * (cap32/rom/, as C headers) -- unlike the 7800 there is no optional external
 * BIOS file.
 */
#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "cap32.h"
#include "slots.h"
#include "cart.h"
#include "libretro-core.h"  // computer_cfg_t / game_cfg_t / EXT_FILE_* -- plain data, no RetroArch pull-in
#include "retro_snd.h"      // audio_status_t for the (unused) floppy sound-effect hook
#include "retro_utils.h"    // retro_malloc/retro_free/file_size prototypes
#include "gfx/video.h"      // retro_video_t

// Fixed 384x272 low-res CPC picture (CPC.scr_style == 3 below): the core also
// supports a double-width "antialiased" mode meant for PC monitors, which
// buys us nothing on an LCD and would just mean twice the pixels to push.
#define CPC_WIDTH   384
#define CPC_HEIGHT  272
#define CPC_FPS     50
#define CPC_SAMPLE_RATE 44100 // freq_table[2] in cap32.c -- the "playback_rate" loadConfiguration() defaults to

// cap32_emu_reset() is declared nowhere: libretro-core.c, the front end we
// are not building, only ever reaches it through its own forward extern
// (capmain() itself is in cap32.h, and driveA/driveB -- used below in
// load_media() -- through the macros next to t_drive there).
extern void cap32_emu_reset(void);

static rg_app_t *app;
static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;

// --- externs the core calls out to (see cap32.c, crtc.c, psg.c, fdc.c) ----

int emu_status = COMPUTER_OFF;
computer_cfg_t retro_computer_cfg;
retro_video_t retro_video = { .bytes = 1 }; // 16bpp; video_init() divides by this before we get a chance to set it

void *retro_malloc(size_t size)
{
    return rg_alloc(size, MEM_SLOW); // up to 576KB of CPC RAM -- PSRAM, like prosystem's ROM buffer
}

void retro_free(void *mem)
{
    if (mem)
        free(mem);
}

int file_size(int file_num)
{
    struct stat st;
    return fstat(file_num, &st) == 0 ? (int)st.st_size : 0;
}

// Screen geometry: retro_getScreenPtr() is read at the start of every scanline
// (crtc.c's caprice_retro_loop and prerender_*), so it must stay pointed at
// `currentUpdate` for the whole of one caprice_retro_loop() call and only
// change between frames -- exactly the surface handed to rg_display_submit().
unsigned int *retro_getScreenPtr(void) { return (unsigned int *)currentUpdate->data; }
int retro_getGfxBpp(void) { return 16; }
int retro_getStyle(void) { return 3; } // selects the dwXScale=1 (384x272) path in video_set_style()
int retro_getGfxBps(void) { return CPC_WIDTH; }
int retro_getAudioBuffer(void) { return 4096; } // pbSndBuffer is allocated but never read -- see retro_audio_mix_sample()

// The floppy motor/seek/read sound effects are a front-end nicety (retro_snd.c
// mixes short WAV samples under the emulated audio); we skip them rather than
// port the mixer along with them.
void retro_snd_cmd(int snd_type, audio_status_t new_status) { (void)snd_type; (void)new_status; }

// psg.c calls this once per synthesized sample (Synthesizer_Stereo16, the
// variant CPC.snd_stereo/snd_bits select by default) -- there is no separate
// "render a frame of audio" step to hook, so we just accumulate here and flush
// once per video frame in app_main()'s loop, like the other cores' mixbuf.
#define CPC_MAX_AUDIO_SAMPLES 1024
static rg_audio_frame_t audioBuffer[CPC_MAX_AUDIO_SAMPLES];
static size_t audioCount;

void retro_audio_mix_sample(int16_t left, int16_t right)
{
    if (audioCount < CPC_MAX_AUDIO_SAMPLES)
    {
        audioBuffer[audioCount].left = left;
        audioBuffer[audioCount].right = right;
        audioCount++;
    }
}

// Referenced from a dead branch of caprice_retro_loop() (the old buffer-based
// mixer, superseded by the per-sample hook above) but the function body still
// calls it, so the symbol must exist.
void retro_audio_mix_batch(void) {}

// The rest of this block is front-end UI/state the core pokes at directly
// rather than through a function we could give a body of our own choosing:
// cart_name for the (never used here -- see load_media()) CPC6128+ cartridge
// path, SHIFTON for vkbd_key()'s shift-modifier handling (we never drive a
// shifted virtual key, so -1 -- "off" -- is permanently correct), and the
// floppy motor/read/write LED and status-bar hooks, which we have no on-screen
// equivalent for.
char cart_name[512];
int SHIFTON = -1;
void retro_show_statusbar(void) {}
void amstrad_ui_set_led(bool value) { (void)value; }

// --- retro-go glue ----------------------------------------------------------

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

// snapshot_save()/snapshot_load() already read/write a complete CPC state
// (Z80, RAM, CRTC, Gate Array, PSG, FDC, drive geometry) to a plain .SNA file
// -- exactly retro-go's save-state contract, so there is no buffer of our own
// to manage here.
static bool save_state_handler(const char *filename)
{
    return snapshot_save((char *)filename) == 0;
}

static bool load_state_handler(const char *filename)
{
    return snapshot_load((char *)filename) == 0;
}

static bool reset_handler(bool hard)
{
    cap32_emu_reset();
    return true;
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(currentUpdate, 0);
}

// --- input -------------------------------------------------------------------
// The CPC has no joystick port of its own -- games read one through the
// keyboard matrix, at fixed rows the core reserves for it (retro_events.h's
// CPC_KEY_JOY_* -- line 9 of the matrix). START/SELECT get mapped onto RETURN
// and ESC since most loaders/menus expect one of the two; there is no sane
// default for the rest of a CPC's 74 keys on a 10-button pad, so we do not
// attempt one. vkbd_key() itself is declared in cap32.h (kbdauto.c).

#define CPC_KEY_JOY_UP     0x90
#define CPC_KEY_JOY_DOWN   0x91
#define CPC_KEY_JOY_LEFT   0x92
#define CPC_KEY_JOY_RIGHT  0x93
#define CPC_KEY_JOY_FIRE1  0x94
#define CPC_KEY_JOY_FIRE2  0x95
#define CPC_KEY_RETURN     0x22
#define CPC_KEY_ESC        0x82

static void update_input(void)
{
    uint32_t joystick = rg_input_read_gamepad();
    vkbd_key(CPC_KEY_JOY_UP, (joystick & RG_KEY_UP) != 0);
    vkbd_key(CPC_KEY_JOY_DOWN, (joystick & RG_KEY_DOWN) != 0);
    vkbd_key(CPC_KEY_JOY_LEFT, (joystick & RG_KEY_LEFT) != 0);
    vkbd_key(CPC_KEY_JOY_RIGHT, (joystick & RG_KEY_RIGHT) != 0);
    vkbd_key(CPC_KEY_JOY_FIRE1, (joystick & RG_KEY_A) != 0);
    vkbd_key(CPC_KEY_JOY_FIRE2, (joystick & RG_KEY_B) != 0);
    vkbd_key(CPC_KEY_RETURN, (joystick & RG_KEY_START) != 0);
    vkbd_key(CPC_KEY_ESC, (joystick & RG_KEY_SELECT) != 0);
}

// --- autotype (RUN"/tape loader) ---------------------------------------------
// A raw .dsk or .cdt boots to a bare BASIC prompt, same as a real CPC with no
// AUTO-EXEC disk -- so we type the load command ourselves. kbd_buf_feed()
// queues the string; kbd_buf_update() must then be pumped once per frame,
// throttled to every other frame so the CPC's keyboard scan (itself once per
// frame) reliably sees each keypress, matching ev_autorun() in the front end
// we are not building. kbd_buf_feed()/kbd_buf_update() are declared in cap32.h.

static int autorunDelay;
static bool autorunPending;
static bool autorunToggle;

static void autorun_start(const char *keys, int delayFrames)
{
    kbd_buf_feed((char *)keys);
    autorunDelay = delayFrames;
    autorunPending = true;
    autorunToggle = false;
}

static void autorun_tick(void)
{
    if (!autorunPending)
        return;
    if (autorunDelay > 0)
    {
        autorunDelay--;
        return;
    }
    autorunToggle = !autorunToggle;
    if (!autorunToggle)
        return;
    if (kbd_buf_update())
        autorunPending = false;
}

// --- media loading ------------------------------------------------------------
// RUN" with no filename is standard AMSDOS behaviour: it runs the first
// program file on the disk, so we don't need to parse the catalogue ourselves.
static bool load_media(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext)
        return false;
    ext++;

    if (!strcasecmp(ext, EXT_FILE_SNA))
        return snapshot_load((char *)path) == 0;

    if (!strcasecmp(ext, EXT_FILE_DSK))
    {
        if (dsk_load((char *)path, &driveA, 'A') != 0)
            return false;
        autorun_start("run\"\n", 100);
        return true;
    }

    if (!strcasecmp(ext, EXT_FILE_CDT))
    {
        if (tape_insert((char *)path) != 0)
            return false;
        autorun_start("|tape\nrun\"\n^        ", 100); // '^' triggers play_tape(), see kbdauto.c
        return true;
    }

    if (!strcasecmp(ext, EXT_FILE_CPR))
    {
        retro_computer_cfg.model = CPC_MODEL_PLUS; // cart_start() refuses to load on any other model
        return cpr_fload(path) == 0;
    }

    return false;
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

    app = rg_system_init(CPC_SAMPLE_RATE, &handlers, NULL);

    // Two buffers on purpose: rg_display_submit() reads the surface in place on
    // another task through a one-deep blocking queue, so a single buffer both
    // tears and stalls the emulator on every submit.
    //
    // MEM_SLOW (PSRAM), not MEM_FAST: the CPC picture is direct 16bpp colour,
    // not paletted like most of the other cores here, so a pair of 384x272
    // buffers is ~408KB -- more than internal SRAM has left once the rest of
    // retro-go and the CPC's own up-to-576KB RAM (also PSRAM, see retro_malloc()
    // below) are accounted for. Confirmed by the linker: at MEM_FAST this
    // overflowed DRAM by ~114KB.
    updates[0] = rg_surface_create(CPC_WIDTH, CPC_HEIGHT, RG_PIXEL_565_LE, MEM_SLOW);
    updates[1] = rg_surface_create(CPC_WIDTH, CPC_HEIGHT, RG_PIXEL_565_LE, MEM_SLOW);
    currentUpdate = updates[0];

    // loadConfiguration(), called from inside capmain(), reads CPC.model/
    // ram_size/keyboard straight out of retro_computer_cfg -- so these have to
    // be set before capmain() runs, not after.
    retro_computer_cfg.model = CPC_MODEL_6128; // widest software compatibility: BASIC 1.1 + AMSDOS
    retro_computer_cfg.ram = 128;
    retro_computer_cfg.lang = 0; // QWERTY

    capmain(0, NULL); // loadConfiguration() + video_init() + audio_init() + caprice_emulator_init()

    if (!load_media(app->romPath))
        rg_system_rom_load_failed(_("Could not load the game file."));

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
        autorun_tick();

        caprice_retro_loop(); // runs exactly one 50Hz CPC video frame, writing into retro_getScreenPtr()

        if (drawFrame)
        {
            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
        }

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit(audioBuffer, audioCount);
        audioCount = 0;

        if (skipFrames == 0)
            skipFrames = app->frameskip;
        else if (skipFrames > 0)
            skipFrames--;
    }
}
