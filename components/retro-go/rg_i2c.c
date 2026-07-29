#include "rg_system.h"
#include "rg_i2c.h"

#include <stdlib.h>

#if defined(ESP_PLATFORM) && defined(RG_GPIO_I2C_SDA) && defined(RG_GPIO_I2C_SCL)
#include <driver/i2c_master.h>
#include <esp_err.h>
#define USE_I2C_DRIVER 1
#endif

static bool i2c_initialized = false;
#if USE_I2C_DRIVER
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
#endif

#define TRY(x)                 \
    if ((err = (x)) != ESP_OK) \
    {                          \
        goto fail;             \
    }


void *rg_i2c_get_bus_handle(void)
{
#if USE_I2C_DRIVER
    return i2c_initialized ? (void *)i2c_bus_handle : NULL;
#else
    return NULL;
#endif
}

bool rg_i2c_init(void)
{
#if USE_I2C_DRIVER
    i2c_master_bus_config_t i2c_bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = RG_GPIO_I2C_SDA,
        .scl_io_num = RG_GPIO_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = ESP_FAIL;

    if (i2c_initialized)
        return true;

    TRY(i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle));
    RG_LOGI("I2C driver ready (SDA:%d SCL:%d).\n", i2c_bus_config.sda_io_num, i2c_bus_config.scl_io_num);
    i2c_initialized = true;
    return true;
fail:
    RG_LOGE("Failed to initialize I2C driver. err=0x%x\n", err);
#else
    RG_LOGE("I2C is not available on this device.\n");
#endif
    i2c_initialized = false;
    return false;
}

bool rg_i2c_deinit(void)
{
#if USE_I2C_DRIVER
    if (i2c_initialized && i2c_bus_handle) {
        i2c_del_master_bus(i2c_bus_handle);
        i2c_bus_handle = NULL;
        RG_LOGI("I2C driver terminated.\n");
    }
#endif
    i2c_initialized = false;
    return true;
}

bool rg_i2c_read(uint8_t addr, int reg, void *read_data, size_t read_len)
{
#if USE_I2C_DRIVER
    if (!i2c_initialized || !i2c_bus_handle)
        return false;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    i2c_master_dev_handle_t dev_handle;
    esp_err_t err = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &dev_handle);
    if (err != ESP_OK) {
        RG_LOGE("Read from 0x%02X failed. reg=0x%02X, err=0x%03X, init=%d\n", addr, reg, err, i2c_initialized);
        return false;
    }

    uint8_t reg_byte = (uint8_t)reg;
    if (reg >= 0) {
        err = i2c_master_transmit_receive(dev_handle, &reg_byte, 1, read_data, read_len, pdMS_TO_TICKS(500));
    } else {
        err = i2c_master_receive(dev_handle, read_data, read_len, pdMS_TO_TICKS(500));
    }

    i2c_master_bus_rm_device(dev_handle);

    if (err != ESP_OK) {
        RG_LOGE("Read from 0x%02X failed. reg=0x%02X, err=0x%03X, init=%d\n", addr, reg, err, i2c_initialized);
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool rg_i2c_write(uint8_t addr, int reg, const void *write_data, size_t write_len)
{
#if USE_I2C_DRIVER
    if (!i2c_initialized || !i2c_bus_handle)
        return false;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    i2c_master_dev_handle_t dev_handle;
    esp_err_t err = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &dev_handle);
    if (err != ESP_OK) {
        RG_LOGE("Write to 0x%02X failed. reg=0x%02X, err=0x%03X, init=%d\n", addr, reg, err, i2c_initialized);
        return false;
    }

    uint8_t *buffer = malloc(write_len + 1);
    if (!buffer) {
        i2c_master_bus_rm_device(dev_handle);
        return false;
    }

    size_t offset = 0;
    if (reg >= 0) {
        buffer[0] = (uint8_t)reg;
        offset = 1;
    }
    memcpy(buffer + offset, write_data, write_len);

    err = i2c_master_transmit(dev_handle, buffer, write_len + offset, pdMS_TO_TICKS(500));
    free(buffer);
    i2c_master_bus_rm_device(dev_handle);

    if (err != ESP_OK) {
        RG_LOGE("Write to 0x%02X failed. reg=0x%02X, err=0x%03X, init=%d\n", addr, reg, err, i2c_initialized);
        return false;
    }
    return true;
#else
    return false;
#endif
}

