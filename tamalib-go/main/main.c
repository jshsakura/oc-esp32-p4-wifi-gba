/*
 * Tamagotchi P1 (TamaLIB) for retro-go.
 *
 * Drives the core's own API rather than a bundled front-end (there isn't one to bundle --
 * TamaLIB ships as a bare library and expects every port to write its own hal_t, the same
 * shape prosystem-go and supervision-go's front-ends already have here). One frame is a
 * batch of tamalib_step() calls until the core's tick counter has advanced one video frame's
 * worth of its own 32768Hz clock; the picture is not read out of a framebuffer the core owns
 * (it doesn't have one -- the P1 drives a segment LCD, one segment at a time) but built up
 * from two callbacks, set_lcd_matrix() and set_lcd_icon(), into our own persistent shadow
 * state, which this file then rasterises into an RGB565 surface every frame.
 */
#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tamalib.h"

/* The P1's real display: a 32x16 monochrome dot matrix plus 8 single-bit icon lamps
 * (LCD_WIDTH/HEIGHT/ICON_NUM come from tamalib_hw.h). There is no art for the icons here --
 * their pictures (meal, light, medicine, ...) live in bitmap assets the game-and-watch
 * reference port ships and this repo does not have, and guessing at them would be fabricating
 * assets, not porting a core. Each icon is instead drawn as a numbered lamp (0-7) using a
 * hand-rolled 3x5 font, so all eight are still visibly and correctly on/off -- just unlabeled
 * by name.
 *
 * Surface size: the matrix is rendered at a fixed 6x pixel scale (192x96) with a lamp row of
 * four 24px icons above and below it (matching the real hardware's icon layout, one row over
 * the screen and one under). 6x keeps every dot a comfortably visible square without the
 * surface becoming enormous -- the native 32x16 would be a few dozen pixels across on this
 * target's screen, and unlike the other cores here TamaLIB has no "native resolution" of its
 * own to defer to since the real device has no pixel raster at all, only discrete segments.
 */
#define TAMA_MATRIX_SCALE   6
#define TAMA_MATRIX_W       (LCD_WIDTH * TAMA_MATRIX_SCALE)   /* 192 */
#define TAMA_MATRIX_H       (LCD_HEIGHT * TAMA_MATRIX_SCALE)  /*  96 */

#define TAMA_ICON_SIZE      24
#define TAMA_ICON_GAP       8
#define TAMA_ICONS_PER_ROW  (ICON_NUM / 2)                    /*   4 */
#define TAMA_ICON_ROW_W     (TAMA_ICONS_PER_ROW * TAMA_ICON_SIZE + (TAMA_ICONS_PER_ROW - 1) * TAMA_ICON_GAP)

#define TAMA_SURFACE_W      TAMA_MATRIX_W
#define TAMA_SURFACE_H      (TAMA_ICON_SIZE + TAMA_ICON_GAP + TAMA_MATRIX_H + TAMA_ICON_GAP + TAMA_ICON_SIZE)

#define TAMA_MATRIX_Y0      (TAMA_ICON_SIZE + TAMA_ICON_GAP)
#define TAMA_ICON_ROW_X0    ((TAMA_SURFACE_W - TAMA_ICON_ROW_W) / 2)
#define TAMA_ICON_TOP_Y0    0
#define TAMA_ICON_BOTTOM_Y0 (TAMA_MATRIX_Y0 + TAMA_MATRIX_H + TAMA_ICON_GAP)

/* RGB565, plain native-endian values written straight into the surface -- same convention
 * supervision-go's core-drawn-RGB565 surface uses (no palette involved). */
#define RGB565(r, g, b)     ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define COLOR_BG            RGB565(198, 213, 173) /* pale LCD-green, segments "off" */
#define COLOR_ON            RGB565(35, 42, 30)     /* dark ink, segments "on" */
#define COLOR_ICON_BORDER   RGB565(96, 104, 84)
#define COLOR_ICON_ON       RGB565(224, 92, 40)    /* amber lamp lit */

