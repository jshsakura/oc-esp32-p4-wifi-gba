#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>

#include <gwenesis.h>

#define USE_CORE1_TASK

#define AUDIO_SAMPLE_RATE (53267)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60 + 1)

// gwenesis_SN76489_run()/ym2612_run() advance their sample index by
// (target-clock)/AUDIO_FREQ_DIVISOR every scanline (VDP_CYCLES_PER_LINE=3420,
// AUDIO_FREQ_DIVISOR=1009, gwenesis_bus.h), so over a full frame the index
// grows by roughly lines_per_frame*3420/1009. For NTSC (262 lines) that is
// ~888.0 -- exactly AUDIO_BUFFER_LENGTH, i.e. zero margin -- and for PAL (313
// lines, LINES_PER_FRAME_PAL) it is ~1061, which overflows an
// AUDIO_BUFFER_LENGTH-sized (888) buffer by ~173 samples/346 bytes every
// single frame. gwenesis_bus.h already defines the correct PAL-safe capacity
// as GWENESIS_AUDIO_BUFFER_LENGTH_PAL (1056) but main.c never used it -- the
// buffers here were sized off the NTSC-only AUDIO_BUFFER_LENGTH instead.
// These arrays are immediately followed by sn76489_index/sn76489_clock and
// ym2612_index/ym2612_clock, so an overflow here is positioned to corrupt
// exactly the state that computes the *next* overflow's size, i.e. it can
// cascade into a wild write. This is independent of gwenesis/main/main.c's
// core1_task_sound (queued vs. direct-call audio) -- it exists in both.
#define AUDIO_MAX_SAMPLES_PER_FRAME GWENESIS_AUDIO_BUFFER_LENGTH_PAL

extern unsigned char* VRAM;
extern int zclk;
int system_clock;
int scan_line;

int16_t gwenesis_sn76489_buffer[AUDIO_MAX_SAMPLES_PER_FRAME];
int sn76489_index;
int sn76489_clock;
int16_t gwenesis_ym2612_buffer[AUDIO_MAX_SAMPLES_PER_FRAME];
int ym2612_index;
int ym2612_clock;

static FILE *savestate_fp = NULL;
static int savestate_errors = 0;

static bool yfm_enabled = true;
static bool z80_enabled = true;
static bool sn76489_enabled = true;

static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;
static rg_app_t *app;

#ifdef USE_CORE1_TASK
static rg_task_t *core1_task_handle;
static bool core1_task_rendering = false;
// Measured 2026-08-02, same ROM, same flash, only this line different:
//   core1_task_sound = true  (the old default): 5-7 fps,   BUSY 100%
//   core1_task_sound = false                  : 56-59 fps, BUSY 72-74%
// A ~10x difference from one flag, confirmed ten times back to back on
// hardware. It is NOT a priority/scheduling accident -- lowering
// rg_display's priority below core1_task's (removing any contention for
// core 1) was tried and recovered essentially nothing, so that was ruled
// out experimentally, not just in theory.
//
// The real mechanism, traced in FreeRTOS-Kernel-SMP/queue.c and tasks.c:
// with core1_task_sound=true, the scanline loop below sends one message
// to core1_task per scanline -- ~262 times a frame -- on a depth-1 queue.
// Each message carries a few multiplies' worth of audio-index work
// (gwenesis_SN76489_run/ym2612_run advance by ~3 samples a line). That is
// far less work than a cross-core wake costs. A depth-1 queue only stays
// a pipeline (fire-and-forget, sender never blocks) as long as the
// consumer drains faster than the producer sends; once per-message work
// is smaller than the round trip to wake the consumer, the producer
// starts finding the queue still full on every send and genuinely blocks
// (xQueueGenericSend's vTaskPlaceOnEventList path, not a busy-poll) until
// the consumer catches up and wakes it back -- two cross-core interrupts
// a line, every line, instead of one message dropped off in passing. The
// "second core" stops parallelizing and becomes a lockstep rendezvous
// partner that only adds latency. This is a structural property of
// feeding a depth-1 queue faster than a cross-core wake can drain it, not
// a bug in rg_task_send/receive or anything specific to this ROM.
//
// Not the same failure as retro-core's snes9x audio task (main_snes.c,
// also RG_TASK_PRIORITY_6 pinned to core 1) -- checked before assuming a
// resemblance: snes9x sends once per *frame* with a full frame's worth of
// mixed audio already batched, not once per scanline, so a round trip
// that would be ruinous 262 times a frame is amortized over an entire
// frame of other work there and never forces this rendezvous.
//
// Given the amount of work this path is worth (a handful of multiplies)
// can never amortize a cross-core round trip at this call frequency, the
// fix is not tuning the queue -- it's not using it here. Defaulting to
// false. The in-game "Sound on core 1" option still flips this at
// runtime for testing -- it no longer crashes in either position (see
// the ROM-header and 24-bit address-mask fixes elsewhere in this tree
// from the same investigation), it's just ~10x slower when on, and now
// you know why.
static bool core1_task_sound = false;
#endif

