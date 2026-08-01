#include "rg_system.h"
#include "rg_input.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>

#if RG_BATTERY_DRIVER == 1
static adc_oneshot_unit_handle_t adc_unit = NULL;
static adc_cali_handle_t adc_cali = NULL;
#endif

#ifdef RG_GAMEPAD_ADC_MAP
static rg_keymap_adc_t keymap_adc[] = RG_GAMEPAD_ADC_MAP;
static adc_oneshot_unit_handle_t gamepad_adc_unit = NULL;
#endif
#ifdef RG_GAMEPAD_GPIO_MAP
static rg_keymap_gpio_t keymap_gpio[] = RG_GAMEPAD_GPIO_MAP;
#endif
#ifdef RG_GAMEPAD_I2C_MAP
static rg_keymap_i2c_t keymap_i2c[] = RG_GAMEPAD_I2C_MAP;
#endif
#ifdef RG_GAMEPAD_KBD_MAP
static rg_keymap_kbd_t keymap_kbd[] = RG_GAMEPAD_KBD_MAP;
#endif
#ifdef RG_GAMEPAD_SERIAL_MAP
static rg_keymap_serial_t keymap_serial[] = RG_GAMEPAD_SERIAL_MAP;
#endif
#ifdef RG_GAMEPAD_VIRT_MAP
static rg_keymap_virt_t keymap_virt[] = RG_GAMEPAD_VIRT_MAP;
#endif
static volatile bool input_task_running = false;
static volatile uint32_t gamepad_state = -1;
static uint32_t gamepad_mapped = 0;
static rg_battery_t battery_state = {0};
// Per-source-key remap: key_remap[i] is the bitmask reported when physical key
// (1<<i) is held. Identity by default. Applied in rg_input_read_gamepad, after
// the debounce task and the VIRT chord detection, so remapping a physical key
// never stops the MENU/OPTION chords from firing.
static uint32_t key_remap[RG_KEY_COUNT];
static void load_key_remap(void);
// Idle backlight dimming state.
static int64_t idle_last_input = 0;
static bool idle_dimmed = false;
static int idle_saved_backlight = 0;

#define UPDATE_GLOBAL_MAP(keymap)                 \
    for (size_t i = 0; i < RG_COUNT(keymap); ++i) \
        gamepad_mapped |= keymap[i].key;          \

static int adc_read_channel(adc_oneshot_unit_handle_t handle, adc_channel_t channel)
{
    int value = -1;
    if (handle != NULL)
    {
        esp_err_t err = adc_oneshot_read(handle, channel, &value);
        if (err != ESP_OK)
            return -1;
    }
    return value;
}

bool rg_input_read_battery_raw(rg_battery_t *out)
{
#if RG_BATTERY_DRIVER == 0
    (void)out;
    return false;
#elif RG_BATTERY_DRIVER == 1
    uint32_t raw_value = 0;
    bool present = true;
    bool charging = false;

    for (int i = 0; i < 4; ++i)
    {
        int value = -1;
        if (adc_unit != NULL)
        {
            esp_err_t err = adc_oneshot_read(adc_unit, RG_BATTERY_ADC_CHANNEL, &value);
            if (err != ESP_OK)
                return false;
        }
        else
        {
            return false;
        }
        
        if (adc_cali != NULL)
        {
            int voltage = 0;
            esp_err_t err = adc_cali_raw_to_voltage(adc_cali, value, &voltage);
            if (err == ESP_OK)
                raw_value += voltage;
            else
                raw_value += value * 2;
        }
        else
        {
            raw_value += value * 2;
        }
    }
    raw_value /= 4;
#elif RG_BATTERY_DRIVER == 2
    uint32_t raw_value = 0;
    bool present = true;
    bool charging = false;
    uint8_t data[5];
    if (!rg_i2c_read(0x20, -1, &data, 5))
        return false;
    raw_value = data[4];
    charging = data[4] == 255;
#endif

#if RG_BATTERY_DRIVER > 0
    if (!out)
        return true;

    *out = (rg_battery_t){
        .level = RG_MAX(0.f, RG_MIN(100.f, RG_BATTERY_CALC_PERCENT(raw_value))),
        .volts = RG_BATTERY_CALC_VOLTAGE(raw_value),
        .present = present,
        .charging = charging,
    };
    return true;
#endif
}