/* The E0C6S46 fetches its program directly out of this array by pointer (tama_cpu_init()
 * keeps the pointer, it does not copy), so it has to live for the app's whole lifetime. 6144
 * words is the chip's full ROM size. */
#define TAMA_ROM_WORDS      6144
static u12_t tama_rom[TAMA_ROM_WORDS] __attribute__((aligned(4)));

/* The core's own clock. Sample rate is pinned to it (not a round 44100/48000) so that one
 * frame's worth of CPU ticks is also exactly one frame's worth of audio samples -- the same
 * trick the game-and-watch reference port uses, and it is what makes hal_play_sound()'s
 * per-tick buzzer log double as a ready-made audio buffer with no resampling. */
#define TAMA_CLOCK_RATE       32768
#define TAMA_SAMPLE_RATE      TAMA_CLOCK_RATE
#define TAMA_FRAME_RATE       60
#define TAMA_CLOCKS_PER_FRAME (TAMA_CLOCK_RATE / TAMA_FRAME_RATE) /* 546 */
#define TAMA_AUDIO_AMPLITUDE  8000

static rg_app_t *app;
static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;
static state_t *state;

/* Shadow of the LCD's segment state, kept up to date by the hal callbacks and rasterised into
 * the current surface once per frame in render_frame(). Rendering straight from the callbacks
 * into currentUpdate would leave the *other* buffer holding a stale picture the next time it
 * becomes current -- the real LCD only redraws segments that changed, but our two RGB565
 * buffers both need the full picture every time they're shown. */
static bool_t tama_matrix[LCD_WIDTH][LCD_HEIGHT];
static bool_t tama_icons[ICON_NUM];

/* Buzzer state, latched by the hal callbacks during tamalib_step() and turned into a square
 * wave in render_audio(). */
static uint8_t tama_buzzer_period;
static int8_t tama_buzzer_log[TAMA_CLOCKS_PER_FRAME]; /* -1 = no change this tick */
static uint64_t frame_start_tick;

/* --- 3x5 numeral font, used only to label the icon lamps 0-7 (see the comment above the
 * layout macros for why there's no real icon art). One byte per row, bits 2..0 = left..right
 * column. --- */
static const uint8_t digit_font[8][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7}, /* 0 */
    {0x2, 0x6, 0x2, 0x2, 0x7}, /* 1 */
    {0x7, 0x1, 0x7, 0x4, 0x7}, /* 2 */
    {0x7, 0x1, 0x7, 0x1, 0x7}, /* 3 */
    {0x5, 0x5, 0x7, 0x1, 0x1}, /* 4 */
    {0x7, 0x4, 0x7, 0x1, 0x7}, /* 5 */
    {0x7, 0x4, 0x7, 0x5, 0x7}, /* 6 */
    {0x7, 0x1, 0x2, 0x2, 0x2}, /* 7 */
};
#define DIGIT_SCALE 3

static inline void put_px(rg_surface_t *s, int x, int y, uint16_t c)
{
    ((uint16_t *)s->data)[y * s->width + x] = c;
}

static void fill_rect(rg_surface_t *s, int x0, int y0, int w, int h, uint16_t c)
{
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            put_px(s, x, y, c);
}

static void draw_digit(rg_surface_t *s, int x0, int y0, int digit, uint16_t c)
{
    for (int row = 0; row < 5; row++)
    {
        uint8_t bits = digit_font[digit][row];
        for (int col = 0; col < 3; col++)
            if (bits & (1 << (2 - col)))
                fill_rect(s, x0 + col * DIGIT_SCALE, y0 + row * DIGIT_SCALE, DIGIT_SCALE, DIGIT_SCALE, c);
    }
}

