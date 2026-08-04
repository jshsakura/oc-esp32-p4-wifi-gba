#include "rg_system.h"
#include "rg_audio.h"
#include "es8311_codec.h"

#if RG_AUDIO_USE_INT_DAC || RG_AUDIO_USE_EXT_DAC

#ifndef ESP_PLATFORM
#error "I2S support can only be built inside esp-idf!"
#elif !CONFIG_IDF_TARGET_ESP32 && RG_AUDIO_USE_INT_DAC
#error "Your chip has no DAC! Please set RG_AUDIO_USE_INT_DAC to 0 in your target file."
#endif

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#if RG_AUDIO_USE_INT_DAC
#include <driver/dac.h>
#endif

static struct {
    const char *last_error;
    int device;
    int volume;
    bool muted;
    i2s_chan_handle_t tx_handle;
} state;

static bool driver_init(int device, int sample_rate)
{
    state.last_error = NULL;
    state.device = device;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 180;
    esp_err_t ret = i2s_new_channel(&chan_cfg, &state.tx_handle, NULL);
    if (ret != ESP_OK) {
        state.last_error = esp_err_to_name(ret);
        return false;
    }

    if (state.device == 0)
    {
    #if RG_AUDIO_USE_INT_DAC
        i2s_std_config_t std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = I2S_GPIO_UNUSED,
                .ws = I2S_GPIO_UNUSED,
                .dout = I2S_GPIO_UNUSED,
                .din = I2S_GPIO_UNUSED,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        };
        ret = i2s_channel_init_std_mode(state.tx_handle, &std_cfg);
        if (ret != ESP_OK)
            state.last_error = esp_err_to_name(ret);
    #else
        state.last_error = "This device does not support internal DAC mode!";
    #endif
    }
    else if (state.device == 1)
    {
    #if RG_AUDIO_USE_EXT_DAC
        i2s_std_config_t std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                // A plain I2S amplifier needs no master clock, but a codec derives its
                // internal clocks from one and stays silent without it.
            #ifdef RG_GPIO_SND_I2S_MCK
                .mclk = RG_GPIO_SND_I2S_MCK,
            #else
                .mclk = I2S_GPIO_UNUSED,
            #endif
                .bclk = RG_GPIO_SND_I2S_BCK,
                .ws = RG_GPIO_SND_I2S_WS,
                .dout = RG_GPIO_SND_I2S_DATA,
                .din = I2S_GPIO_UNUSED,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        };
    #ifdef RG_GPIO_SND_I2S_MCK
        // Must agree with the divider the codec is configured with, or everything plays at
        // the wrong pitch.
        std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    #endif
        ret = i2s_channel_init_std_mode(state.tx_handle, &std_cfg);
        if (ret != ESP_OK)
            state.last_error = esp_err_to_name(ret);
    #else
        state.last_error = "This device does not support external DAC mode!";
    #endif
    }

    if (ret == ESP_OK)
        ret = i2s_channel_enable(state.tx_handle);

    // The codec has to be told the format before it will unmute. Do it after the channel is
    // running so MCLK is already ticking when the codec reads its clock registers.
    if (ret == ESP_OK && state.device == 1 && !rg_es8311_init(state.tx_handle, sample_rate))
        state.last_error = rg_es8311_get_error();

    return state.last_error == NULL;
}

static bool driver_set_sample_rates(int sampleRate)
{
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate);
    return i2s_channel_reconfig_std_clock(state.tx_handle, &clk_cfg) == ESP_OK;
}

static bool driver_deinit(void)
{
    rg_es8311_deinit();
    if (state.tx_handle) {
        i2s_channel_disable(state.tx_handle);
        i2s_del_channel(state.tx_handle);
        state.tx_handle = NULL;
    }
    if (state.device == 0)
    {
    #if RG_AUDIO_USE_INT_DAC
        if (RG_AUDIO_USE_INT_DAC & I2S_DAC_CHANNEL_RIGHT_EN)
            dac_output_disable(DAC_CHANNEL_1);
        if (RG_AUDIO_USE_INT_DAC & I2S_DAC_CHANNEL_LEFT_EN)
            dac_output_disable(DAC_CHANNEL_2);
        dac_i2s_disable();
    #endif
    }
    else if (state.device == 1)
    {
    #if RG_AUDIO_USE_EXT_DAC
        gpio_reset_pin(RG_GPIO_SND_I2S_BCK);
        gpio_reset_pin(RG_GPIO_SND_I2S_DATA);
        gpio_reset_pin(RG_GPIO_SND_I2S_WS);
    #endif
    }
    #ifdef RG_GPIO_SND_AMP_ENABLE
    gpio_reset_pin(RG_GPIO_SND_AMP_ENABLE);
    #endif
    return true;
}

