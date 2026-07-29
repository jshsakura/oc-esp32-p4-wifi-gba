#include "es8311_codec.h"

#include "rg_system.h"
#include "rg_i2c.h"

#if defined(RG_I2C_ES8311_ADDR) && defined(ESP_PLATFORM)

#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <driver/i2s_std.h>

// The codec derives its internal clocks from MCLK, so this has to match the multiple the
// I2S channel is configured with or the audio comes out at the wrong pitch. 256x is the
// usual choice and is valid for the 16-bit samples retro-go feeds it.
#define ES8311_MCLK_MULTIPLE 256

// Analogue side of the board, used by the driver to work out how much of its gain range it
// can actually use before the amplifier clips.
#ifndef RG_ES8311_PA_VOLTAGE
#define RG_ES8311_PA_VOLTAGE 5.0
#endif
#ifndef RG_ES8311_DAC_VOLTAGE
#define RG_ES8311_DAC_VOLTAGE 3.3
#endif

static struct
{
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_data_if_t *data_if;
    const audio_codec_gpio_if_t *gpio_if;
    const audio_codec_if_t *codec_if;
    esp_codec_dev_handle_t dev;
    const char *error;
} es = {0};

const char *rg_es8311_get_error(void)
{
    return es.error;
}

bool rg_es8311_init(void *tx_handle, int sample_rate)
{
    if (es.dev)
        return true;

    es.error = NULL;

    void *bus = rg_i2c_get_bus_handle();
    if (!bus)
    {
        // rg_i2c_init() runs later than audio on some paths; try to bring it up ourselves
        // rather than silently coming up mute.
        rg_i2c_init();
        bus = rg_i2c_get_bus_handle();
    }
    if (!bus)
    {
        es.error = "I2C bus unavailable";
        return false;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0,
        .addr = RG_I2C_ES8311_ADDR << 1, // esp_codec_dev takes the 8-bit form
        .bus_handle = bus,
    };
    es.ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = 0,
        .tx_handle = tx_handle,
        .rx_handle = NULL,
    };
    es.data_if = audio_codec_new_i2s_data(&i2s_cfg);
    es.gpio_if = audio_codec_new_gpio();

    if (!es.ctrl_if || !es.data_if || !es.gpio_if)
    {
        es.error = "codec interface allocation failed";
        rg_es8311_deinit();
        return false;
    }

    es8311_codec_cfg_t cfg = {
        .ctrl_if = es.ctrl_if,
        .gpio_if = es.gpio_if,
        // Playback only. The board has no microphone, and asking for a capture path the
        // hardware cannot provide just makes the codec configure inputs nobody reads.
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .master_mode = false, // the ESP32 drives BCLK and WS
        .use_mclk = true,
        .mclk_div = ES8311_MCLK_MULTIPLE,
#ifdef RG_GPIO_SND_AMP_ENABLE
        .pa_pin = RG_GPIO_SND_AMP_ENABLE,
        .pa_reverted = false,
#else
        .pa_pin = -1,
#endif
        .hw_gain = {
            .pa_voltage = RG_ES8311_PA_VOLTAGE,
            .codec_dac_voltage = RG_ES8311_DAC_VOLTAGE,
        },
    };

    es.codec_if = es8311_codec_new(&cfg);
    if (!es.codec_if)
    {
        es.error = "ES8311 not responding";
        rg_es8311_deinit();
        return false;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es.codec_if,
        .data_if = es.data_if,
    };
    es.dev = esp_codec_dev_new(&dev_cfg);
    if (!es.dev)
    {
        es.error = "codec device allocation failed";
        rg_es8311_deinit();
        return false;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = 0,
        .sample_rate = (uint32_t)sample_rate,
        .mclk_multiple = ES8311_MCLK_MULTIPLE,
    };
    int err = esp_codec_dev_open(es.dev, &fs);
    if (err != ESP_CODEC_DEV_OK)
    {
        es.error = "codec open failed";
        RG_LOGE("esp_codec_dev_open failed: %d", err);
        rg_es8311_deinit();
        return false;
    }

    // Fixed output level. Retro-Go already scales every sample by its own volume setting
    // before the frame reaches I2S, so following that here as well would attenuate twice and
    // throw away bits at low settings for nothing. This just puts the codec somewhere sane
    // and leaves the user-facing control where it has always been.
    esp_codec_dev_set_out_vol(es.dev, 80);

    RG_LOGI("ES8311 ready at 0x%02X, %d Hz, MCLK x%d", RG_I2C_ES8311_ADDR, sample_rate,
            ES8311_MCLK_MULTIPLE);
    return true;
}

bool rg_es8311_deinit(void)
{
    if (es.dev)
    {
        esp_codec_dev_close(es.dev);
        esp_codec_dev_delete(es.dev);
    }
    if (es.codec_if)
        audio_codec_delete_codec_if(es.codec_if);
    if (es.ctrl_if)
        audio_codec_delete_ctrl_if(es.ctrl_if);
    if (es.data_if)
        audio_codec_delete_data_if(es.data_if);
    if (es.gpio_if)
        audio_codec_delete_gpio_if(es.gpio_if);
    es = (typeof(es)){0};
    return true;
}

bool rg_es8311_set_volume(int volume)
{
    if (!es.dev)
        return false;
    return esp_codec_dev_set_out_vol(es.dev, volume) == ESP_CODEC_DEV_OK;
}

bool rg_es8311_set_mute(bool mute)
{
    if (!es.dev)
        return false;
    return esp_codec_dev_set_out_mute(es.dev, mute) == ESP_CODEC_DEV_OK;
}

#else // no ES8311 on this board

bool rg_es8311_init(void *tx_handle, int sample_rate)
{
    (void)tx_handle, (void)sample_rate;
    return true;
}
bool rg_es8311_deinit(void) { return true; }
bool rg_es8311_set_volume(int volume) { (void)volume; return true; }
bool rg_es8311_set_mute(bool mute) { (void)mute; return true; }
const char *rg_es8311_get_error(void) { return NULL; }

#endif