static void draw_icon(rg_surface_t *s, int x0, int y0, int index, bool_t on)
{
    /* 2px border always visible so a lamp's *position* reads even when it's off. */
    fill_rect(s, x0, y0, TAMA_ICON_SIZE, TAMA_ICON_SIZE, COLOR_ICON_BORDER);
    fill_rect(s, x0 + 2, y0 + 2, TAMA_ICON_SIZE - 4, TAMA_ICON_SIZE - 4, on ? COLOR_ICON_ON : COLOR_BG);

    int dw = 3 * DIGIT_SCALE, dh = 5 * DIGIT_SCALE;
    draw_digit(s, x0 + (TAMA_ICON_SIZE - dw) / 2, y0 + (TAMA_ICON_SIZE - dh) / 2, index,
               on ? COLOR_BG : COLOR_ICON_BORDER);
}

static void render_frame(rg_surface_t *dst)
{
    rg_surface_fill(dst, NULL, COLOR_BG);

    for (int x = 0; x < LCD_WIDTH; x++)
        for (int y = 0; y < LCD_HEIGHT; y++)
            if (tama_matrix[x][y])
                fill_rect(dst, x * TAMA_MATRIX_SCALE, TAMA_MATRIX_Y0 + y * TAMA_MATRIX_SCALE,
                          TAMA_MATRIX_SCALE, TAMA_MATRIX_SCALE, COLOR_ON);

    for (int i = 0; i < TAMA_ICONS_PER_ROW; i++)
    {
        int x = TAMA_ICON_ROW_X0 + i * (TAMA_ICON_SIZE + TAMA_ICON_GAP);
        draw_icon(dst, x, TAMA_ICON_TOP_Y0, i, tama_icons[i]);
        draw_icon(dst, x, TAMA_ICON_BOTTOM_Y0, i + TAMA_ICONS_PER_ROW, tama_icons[i + TAMA_ICONS_PER_ROW]);
    }
}

/* --- TamaLIB hal_t: every pointer must be non-NULL (see tamalib_hal.h), even the ones this
 * front-end has nothing to do for. --- */

static void hal_halt(void) {}
static bool_t hal_is_log_enabled(log_level_t level) { return 0; }
static void hal_log(log_level_t level, char *buff, ...) {}

static void hal_set_lcd_matrix(u8_t x, u8_t y, bool_t val)
{
    if (x < LCD_WIDTH && y < LCD_HEIGHT)
        tama_matrix[x][y] = val;
}

static void hal_set_lcd_icon(u8_t icon, bool_t val)
{
    if (icon < ICON_NUM)
        tama_icons[icon] = val;
}

static void hal_set_sound_period(u8_t period) { tama_buzzer_period = period; }

static void hal_play_sound(bool_t en)
{
    /* Index by ticks since this frame started, not since boot: the log is a per-frame buffer
     * (see the comment on tama_buzzer_log) and gets rewound to -1 at the top of every frame. */
    uint64_t idx = *state->tick_counter - frame_start_tick;
    if (idx < TAMA_CLOCKS_PER_FRAME)
        tama_buzzer_log[idx] = en ? tama_buzzer_period : 0;
}

static hal_t hal = {
    .halt = &hal_halt,
    .is_log_enabled = &hal_is_log_enabled,
    .log = &hal_log,
    .set_lcd_matrix = &hal_set_lcd_matrix,
    .set_lcd_icon = &hal_set_lcd_icon,
    .set_sound_period = &hal_set_sound_period,
    .play_sound = &hal_play_sound,
};

/* Turns this frame's buzzer-period log into a square wave. current_period/half_period/phase
 * are static (persist across frames) so a tone that's still playing at a frame boundary keeps
 * its phase instead of clicking back to zero. */
static void render_audio(rg_audio_frame_t *mixbuf)
{
    static uint8_t current_period = 0, half_period = 0, phase = 0;

    for (int i = 0; i < TAMA_CLOCKS_PER_FRAME; i++)
    {
        int8_t next = tama_buzzer_log[i];
        if (next >= 0 && (uint8_t)next != current_period)
        {
            current_period = (uint8_t)next;
            half_period = current_period >> 1;
            phase = 0;
        }

        int16_t sample = 0;
        if (current_period > 0)
        {
            sample = (phase < half_period) ? TAMA_AUDIO_AMPLITUDE : -TAMA_AUDIO_AMPLITUDE;
            if (++phase >= current_period)
                phase = 0;
        }
        mixbuf[i].left = sample;
        mixbuf[i].right = sample;
    }
}

