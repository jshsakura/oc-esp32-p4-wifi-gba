#pragma once

// The odroid_* API, reimplemented on top of retro-go 2.0's rg_*.
//
// Why this exists: the Game & Watch retro-go fork carries around thirty emulator porting
// layers -- Virtual Boy, WonderSwan, Neo Geo Pocket, Supervision, Amstrad, native Zelda 3
// and Super Mario World ports, and more -- amounting to some forty thousand lines. Every one
// of them talks to its platform through this API and nothing else. Retro-Go renamed
// odroid_* to rg_* years ago and moved a few things around, but the shape barely changed.
//
// So rather than rewrite thirty porting layers, this file is written once and they compile
// against it unchanged. It is a translation layer, not a rewrite: where a call maps onto an
// rg_* one it forwards, and where the difference is real -- the STM32's flash-caching
// helpers have no purpose on a chip with 32MB of PSRAM -- it says so rather than pretending.
//
// Anything here that cannot be honestly implemented returns a value that fails safely and
// logs once. Silently doing nothing is how a port ends up half-working in ways nobody can
// find.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <rg_system.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

// The fork's button bits. Values are its own; only the mapping to RG_KEY_* matters.
enum
{
    ODROID_INPUT_UP = 0,
    ODROID_INPUT_RIGHT,
    ODROID_INPUT_DOWN,
    ODROID_INPUT_LEFT,
    ODROID_INPUT_SELECT,
    ODROID_INPUT_START,
    ODROID_INPUT_A,
    ODROID_INPUT_B,
    ODROID_INPUT_X,
    ODROID_INPUT_Y,
    ODROID_INPUT_MENU,
    ODROID_INPUT_VOLUME,
    ODROID_INPUT_ANY,
    ODROID_INPUT_MAX,
};

// The fork indexes this by ODROID_INPUT_*, one byte per key, rather than using a bitmask.
typedef struct
{
    uint8_t values[ODROID_INPUT_MAX];
} odroid_gamepad_state_t;

odroid_gamepad_state_t odroid_input_read_gamepad(void);

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

typedef enum
{
    ODROID_DISPLAY_SCALING_OFF = 0, // no scaling, centred
    ODROID_DISPLAY_SCALING_FIT,     // scaled to fit, aspect preserved
    ODROID_DISPLAY_SCALING_FULL,    // stretched to fill
    ODROID_DISPLAY_SCALING_CUSTOM,
    ODROID_DISPLAY_SCALING_COUNT,
} odroid_display_scaling_t;

odroid_display_scaling_t odroid_display_get_scaling_mode(void);
void odroid_display_set_scaling_mode(odroid_display_scaling_t mode);

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

void odroid_audio_mute(bool mute);
int odroid_audio_volume_get(void);
void odroid_audio_volume_set(int volume);
int odroid_audio_sample_rate_get(void);

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

typedef enum
{
    ODROID_DIALOG_INIT = 0,
    ODROID_DIALOG_PREV,
    ODROID_DIALOG_NEXT,
    ODROID_DIALOG_ENTER,
    ODROID_DIALOG_ALT,
} odroid_dialog_event_t;

#define ODROID_DIALOG_CHOICE_LAST {-1, NULL, NULL, 0, NULL}

typedef struct odroid_dialog_choice_s odroid_dialog_choice_t;
typedef bool (*odroid_dialog_callback_t)(odroid_dialog_choice_t *option, odroid_dialog_event_t event,
                                         uint32_t repeat);

struct odroid_dialog_choice_s
{
    int id;
    const char *label;
    char *value;
    int enabled;
    odroid_dialog_callback_t update_cb;
};

// ---------------------------------------------------------------------------
// System and paths
// ---------------------------------------------------------------------------

typedef enum
{
    ODROID_PATH_SAVE_STATE = 0,
    ODROID_PATH_SAVE_SRAM,
    ODROID_PATH_SAVE_BACK,
    ODROID_PATH_SCREENSHOT,
    ODROID_PATH_ROM_FILE,
} odroid_path_type_t;

void odroid_system_init(int appId, int sampleRate);
void odroid_system_emu_init(void *load_cb, void *save_cb, void *reset_cb);
bool odroid_system_emu_load_state(int slot);
bool odroid_system_emu_save_state(int slot);
char *odroid_system_get_path(odroid_path_type_t type, const char *romPath);
void odroid_system_sleep(void);
void odroid_system_switch_app(int app);

// ---------------------------------------------------------------------------
// Things that existed only because the Game & Watch had 724KB and a flash chip
// ---------------------------------------------------------------------------