bool rg_input_read_gamepad_raw(uint32_t *out)
{
    uint32_t state = 0;

#if defined(RG_GAMEPAD_ADC_MAP)
    static int old_adc_values[RG_COUNT(keymap_adc)];
    for (size_t i = 0; i < RG_COUNT(keymap_adc); ++i)
    {
        const rg_keymap_adc_t *mapping = &keymap_adc[i];
        int value = adc_read_channel(gamepad_adc_unit, mapping->channel);
        if (value >= mapping->min && value <= mapping->max)
        {
            if (abs(old_adc_values[i] - value) < RG_GAMEPAD_ADC_FILTER_WINDOW)
                state |= mapping->key;
            old_adc_values[i] = value;
        }
    }
#endif

#if defined(RG_GAMEPAD_GPIO_MAP)
    for (size_t i = 0; i < RG_COUNT(keymap_gpio); ++i)
    {
        const rg_keymap_gpio_t *mapping = &keymap_gpio[i];
        if (mapping->num == GPIO_NUM_NC)
            continue;
        if (gpio_get_level(mapping->num) == mapping->level)
            state |= mapping->key;
    }
#endif

#if defined(RG_GAMEPAD_I2C_MAP)
// Only boards using the legacy single-device path define this. A board whose map gives
// every entry an explicit address never reaches the fallback, so 0 is fine as a stand-in.
#ifndef RG_I2C_GPIO_ADDR
#define RG_I2C_GPIO_ADDR 0
#endif
    // The map may span several chips (an expander beside each cluster of buttons keeps
    // their wiring short). Read each distinct device once per poll rather than once per
    // key: at ~13 keys a naive loop would put a dozen transactions on the bus every frame,
    // on a bus the audio codec also lives on.
    // A chip that is not there NACKs every time. Polling one at the input rate floods the
    // log and wastes the bus on a device shared with the audio codec, so a device that has
    // failed repeatedly is dropped and only retried occasionally -- often enough that a
    // loose connector recovers on its own, rarely enough to be quiet about it.
    #define I2C_GAMEPAD_MAX_FAILS 5
    #define I2C_GAMEPAD_RETRY_US (2 * 1000000)

    static struct { int addr, reg; uint32_t bits; bool ok; int fails; int64_t retry_at; }
        devs[RG_COUNT(keymap_i2c)];
    static size_t ndevs = 0;

    if (ndevs == 0)
    {
        for (size_t i = 0; i < RG_COUNT(keymap_i2c); ++i)
        {
            int addr = keymap_i2c[i].addr ? keymap_i2c[i].addr : RG_I2C_GPIO_ADDR;
            int reg = keymap_i2c[i].addr ? keymap_i2c[i].reg : -1;
            size_t d = 0;
            while (d < ndevs && !(devs[d].addr == addr && devs[d].reg == reg))
                ++d;
            if (d == ndevs)
                devs[ndevs++] = (typeof(devs[0])){.addr = addr, .reg = reg};
        }
    }

    int64_t now = rg_system_timer();
    for (size_t d = 0; d < ndevs; ++d)
    {
        if (devs[d].fails >= I2C_GAMEPAD_MAX_FAILS && now < devs[d].retry_at)
        {
            devs[d].ok = false;
            continue;
        }

        uint8_t data[5] = {0};
        // A device selected by register (an 8-bit expander) answers with one byte. The
        // legacy path reads five and takes bytes 1 and 2 as a 16-bit word.
        size_t len = (devs[d].reg >= 0) ? 1 : 5;
        devs[d].ok = rg_i2c_read(devs[d].addr, devs[d].reg, &data, len);
        devs[d].bits = (len == 1) ? data[0] : (uint32_t)((data[2] << 8) | data[1]);

        if (devs[d].ok)
            devs[d].fails = 0;
        else if (++devs[d].fails == I2C_GAMEPAD_MAX_FAILS)
            RG_LOGW("I2C gamepad at 0x%02X not responding, backing off", devs[d].addr);

        if (devs[d].fails >= I2C_GAMEPAD_MAX_FAILS)
            devs[d].retry_at = now + I2C_GAMEPAD_RETRY_US;
    }

    for (size_t i = 0; i < RG_COUNT(keymap_i2c); ++i)
    {
        const rg_keymap_i2c_t *mapping = &keymap_i2c[i];
        int addr = mapping->addr ? mapping->addr : RG_I2C_GPIO_ADDR;
        int reg = mapping->addr ? mapping->reg : -1;
        for (size_t d = 0; d < ndevs; ++d)
        {
            if (devs[d].addr != addr || devs[d].reg != reg)
                continue;
            // A chip that did not answer reports nothing rather than every key at once:
            // with active low buttons an unread 0 would look like the whole pad held down.
            if (devs[d].ok && ((devs[d].bits >> mapping->num) & 1) == (uint32_t)mapping->level)
                state |= mapping->key;
            break;
        }
    }
#endif

#if defined(RG_GAMEPAD_KBD_MAP)
#warning "KBD gamepad not implemented for ESP32-P4"
#endif

#if defined(RG_GAMEPAD_SERIAL_MAP)
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 0);
    rg_usleep(5);
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 1);
    rg_usleep(1);
    uint32_t buttons = 0;
    for (int i = 0; i < 16; i++)
    {
        buttons |= gpio_get_level(RG_GPIO_GAMEPAD_DATA) << (15 - i);
        gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 0);
        rg_usleep(1);
        gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 1);
        rg_usleep(1);
    }
    for (size_t i = 0; i < RG_COUNT(keymap_serial); ++i)
    {
        const rg_keymap_serial_t *mapping = &keymap_serial[i];
        if (((buttons >> mapping->num) & 1) == mapping->level)
            state |= mapping->key;
    }