static const char *SETTING_YFM_EMULATION = "yfm_enable";
static const char *SETTING_Z80_EMULATION = "z80_enable";
static const char *SETTING_SN76489_EMULATION = "sn_enable";
// --- MAIN

typedef struct {
    char key[28];
    uint32_t length;
} svar_t;

SaveState* saveGwenesisStateOpenForRead(const char* fileName)
{
    return (void*)1;
}

SaveState* saveGwenesisStateOpenForWrite(const char* fileName)
{
    return (void*)1;
}

int saveGwenesisStateGet(SaveState* state, const char* tagName)
{
    int value = 0;
    saveGwenesisStateGetBuffer(state, tagName, &value, sizeof(int));
    return value;
}

void saveGwenesisStateSet(SaveState* state, const char* tagName, int value)
{
    saveGwenesisStateSetBuffer(state, tagName, &value, sizeof(int));
}

void saveGwenesisStateGetBuffer(SaveState* state, const char* tagName, void* buffer, int length)
{
    size_t initial_pos = ftell(savestate_fp);
    bool from_start = false;
    svar_t var;

    // Odds are that calls to this func will be in order, so try searching from current file position.
    while (!from_start || ftell(savestate_fp) < initial_pos)
    {
        if (!fread(&var, sizeof(svar_t), 1, savestate_fp))
        {
            if (!from_start)
            {
                fseek(savestate_fp, 0, SEEK_SET);
                from_start = true;
                continue;
            }
            break;
        }
        if (strncmp(var.key, tagName, sizeof(var.key)) == 0)
        {
            fread(buffer, RG_MIN(var.length, length), 1, savestate_fp);
            RG_LOGI("Loaded key '%s'\n", tagName);
            return;
        }
        fseek(savestate_fp, var.length, SEEK_CUR);
    }
    RG_LOGW("Key %s NOT FOUND!\n", tagName);
    savestate_errors++;
}

void saveGwenesisStateSetBuffer(SaveState* state, const char* tagName, void* buffer, int length)
{
    // TO DO: seek the file to find if the key already exists. It's possible it could be written twice.
    svar_t var = {{0}, length};
    strncpy(var.key, tagName, sizeof(var.key) - 1);
    fwrite(&var, sizeof(var), 1, savestate_fp);
    fwrite(buffer, length, 1, savestate_fp);
    RG_LOGI("Saved key '%s'\n", tagName);
}

void gwenesis_io_get_buttons()
{
}


static rg_gui_event_t yfm_update_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        yfm_enabled = !yfm_enabled;
        rg_settings_set_number(NS_APP, SETTING_YFM_EMULATION, yfm_enabled);
        memset(gwenesis_ym2612_buffer, 0, sizeof(gwenesis_ym2612_buffer));
    }
    strcpy(option->value, yfm_enabled ? _("On") : _("Off"));

    return RG_DIALOG_VOID;
}

static rg_gui_event_t sn76489_update_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        sn76489_enabled = !sn76489_enabled;
        rg_settings_set_number(NS_APP, SETTING_SN76489_EMULATION, sn76489_enabled);
        memset(gwenesis_sn76489_buffer, 0, sizeof(gwenesis_sn76489_buffer));
    }
    strcpy(option->value, sn76489_enabled ? _("On") : _("Off"));

    return RG_DIALOG_VOID;
}