int rg_i2c_read_byte(uint8_t addr, uint8_t reg)
{
    uint8_t value;
    return rg_i2c_read(addr, reg, &value, 1) ? value : -1;
}

bool rg_i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t value)
{
    return rg_i2c_write(addr, reg, &value, 1);
}


#ifdef RG_I2C_GPIO_DRIVER

typedef struct {int input_reg, output_reg, direction_reg, pullup_reg;} _gpio_port;
typedef struct {uint8_t reg, value;} _gpio_sequence;

#if RG_I2C_GPIO_DRIVER == 1 // AW9523

static const _gpio_port gpio_ports[] = {
    {0x00, 0x02, 0x04, -1},
    {0x01, 0x03, 0x05, -1},
};
static const _gpio_sequence gpio_init_seq[] = {
    {0x7F, 0x00  },
    {0x11, 1 << 4},
};
static const _gpio_sequence gpio_deinit_seq[] = {};

#elif RG_I2C_GPIO_DRIVER == 2 // PCF9539

static const _gpio_port gpio_ports[] = {
    {0x00, 0x02, 0x06, -1},
    {0x01, 0x03, 0x07, -1},
};
static const _gpio_sequence gpio_init_seq[] = {};
static const _gpio_sequence gpio_deinit_seq[] = {};

#elif RG_I2C_GPIO_DRIVER == 3 // MCP23017

static const _gpio_port gpio_ports[] = {
    {0x12, 0x14, 0x00, 0x0C},
    {0x13, 0x15, 0x01, 0x0D},
};
static const _gpio_sequence gpio_init_seq[] = {};
static const _gpio_sequence gpio_deinit_seq[] = {};

#elif RG_I2C_GPIO_DRIVER == 4 // PCF8575

static const _gpio_port gpio_ports[] = {
    {-1, -1, -1, -1},
    {-1, -1, -1, -1},
};
static const _gpio_sequence gpio_init_seq[] = {};
static const _gpio_sequence gpio_deinit_seq[] = {};
static rg_gpio_mode_t PCF857x_mode = -1;

#elif RG_I2C_GPIO_DRIVER == 5 // PCF8574

static const _gpio_port gpio_ports[] = {
    {-1, -1, -1, -1},
};
static const _gpio_sequence gpio_init_seq[] = {};
static const _gpio_sequence gpio_deinit_seq[] = {};
static rg_gpio_mode_t PCF857x_mode = -1;

#else

#error "Unknown I2C GPIO Extender driver type!"

#endif

static const size_t gpio_ports_count = RG_COUNT(gpio_ports);
static uint8_t gpio_output_values[RG_COUNT(gpio_ports)];
static uint8_t gpio_address = RG_I2C_GPIO_ADDR;
static bool gpio_initialized = false;


bool rg_i2c_gpio_init(void)
{
    if (gpio_initialized)
        return true;

    if (!i2c_initialized && !rg_i2c_init())
        return false;

    for (size_t i = 0; (int)i < (int)RG_COUNT(gpio_init_seq); ++i)
    {
        if (!rg_i2c_write_byte(gpio_address, gpio_init_seq[i].reg, gpio_init_seq[i].value))
            goto fail;
    }

    for (size_t i = 0; i < gpio_ports_count; ++i)
    {
        if (!rg_i2c_gpio_configure_port(i, 0xFF, RG_GPIO_INPUT))
            goto fail;
        if (!rg_i2c_gpio_write_port(i, 0x00))
            goto fail;
    }

    RG_LOGI("GPIO Extender ready (driver:%d, addr:0x%02X).", RG_I2C_GPIO_DRIVER, gpio_address);
    gpio_initialized = true;
    return true;
fail:
    RG_LOGE("Failed to initialize extender (driver:%d, addr:0x%02X).", RG_I2C_GPIO_DRIVER, gpio_address);
    gpio_initialized = false;
    return false;
}

