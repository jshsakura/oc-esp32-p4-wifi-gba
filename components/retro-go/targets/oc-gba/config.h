#pragma once

#ifdef ESP_PLATFORM
#include <driver/gpio.h>
#include <driver/sdmmc_types.h>
#endif

// GBA (AGB-001) handheld: Waveshare ESP32-P4-WIFI6 module on a custom carrier board,
// driving a FunnyPlaying 3.0" IPS AGB kit over the stock Game Boy Advance parallel LCD bus.
//
// Every GPIO below is wired on the carrier board, not chosen here. They are transcribed from
// the KiCad board and each carries a `// net <name>` comment that
// tools/pinmap_from_kicad.py parses. After any board revision, run:
//
//     python3 tools/pinmap_from_kicad.py <board.kicad_pcb> --check
//
// and it will fail loudly rather than let the firmware drift away from the copper.

#define RG_TARGET_NAME             "OC-GBA"

// ESP32-P4 performance
#define RG_TARGET_DUAL_CORE        1
#define RG_TARGET_CPU_FREQ_MHZ     360
#define RG_TARGET_HAS_L2_CACHE     1
#define RG_TARGET_CACHE_LINE_SIZE  64

// Core affinity: emulation gets core 0 to itself, I/O lives on core 1.
#define RG_CORE_EMULATOR           0
#define RG_CORE_DISPLAY            1
#define RG_CORE_AUDIO              1
#define RG_CORE_INPUT              0
#define RG_CORE_SYSTEM             0

// Storage -- the microSD slot is on the Waveshare module itself, so these pins never
// reach the header and cost the carrier board nothing.
#define RG_STORAGE_ROOT             "/sd"
#define RG_STORAGE_SDMMC_HOST       SDMMC_HOST_SLOT_1
#define RG_STORAGE_SDMMC_SPEED      SDMMC_FREQ_HIGHSPEED
#define RG_GPIO_SDMMC_CLK           GPIO_NUM_43
#define RG_GPIO_SDMMC_CMD           GPIO_NUM_44
#define RG_GPIO_SDMMC_D0            GPIO_NUM_39
#define RG_GPIO_SDMMC_D1            GPIO_NUM_40
#define RG_GPIO_SDMMC_D2            GPIO_NUM_41
#define RG_GPIO_SDMMC_D3            GPIO_NUM_42
#define RG_SDMMC_LDO_CHAN           4

// I2C -- shared bus. The module's ES8311 codec already sits on it at 0x18; the carrier
// board adds the two button expanders and the ADC. No address collisions.
#define RG_GPIO_I2C_SDA             GPIO_NUM_7   // net I2C_SDA
#define RG_GPIO_I2C_SCL             GPIO_NUM_8   // net I2C_SCL

// ---------------------------------------------------------------------------
// Video -- D310N9362V0: 3.1" 480x800 IPS, ST7701S, MIPI DSI video mode, 2 data lanes.
//
// This replaced a stock GBA parallel RGB panel. That panel needed 23 GPIOs, an external
// AND gate to produce a gated dot clock, and SPL smuggled through a spare data bitplane.
// None of that survives the switch: DSI leaves the module on its own FPC connector (J20 on
// the Waveshare module, carrying DSI0_CLK_P/N and DSI0_D0/D1_P/N), so the carrier board
// routes no display signals at all and every one of those 23 pins is now free.
//
// Orientation: the panel is physically portrait and is mounted rotated in the shell.
// 240x160 scaled 3x is 720x480, which fits 800x480 exactly in height and leaves 40 pixels
// of pillarbox either side. Integer scaling means no resampling blur, and the resulting
// image measures 60.48 x 40.32 mm against the original GBA's 61.2 x 40.8 -- within one
// percent, so a stock-sized shell opening hides the pillarbox bars entirely.
// ---------------------------------------------------------------------------
#define RG_SCREEN_DRIVER            1   // 0=ST7789 SPI, 1=ST7701 MIPI DSI
#define RG_SCREEN_WIDTH             800
#define RG_SCREEN_HEIGHT            480
#define RG_SCREEN_ROTATE            90  // portrait panel, landscape shell

// Rows handed to the driver per call. The rotation turns each logical row into a panel
// column, so this sets how many pixels the driver can write back to back before moving to
// the next column -- which is to say, how much of every 64 byte cache line it actually
// uses. Sixteen rows is 32 bytes per run, and takes the frame's cache line traffic from
// roughly 6 MB down to 1.5 MB. It costs a 25 KB staging buffer.
#define RG_SCREEN_BUFFER_ROWS       16
#define RG_SCREEN_VISIBLE_AREA      {0, 0, 0, 0}
#define RG_SCREEN_SAFE_AREA         {0, 0, 0, 0}
#define RG_SCREEN_INIT()            /* ST7701S init lives in the driver */