static rg_gui_event_t z80_update_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        z80_enabled = !z80_enabled;
        rg_settings_set_number(NS_APP, SETTING_Z80_EMULATION, z80_enabled);
    }
    strcpy(option->value, z80_enabled ? _("On") : _("Off"));

    return RG_DIALOG_VOID;
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

static bool save_state_handler(const char *filename)
{
    if ((savestate_fp = fopen(filename, "wb")))
    {
        savestate_errors = 0;
        gwenesis_save_state();
        fclose(savestate_fp);
        return savestate_errors == 0;
    }
    return false;
}

static bool load_state_handler(const char *filename)
{
    if ((savestate_fp = fopen(filename, "rb")))
    {
        savestate_errors = 0;
        gwenesis_load_state();
        fclose(savestate_fp);
        if (savestate_errors == 0)
            return true;
    }
    reset_emulation();
    return false;
}

static bool reset_handler(bool hard)
{
    reset_emulation();
    return true;
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
    {
        rg_display_submit(currentUpdate, 0);
    }
}

static rg_gui_event_t core1_rendering_update_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
        core1_task_rendering = !core1_task_rendering;
    strcpy(option->value, core1_task_rendering ? _("On") : _("Off"));
    return RG_DIALOG_VOID;
}

static rg_gui_event_t core1_sound_update_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
        core1_task_sound = !core1_task_sound;
    strcpy(option->value, core1_task_sound ? _("On") : _("Off"));
    return RG_DIALOG_VOID;
}

static void core1_task(void *arg)
{
    rg_task_msg_t msg;
    while (rg_task_receive(&msg))
    {
        switch (msg.type)
        {
            case 1: // Rendering
                gwenesis_vdp_render_line(msg.dataInt);
                break;
            case 2: // Sound
                gwenesis_SN76489_run(msg.dataInt);
                ym2612_run(msg.dataInt);
                break;
            case RG_TASK_MSG_STOP:
                return;
            default: // Sync/no-op
                continue;
        }
    }
}

