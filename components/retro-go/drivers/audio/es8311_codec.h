#pragma once

#include <stdbool.h>
#include <stddef.h>

// ES8311 codec control, for boards whose audio goes out through one.
//
// This only configures the codec. The PCM itself still goes straight out of the I2S channel
// the way it always has -- the codec sits on the I2C bus and needs to be told the sample
// format, the clock ratio and the volume before it will make a sound, and left alone after.
//
// The register work is Espressif's (esp_codec_dev), not ours, on purpose: an ES8311 needs a
// clock-coefficient table keyed on every MCLK and sample-rate pair it supports, and a
// hand-copied one is a silent wrong-pitch bug waiting to happen.
//
// A board opts in by defining RG_I2C_ES8311_ADDR. Without it every function here is a no-op
// that reports success, so a target with a plain amplifier is unaffected.

// tx_handle is the i2s_chan_handle_t the audio driver already created. Safe to call twice.
bool rg_es8311_init(void *tx_handle, int sample_rate);
bool rg_es8311_deinit(void);

// volume is 0-100, matching rg_audio.
bool rg_es8311_set_volume(int volume);
bool rg_es8311_set_mute(bool mute);

// Non-NULL when the last call failed.
const char *rg_es8311_get_error(void);