static void update_buttons(void)
{
    uint32_t pad = rg_input_read_gamepad();
    /* Tamagotchi P1 only has three buttons. Mapped the same way the game-and-watch reference
     * port maps its d-pad/B/A: any direction is the left button, B is middle, A is right. */
    bool_t left = (pad & (RG_KEY_UP | RG_KEY_DOWN | RG_KEY_LEFT | RG_KEY_RIGHT)) ? 1 : 0;
    tamalib_set_button(BTN_LEFT, left ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
    tamalib_set_button(BTN_MIDDLE, (pad & RG_KEY_B) ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
    tamalib_set_button(BTN_RIGHT, (pad & RG_KEY_A) ? BTN_STATE_PRESSED : BTN_STATE_RELEASED);
}

/* ROM words are packed two bytes big-endian per 12-bit word, high byte's top nibble unused --
 * the standard layout tamalib/tamatool ROM dumps use, and what the game-and-watch reference
 * port's load_rom() unpacks the same way. */
static bool load_rom(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0 || size > (long)(TAMA_ROM_WORDS * 2))
    {
        fclose(fp);
        return false;
    }

    uint8_t *raw = rg_alloc(size, MEM_SLOW);
    bool ok = raw && fread(raw, 1, size, fp) == (size_t)size;
    fclose(fp);

    if (ok)
    {
        size_t words = size / 2;
        memset(tama_rom, 0, sizeof(tama_rom));
        for (size_t i = 0; i < words; i++)
            tama_rom[i] = raw[i * 2 + 1] | ((raw[i * 2] & 0x0F) << 8);
    }
    free(raw);
    return ok;
}

/* --- Save states -----------------------------------------------------------------------
 * A flat snapshot of everything tama_cpu_reset() initialises: registers, timers, RAM/display
 * memory and interrupt latches. Same fields the game-and-watch reference port's state_tama.c
 * saves, packed into one struct here instead of a separate file since this core only has the
 * one front-end file. save_time is carried through but otherwise unused -- it only matters to
 * a fast-forward-since-last-save feature this port doesn't implement.
 */
typedef struct __attribute__((packed))
{
    char magic[4];
    uint8_t version;
    uint64_t save_time;

    u13_t pc;
    u12_t x;
    u12_t y;
    u4_t a;
    u4_t b;
    u5_t np;
    u8_t sp;
    u4_t flags;

    u64_t tick_counter;
    u64_t clk_timer_timestamp;
    u64_t prog_timer_timestamp;
    bool_t prog_timer_enabled;
    u8_t prog_timer_data;
    u8_t prog_timer_rld;
    u32_t call_depth;

    MEM_BUFFER_TYPE memory[MEM_BUFFER_SIZE];
    interrupt_t interrupts[INT_SLOT_NUM];

    uint32_t crc32;
} tama_save_t;

#define TAMA_SAVE_VERSION 1

static bool save_state_handler(const char *filename)
{
    static tama_save_t save; /* function-static: too big to be comfortable on a task stack */
    memset(&save, 0, sizeof(save));

    memcpy(save.magic, "TAMA", 4);
    save.version = TAMA_SAVE_VERSION;
    save.save_time = (uint64_t)rg_system_timer();

    save.pc = *state->pc;
    save.x = *state->x;
    save.y = *state->y;
    save.a = *state->a;
    save.b = *state->b;
    save.np = *state->np;
    save.sp = *state->sp;
    save.flags = *state->flags;

    save.tick_counter = *state->tick_counter;
    save.clk_timer_timestamp = *state->clk_timer_timestamp;
    save.prog_timer_timestamp = *state->prog_timer_timestamp;
    save.prog_timer_enabled = *state->prog_timer_enabled;
    save.prog_timer_data = *state->prog_timer_data;
    save.prog_timer_rld = *state->prog_timer_rld;
    save.call_depth = *state->call_depth;

    memcpy(save.memory, state->memory, sizeof(save.memory));
    memcpy(save.interrupts, state->interrupts, sizeof(save.interrupts));

    save.crc32 = rg_crc32(0, (const uint8_t *)&save, sizeof(save) - sizeof(save.crc32));

    FILE *fp = fopen(filename, "wb");
    if (!fp)
        return false;
    bool ok = fwrite(&save, 1, sizeof(save), fp) == sizeof(save);
    fclose(fp);
    return ok;
}

static bool load_state_handler(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp)
        return false;

    static tama_save_t save;
    bool ok = fread(&save, 1, sizeof(save), fp) == sizeof(save);
    fclose(fp);

    if (!ok || memcmp(save.magic, "TAMA", 4) != 0 || save.version != TAMA_SAVE_VERSION)
        return false;

    uint32_t expected = save.crc32;
    if (rg_crc32(0, (const uint8_t *)&save, sizeof(save) - sizeof(save.crc32)) != expected)
        return false;

    *state->pc = save.pc;
    *state->x = save.x;
    *state->y = save.y;
    *state->a = save.a;
    *state->b = save.b;
    *state->np = save.np;
    *state->sp = save.sp;
    *state->flags = save.flags;

    *state->tick_counter = save.tick_counter;
    *state->clk_timer_timestamp = save.clk_timer_timestamp;
    *state->prog_timer_timestamp = save.prog_timer_timestamp;
    *state->prog_timer_enabled = save.prog_timer_enabled;
    *state->prog_timer_data = save.prog_timer_data;
    *state->prog_timer_rld = save.prog_timer_rld;
    *state->call_depth = save.call_depth;

    memcpy(state->memory, save.memory, sizeof(save.memory));
    memcpy(state->interrupts, save.interrupts, sizeof(save.interrupts));

    /* The LCD/icon shadow state was built up from callbacks the CPU isn't going to repeat
     * (it only reports segment *changes*), so pull the display memory back out by hand. */
    tamalib_refresh_hw();

    return true;
}

