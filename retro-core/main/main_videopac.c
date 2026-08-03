//============================================================================
// main.c - Retro-Go frontend for the O2EM core.
//
// O2EM emulates the Magnavox Odyssey2 / Philips Videopac+: an Intel 8048
// machine with a custom VDC. The core renders a 340x250 8-bit indexed bitmap
// (320x240 of which is visible) and fills a 1056-byte unsigned-8-bit mono
// audio buffer per frame, which at 60 Hz is a 63360 Hz sample rate.
//
// This file is the glue between that core and retro-go, modelled on
// supervision-go/main/main.c for the framework side and on the STM32
// Game-and-Watch port (main_videopac.c) for the O2EM-specific boot, cart
// loading, blit, and audio-conversion logic. The core itself is built
// unmodified from external/o2em-go with the same -D__LIBRETRO__ -DTARGET_GNW
// switches the G&W tree uses, so the code paths here are the ones already
// proven on metal there.
//============================================================================

#include "shared.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"        // SOUND_BUFFER_LEN
#include "o2em_config.h"  // ROM_O2 / ROM_G7400 / ROM_C52 / ROM_JOPAC / ROM_UNKNOWN
// Path-qualified rather than a bare "cpu.h": folded into retro-core, this file
// is compiled with gnuboy's include dir on the same search path, and gnuboy
// has its own cpu.h that would otherwise shadow o2em's (search order puts
// gnuboy first). The relative path resolves directly and skips the ambiguity.
#include "../components/o2em/src/cpu.h"  // init_cpu, cpu_exec
#include "keyboard.h"     // set_defjoykeys, set_defsystemkeys
#include "score.h"        // set_score
#include "vdc.h"          // init_display
#include "vmachine.h"     // app_data, rom_table, extROM, o2em_rom, init_system, savestate_*
#include "wrapalleg.h"    // APALETTE, key[], RGB565

// The Odyssey2 is a 60 Hz NTSC console. The core fills exactly SOUND_BUFFER_LEN
// (1056) unsigned-8-bit mono samples per frame, so the implied sample rate is
// 1056 * 60 = 63360 Hz. Feeding that exact rate to rg_system_init keeps the
// audio pitch correct: the framework resamples to the hardware DAC but only if
// the source rate differs from what it expects.
#define O2_FPS                  60
// Named O2_AUDIO_SAMPLE_RATE rather than AUDIO_SAMPLE_RATE: that name is
// already defined in shared.h (folded cores share one translation unit's
// worth of headers with the rest of retro-core), and to a different value.
#define O2_AUDIO_SAMPLE_RATE    63360
#define AUDIO_SAMPLES_PER_FRAME (SOUND_BUFFER_LEN)

// The core's savestate_size() reports a few hundred bytes (8048 regs + int/ext
// RAM + VDC state). 4 KB is generous and harmless; the returned size governs
// the actual I/O.
#define STATE_BUFFER_SIZE       (4 * 1024)

// The core's internal bitmap is 340 wide (BMPW) by 250 tall (BMPH); the
// Odyssey2 only ever displays a 320x240 window inside it. retro_blit (the
// non-TARGET_GNW path) writes the full 340x250 image into mbmp with a stride
// of TEX_WIDTH (400). The visible window sits at column 10 within that 340,
// so after cpu_exec() we copy columns 10..329, rows 0..239 into the surface.
#define VIS_WIDTH       320
#define VIS_HEIGHT      240
#define MBMP_STRIDE     400   // TEX_WIDTH in wrapalleg.h
#define MBMP_X_OFFSET   10    // horizontal centring inside the 340-wide blit

#define RETROK_RETURN   13

// --- Globals consumed by the O2EM core --------------------------------------
// The core declares these as extern (wrapalleg.h / vmachine.h) and expects the
// platform to define them. They match the names and layouts the G&W port uses.
uint8_t soundBuffer[SOUND_BUFFER_LEN];               // filled by update_audio() during cpu_exec()
int     RLOOP = 0;                                    // frame latch: set 1, core clears it at vblank
int     joystick_data[2][5] = {{0,0,0,0,0},{0,0,0,0,0}};  // [stick][up,down,left,right,action]