static void options_handler(rg_gui_option_t *dest)
{
    *dest++ = (rg_gui_option_t){0, _("YM2612 audio "), "-", RG_DIALOG_FLAG_NORMAL, &yfm_update_cb};
    *dest++ = (rg_gui_option_t){0, _("SN76489 audio"), "-", RG_DIALOG_FLAG_NORMAL, &sn76489_update_cb};
    *dest++ = (rg_gui_option_t){0, _("Z80 emulation"), "-", RG_DIALOG_FLAG_NORMAL, &z80_update_cb};

    *dest++ = (rg_gui_option_t){0, _("Render on core 1"), "-", RG_DIALOG_FLAG_NORMAL, &core1_rendering_update_cb};
    *dest++ = (rg_gui_option_t){0, _("Sound on core 1"),  "-", RG_DIALOG_FLAG_NORMAL, &core1_sound_update_cb};

    *dest++ = (rg_gui_option_t)RG_DIALOG_END;
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

    app = rg_system_init(AUDIO_SAMPLE_RATE / 2, &handlers, NULL);

    yfm_enabled = rg_settings_get_number(NS_APP, SETTING_YFM_EMULATION, 1);
    sn76489_enabled = rg_settings_get_number(NS_APP, SETTING_SN76489_EMULATION, 0);
    z80_enabled = rg_settings_get_number(NS_APP, SETTING_Z80_EMULATION, 1);

    updates[0] = rg_surface_create(320, 241, RG_PIXEL_PAL565_BE, MEM_FAST);
    // updates[1] = rg_surface_create(320, 241, RG_PIXEL_PAL565_BE, MEM_FAST);
    currentUpdate = updates[0];

    // This is a hack because our new surface format doesn't yet support overdraw space easily
    updates[0]->data += 160;
    updates[0]->height = 240;
    // updates[1]->data += 160;
    // updates[1]->height = 240;

    VRAM = rg_alloc(VRAM_MAX_SIZE, MEM_FAST);

#ifdef USE_CORE1_TASK
    core1_task_handle = rg_task_create("core1_task", &core1_task, NULL, 4096, RG_TASK_PRIORITY_6, 1);
    RG_ASSERT(core1_task_handle, "Failed to create core1 task!");
#endif

    RG_LOGI("Genesis start\n");

    size_t rom_size;
    void *rom_data;

    if (rg_extension_match(app->romPath, "zip"))
    {
        if (!rg_storage_unzip_file(app->romPath, NULL, &rom_data, &rom_size, RG_FILE_ALIGN_64KB))
            rg_system_rom_load_failed(_("ROM file unzipping failed!"));
    }
    else if (!rg_storage_read_file(app->romPath, &rom_data, &rom_size, RG_FILE_ALIGN_64KB))
    {
        rg_system_rom_load_failed(_("ROM load failed!"));
    }

    RG_LOGI("load_cartridge(%p, %d)\n", rom_data, rom_size);
    if (!load_cartridge(rom_data, rom_size))
    {
        // load_cartridge() already printed why (missing SEGA signature,
        // reset vector out of range, etc -- see gwenesis_rom_looks_valid()
        // in gwenesis_bus.c). Refuse to run it rather than execute
        // whatever garbage sits at the wrong offsets: that is exactly how
        // a 512-byte copier header ahead of the ROM data produced "3-4 fps"
        // that was actually the 68000 running noise from its very first
        // fetch, not a performance bug.
        rg_system_rom_load_failed(_("ROM header invalid (bad or unhandled copier header?)"));
    }
    // free(rom_data); // load_cartridge takes ownership

    RG_LOGI("power_on()\n");
    power_on();

    RG_LOGI("reset_emulation()\n");
    reset_emulation();

    if (app->bootFlags & RG_BOOT_RESUME)
    {
        rg_emu_load_state(app->saveSlot);
    }

    rg_system_set_tick_rate(60);
    app->frameskip = 2;

    extern unsigned char gwenesis_vdp_regs[0x20];
    extern unsigned int gwenesis_vdp_status;
    extern unsigned short CRAM565[256];
    extern unsigned int screen_width, screen_height;
    extern int hint_pending;

    uint32_t keymap[8] = {RG_KEY_UP, RG_KEY_DOWN, RG_KEY_LEFT, RG_KEY_RIGHT, RG_KEY_A, RG_KEY_B, RG_KEY_SELECT, RG_KEY_START};
    uint32_t joystick = 0, joystick_old;

    int skipFrames = 0;

    RG_LOGI("emulation loop\n");
    while (true)
    {
        joystick_old = joystick;
        joystick = rg_input_read_gamepad();

        if (joystick & (RG_KEY_MENU | RG_KEY_OPTION))
        {
            if (joystick & RG_KEY_MENU)
                rg_gui_game_menu();
            else
                rg_gui_options_menu();
        }
        else if (joystick != joystick_old)
        {
            for (int i = 0; i < 8; i++)
            {
                if ((joystick & keymap[i]) == keymap[i])
                    gwenesis_io_pad_press_button(0, i);
                else
                    gwenesis_io_pad_release_button(0, i);
            }
        }

        int64_t startTime = rg_system_timer();
        bool drawFrame = skipFrames == 0;
        bool slowFrame = false;

        int lines_per_frame = REG1_PAL ? LINES_PER_FRAME_PAL : LINES_PER_FRAME_NTSC;
        int hint_counter = gwenesis_vdp_regs[10];

        screen_width = REG12_MODE_H40 ? 320 : 256;
        screen_height = REG1_PAL ? 240 : 224;

        gwenesis_vdp_set_buffer(currentUpdate->data);
        gwenesis_vdp_render_config();

        /* Reset the difference clocks and audio index */
        system_clock = 0;
        zclk = z80_enabled ? 0 : 0x1000000;

        ym2612_clock = yfm_enabled ? 0 : 0x1000000;
        ym2612_index = 0;

        sn76489_clock = sn76489_enabled ? 0 : 0x1000000;
        sn76489_index = 0;

        scan_line = 0;

        while (scan_line < lines_per_frame)
        {
            m68k_run(system_clock + VDP_CYCLES_PER_LINE);
            z80_run(system_clock + VDP_CYCLES_PER_LINE);

            /* Audio */
            /*  GWENESIS_AUDIO_ACCURATE:
            *    =1 : cycle accurate mode. audio is refreshed when CPUs are performing a R/W access
            *    =0 : line  accurate mode. audio is refreshed every lines.
            */
            if (GWENESIS_AUDIO_ACCURATE == 0) {
                if (core1_task_sound)
                {
                    rg_task_send(core1_task_handle, &(rg_task_msg_t){.type = 2, .dataInt = system_clock + VDP_CYCLES_PER_LINE});
                }
                else
                {
                    gwenesis_SN76489_run(system_clock + VDP_CYCLES_PER_LINE);
                    ym2612_run(system_clock + VDP_CYCLES_PER_LINE);
                }
            }

            /* Video */
            if (drawFrame && scan_line < screen_height)
            {
                if (core1_task_rendering)
                    rg_task_send(core1_task_handle, &(rg_task_msg_t){.type = 1, .dataInt = scan_line});
                else
                    gwenesis_vdp_render_line(scan_line); /* render scan_line */
            }

            // On these lines, the line counter interrupt is reloaded
            if ((scan_line == 0) || (scan_line > screen_height)) {
                //  if (REG0_LINE_INTERRUPT != 0)
                //    printf("HINTERRUPT counter reloaded: (scan_line: %d, new
                //    counter: %d)\n", scan_line, REG10_LINE_COUNTER);
                hint_counter = REG10_LINE_COUNTER;
            }

            // interrupt line counter
            if (--hint_counter < 0) {
                if ((REG0_LINE_INTERRUPT != 0) && (scan_line <= screen_height)) {
                    hint_pending = 1;
                    // printf("Line int pending %d\n",scan_line);
                    if ((gwenesis_vdp_status & STATUS_VIRQPENDING) == 0)
                    m68k_update_irq(4);
                }
                hint_counter = REG10_LINE_COUNTER;
            }

            scan_line++;

            // vblank begin at the end of last rendered line
            if (scan_line == screen_height) {
                if (REG1_VBLANK_INTERRUPT != 0) {
                    gwenesis_vdp_status |= STATUS_VIRQPENDING;
                    m68k_set_irq(6);
                }
                z80_irq_line(1);
            }
            if (scan_line == (screen_height + 1)) {
                z80_irq_line(0);
            }

            system_clock += VDP_CYCLES_PER_LINE;
        }

        // Make sure all our previous messages have been processed before we continue
        if (core1_task_rendering || core1_task_sound)
        {
            rg_task_send(core1_task_handle, &(rg_task_msg_t){0});
            rg_task_send(core1_task_handle, &(rg_task_msg_t){0});
        }

        /* Audio
        * synchronize YM2612 and SN76489 to system_clock
        * it completes the missing audio sample for accurate audio mode
        */
        if (GWENESIS_AUDIO_ACCURATE == 1) {
            gwenesis_SN76489_run(system_clock);
            ym2612_run(system_clock);
        }

        // reset m68k cycles to the begin of next frame cycle
        m68k.cycles -= system_clock;

        if (drawFrame)
        {
            for (int i = 0; i < 256; ++i)
                currentUpdate->palette[i] = (CRAM565[i] << 8) | (CRAM565[i] >> 8);
            slowFrame = !rg_display_sync(false);
            currentUpdate->width = screen_width;
            currentUpdate->height = screen_height;
            rg_display_submit(currentUpdate, 0);
        }

        rg_system_tick(rg_system_timer() - startTime);

        // TODO: Mix in gwenesis_sn76489_buffer
        rg_audio_submit((void *)gwenesis_ym2612_buffer, AUDIO_BUFFER_LENGTH >> 1);

        if (skipFrames == 0)
        {
            int elapsed = rg_system_timer() - startTime;
            if (app->frameskip > 0)
                skipFrames = app->frameskip;
            else if (elapsed > app->frameTime + 1500) // Allow some jitter
                skipFrames = 1; // (elapsed / frameTime)
            else if (drawFrame && slowFrame)
                skipFrames = 1;
        }
        else if (skipFrames > 0)
        {
            skipFrames--;
        }
    }
}