static bool driver_submit(const rg_audio_frame_t *frames, size_t count)
{
    float volume = state.muted ? 0.f : (state.volume * 0.01f);
    // One frame's worth of audio in one go. At 180 this staged buffer was smaller than what
    // a core submits per video frame -- a Mega Drive frame is 444 -- so each submission
    // became three i2s_channel_write() calls, and that call genuinely blocks until the DMA
    // ring has room. Three waits per frame instead of one cost about 0.9ms of the 16.6ms
    // budget, which is most of the 5% by which the codec was being underfed, and an underfed
    // codec repeats its last descriptor: the periodic rattle heard on the speaker.
    // static, not a local: at 1024 frames of 4 bytes this is 4KB, and it was
    // 4KB taken from the stack of whichever core happens to be submitting audio.
    // PC Engine died here with a stack protection fault -- the fault address is
    // in driver_submit, but the cost is charged to every core that calls it, and
    // folded cores now run on app_main's frame rather than at the base of their
    // own task. Raising CONFIG_ESP_MAIN_TASK_STACK_SIZE again would have paid
    // for this buffer once per task instead of once.
    //
    // Safe because there is exactly one call site (rg_audio.c:146) and it holds
    // audio.lock across the call, so two submissions cannot overlap here.
    static rg_audio_frame_t buffer[1024];
    size_t written = 0;
    size_t pos = 0;

    bool use_internal_dac = state.device == 0;

    for (size_t i = 0; i < count; ++i)
    {
        int left = frames[i].left * volume;
        int right = frames[i].right * volume;

        if (use_internal_dac)
        {
        #if RG_AUDIO_USE_INT_DAC == 1
            left = ((left + right) >> 1) + 0x8000;
            right = 0;
        #elif RG_AUDIO_USE_INT_DAC == 2
            left = 0;
            right = ((left + right) >> 1) + 0x8000;
        #elif RG_AUDIO_USE_INT_DAC == 3
            int sample = (left + right) >> 1;
            if (sample > 0x7F00)
            {
                left = 0x8000 + (sample - 0x7F00);
                right = -0x8000 + 0x7F00;
            }
            else if (sample < -0x7F00)
            {
                left = 0x8000 + (sample + 0x7F00);
                right = -0x8000 + -0x7F00;
            }
            else
            {
                left = 0x8000;
                right = -0x8000 + sample;
            }
        #endif
        }

        buffer[pos].left = left;
        buffer[pos].right = right;

        if (i == count - 1 || ++pos == RG_COUNT(buffer))
        {
            if (i2s_channel_write(state.tx_handle, (void *)buffer, pos * 4, &written, 1000) != ESP_OK)
                RG_LOGW("I2S Submission error! Written: %d/%d\n", written, pos * 4);
            pos = 0;
        }
    }
    return true;
}

static bool driver_set_mute(bool mute)
{
    i2s_channel_disable(state.tx_handle);
    #if defined(RG_I2C_ES8311_ADDR)
        // The codec owns the amplifier enable pin -- it was handed to esp_codec_dev as
        // pa_pin. Driving the same GPIO from here as well would have the two of them
        // fighting over it.
        rg_es8311_set_mute(mute);
    #elif defined(RG_GPIO_SND_AMP_ENABLE)
        gpio_set_direction(RG_GPIO_SND_AMP_ENABLE, GPIO_MODE_OUTPUT);
        #ifdef RG_GPIO_SND_AMP_ENABLE_INVERT
            gpio_set_level(RG_GPIO_SND_AMP_ENABLE, mute ? 1 : 0);
        #else
            gpio_set_level(RG_GPIO_SND_AMP_ENABLE, mute ? 0 : 1);
        #endif
    #endif
    if (!mute)
        i2s_channel_enable(state.tx_handle);
    state.muted = mute;
    return true;
}

static bool driver_set_volume(int volume)
{
    state.volume = volume;
    return true;
}

static const char *driver_get_error(void)
{
    return state.last_error;
}

const rg_audio_driver_t rg_audio_driver_i2s = {
    .name = "i2s",
    .init = driver_init,
    .deinit = driver_deinit,
    .submit = driver_submit,
    .set_mute = driver_set_mute,
    .set_volume = driver_set_volume,
    .set_sample_rate = driver_set_sample_rates,
    .get_error = driver_get_error,
};

#endif