#endif

#if defined(RG_GAMEPAD_VIRT_MAP)
    for (size_t i = 0; i < RG_COUNT(keymap_virt); ++i)
    {
        if (state == keymap_virt[i].src)
            state = keymap_virt[i].key;
    }
#endif

    if (out)
        *out = state;
    return true;
}

static void input_task(void *arg)
{
    uint8_t debounce[RG_KEY_COUNT];
    uint32_t local_gamepad_state = 0;
    uint32_t state;
    int64_t next_battery_update = 0;

    memset(debounce, 0xFF, sizeof(debounce));
    input_task_running = true;

    while (input_task_running)
    {
        if (rg_input_read_gamepad_raw(&state))
        {
            for (int i = 0; i < RG_KEY_COUNT; ++i)
            {
                uint32_t val = ((debounce[i] << 1) | ((state >> i) & 1));
                debounce[i] = val & 0xFF;

                if ((val & ((1 << RG_GAMEPAD_DEBOUNCE_PRESS) - 1)) == ((1 << RG_GAMEPAD_DEBOUNCE_PRESS) - 1))
                {
                    local_gamepad_state |= (1 << i);
                }
                else if ((val & ((1 << RG_GAMEPAD_DEBOUNCE_RELEASE) - 1)) == 0)
                {
                    local_gamepad_state &= ~(1 << i);
                }
            }
            gamepad_state = local_gamepad_state;
            __sync_synchronize();

            // Idle backlight dimming: after ~30s of no input, drop the backlight
            // to a fraction of the user's level to save power; any key restores it.
            // The dim is transient (rg_display_dim_backlight does not save), so the
            // user's brightness setting is untouched.
            int64_t now = rg_system_timer();
            if (local_gamepad_state != 0)
            {
                idle_last_input = now;
                if (idle_dimmed)
                {
                    rg_display_dim_backlight(idle_saved_backlight);
                    idle_dimmed = false;
                }
            }
            else if (!idle_dimmed && idle_last_input && (now - idle_last_input) > 30 * 1000000)
            {
                idle_saved_backlight = rg_display_get_backlight();
                rg_display_dim_backlight(RG_MAX(idle_saved_backlight / 5, 5));
                idle_dimmed = true;
            }
        }

        if (rg_system_timer() >= next_battery_update)
        {
            rg_battery_t temp = {0};
            if (rg_input_read_battery_raw(&temp))
            {
                if (fabsf(battery_state.level - temp.level) < RG_BATTERY_UPDATE_THRESHOLD)
                    temp.level = battery_state.level;
                if (fabsf(battery_state.volts - temp.volts) < RG_BATTERY_UPDATE_THRESHOLD_VOLT)
                    temp.volts = battery_state.volts;
            }
            battery_state = temp;
            next_battery_update = rg_system_timer() + 2 * 1000000;
        }

        rg_task_delay(10);
    }

    input_task_running = false;
    gamepad_state = -1;
}

void rg_input_init(void)
{
    RG_ASSERT(!input_task_running, "Input already initialized!");

#if defined(RG_GAMEPAD_ADC_MAP)
    RG_LOGI("Initializing ADC gamepad driver...");
    
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_config, &gamepad_adc_unit);
    if (err != ESP_OK)
    {
        RG_LOGE("Failed to initialize ADC unit for gamepad: %s", esp_err_to_name(err));
    }
    else
    {
        for (size_t i = 0; i < RG_COUNT(keymap_adc); ++i)
        {
            const rg_keymap_adc_t *mapping = &keymap_adc[i];
            adc_oneshot_chan_cfg_t chan_config = {
                .atten = mapping->atten,
                .bitwidth = ADC_BITWIDTH_DEFAULT,
            };
            err = adc_oneshot_config_channel(gamepad_adc_unit, mapping->channel, &chan_config);
            if (err != ESP_OK)
            {
                RG_LOGE("Failed to configure ADC channel %d: %s", mapping->channel, esp_err_to_name(err));
            }
        }
    }
    UPDATE_GLOBAL_MAP(keymap_adc);
