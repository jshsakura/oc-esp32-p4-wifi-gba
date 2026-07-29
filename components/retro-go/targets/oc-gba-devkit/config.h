#pragma once

// Bench version of the oc-gba handheld, for a bare Waveshare ESP32-P4-WIFI6 dev board.
//
// The point of this target is that the carrier board is not on the critical path. The
// panel is MIPI DSI and plugs into the module's own FPC connector, and the microSD slot,
// the ES8311 codec and the Wi-Fi radio are all on the module too -- so a dev board plus the
// panel exercises the display, the launcher, storage, audio and the emulators, on exactly
// the silicon the finished handheld will use.
//
// Everything here is inherited from the real target except the buttons. Wire two TCA9554
// breakouts to GPIO7/8 and the input path is identical to production; with nothing wired,
// the GPIO map below lets you navigate with jumper wires to ground. Both maps are active at
// once and rg_input ORs them, and an expander that does not answer contributes nothing, so
// there is no need to choose.

#include "../oc-gba/config.h"

#undef RG_TARGET_NAME
#define RG_TARGET_NAME             "OC-GBA-DEVKIT"

// Ten buttons on free header pins, active low against the internal pull-up. These were the
// parallel LCD bus before the panel became DSI, so they are all brought out on the header
// and none of them clash with the module's SD, codec or USB pins.
#define RG_GAMEPAD_GPIO_MAP {\
    {RG_KEY_UP,     .num = GPIO_NUM_20, .pullup = 1, .level = 0},\
    {RG_KEY_DOWN,   .num = GPIO_NUM_21, .pullup = 1, .level = 0},\
    {RG_KEY_LEFT,   .num = GPIO_NUM_22, .pullup = 1, .level = 0},\
    {RG_KEY_RIGHT,  .num = GPIO_NUM_23, .pullup = 1, .level = 0},\
    {RG_KEY_A,      .num = GPIO_NUM_26, .pullup = 1, .level = 0},\
    {RG_KEY_B,      .num = GPIO_NUM_27, .pullup = 1, .level = 0},\
    {RG_KEY_START,  .num = GPIO_NUM_28, .pullup = 1, .level = 0},\
    {RG_KEY_SELECT, .num = GPIO_NUM_29, .pullup = 1, .level = 0},\
    {RG_KEY_L,      .num = GPIO_NUM_30, .pullup = 1, .level = 0},\
    {RG_KEY_R,      .num = GPIO_NUM_31, .pullup = 1, .level = 0},\
}