// Panel native geometry, before rotation.
#define RG_DSI_PANEL_H_RES          480
#define RG_DSI_PANEL_V_RES          800
#define RG_DSI_LANE_NUM             2

// These porches and the DPI clock came with the base driver rather than from the
// D310N9362V0 datasheet. They give 544 x 835 total at 30 MHz, about 66 Hz. That is a
// reasonable starting point, not a guess to be afraid of: the sequence in the driver is
// close to the stock ST7701S 480x800 two-lane one, and its resolution command (0xC0 =
// 0x63) already sets 800 lines, so there is a fair chance the panel simply lights up.
//
// Try it before chasing the vendor. If it misbehaves, the symptom says where to look:
//   backlight on but black   -> BK1 power registers (0xB0..0xD0) or the GIP tables
//                               (0xE0..0xED) in st7701_init_sequence[]
//   image but wrong colour   -> gamma, 0xB0 / 0xB1 in BK0
//   rolling or torn          -> the porches and clock right here
//   nothing at all           -> RESET wiring, DSI lane order, backlight boost
// Of those, only the last two are ours; the middle two are the panel's own calibration and
// are worth asking the vendor for in parallel.
#define RG_DSI_DPI_CLOCK_MHZ        30
#define RG_DSI_LANE_BIT_RATE_MBPS   500
#define RG_DSI_HSYNC                2
#define RG_DSI_HBP                  30
#define RG_DSI_HFP                  32
#define RG_DSI_VSYNC                2
#define RG_DSI_VBP                  17
#define RG_DSI_VFP                  16

// TODO(board): the display no longer dictates the pinout, so these two are a firmware
// request to the board rather than a reading of it. Both sit at the header's edge, on pins
// the old parallel bus used, which keeps them easy to route.
//   - RESET drives the panel's pin 7.
//   - The backlight is a bare LED string (LEDA/LEDK, panel pins 1 and 3), so it needs a
//     constant-current boost driver on the carrier board -- this GPIO only gates/dims it.
//     Check first whether the module's J20 already supplies backlight power.
#define RG_SCREEN_BACKLIGHT         1
#define RG_GPIO_LCD_RST             GPIO_NUM_51
#define RG_GPIO_LCD_BCKL            GPIO_NUM_52

// ---------------------------------------------------------------------------
// Input -- twelve buttons across two TCA9554 expanders, so the wiring stays next to the
// buttons instead of crossing 100mm of board. Each switch shorts its pin to ground and
// the TCA9554's internal 100k pull-up provides the high level, so every key is active low.
//
// The paddles are split across the two expanders (X on the left chip's last free pin, Y on
// the right chip) because each one is wired to whichever expander it sits next to, which is
// the entire reason there are two.
//
// The front stays stock GBA. Two extra paddles sit on the back of the shell, and they are
// bound to X and Y rather than to MENU and OPTION on purpose: a menu opens fine from a
// chord because nothing about it is time critical, whereas a game button cannot be a chord
// at all. Spending the paddles on X/Y is what makes the SNES core playable on a two-face-
// button shell; MENU and OPTION drop to the chords below.
//
// Expander pins still free after this: R P4/P5/P6/P7 (the left chip is now full).
// ---------------------------------------------------------------------------
#define RG_I2C_TCA9554_L_ADDR       0x20    // A0=A1=A2=GND
#define RG_I2C_TCA9554_R_ADDR       0x21    // A0=VCC
#define RG_I2C_TCA9554_INPUT_REG    0x00    // input port register

