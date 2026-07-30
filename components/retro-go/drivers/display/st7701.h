#ifndef __ST7701_H__
#define __ST7701_H__

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_ldo_regulator.h>
#include "rg_system.h"

#ifdef __cplusplus
extern "C" {
#endif

// Panel geometry and timing. A target that knows its own panel overrides these from its
// config.h; the values here are the generic 480x800 ST7701 set the driver shipped with and
// are a starting point, not a datasheet.
#ifdef RG_DSI_PANEL_H_RES
#define ST7701_LCD_H_RES             RG_DSI_PANEL_H_RES
#define ST7701_LCD_V_RES             RG_DSI_PANEL_V_RES
#define ST7701_LCD_HBP               RG_DSI_HBP
#define ST7701_LCD_HSYNC             RG_DSI_HSYNC
#define ST7701_LCD_HFP               RG_DSI_HFP
#define ST7701_LCD_VBP               RG_DSI_VBP
#define ST7701_LCD_VSYNC             RG_DSI_VSYNC
#define ST7701_LCD_VFP               RG_DSI_VFP
#define ST7701_LANE_NUM              RG_DSI_LANE_NUM
#define ST7701_DPI_CLOCK_MHZ         RG_DSI_DPI_CLOCK_MHZ
#define ST7701_LANE_BIT_RATE         RG_DSI_LANE_BIT_RATE_MBPS
#else
#define ST7701_LCD_H_RES             480
#define ST7701_LCD_V_RES             800

#define ST7701_LCD_HBP               30
#define ST7701_LCD_HSYNC             2
#define ST7701_LCD_HFP               32
#define ST7701_LCD_VBP               17
#define ST7701_LCD_VSYNC             2
#define ST7701_LCD_VFP               16

#define ST7701_LANE_NUM              2
#define ST7701_DPI_CLOCK_MHZ         30
#define ST7701_LANE_BIT_RATE         500
#endif

#define ST7701_LEDC_TIMER            LEDC_TIMER_0
#define ST7701_LEDC_MODE             LEDC_LOW_SPEED_MODE
#define ST7701_LEDC_CHANNEL          LEDC_CHANNEL_0
#define ST7701_LEDC_DUTY_RES         LEDC_TIMER_13_BIT
#define ST7701_LEDC_FREQUENCY        5000
#define ST7701_LEDC_DUTY_MAX         8191
#define ST7701_DEFAULT_BRIGHTNESS    90

#define ST7701_MIPI_DSI_PHY_LDO_CHANNEL   3
#define ST7701_MIPI_DSI_PHY_LDO_VOLTAGE   2500

typedef struct {
    uint8_t type;
    uint8_t delay;
    uint8_t cmd;
    const uint8_t *data;
    uint8_t data_len;
} st7701_cmd_t;

typedef struct {
    esp_lcd_dsi_bus_handle_t dsi_bus;
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    uint16_t *framebuffer;
    size_t framebuffer_size;
    bool initialized;
    int window_left;
    int window_top;
    int window_width;
    int window_height;
    int current_y;
} st7701_context_t;

static st7701_context_t st7701_ctx = {0};
static esp_ldo_channel_handle_t st7701_ldo_handle = NULL;

static const st7701_cmd_t st7701_init_sequence[] = {
    {0x05, 120, 0x11, NULL, 0},
    {0x39, 0, 0xFF, (const uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5},
    {0x39, 5, 0xC0, (const uint8_t[]){0x63, 0x00}, 2},
    {0x39, 5, 0xC1, (const uint8_t[]){0x09, 0x02}, 2},
    {0x39, 0, 0xC2, (const uint8_t[]){0x20, 0x02}, 2},
    {0x15, 5, 0xCC, (const uint8_t[]){0x18}, 1},
    {0x39, 5, 0xB0, (const uint8_t[]){0x40, 0x0E, 0x51, 0x0F, 0x11, 0x07, 0x00, 0x09, 0x06, 0x1E, 0x04, 0x12, 0x11, 0x64, 0x29, 0xDF}, 16},
    {0x39, 5, 0xB1, (const uint8_t[]){0x40, 0x07, 0x4C, 0x0A, 0x0E, 0x04, 0x00, 0x08, 0x09, 0x1D, 0x01, 0x0E, 0x0C, 0x6A, 0x34, 0xDF}, 16},
    {0x39, 5, 0xFF, (const uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5},
    {0x15, 5, 0xB0, (const uint8_t[]){0x30}, 1},
    {0x15, 5, 0xB1, (const uint8_t[]){0x48}, 1},
    {0x15, 5, 0xB2, (const uint8_t[]){0x80}, 1},
    {0x15, 5, 0xB3, (const uint8_t[]){0x80}, 1},
    {0x15, 5, 0xB5, (const uint8_t[]){0x4F}, 1},
    {0x15, 5, 0xB7, (const uint8_t[]){0x85}, 1},
    {0x15, 5, 0xB8, (const uint8_t[]){0x23}, 1},
    {0x39, 5, 0xB9, (const uint8_t[]){0x22, 0x13}, 2},
    {0x15, 5, 0xC1, (const uint8_t[]){0x78}, 1},
    {0x15, 5, 0xC2, (const uint8_t[]){0x78}, 1},
    {0x15, 100, 0xD0, (const uint8_t[]){0x88}, 1},
    {0x39, 5, 0xE0, (const uint8_t[]){0x00, 0x00, 0x02}, 3},
    {0x39, 5, 0xE1, (const uint8_t[]){0x04, 0x00, 0x00, 0x00, 0x05, 0x00, 0x09, 0x00, 0x00, 0x10, 0x10}, 11},
    {0x39, 5, 0xE2, (const uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 12},
    {0x39, 5, 0xE3, (const uint8_t[]){0x00, 0x00, 0x33, 0x00}, 4},
    {0x39, 5, 0xE4, (const uint8_t[]){0x22, 0x00}, 2},
    {0x39, 5, 0xE5, (const uint8_t[]){0x03, 0x34, 0xAF, 0xB3, 0x05, 0x34, 0xAF, 0xB3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16},
    {0x39, 5, 0xE6, (const uint8_t[]){0x00, 0x00, 0x33, 0x00}, 4},
    {0x39, 5, 0xE7, (const uint8_t[]){0x22, 0x00}, 2},
    {0x39, 5, 0xE8, (const uint8_t[]){0x04, 0x34, 0xAF, 0xB3, 0x06, 0x34, 0xAF, 0xB3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 16},
    {0x39, 5, 0xEB, (const uint8_t[]){0x02, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00}, 7},
    {0x39, 5, 0xEC, (const uint8_t[]){0x00, 0x00}, 2},
    {0x39, 5, 0xED, (const uint8_t[]){0xFA, 0x45, 0x0B, 0x54, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xB0, 0x54, 0xAF}, 16},
    {0x39, 0, 0xFF, (const uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5},
    {0x05, 20, 0x29, NULL, 0},
};

static void st7701_pwm_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = ST7701_LEDC_MODE,
        .timer_num        = ST7701_LEDC_TIMER,
        .duty_resolution  = ST7701_LEDC_DUTY_RES,
        .freq_hz          = ST7701_LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = ST7701_LEDC_MODE,
        .channel        = ST7701_LEDC_CHANNEL,
        .timer_sel      = ST7701_LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = RG_GPIO_LCD_BCKL,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    RG_LOGI("PWM initialized on GPIO %d", RG_GPIO_LCD_BCKL);
}

static void st7701_set_backlight_brightness(uint8_t brightness_percent)
{
    if (brightness_percent > 100) brightness_percent = 100;
    uint32_t duty = (brightness_percent * ST7701_LEDC_DUTY_MAX) / 100;
    ledc_set_duty(ST7701_LEDC_MODE, ST7701_LEDC_CHANNEL, duty);
    ledc_update_duty(ST7701_LEDC_MODE, ST7701_LEDC_CHANNEL);
}

static esp_err_t st7701_enable_mipi_phy_power(void)
{
    esp_ldo_channel_config_t ldo_config = {
        .chan_id = ST7701_MIPI_DSI_PHY_LDO_CHANNEL,
        .voltage_mv = ST7701_MIPI_DSI_PHY_LDO_VOLTAGE
    };
    RG_LOGI("Enabling MIPI DSI PHY power (2.5V)");
    return esp_ldo_acquire_channel(&ldo_config, &st7701_ldo_handle);
}

static void st7701_disable_mipi_phy_power(void)
{
    if (st7701_ldo_handle) {
        esp_ldo_release_channel(st7701_ldo_handle);
        st7701_ldo_handle = NULL;
        RG_LOGI("MIPI DSI PHY power released");
    }
}

static void lcd_set_backlight(float percent)
{
    float level = RG_MIN(RG_MAX(percent / 100.f, 0), 1.f);
    st7701_set_backlight_brightness((uint8_t)(100 * level));
    RG_LOGI("Backlight set to %d%%", (int)(100 * level));
}

static void lcd_set_rotation(int rotation)
{
    (void)rotation;
}

static void lcd_set_window(int left, int top, int width, int height)
{
    st7701_ctx.window_left = left;
    st7701_ctx.window_top = top;
    st7701_ctx.window_width = width;
    st7701_ctx.window_height = height;
    st7701_ctx.current_y = 0;
}

static void lcd_sync(void)
{
}

// NOTE: This buffer is shared and must be used immediately after lcd_get_buffer
// is called. Do not call lcd_get_buffer again before lcd_send_buffer is called.
//
// It is sized from RG_SCREEN_WIDTH, the width retro-go draws in, which is not the panel's
// width when the panel is mounted rotated. Sizing it from the panel instead used to
// overflow it by 1280 pixels on an 800x480 target, silently, because the length argument
// was ignored.
#ifndef RG_SCREEN_BUFFER_ROWS
#define RG_SCREEN_BUFFER_ROWS 4
#endif

static inline uint16_t *lcd_get_buffer(size_t length)
{
    static uint16_t temp_buffer[RG_SCREEN_WIDTH * RG_SCREEN_BUFFER_ROWS];
    RG_ASSERT(length <= RG_COUNT(temp_buffer), "lcd buffer too small");
    return temp_buffer;
}

static inline void lcd_send_buffer(uint16_t *buffer, size_t length)
{
    if (!buffer || !st7701_ctx.framebuffer || length == 0) return;

    int width = st7701_ctx.window_width;
    if (width <= 0) return;

    int height = (int)length / width;
    if (height <= 0) return;

    int y0 = st7701_ctx.window_top + st7701_ctx.current_y;
    int x0 = st7701_ctx.window_left;

#if RG_SCREEN_ROTATE == 90
    // The panel is physically portrait and is mounted turned a quarter turn. An ST7701
    // cannot transpose for us -- its source drivers run along one edge, so the registers
    // offer mirroring but not a swapped scan order -- so a logical row becomes a panel
    // column right here.
    //
    // Logical (lx, ly) maps to panel (H_RES - 1 - ly, lx). Flip to (ly, V_RES - 1 - lx) if
    // the picture comes out upside down once the panel is actually in the shell.
    //
    // The loop runs x outermost on purpose. Doing it the natural way -- a source row at a
    // time -- means every single store lands in a different cache line, because successive
    // source pixels are a whole panel row apart. Walking a panel column instead writes
    // RG_SCREEN_BUFFER_ROWS pixels back to back, so one cache line covers the batch. With
    // the default four rows that is 8 bytes of a 64 byte line; the target raises the batch
    // to make the ratio worth having.
    int rows = height, cols = width;
    if (y0 < 0 || x0 < 0) return;
    if (y0 + rows > ST7701_LCD_H_RES) rows = ST7701_LCD_H_RES - y0;
    if (x0 + cols > ST7701_LCD_V_RES) cols = ST7701_LCD_V_RES - x0;

    for (int x = 0; x < cols; x++) {
        // Descending: ly grows, and panel x is H_RES - 1 - ly, so the run walks backwards.
        uint16_t *dst = st7701_ctx.framebuffer
                      + (size_t)(x0 + x) * ST7701_LCD_H_RES
                      + (ST7701_LCD_H_RES - 1 - y0);
        const uint16_t *src = buffer + x;
        for (int y = 0; y < rows; y++) {
            uint16_t px = src[(size_t)y * width];
            dst[-y] = (uint16_t)((px >> 8) | (px << 8));
        }
    }
#else
    for (int y = 0; y < height; y++) {
        int ly = y0 + y;
        if (ly < 0 || ly >= ST7701_LCD_V_RES) continue;
        const uint16_t *src = buffer + ((size_t)y * width);
        uint16_t *dst = st7701_ctx.framebuffer + ((size_t)ly * ST7701_LCD_H_RES) + x0;
        int count = width;
        if (x0 + count > ST7701_LCD_H_RES)
            count = ST7701_LCD_H_RES - x0;
        for (int x = 0; x < count; x++) {
            dst[x] = (uint16_t)((src[x] >> 8) | (src[x] << 8));
        }
    }
#endif

    st7701_ctx.current_y += height;
}


// How long to wait for the panel to accept its init sequence before giving up on it.
// Generous: the sequence itself asks for about 300ms of delays.
#define ST7701_INIT_TIMEOUT_MS 3000

static SemaphoreHandle_t st7701_init_done = NULL;

static void st7701_panel_init_task(void *arg)
{
    (void)arg;
    int num_cmds = sizeof(st7701_init_sequence) / sizeof(st7701_cmd_t);
    for (int i = 0; i < num_cmds; i++) {
        const st7701_cmd_t *cmd = &st7701_init_sequence[i];
        // This is the call that blocks forever with no panel attached.
        esp_err_t err = esp_lcd_panel_io_tx_param(st7701_ctx.io, cmd->cmd, cmd->data, cmd->data_len);
        if (err != ESP_OK) {
            RG_LOGE("Failed to send command 0x%02x", cmd->cmd);
        }
        if (cmd->delay > 0) {
            vTaskDelay(pdMS_TO_TICKS(cmd->delay));
        }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    xSemaphoreGive(st7701_init_done);
    vTaskDelete(NULL);
}

static void lcd_init(void)
{
    esp_err_t ret;

    RG_LOGI("Initializing ST7701 MIPI DSI display");

    ret = st7701_enable_mipi_phy_power();
    if (ret != ESP_OK) {
        RG_PANIC("Failed to enable MIPI PHY power");
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    st7701_pwm_init();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RG_GPIO_LCD_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_set_level(RG_GPIO_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(RG_GPIO_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = ST7701_LANE_NUM,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = ST7701_LANE_BIT_RATE,
    };

    RG_LOGI("creating DSI bus...");
    ret = esp_lcd_new_dsi_bus(&bus_config, &st7701_ctx.dsi_bus);
    if (ret != ESP_OK) {
        RG_PANIC("Failed to create DSI bus");
    }

    esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    RG_LOGI("DSI bus ok, creating DBI io...");
    ret = esp_lcd_new_panel_io_dbi(st7701_ctx.dsi_bus, &dbi_config, &st7701_ctx.io);
    if (ret != ESP_OK) {
        RG_PANIC("Failed to create DBI interface");
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    // Send the panel's init sequence, but do not stake the whole device on it.
    //
    // esp_lcd_panel_io_tx_param() over DSI never returns when nothing is on the other end of
    // the ribbon: the PHY waits for a link state that a missing panel will never produce.
    // Left inline that turns an unplugged or half-seated FPC into a device that boots to
    // nothing and cannot even reach the recovery screen, since the recovery screen needs the
    // display it is trying to recover.
    //
    // So it runs on its own task and we wait with a deadline. Past the deadline we carry on
    // without a panel: lcd_send_buffer() already treats a NULL framebuffer as "nowhere to
    // draw", so input, audio, storage and the serial log all keep working and the problem is
    // legible instead of silent. The stuck task is abandoned rather than killed -- it is
    // blocked inside the driver and deleting it there would leave the DSI lock held.
    RG_LOGI("DBI io ok, sending panel init sequence...");
    st7701_init_done = xSemaphoreCreateBinary();
    if (!st7701_init_done)
        RG_PANIC("Failed to create panel init semaphore");

    TaskHandle_t init_task = NULL;
    if (xTaskCreate(st7701_panel_init_task, "st7701_init", 4096, NULL, 5, &init_task) != pdPASS)
        RG_PANIC("Failed to start panel init task");

    if (xSemaphoreTake(st7701_init_done, pdMS_TO_TICKS(ST7701_INIT_TIMEOUT_MS)) != pdTRUE) {
        RG_LOGE("Panel did not answer in %d ms -- is the DSI ribbon connected?",
                ST7701_INIT_TIMEOUT_MS);
        RG_LOGE("Continuing without a display. Everything else still runs.");

        // "Abandoned" turned out to mean "still running at priority 5". The task is not
        // blocked in the driver as assumed -- it spins, and FreeRTOS run-time stats on a
        // board with no panel attached showed it taking 90% of the CPU while the emulator
        // task got 2%. Every measurement made in that state was really a measurement of
        // this. It is still not safe to delete (it holds the DSI lock), so it is demoted
        // instead: it may keep spinning, but only in time nothing else wants.
        if (init_task)
            vTaskPrioritySet(init_task, tskIDLE_PRIORITY + 1);

        st7701_ctx.framebuffer = NULL;
        st7701_ctx.initialized = false;
        return;
    }

    esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = ST7701_DPI_CLOCK_MHZ,
        .num_fbs = 1,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .video_timing = {
            .h_size = ST7701_LCD_H_RES,
            .v_size = ST7701_LCD_V_RES,
            .hsync_back_porch = ST7701_LCD_HBP,
            .hsync_pulse_width = ST7701_LCD_HSYNC,
            .hsync_front_porch = ST7701_LCD_HFP,
            .vsync_back_porch = ST7701_LCD_VBP,
            .vsync_pulse_width = ST7701_LCD_VSYNC,
            .vsync_front_porch = ST7701_LCD_VFP,
        },
        .flags.use_dma2d = 1,
    };

    RG_LOGI("init sequence sent, creating DPI panel...");
    ret = esp_lcd_new_panel_dpi(st7701_ctx.dsi_bus, &dpi_config, &st7701_ctx.panel);
    if (ret != ESP_OK) {
        RG_PANIC("Failed to create DPI panel");
    }

    RG_LOGI("DPI panel created, initialising...");
    ret = esp_lcd_panel_init(st7701_ctx.panel);
    if (ret != ESP_OK) {
        RG_PANIC("Failed to initialize DPI panel");
    }

    void *fb_ptr = NULL;
    ret = esp_lcd_dpi_panel_get_frame_buffer(st7701_ctx.panel, 0, &fb_ptr, &st7701_ctx.framebuffer_size);
    if (ret == ESP_OK && fb_ptr) {
        st7701_ctx.framebuffer = (uint16_t *)fb_ptr;
        memset(st7701_ctx.framebuffer, 0, st7701_ctx.framebuffer_size);
        RG_LOGI("DPI framebuffer: %p, size: %d", fb_ptr, st7701_ctx.framebuffer_size);
    } else {
        RG_LOGE("Failed to get DPI framebuffer: 0x%x, using allocated buffer", ret);
        st7701_ctx.framebuffer = heap_caps_malloc(ST7701_LCD_H_RES * ST7701_LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
        if (!st7701_ctx.framebuffer) {
            st7701_ctx.framebuffer = heap_caps_malloc(ST7701_LCD_H_RES * ST7701_LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        }
        if (!st7701_ctx.framebuffer) {
            RG_PANIC("Failed to allocate framebuffer");
        }
        st7701_ctx.framebuffer_size = ST7701_LCD_H_RES * ST7701_LCD_V_RES * sizeof(uint16_t);
        memset(st7701_ctx.framebuffer, 0, st7701_ctx.framebuffer_size);
        RG_LOGI("Allocated framebuffer: %p", st7701_ctx.framebuffer);
    }

    st7701_set_backlight_brightness(ST7701_DEFAULT_BRIGHTNESS);

    st7701_ctx.window_left = 0;
    st7701_ctx.window_top = 0;
    st7701_ctx.window_width = ST7701_LCD_H_RES;
    st7701_ctx.window_height = ST7701_LCD_V_RES;
    st7701_ctx.current_y = 0;

    st7701_ctx.initialized = true;
    RG_LOGI("ST7701 initialization completed");
}

static void lcd_deinit(void)
{
    if (!st7701_ctx.initialized) return;

    ledc_set_duty(ST7701_LEDC_MODE, ST7701_LEDC_CHANNEL, 0);
    ledc_update_duty(ST7701_LEDC_MODE, ST7701_LEDC_CHANNEL);

    if (st7701_ctx.framebuffer) {
        free(st7701_ctx.framebuffer);
        st7701_ctx.framebuffer = NULL;
    }
    st7701_ctx.framebuffer_size = 0;

    if (st7701_ctx.panel) {
        esp_lcd_panel_del(st7701_ctx.panel);
        st7701_ctx.panel = NULL;
    }
    if (st7701_ctx.io) {
        esp_lcd_panel_io_del(st7701_ctx.io);
        st7701_ctx.io = NULL;
    }
    if (st7701_ctx.dsi_bus) {
        esp_lcd_del_dsi_bus(st7701_ctx.dsi_bus);
        st7701_ctx.dsi_bus = NULL;
    }

    st7701_disable_mipi_phy_power();
    st7701_ctx.initialized = false;

    RG_LOGI("ST7701 deinitialized");
}

#ifdef __cplusplus
}
#endif

#endif