// On the STM32 these copied a file into external flash or RAM so a core could reach it
// without holding it all at once. This chip has 32MB of PSRAM: the file is simply read into
// memory and the pointer handed back, which is what every caller wanted in the first place.
void *odroid_overlay_cache_file_in_flash(const char *filename, size_t *size_out);
void *odroid_overlay_cache_file_in_ram(const char *filename, size_t *size_out);


// ---------------------------------------------------------------------------
// Battery
// ---------------------------------------------------------------------------

typedef struct
{
    int millivolts;
    int percentage;
} odroid_battery_state_t;

void odroid_input_battery_level_init(void);
void odroid_input_battery_level_read(odroid_battery_state_t *out);
odroid_battery_state_t odroid_input_read_battery(void);

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

typedef void (*void_callback_t)(void);

typedef enum
{
    ODROID_BACKLIGHT_LEVEL0 = 0,
    ODROID_BACKLIGHT_LEVEL1,
    ODROID_BACKLIGHT_LEVEL2,
    ODROID_BACKLIGHT_LEVEL3,
    ODROID_BACKLIGHT_LEVEL4,
    ODROID_BACKLIGHT_LEVEL5,
    ODROID_BACKLIGHT_LEVEL6,
    ODROID_BACKLIGHT_LEVEL_COUNT,
} odroid_backlight_level_t;

typedef enum
{
    ODROID_DISPLAY_FILTER_OFF = 0,
    ODROID_DISPLAY_FILTER_LINEAR_X,
    ODROID_DISPLAY_FILTER_LINEAR_Y,
    ODROID_DISPLAY_FILTER_LINEAR_XY,
    ODROID_DISPLAY_FILTER_COUNT,
} odroid_display_filter_t;

typedef enum
{
    ODROID_MENU_FLAG_NONE = 0,
    ODROID_MENU_FLAG_INGAME = 1,
} odroid_menu_flags_t;

int odroid_display_get_backlight(void);
void odroid_display_set_backlight(int level);
odroid_display_filter_t odroid_display_get_filter_mode(void);
void odroid_display_write_rect(short left, short top, short width, short height, short stride,
                               const uint16_t *buffer);
void odroid_display_force_refresh(void);
void odroid_display_show_hourglass(void);
void odroid_display_lock(void);
void odroid_display_unlock(void);

int odroid_overlay_draw_text(uint16_t x_pos, uint16_t y_pos, uint16_t width, const char *text,
                             uint16_t color, uint16_t color_bg);
void odroid_overlay_draw_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
void odroid_overlay_draw_battery(int x_pos, int y_pos, int unused);
void odroid_overlay_draw_logo(int x_pos, int y_pos, int logo_id, uint16_t color);
void odroid_overlay_clock(int x_pos, int y_pos);
void odroid_overlay_alert(const char *text);
int odroid_overlay_dialog(const char *header, odroid_dialog_choice_t *options, int selected);
int odroid_overlay_settings_menu(odroid_dialog_choice_t *extra_options, void_callback_t repaint,
                                 odroid_menu_flags_t flags);

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------

int odroid_sdcard_read_file(const char *path, void *buf, size_t buf_size);
bool odroid_sdcard_mkdir(const char *dir);
bool odroid_sdcard_unlink(const char *path);

// ---------------------------------------------------------------------------
// Odds and ends
// ---------------------------------------------------------------------------

void odroid_audio_init(int sample_rate);
void odroid_audio_submit_zero(void);
void odroid_audio_volume_change(void);
void *odroid_system_get_app(void);
void odroid_system_led_set(int on);
int odroid_settings_cpu_oc_level_get(void);
int odroid_settings_turbo_buttons_get(void);
uint32_t odroid_button_turbos(void);
bool odroid_idle_timeout_expired(uint32_t idle_seconds);

// The Game & Watch drove an on-screen keyboard for MSX and Amstrad. Retro-Go has its own
// virtual keyboard with a different shape entirely, so these are declared for the cores that
// reference them and are not implemented; a core that needs one will say so at runtime
// rather than appearing to work with a keyboard that reads nothing.
typedef struct
{
    uint8_t keys[8];
} odroid_key_state_t;

#define ODROID_KEY_PRESSED 1
#define ODROID_KEY_RELEASED 0

bool odroid_keyboard_init(void);
void odroid_keyboard_event_callback_set(void *cb);
void odroid_keyboard_leds_set(int leds);
void odroid_keyboard_state_get(odroid_key_state_t *out);
int odroid_keyboard_state_key_get(const odroid_key_state_t *state, int key);

#ifdef __cplusplus
}
#endif