bool rg_i2c_gpio_deinit(void)
{
    if (gpio_initialized)
    {
        for (size_t i = 0; (int)i < (int)RG_COUNT(gpio_deinit_seq); ++i)
            rg_i2c_write_byte(gpio_address, gpio_deinit_seq[i].reg, gpio_deinit_seq[i].value);
        gpio_initialized = false;
    }
    return true;
}

static bool update_register(int reg, uint8_t clear_mask, uint8_t set_mask)
{
    uint8_t value;
    return rg_i2c_read(gpio_address, reg, &value, 1) &&
           rg_i2c_write_byte(gpio_address, reg, (value & ~clear_mask) | set_mask);
}

bool rg_i2c_gpio_configure_port(int port, uint8_t mask, rg_gpio_mode_t mode)
{
    if (port < 0 || port >= gpio_ports_count)
        return false;
#if RG_I2C_GPIO_DRIVER == 4 || RG_I2C_GPIO_DRIVER == 5
    uint32_t temp = 0xFFFFFFFF;
    if (mask != 0xFF && mode != PCF857x_mode && (mode == RG_GPIO_OUTPUT || PCF857x_mode == RG_GPIO_OUTPUT))
        RG_LOGW("PCF857x mode cannot be set by pin. (mask is 0x%02X, expected 0xFF)", mask);
    PCF857x_mode = mode;
    if (mode != RG_GPIO_OUTPUT)
        return rg_i2c_write(gpio_address, -1, &temp, gpio_ports_count)
            && rg_i2c_read(gpio_address, -1, &temp, gpio_ports_count);
    return rg_i2c_write(gpio_address, -1, &gpio_output_values, gpio_ports_count);
#else
    int direction_reg = gpio_ports[port].direction_reg;
    int pullup_reg = gpio_ports[port].pullup_reg;
    if (pullup_reg != -1 && !update_register(pullup_reg, mask, mode == RG_GPIO_INPUT_PULLUP ? mask : 0))
        return false;
    return update_register(direction_reg, mask, mode != RG_GPIO_OUTPUT ? mask : 0);
#endif
}

int rg_i2c_gpio_read_port(int port)
{
    if (port < 0 || port >= gpio_ports_count)
        return -1;
#if RG_I2C_GPIO_DRIVER == 4 || RG_I2C_GPIO_DRIVER == 5
    uint8_t values[gpio_ports_count];
    return rg_i2c_read(gpio_address, -1, &values, gpio_ports_count) ? values[port] : -1;
#else
    return rg_i2c_read_byte(gpio_address, gpio_ports[port].input_reg);
#endif
}

bool rg_i2c_gpio_write_port(int port, uint8_t value)
{
    if (port < 0 || port >= gpio_ports_count)
        return false;
    gpio_output_values[port] = value;
#if RG_I2C_GPIO_DRIVER == 4 || RG_I2C_GPIO_DRIVER == 5
    if (PCF857x_mode != RG_GPIO_OUTPUT)
        return true;
    return rg_i2c_write(gpio_address, -1, &gpio_output_values, gpio_ports_count);
#else
    return rg_i2c_write_byte(gpio_address, gpio_ports[port].output_reg, value);
#endif
}

bool rg_i2c_gpio_set_direction(int pin, rg_gpio_mode_t mode)
{
    return rg_i2c_gpio_configure_port(pin >> 3, 1 << (pin & 7), mode);
}

int rg_i2c_gpio_get_level(int pin)
{
    return (rg_i2c_gpio_read_port(pin >> 3) >> (pin & 7)) & 1;
}

bool rg_i2c_gpio_set_level(int pin, int level)
{
    uint8_t port = (pin >> 3) % gpio_ports_count, mask = 1 << (pin & 7);
    uint8_t value = (gpio_output_values[port] & ~mask) | (level ? mask : 0);
    return rg_i2c_gpio_write_port(port, value);
}
#endif