// handle_vbl() calls update_joy() once per frame. We push joystick state into
// joystick_data directly before cpu_exec(), so there is nothing to sample here.
void update_joy(void) { }

static rg_app_t *app;
static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;

// retro_blit() (the non-TARGET_GNW path in o2em_vdc.c) writes the converted
// RGB565 frame here every vblank. We allocate it in PSRAM (MEM_SLOW) because
// it is 400*250*2 = 200 KB — far too large for internal SRAM — and only
// touched once per frame, so the PSRAM latency is immaterial.
uint16_t *mbmp;

// Light single-pole low-pass on the audio, same idea as the G&W port: the
// core's built-in filter is disabled (app_data.filter = 0) because it colours
// the sound badly, so we tame the raw square-wave hash with a 6 dB/octave RC.
static int32_t lp_range = (60 * 0x10000) / 100;
static int32_t lp_prev  = 0;

// --- Save / load state ------------------------------------------------------
// The core exposes savestate_to_mem / loadstate_from_mem for a flat blob, so we
// marshal through a static buffer and hand the file I/O to retro-go.
static bool save_state_handler(const char *filename)
{
    uint8_t *buffer = (uint8_t *)rg_alloc(STATE_BUFFER_SIZE, MEM_SLOW);
    if (!buffer)
        return false;

    size_t sz = savestate_size();
    bool ok = (sz > 0 && sz <= STATE_BUFFER_SIZE && savestate_to_mem(buffer, sz));
    if (ok)
    {
        FILE *f = fopen(filename, "wb");
        if (f)
        {
            ok = (fwrite(buffer, 1, sz, f) == sz);
            fclose(f);
        }
        else ok = false;
    }
    free(buffer);
    return ok;
}

static bool load_state_handler(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > STATE_BUFFER_SIZE)
    {
        fclose(f);
        return false;
    }

    uint8_t *buffer = (uint8_t *)rg_alloc(STATE_BUFFER_SIZE, MEM_SLOW);
    if (!buffer)
    {
        fclose(f);
        return false;
    }

    bool ok = (fread(buffer, 1, sz, f) == (size_t)sz);
    fclose(f);
    // loadstate_from_mem checks the saved cart CRC against the just-loaded
    // cart, so a wrong-slot file no-ops cleanly instead of corrupting RAM.
    if (ok)
        ok = loadstate_from_mem(buffer, sz);

    free(buffer);
    return ok;
}

