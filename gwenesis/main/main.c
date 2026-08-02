#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>

#include <gwenesis.h>

#define USE_CORE1_TASK

#define AUDIO_SAMPLE_RATE (53267)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60 + 1)

// What rg_system_init() actually asks the codec for. Not AUDIO_SAMPLE_RATE and not a clean
// divisor of it -- see the comment at the rg_system_init() call site in app_main() for why
// (the ES8311 only accepts rates from its own coefficient table; 26633 wasn't in it and was
// silently ignored while I2S clocked out samples at that nominal rate anyway). Named and
// defined once, rather than left as the literal 32000 in two unrelated places, because that
// is exactly how the output buffer below went out of sync with the resample ratio once
// already: the buffer was sized for the previous rate pair and nobody had one symbol to grep
// for when the rate changed.
#define AUDIO_OUT_RATE (32000)

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

// Holds the downmixed/resampled output handed to rg_audio_submit(). See the long comment at
// the call site (search AUDIO_OUT_RATE below) for what this holds and how it's filled.
//
// Sized for the worst case of AUDIO_MAX_SAMPLES_PER_FRAME (1056, PAL-clamped) input samples
// at AUDIO_SAMPLE_RATE:AUDIO_OUT_RATE (53267:32000, ~1.6646:1) -- ceil(1056 * 32000 / 53267)
// = 635 output frames, plus a few for the fixed-point accumulator's own rounding, not the 528
// this used to be. 528 was `(AUDIO_MAX_SAMPLES_PER_FRAME + 1) / 2`, sized for a 2:1 downsample
// (the ratio this file used before AUDIO_OUT_RATE existed); it was reused unchanged as the
// resample loop's own bound (`while (audio_frames < RG_COUNT(gwenesis_audio_out))`, below)
// when the ratio changed to 53267:32000, and 528 < 635 so it silently kept working -- kept
// compiling, never wrote out of bounds, just quietly submitted less audio than a frame is
// worth. Simulated the fixed-point accumulator exactly (Python) rather than trust the ratio's
// float approximation: uncapped it produces 534 output frames for NTSC's ~888 input samples
// and 635 for PAL-clamped input, so at 528 this was dropping about 6/534 (~1.1%) of every
// NTSC frame's audio and about 106/635 (~17%) of every PAL frame's -- both silent, both
// underfeeding the DAC by exactly the mechanism the frameskip/pacing investigation elsewhere
// in this file is about, just a second, independent source of the same symptom.
static rg_audio_frame_t gwenesis_audio_out[(AUDIO_MAX_SAMPLES_PER_FRAME * AUDIO_OUT_RATE) / AUDIO_SAMPLE_RATE + 4];

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

    // AUDIO_OUT_RATE (32000), not the YM2612's own 53267 nor half of it. The ES8311 derives its
    // clocks from a coefficient table of standard rates and refuses anything outside it --
    // asking for AUDIO_SAMPLE_RATE/2 (26633) got "Unable to configure sample rate 26633Hz with
    // 6818048Hz MCLK" in the boot log while I2S went on shipping samples at that nominal rate
    // regardless, so the codec played at some other actual speed and the two drifted apart
    // continuously. That is what was audible on the speaker after the buffer and pacing fixes
    // above: still wrong, just less. 32000 is in the table, and fmsx and stella-go already use
    // it here, so AUDIO_OUT_RATE is defined next to AUDIO_SAMPLE_RATE above.
    app = rg_system_init(AUDIO_OUT_RATE, &handlers, NULL);

    yfm_enabled = rg_settings_get_number(NS_APP, SETTING_YFM_EMULATION, 1);
    // Default on, like the FM chip beside it. It defaulted to 0, which meant the PSG never
    // ran unless someone found the menu entry -- and the PSG is where a Mega Drive keeps its
    // percussion and most of its sound effects, so the machine simply sounded wrong out of
    // the box. Nothing in the history says why it was off; the submit path was also throwing
    // its buffer away, so it made no audible difference either way until that was fixed.
    sn76489_enabled = rg_settings_get_number(NS_APP, SETTING_SN76489_EMULATION, 1);
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

        // gwenesis_ym2612_buffer and gwenesis_sn76489_buffer are both mono, produced at the real
        // YM2612 rate (AUDIO_SAMPLE_RATE, 53267Hz for NTSC -- see ym2612_run()/YM2612Update() in
        // sound/ym2612.c and gwenesis_SN76489_run()/_Update() in sound/gwenesis_sn76489.c, which
        // each write one int16 per sample). rg_system_init() above asks the codec for AUDIO_OUT_RATE
        // (32000), so this resamples 53267 -> 32000 (~1.6646 input samples per output sample, not a
        // clean ratio) with a fixed-point box filter: each output sample is the mean of the run of
        // input samples it covers, which resamples and low-pass filters in the same pass -- averaging
        // is a legitimate anti-alias filter for a decimating ratio, not just a cheap stand-in for one.
        //
        // History, so the next person doesn't redo this investigation:
        //  1. Originally rg_audio_submit() was handed gwenesis_ym2612_buffer directly and its count
        //     was miscomputed (rg_audio_frame_t is {int16 left; int16 right;} counted in stereo
        //     frames, not samples), so consecutive mono samples were read as one L/R frame each --
        //     no anti-alias filter, and a fixed one-sample (~18.8us) offset between channels.
        //  2. Fixed 2026-08-02 by switching to AUDIO_SAMPLE_RATE/2 (26633) and averaging adjacent
        //     pairs, since 53267/26633 = 2.00004 is close enough to exactly 2:1 that a plain average
        //     is a correct decimation filter for that ratio. Built and measured -- except the ES8311
        //     codec rejects 26633 outright ("Unable to configure sample rate 26633Hz with 6818048Hz
        //     MCLK" in the boot log, since it can only derive clocks from a coefficient table of
        //     standard rates) and plays at some other actual rate while I2S keeps shipping samples
        //     timed for 26633, so the two drift apart continuously. Still wrong, just less audibly so
        //     than the original bug -- caught before it reached the user.
        //  3. Moved to AUDIO_OUT_RATE (32000, in the ES8311's table -- fmsx/main/main.c and
        //     stella-go/main/main.cpp already run at 32000), which is not a 2:1 or otherwise clean
        //     ratio of 53267, hence the fixed-point resample below instead of a plain pairwise average.
        //  4. gwenesis_sn76489_buffer (the PSG -- percussion and most sound effects on this machine)
        //     was being synthesized into its own buffer every frame and never read; only the YM2612
        //     reached rg_audio_submit(). Added to the mix here.
        //
        // Whether ym2612_index and sn76489_index address the same instant in each buffer (item 4
        // depends on this) was checked, not assumed: gwenesis_SN76489_run() and ym2612_run() are
        // called with the identical `target` argument at the identical points every scanline (below,
        // and again for the GWENESIS_AUDIO_ACCURATE==1 sync and the core1_task_sound path), both
        // reset to index=0/clock=0 together at the top of this loop, and both advance by the
        // identical recurrence (index += (target-clock)/divisor) with the identical divisor --
        // gwenesis_SN76489_Init() (gwenesis_bus.c) is called with AUDIO_FREQ_DIVISOR, and
        // YM2612Config() sets ym2612.divisor = AUDIO_FREQ_DIVISOR too (ym2612.c). Same divisor, same
        // inputs, same starting state -> the two index sequences are identical by construction, not
        // merely close, so `i` addresses the same instant in both buffers. This does not hold when
        // SN76489 is disabled: sn76489_clock is set to 0x1000000 below, which pins sn76489_index at 0
        // all frame, and the `i < sn76489_index` guard below already handles that (PSG contributes 0
        // past index 0 rather than reading stale data).
        //
        // sn76489_enabled defaults to *off* (SETTING_SN76489_EMULATION's fallback below is 0, unlike
        // yfm_enabled's 1) -- on a fresh settings file, or any save from before this option existed,
        // this mixing code runs and finds sn76489_index pinned at 0 all frame, contributing nothing.
        // The PSG-is-missing symptom this change targets will still reproduce until that default (or
        // the user's saved setting) is flipped on; the option already exists ("SN76489 audio" in
        // options_handler below), it is just off by default. Flagged rather than changed here --
        // whether it defaults off on purpose (it predates this change; the earlier state of this file
        // had the same fallback while the PSG buffer was going unread, which is at least consistent
        // with "off" being intentional at the time) is a product decision for whoever ships this, not
        // a mechanical bug -- but shipping today's change without addressing it will not fix what the
        // user is hearing.
        //
        // Weighting: gwenesis_sn76489_buffer is halved before mixing. No documented relative level
        // between the YM2612 and SN76489 outputs exists anywhere in this gwenesis core (checked
        // sound/gwenesis_sn76489.c, sound/ym2612.c, bus/gwenesis_bus.c -- the only per-chip level
        // tables are each chip's own internal per-channel attenuation, nothing relating one chip's
        // output scale to the other's). By the numbers that do exist: SN76489's raw per-sample sum
        // tops out around PSG_MAX_VOLUME_MAX*3 + noise-doubled PSG_MAX_VOLUME_MAX*2 =~ 15,500
        // (gwenesis_sn76489.c), well inside int16 range; YM2612's raw sum can reach 6*8192 =~ 49,152
        // (six channels each clamped to the 14-bit accumulator range in ym2612.c's YM2612Update, then
        // stored to an int16 buffer with no clamp of its own) before this file's clamp is ever applied
        // -- FM alone can already want more than full scale. Given that, halving the PSG is not
        // obviously the safer choice for headroom; it's the FM side that can dominate. With no
        // reference to defer to, this is a listening call, and the point of wiring up the speaker was
        // to be able to make it: leaving PSG at half weight for now as the more conservative starting
        // guess (won't drown out FM on the first listen), but this constant should move once someone
        // has actually heard it, not stay because a different port used it.
        //
        // Clipping: summed and averaged in int32 (`sum`, `mixed`), clamped to int16 range only once,
        // after the divide -- clamping the raw per-sample inputs before summing would be the wrong
        // order (it would clip twice and distort the average); clamping the finished average once is
        // correct as written. Overflow before the divide was checked, not assumed: each term is at
        // most one int16 YM sample (+/-32768) plus one halved int16 SN sample (+/-16384), and the
        // fixed-point step for 53267:32000 (~1.6646) means the accumulated run per output sample (`n`
        // below) is 1 or 2 input samples, never more -- worst-case sum is ~2*49152 =~ 98,304, nowhere
        // near int32's ~2.1 billion range. No overflow risk at this ratio and these sample widths.
        //
        // gwenesis_audio_out[] is sized in its own declaration comment above for this loop's actual
        // worst case (635 output frames, not the 528 a stale 2:1-ratio sizing would give) -- verified
        // by simulating this exact fixed-point accumulator, not by trusting the float ratio.
        const uint32_t step = ((uint32_t)AUDIO_SAMPLE_RATE << 16) / AUDIO_OUT_RATE;
        int audio_frames = 0;
        uint32_t pos = 0;
        while (audio_frames < (int)RG_COUNT(gwenesis_audio_out))
        {
            uint32_t next = pos + step;
            int from = pos >> 16, to = next >> 16;
            if (to > ym2612_index)
                break;
            int32_t sum = 0, n = 0;
            for (int i = from; i < to; ++i, ++n)
                sum += gwenesis_ym2612_buffer[i] + (i < sn76489_index ? gwenesis_sn76489_buffer[i] / 2 : 0);
            int32_t mixed = n ? sum / n : 0;
            if (mixed > 32767) mixed = 32767;
            else if (mixed < -32768) mixed = -32768;
            int16_t sample = (int16_t)mixed;
            gwenesis_audio_out[audio_frames].left = sample;
            gwenesis_audio_out[audio_frames].right = sample;
            ++audio_frames;
            pos = next;
        }
        rg_audio_submit(gwenesis_audio_out, audio_frames);

        // Investigated 2026-08-02 alongside the audio rate fix above, because the reported symptom
        // ("드르르륵", a periodic rattle rather than a continuous distortion) pointed at underfeeding
        // the DAC, not at the channel-interleaving bug fixed above -- that bug is continuous and
        // frame-independent, so it can't produce a periodic artifact on its own. Recorded here rather
        // than changed, because every piece of it lives outside this file:
        //
        // 1. The debug line ("FPS:57 (43+0+14)" at "BUSY:66%") undercounts busy time. rg_system_tick()
        //    is called (above, before this audio submit) with an elapsed time captured *before* this
        //    frame's rg_audio_submit() runs, so whatever rg_audio_submit() -> driver_submit() ->
        //    i2s_channel_write() (components/retro-go/drivers/audio/i2s.c) spends blocked for a free
        //    DMA descriptor is real wall-clock time this frame took, but is invisible to statistics.
        //    busyPercent (components/retro-go/rg_system.c, update_statistics()). The "34% idle" the
        //    log implies is largely this blocking wait, not spare CPU.
        // 2. That blocking wait is also the loop's de facto pacer: the codec drains the I2S ring at a
        //    fixed real-time rate regardless of what the CPU is doing, so i2s_channel_write() (1000ms
        //    timeout, a real FreeRTOS block, not a spin) only returns once there's room, which in
        //    steady state paces this loop to roughly real time on its own -- no separate vsync/audio
        //    sync call was written for it because the blocking write already behaves like one.
        // 3. driver_submit()'s local staging buffer is only 180 rg_audio_frame_t (matching the 4x180
        //    DMA descriptor config passed to i2s_new_channel()), so a submission this size (~534
        //    frames/emulated-frame for NTSC at the current 53267->32000 resample, was ~444 at the
        //    previous 26633 target) still splits into three separate i2s_channel_write() calls
        //    (180+180+174 now, was 180+180+84) instead of one. Three blocking round trips instead of
        //    one, each with its own FreeRTOS wake latency, was a plausible source of the ~0.87ms/frame
        //    overshoot (17.54ms actual vs. 16.67ms ideal at 60fps) measured against the FPS:57,
        //    BUSY:66% figures quoted above -- those figures were captured before AUDIO_OUT_RATE moved
        //    to 32000 and before the PSG mix was added, both of which change what runs in this window,
        //    so they're recorded here as the reasoning that was verified then, not re-verified against
        //    this exact build. The mechanism (three round trips instead of one) is unchanged by either
        //    of those edits and so is still the leading suspect, but the resulting FPS/shortfall number
        //    should be re-measured on hardware against this version rather than assumed to still be 57.
        // 4. Separately, `if (app->frameskip > 0) skipFrames = app->frameskip;` just below unconditionally
        //    re-arms the skip counter every time it reaches 0 whenever app->frameskip is nonzero (2 by
        //    default, set above) -- it does not consult `elapsed` or `slowFrame` in that case, so this
        //    core skips 2 of every 3 frames' *rendering* by fixed ratio regardless of how much headroom
        //    the previous frame actually had. This affects video (frames drawn), not audio (submitted
        //    every iteration regardless of drawFrame) or the loop rate itself, so it does not explain the
        //    57fps/previous-26,633Hz mismatch above, but it is worth knowing it's there.
        //
        // None of (1)-(4) are specific to gwenesis: the exact skipFrames pattern in (4) is duplicated,
        // apparently copy-pasted, verbatim in wswan-go, vb-go, retro-core's main_sms/gbc/pce/lynx/nes/snes,
        // pkmini-go, stella-go, and supervision-go's main loops, and (1)-(3) live in shared rg_system.c /
        // rg_audio.c / drivers/audio/i2s.c that every core links against. Fixing any of it here would fix
        // it for gwenesis alone and leave the other ports with the identical symptom; left unpatched on
        // purpose pending a framework-level decision, per instruction not to patch one app around a
        // question that paces every core on this device.
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