#endif

#if defined(RG_GAMEPAD_GPIO_MAP)
    RG_LOGI("Initializing GPIO gamepad driver...");
    for (size_t i = 0; i < RG_COUNT(keymap_gpio); ++i)
    {
        const rg_keymap_gpio_t *mapping = &keymap_gpio[i];
        if (mapping->num == GPIO_NUM_NC)
            continue;
        gpio_set_direction(mapping->num, GPIO_MODE_INPUT);
        if (mapping->pullup && mapping->pulldown)
            gpio_set_pull_mode(mapping->num, GPIO_PULLUP_PULLDOWN);
        else if (mapping->pullup || mapping->pulldown)
            gpio_set_pull_mode(mapping->num, mapping->pullup ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);
        else
            gpio_set_pull_mode(mapping->num, GPIO_FLOATING);
    }
    UPDATE_GLOBAL_MAP(keymap_gpio);
#endif

#if defined(RG_GAMEPAD_I2C_MAP)
    RG_LOGI("Initializing I2C gamepad driver...");
    rg_i2c_init();
    UPDATE_GLOBAL_MAP(keymap_i2c);
#endif

#if defined(RG_GAMEPAD_KBD_MAP)
    RG_LOGI("Initializing KBD gamepad driver...");
    UPDATE_GLOBAL_MAP(keymap_kbd);
#endif

#if defined(RG_GAMEPAD_SERIAL_MAP)
    RG_LOGI("Initializing SERIAL gamepad driver...");
    gpio_set_direction(RG_GPIO_GAMEPAD_CLOCK, GPIO_MODE_OUTPUT);
    gpio_set_direction(RG_GPIO_GAMEPAD_LATCH, GPIO_MODE_OUTPUT);
    gpio_set_direction(RG_GPIO_GAMEPAD_DATA, GPIO_MODE_INPUT);
    gpio_set_level(RG_GPIO_GAMEPAD_LATCH, 0);
    gpio_set_level(RG_GPIO_GAMEPAD_CLOCK, 1);
    UPDATE_GLOBAL_MAP(keymap_serial);
#endif


#if RG_BATTERY_DRIVER == 1
    RG_LOGI("Initializing ADC battery driver...");
    
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = RG_BATTERY_ADC_UNIT,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_config, &adc_unit);
    if (err != ESP_OK)
    {
        RG_LOGE("Failed to initialize ADC unit: %s", esp_err_to_name(err));
    }
    else
    {
        adc_oneshot_chan_cfg_t chan_config = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        err = adc_oneshot_config_channel(adc_unit, RG_BATTERY_ADC_CHANNEL, &chan_config);
        if (err != ESP_OK)
        {
            RG_LOGE("Failed to configure ADC channel: %s", esp_err_to_name(err));
        }
        else
        {
            adc_cali_curve_fitting_config_t cali_config = {
                .unit_id = RG_BATTERY_ADC_UNIT,
                .atten = ADC_ATTEN_DB_12,
                .bitwidth = ADC_BITWIDTH_DEFAULT,
            };
            err = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali);
            if (err != ESP_OK)
            {
                RG_LOGE("Failed to initialize ADC calibration: %s", esp_err_to_name(err));
                RG_LOGW("Using fallback voltage conversion");
            }
        }
    }
#endif

    load_key_remap();

    rg_input_read_gamepad_raw(NULL);

    rg_task_create("rg_input", &input_task, NULL, 3 * 1024, RG_TASK_PRIORITY_5, RG_PERF_CORE_0);
    while (gamepad_state == -1)
        rg_task_yield();
    RG_LOGI("Input ready. state=" PRINTF_BINARY_16 "\n", PRINTF_BINVAL_16(gamepad_state));
}

void rg_input_deinit(void)
{
    input_task_running = false;
    RG_LOGI("Input terminated.\n");
}

bool rg_input_key_is_present(rg_key_t mask)
{
    return (gamepad_mapped & mask) == mask;
}

uint32_t rg_input_read_gamepad(void)
{
    __sync_synchronize();
    uint32_t state = gamepad_state;
    // Apply the runtime remap at the logical layer. Each held source bit is
    // replaced by its mapped target, so swaps, redirects, and even disabling a
    // key (target 0) all work. MENU/OPTION keep identity unless explicitly set.
    uint32_t remapped = 0;
    for (int i = 0; i < RG_KEY_COUNT; i++)
    {
        if (state & (1u << i))
            remapped |= key_remap[i];
    }
    return remapped;
}