#define RG_GAMEPAD_I2C_MAP {\
    {RG_KEY_UP,     .addr = RG_I2C_TCA9554_L_ADDR, .reg = RG_I2C_TCA9554_INPUT_REG, .num = 0, .level = 0},\
    {RG_KEY_DOWN,   .addr = RG_I2C_TCA9554_L_ADDR, .reg = RG_I2C_TCA9554_INPUT_REG, .num = 1, .level = 0},\
    {RG_KEY_LEFT,   .addr = RG_I2C_TCA9554_L_ADDR, .reg = RG_I2C_TCA9554_INPUT_REG, .num = 2, .level = 0},\
    {RG_KEY_RIGHT,  .addr = RG_I2C_TCA9554_L_ADDR, .reg = RG_I2C_TCA9554_INPUT_REG, .num = 3, .level = 0},\
    {RG_KEY_START,  .addr = RG_I2C_TCA9554_L_ADDR, .reg = RG_I2C_TCA9554_INPUT_REG, .num = 4, .level = 0},\
    {RG_KEY_SELECT, .addr = RG_I2C_TCA9554_L_ADDR, .reg = RG_I2C_TCA9554_INPUT_REG, .num = 5, .level = 0},\
    {RG_KEY_L,      .addr = RG_I2C_TCA9554_L_ADDR, .reg = RG_I2C_TCA9554_INPUT_REG, .num = 6, .level = 0},\
    {RG_KEY_A,      .addr = RG_I2C_TCA9554_R_ADDR, .reg = RG_I2C_TCA9554_INPUT_REG, .num = 0, .level = 0},\
    {RG_KEY_B,      .addr = RG_I2C_TCA9554_R_ADDR, .reg = RG_I2C_TCA9554_INPUT_REG, .num = 1, .level = 0},\
    {RG_KEY_R,      .addr = RG_I2C_TCA9554_R_ADDR, .reg = RG_I2C_TCA9554_INPUT_REG, .num = 2, .level = 0},\
    {RG_KEY_X,      .addr = RG_I2C_TCA9554_L_ADDR, .reg = RG_I2C_TCA9554_INPUT_REG, .num = 7, .level = 0},/* rear paddle, net BTN_X */\
    {RG_KEY_Y,      .addr = RG_I2C_TCA9554_R_ADDR, .reg = RG_I2C_TCA9554_INPUT_REG, .num = 3, .level = 0},/* rear paddle, net BTN_Y */\
}

// The GBA shell has exactly the ten buttons a GBA has, and games need all of them --
// L and R included. That leaves no physical key for Retro-Go's own MENU and OPTION, so
// they come from chords. A virtual entry fires only when the pressed set matches exactly,
// which is why these cannot be triggered by accident during play.
#define RG_GAMEPAD_VIRT_MAP {\
    {RG_KEY_MENU,   RG_KEY_START | RG_KEY_SELECT},\
    {RG_KEY_OPTION, RG_KEY_START | RG_KEY_L},\
}

// ---------------------------------------------------------------------------
// Battery -- the carrier board moved from a TP4056 to an IP5306 power module, which
// reports charge state over I2C at 0x75 rather than through an ADC channel. PCF8591's
// AIN0 (net VBAT_SENSE) is unconnected on the current board as a result.
// ---------------------------------------------------------------------------
#define RG_BATTERY_DRIVER           0   // TODO: IP5306 driver, see tasks
#define RG_I2C_IP5306_ADDR          0x75

// Volume wheel: the AGB potentiometer feeds an MCP3421, a single channel 18-bit converter
// that replaced the four channel PCF8591 once the IP5306 took over battery reporting and
// left only one analogue signal on the board.
//
// The MCP3421's address is set at the factory by the part's suffix, not by strapping pins:
// A0 is 0x68 and the variants run to 0x6F. Check the part actually fitted before trusting
// this, because nothing on the board makes it visible.
#define RG_I2C_MCP3421_ADDR         0x68


// ---------------------------------------------------------------------------
// Audio -- ES8311 codec plus NS4150B amplifier, both on the module and wired to GPIOs
// that never reach the header. The I2S pins below are correct, but the codec still needs
// its I2C register init sequence before it will make a sound, so it stays off until then.
// ---------------------------------------------------------------------------
#define RG_AUDIO_USE_INT_DAC        0
#define RG_AUDIO_USE_EXT_DAC        0   // TODO: enable once ES8311 init lands
#define RG_I2C_ES8311_ADDR          0x18
#define RG_GPIO_SND_I2S_MCK         GPIO_NUM_13
#define RG_GPIO_SND_I2S_BCK         GPIO_NUM_12
#define RG_GPIO_SND_I2S_WS          GPIO_NUM_10
#define RG_GPIO_SND_I2S_DATA        GPIO_NUM_9    // DSDIN, MCU -> codec
#define RG_GPIO_SND_I2S_DATA_IN     GPIO_NUM_11   // ASDOUT, codec -> MCU
#define RG_GPIO_SND_AMP_ENABLE      GPIO_NUM_53   // NS4150B PA_CTRL

// Status LED -- the GBA shell has a power LED but the board does not drive it from a GPIO.
#define RG_GPIO_LED                 GPIO_NUM_NC

// ---------------------------------------------------------------------------
// Failing gracefully
// ---------------------------------------------------------------------------

// Hold these together at power on to reach recovery mode, which starts with settings
// disabled so a bad setting cannot lock you out. It has to be a deliberate chord: the
// default is "any button", and in a shell a button can rest against the case and boot
// the thing into recovery every time.
#define RG_RECOVERY_BTN             (RG_KEY_START | RG_KEY_SELECT | RG_KEY_L | RG_KEY_R)

// The base defaults to Chinese, inherited from the fork this came from. English until the
// Korean translation and a Hangul font exist.
#define RG_LANG_DEFAULT             RG_LANG_EN
#define RG_FONT_DEFAULT             RG_FONT_DEJAVU_15
