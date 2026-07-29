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

#ifdef __cplusplus
}
#endif