bool rg_input_key_is_pressed(rg_key_t mask)
{
    return (bool)(rg_input_read_gamepad() & mask);
}

bool rg_input_wait_for_key(rg_key_t mask, bool pressed, int timeout_ms)
{
    int64_t expiration = timeout_ms < 0 ? INT64_MAX : (rg_system_timer() + timeout_ms * 1000);
    while (rg_input_key_is_pressed(mask) != pressed)
    {
        if (rg_system_timer() > expiration)
            return false;
        rg_task_delay(10);
    }
    return true;
}

rg_battery_t rg_input_read_battery(void)
{
    return battery_state;
}

const char *rg_input_get_key_name(rg_key_t key)
{
    switch (key)
    {
    case RG_KEY_UP: return "Up";
    case RG_KEY_RIGHT: return "Right";
    case RG_KEY_DOWN: return "Down";
    case RG_KEY_LEFT: return "Left";
    case RG_KEY_SELECT: return "Select";
    case RG_KEY_START: return "Start";
    case RG_KEY_MENU: return "Menu";
    case RG_KEY_OPTION: return "Option";
    case RG_KEY_A: return "A";
    case RG_KEY_B: return "B";
    case RG_KEY_X: return "X";
    case RG_KEY_Y: return "Y";
    case RG_KEY_L: return "Left Shoulder";
    case RG_KEY_R: return "Right Shoulder";
    case RG_KEY_NONE: return "None";
    default: return "Unknown";
    }
}

// --- Runtime key remapping ---------------------------------------------------

static int key_to_index(rg_key_t key)
{
    for (int i = 0; i < RG_KEY_COUNT; i++)
        if (key == (rg_key_t)(1u << i))
            return i;
    return -1;
}

static void load_key_remap(void)
{
    for (int i = 0; i < RG_KEY_COUNT; i++)
    {
        rg_key_t key = (rg_key_t)(1u << i);
        char setting[24];
        snprintf(setting, sizeof(setting), "Keymap.%s", rg_input_get_key_name(key));
        // Default is identity: target index == source index.
        int target = rg_settings_get_number(NS_GLOBAL, setting, i);
        key_remap[i] = (target >= 0 && target < RG_KEY_COUNT) ? (1u << target) : (1u << i);
    }
}

void rg_input_set_key_remap(rg_key_t from, rg_key_t to)
{
    int from_idx = key_to_index(from);
    int to_idx = key_to_index(to);
    if (from_idx < 0 || to_idx < 0)
        return;
    key_remap[from_idx] = (1u << to_idx);
    char setting[24];
    snprintf(setting, sizeof(setting), "Keymap.%s", rg_input_get_key_name(from));
    rg_settings_set_number(NS_GLOBAL, setting, to_idx);
}

rg_key_t rg_input_get_key_remap(rg_key_t from)
{
    int idx = key_to_index(from);
    if (idx < 0)
        return from;
    return (rg_key_t)key_remap[idx];
}

void rg_input_reset_key_remap(void)
{
    for (int i = 0; i < RG_KEY_COUNT; i++)
    {
        rg_key_t key = (rg_key_t)(1u << i);
        key_remap[i] = (1u << i);
        char setting[24];
        snprintf(setting, sizeof(setting), "Keymap.%s", rg_input_get_key_name(key));
        rg_settings_set_number(NS_GLOBAL, setting, i);
    }
}

rg_key_t rg_input_capture_key(int timeout_ms)
{
    int64_t expiration = timeout_ms < 0 ? INT64_MAX : (rg_system_timer() + (int64_t)timeout_ms * 1000);
    // Read the pre-remap debounced state so we learn which physical key moved,
    // not what it currently maps to.
    __sync_synchronize();
    while (gamepad_state != 0)
    {
        if (rg_system_timer() > expiration)
            return RG_KEY_NONE;
        rg_task_delay(10);
        __sync_synchronize();
    }
    while (gamepad_state == 0)
    {
        if (rg_system_timer() > expiration)
            return RG_KEY_NONE;
        rg_task_delay(10);
        __sync_synchronize();
    }
    // Return the first held key. A chord would show up as MENU/OPTION here, which
    // the caller is expected to reject.
    uint32_t s = gamepad_state;
    for (int i = 0; i < RG_KEY_COUNT; i++)
        if (s & (1u << i))
            return (rg_key_t)(1u << i);
    return RG_KEY_NONE;
}