static bool reset_handler(bool hard)
{
    // A full cold re-init is the safe reset: re-load BIOS/cart and re-init the
    // CPU + machine, the same sequence as boot minus the file reads we can
    // skip (rom_table is still populated from the original load).
    init_cpu();
    init_system();
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

// --- BIOS loading -----------------------------------------------------------
// The Odyssey2 BIOS is a 1024-byte ROM image the user supplies. Four known
// variants are recognised by CRC; an unknown one is accepted but logged. We
// copy it into all eight rom_table banks the way the G&W port does (the 8048
// bank-switching logic mirrors the BIOS across banks).
static bool load_bios(void)
{
    uint8_t bios_data[1024];
    const char *path = RG_BASE_PATH_BIOS "/videopac/o2rom.bin";
    FILE *bf = fopen(path, "rb");
    if (!bf)
    {
        rg_gui_alert(_("BIOS missing"), _("Place o2rom.bin in /retro-go/bios/videopac/"));
        return false;
    }
    size_t bios_size = fread(bios_data, 1, sizeof(bios_data), bf);
    fclose(bf);

    if (bios_size != 1024)
    {
        printf("[O2EM] BIOS %s is %u bytes, expected 1024\n", path, (unsigned)bios_size);
        rg_gui_alert(_("BIOS error"), _("o2rom.bin must be exactly 1024 bytes"));
        return false;
    }

    memcpy(rom_table[0], bios_data, 1024);
    for (int i = 1; i < 8; i++)
        memcpy(rom_table[i], rom_table[0], 1024);

    uint32_t crc = rg_crc32(0, rom_table[0], 1024);
    switch (crc)
    {
        case 0x8016A315:
            printf("[O2EM] Magnavox Odyssey2 BIOS (G7000, US)\n");
            app_data.vpp = 0; app_data.bios = ROM_O2;    break;
        case 0xE20A9F41:
            printf("[O2EM] Philips Videopac+ BIOS (G7400, EU)\n");
            app_data.vpp = 1; app_data.bios = ROM_G7400; break;
        case 0xA318E8D6:
            printf("[O2EM] Philips Videopac BIOS (C52, FR)\n");
            app_data.vpp = 0; app_data.bios = ROM_C52;   break;
        case 0x11647CA5:
            printf("[O2EM] Philips Jopac BIOS (G7400, FR)\n");
            app_data.vpp = 1; app_data.bios = ROM_JOPAC; break;
        default:
            printf("[O2EM] BIOS loaded (unknown CRC %08X)\n", (unsigned)crc);
            app_data.vpp = 0; app_data.bios = ROM_UNKNOWN; break;
    }
    return true;
}

// --- Cartridge loading ------------------------------------------------------
// Mirrors main_videopac.c's load_cart(): classifies the cart by size/CRC into
// the 2K/3K/EXROM bank layouts the 8048 address decoder expects, and copies it
// into rom_table[]. The MegaCart path is intentionally disabled (#if 0 in the
// G&W original too) — it needs a 1 MB malloc we do not want on ESP32.
static bool load_cart(const uint8_t *data, size_t size)
{
    app_data.crc = rg_crc32(0, data, size);

    // Three carts use an extra 1K EXROM bank.
    if (app_data.crc == 0xAFB23F89 ||    // Musician
        app_data.crc == 0x3BFEF56B ||    // Four in 1 Row!
        app_data.crc == 0x9B5E9356)      // Four in 1 Row! (French)
        app_data.exrom = 1;

    // Two known-incomplete dumps that would run as garbage.
    if (app_data.crc == 0x975AB8DA || app_data.crc == 0xE246A812)
    {
        rg_gui_alert(_("ROM error"), _("Incomplete ROM dump"));
        return false;
    }

    if ((size % 1024) != 0)
    {
        rg_gui_alert(_("ROM error"), _("Invalid ROM size (not a multiple of 1K)"));
        return false;
    }

    int nb;
    if ((size % 3072) == 0)
    {
        // 3K-per-bank carts (A10 strapped differently).
        app_data.three_k = 1;
        nb = size / 3072;
        for (int i = nb - 1; i >= 0; i--)
        {
            memcpy(&rom_table[i][1024], data, 3072);
            data += 3072;
        }
        printf("[O2EM] %uK (3K banks)\n", (unsigned)(nb * 3));
    }
    else
    {
        nb = size / 2048;
        if (nb == 2 && app_data.exrom)
        {
            // 3K EXROM cart: first 1K is the EXROM, next 3K is bank 0.
            memcpy(&extROM[0], data, 1024);
            data += 1024;
            memcpy(&rom_table[0][1024], data, 3072);
            data += 3072;
            printf("[O2EM] 3K EXROM cart\n");
        }
        else
        {
            for (int i = nb - 1; i >= 0; i--)
            {
                memcpy(&rom_table[i][1024], data, 2048);
                data += 2048;
                // Simulate the missing A10 line: mirror the top 1K into the
                // bottom 1K of each 4K bank.
                memcpy(&rom_table[i][3072], &rom_table[i][2048], 1024);
            }
            printf("[O2EM] %uK (2K banks)\n", (unsigned)(nb * 2));
        }
    }

    o2em_rom = rom_table[0];

    // Bank count -> internal bank selector the BIOS uses.
    if      (nb == 1) app_data.bank = 1;
    else if (nb == 2) app_data.bank = app_data.exrom ? 1 : 2;
    else if (nb == 4) app_data.bank = 3;
    else              app_data.bank = 4;

    // "OPNB" magic in the last bank signals an OpenBank cart.
    if (rom_table[nb - 1][1024 + 12] == 'O' &&
        rom_table[nb - 1][1024 + 13] == 'P' &&
        rom_table[nb - 1][1024 + 14] == 'N' &&
        rom_table[nb - 1][1024 + 15] == 'B')
        app_data.openb = 1;

    return true;
}

// --- ROM file loading -------------------------------------------------------
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

// --- Input ------------------------------------------------------------------
// joystick_data layout expected by keyjoy(): [stick][0=up 1=down 2=left
// 3=right 4=action]. The Odyssey2 has one action button, so both A and B map
// to it. Start maps to the Return key so it works as "select/enter" on BIOS
// menus. Level-based is fine: the core reads the joystick as a held state.
static void push_input(uint32_t pad)
{
    joystick_data[0][0] = (pad & RG_KEY_UP)    ? 1 : 0;
    joystick_data[0][1] = (pad & RG_KEY_DOWN)  ? 1 : 0;
    joystick_data[0][2] = (pad & RG_KEY_LEFT)  ? 1 : 0;
    joystick_data[0][3] = (pad & RG_KEY_RIGHT) ? 1 : 0;
    // A or B = action; A also doubles as keypad "1" (see auto-select below).
    joystick_data[0][4] = (pad & (RG_KEY_A | RG_KEY_B)) ? 1 : 0;

    key[RETROK_RETURN] = (pad & RG_KEY_START) ? 1 : 0;
}

// --- App entry point --------------------------------------------------------
void videopac_main(void)
{
    const rg_handlers_t handlers = {
        .loadState  = &load_state_handler,
        .saveState  = &save_state_handler,
        .reset      = &reset_handler,
        .screenshot = &screenshot_handler,
        .event      = &event_handler,
    };

    app = rg_system_reinit(O2_AUDIO_SAMPLE_RATE, &handlers, NULL);

    // 320x240 RGB565, double buffered. We keep these in internal SRAM
    // (MEM_FAST) because rg_display_submit DMA-reads them straight to the
    // panel — PSRAM sources would add latency the blit path was not designed
    // for. The two surfaces are 2 x 153 KB, which the other 320x240 apps
    // (retro-core SNES, gbsp) already carry without trouble.
    updates[0] = rg_surface_create(VIS_WIDTH, VIS_HEIGHT, RG_PIXEL_565_LE, MEM_FAST);
    updates[1] = rg_surface_create(VIS_WIDTH, VIS_HEIGHT, RG_PIXEL_565_LE, MEM_FAST);
    currentUpdate = updates[0];

    // The core's retro_blit writes one 340x250 RGB565 frame here per vblank
    // with a stride of 400 (TEX_WIDTH). 200 KB in PSRAM: too big for internal
    // SRAM, and the one-frame-only access pattern makes PSRAM latency a non-
    // issue.
    mbmp = (uint16_t *)rg_alloc(MBMP_STRIDE * 250 * sizeof(uint16_t), MEM_SLOW);
    if (!mbmp)
        rg_system_panic("videopac-go", "Could not allocate mbmp in PSRAM");

    // --- O2EM initialisation, in the order main_videopac.c established -------
    // load_data(): set defaults, load BIOS, load cart.
    app_data.stick[0] = app_data.stick[1] = 1;
    app_data.sticknumber[0] = app_data.sticknumber[1] = 0;
    set_defjoykeys(0, 0);
    set_defjoykeys(1, 1);
    set_defsystemkeys();
    app_data.bank     = 0;
    app_data.limit    = 1;     // lock to real-time (RLOOP gate active)
    app_data.sound_en = 1;
    app_data.speed    = 100;   // 100 % speed
    app_data.wsize    = 2;
    app_data.scanlines = 0;
    app_data.voice    = 1;
    app_data.filter   = 0;     // core's own filter sounds bad; we low-pass ourselves
    app_data.exrom    = 0;
    app_data.three_k  = 0;
    app_data.crc      = 0;
    app_data.openb    = 0;
    app_data.vpp      = 0;
    app_data.bios     = 0;
    app_data.scoretype    = 0;
    app_data.scoreaddress = 0;
    app_data.default_highscore = 0;
    app_data.breakpoint  = 65535;
    app_data.megaxrom    = 0;

    init_audio();

    if (!load_bios())
        rg_system_rom_load_failed(_("Videopac BIOS not found"));

    uint32_t rom_size = 0;
    uint8_t *rom_data = load_rom(app->romPath, &rom_size);
    if (!rom_data || !load_cart(rom_data, rom_size))
        rg_system_rom_load_failed(_("Could not load the game file."));

    // init_display creates the static bitmaps + collision buffer and calls
    // init_keyboard; init_cpu zeros the 8048; init_system wires up memory +
    // interrupts + region. Order matters: the G&W port found that restoring a
    // save state before init_system was silently wiped by the machine reset.
    init_display();
    init_cpu();
    init_system();

    set_score(app_data.scoretype, app_data.scoreaddress, app_data.default_highscore);
    app_data.euro = 0;   // NTSC

    // Resume: restore after the cart load + machine reset so it is not wiped.
    if (app->bootFlags & RG_BOOT_RESUME)
        rg_emu_load_state(app->saveSlot);

    // --- Odyssey2 "SELECT GAME" auto-start -----------------------------------
    // The BIOS waits on a keypad game-number entry screen after boot. We hold
    // keypad "1" (key[49]) for the first ~3 seconds (180 frames @ 60 Hz) to
    // auto-select game 1, then release. After that, the A button re-presses
    // "1" manually so the user can re-enter the selection if needed. Holding
    // key[49] in-game is harmless: games read the joystick, not the keypad.
    #define O2_AUTOSEL_FRAMES 180
    int autosel = 0;

    long skipFrames = 0;

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

        push_input(pad);

        // Auto-select game 1 during the boot window; after that, A holds "1".
        key[49] = (autosel < O2_AUTOSEL_FRAMES || (pad & RG_KEY_A)) ? 1 : 0;
        if (autosel < O2_AUTOSEL_FRAMES) autosel++;

        // Run exactly one frame. cpu_exec() returns after handle_evbl clears
        // RLOOP. During the frame, retro_blit writes the converted RGB565
        // image into mbmp (PSRAM); we then crop the visible 320x240 window
        // out of it into the display surface.
        RLOOP = 1;
        cpu_exec();

        if (drawFrame)
        {
            // Crop the 320x240 visible window from mbmp (stride 400, x-offset
            // 10) into the surface (stride 320). Row-by-row memcpy because
            // the strides differ.
            uint16_t *dst = (uint16_t *)currentUpdate->data;
            for (int y = 0; y < VIS_HEIGHT; y++)
                memcpy(dst + y * VIS_WIDTH,
                       mbmp + y * MBMP_STRIDE + MBMP_X_OFFSET,
                       VIS_WIDTH * sizeof(uint16_t));

            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
        }

        // Audio: soundBuffer holds SOUND_BUFFER_LEN U8 mono samples the core
        // filled during cpu_exec(). Expand to signed-16 stereo with the
        // low-pass filter, the same conversion the G&W port does.
        rg_audio_frame_t mixbuf[AUDIO_SAMPLES_PER_FRAME];
        int32_t lp = lp_prev;
        int32_t fa = lp_range;
        int32_t fb = 0x10000 - fa;
        for (int i = 0; i < AUDIO_SAMPLES_PER_FRAME; i++)
        {
            // U8 (0-255, centre 128) -> signed-16, then through the RC filter.
            int32_t s = (((int32_t)soundBuffer[i] - 128) << 8);
            lp = (lp * fa + s * fb) >> 16;
            mixbuf[i].left  = (int16_t)lp;
            mixbuf[i].right = (int16_t)lp;
        }
        lp_prev = lp;

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit(mixbuf, AUDIO_SAMPLES_PER_FRAME);

        if (skipFrames == 0)
            skipFrames = app->frameskip;
        else if (skipFrames > 0)
            skipFrames--;
    }
}
