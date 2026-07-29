// rg_tool.py turns the target directory name into RG_TARGET_<NAME> and passes it as a
// compiler define. Dispatch on it rather than hardcoding one target: this file used to
// include targets/esp32p4/config.h unconditionally, so every --target built the esp32p4
// pinout and produced a byte-identical binary while appearing to succeed.
//
// The #error is the point of the exercise. A target that is not listed here must fail to
// build, not quietly inherit somebody else's board.
#if defined(RG_TARGET_OC_GBA)
#include "targets/oc-gba/config.h"
#elif defined(RG_TARGET_ESP32P4)
#include "targets/esp32p4/config.h"
#elif defined(RG_TARGET_ESP32_P4BACKUP)
#include "targets/esp32-p4backup/config.h"
#else
#error "No target selected. Build with rg_tool.py --target <name>, and add the target here."
#endif

#ifndef RG_PROJECT_NAME
#define RG_PROJECT_NAME "Retro-Go"
#endif

#ifndef RG_PROJECT_WEBSITE
#define RG_PROJECT_WEBSITE "https://github.com/ducalex/retro-go"
#endif

#ifndef RG_PROJECT_CREDITS
#define RG_PROJECT_CREDITS \
    "Retro-Go: ducalex\n"
#endif

#ifndef RG_PROJECT_APP
#define RG_PROJECT_APP "retro-go"
#endif

#ifndef RG_PROJECT_VER
#define RG_PROJECT_VER "1.0"
#endif

#ifndef RG_BUILD_INFO
#define RG_BUILD_INFO "(none)"
#endif

#ifndef RG_BUILD_TIME
#define RG_BUILD_TIME 1580446800
#endif

#ifndef RG_BUILD_DATE
#define RG_BUILD_DATE __DATE__ " " __TIME__
#endif

#ifndef RG_APP_LAUNCHER
#define RG_APP_LAUNCHER "launcher"
#endif

#ifndef RG_APP_FACTORY
#define RG_APP_FACTORY NULL
#endif

#ifndef RG_UPDATER_ENABLE
#define RG_UPDATER_ENABLE 1
#endif

#ifndef RG_UPDATER_GITHUB_RELEASES
#define RG_UPDATER_GITHUB_RELEASES "https://api.github.com/repos/ducalex/retro-go/releases?per_page=10"
#endif

#ifndef RG_PATH_MAX
#define RG_PATH_MAX 255
#endif

#ifndef RG_RECOVERY_BTN
#define RG_RECOVERY_BTN RG_KEY_ANY
#endif

#ifndef RG_BATTERY_CALC_PERCENT
#define RG_BATTERY_CALC_PERCENT(raw) (100)
#endif

#ifndef RG_BATTERY_CALC_VOLTAGE
#define RG_BATTERY_CALC_VOLTAGE(raw) (0)
#endif

#ifndef RG_BATTERY_UPDATE_THRESHOLD
#define RG_BATTERY_UPDATE_THRESHOLD 1.0f
#endif
#ifndef RG_BATTERY_UPDATE_THRESHOLD_VOLT
#define RG_BATTERY_UPDATE_THRESHOLD_VOLT 0.010f
#endif

#ifndef RG_GAMEPAD_DEBOUNCE_PRESS
#define RG_GAMEPAD_DEBOUNCE_PRESS (2)
#endif

#ifndef RG_GAMEPAD_DEBOUNCE_RELEASE
#define RG_GAMEPAD_DEBOUNCE_RELEASE (2)
#endif

#ifndef RG_GAMEPAD_ADC_FILTER_WINDOW
#define RG_GAMEPAD_ADC_FILTER_WINDOW (150)
#endif

#ifndef RG_LOG_COLORS
#define RG_LOG_COLORS (1)
#endif

#ifndef RG_TICK_RATE
#ifdef CONFIG_FREERTOS_HZ
#define RG_TICK_RATE CONFIG_FREERTOS_HZ
#else
#define RG_TICK_RATE 1000
#endif
#endif

#ifndef RG_ZIP_SUPPORT
#define RG_ZIP_SUPPORT 1
#endif

#ifndef RG_SCREEN_PARTIAL_UPDATES
#define RG_SCREEN_PARTIAL_UPDATES 1
#endif

#ifndef RG_SCREEN_SAFE_AREA
#define RG_SCREEN_SAFE_AREA {0, 0, 0, 0}
#endif

#ifndef RG_SCREEN_VISIBLE_AREA
#define RG_SCREEN_VISIBLE_AREA {0, 0, 0, 0}
#endif

#ifndef RG_LANG_DEFAULT
#define RG_LANG_DEFAULT RG_LANG_ZH
#endif

#ifndef RG_FONT_DEFAULT
#define RG_FONT_DEFAULT RG_FONT_LXGW_LITE
#endif

#ifndef RG_EMU_ENABLE_GW
#define RG_EMU_ENABLE_GW 1
#endif

#ifndef RG_EMU_ENABLE_LYNX
#define RG_EMU_ENABLE_LYNX 1
#endif

#ifndef RG_EMU_ENABLE_MSX
#define RG_EMU_ENABLE_MSX 1
#endif