static bool reset_handler(bool hard)
{
    tamalib_reset();
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

void app_main(void)
{
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
    };

    app = rg_system_init(TAMA_SAMPLE_RATE, &handlers, NULL);

    /* Two buffers on purpose: rg_display_submit() reads the surface in place on another task
     * through a one-deep blocking queue, so a single buffer both tears and stalls the
     * emulator on every submit. */
    updates[0] = rg_surface_create(TAMA_SURFACE_W, TAMA_SURFACE_H, RG_PIXEL_565_LE, MEM_FAST);
    updates[1] = rg_surface_create(TAMA_SURFACE_W, TAMA_SURFACE_H, RG_PIXEL_565_LE, MEM_FAST);
    currentUpdate = updates[0];

    if (!load_rom(app->romPath))
        rg_system_rom_load_failed(_("Could not load the game file."));

    tamalib_register_hal(&hal);
    tamalib_init(tama_rom, TAMA_CLOCK_RATE);
    state = tamalib_get_state();

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

        update_buttons();

        memset(tama_buzzer_log, -1, sizeof(tama_buzzer_log));
        frame_start_tick = *state->tick_counter;
        uint64_t target = frame_start_tick + TAMA_CLOCKS_PER_FRAME;
        while (*state->tick_counter < target)
            tamalib_step();

        if (drawFrame)
        {
            render_frame(currentUpdate);
            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
        }

        rg_audio_frame_t mixbuf[TAMA_CLOCKS_PER_FRAME];
        render_audio(mixbuf);

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit(mixbuf, TAMA_CLOCKS_PER_FRAME);

        if (skipFrames == 0)
            skipFrames = app->frameskip;
        else if (skipFrames > 0)
            skipFrames--;
    }
}
