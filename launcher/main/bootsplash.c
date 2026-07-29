#include "bootsplash.h"

#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPLASH_DIR RG_STORAGE_ROOT "/boot"
#define SPLASH_IMAGE SPLASH_DIR "/logo.png"
#define SPLASH_SOUND SPLASH_DIR "/boot.wav"
#define SPLASH_CONFIG SPLASH_DIR "/boot.cfg"

// Enough for a couple of hundred milliseconds of audio at a time. Small on purpose: this
// runs before the launcher has drawn anything and the heap is otherwise untouched, but
// there is no reason to hold a whole jingle in RAM when it can be streamed.
#define SOUND_CHUNK_FRAMES 512

typedef struct
{
    int duration_ms;
    uint16_t background;
    bool skippable;
} splash_config_t;

static bool file_exists(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return false;
    fclose(fp);
    return true;
}

static void read_config(splash_config_t *cfg)
{
    cfg->duration_ms = 2500;
    cfg->background = 0;
    cfg->skippable = true;

    FILE *fp = fopen(SPLASH_CONFIG, "r");
    if (!fp)
        return;

    char line[96];
    while (fgets(line, sizeof(line), fp))
    {
        char key[32];
        long value;
        // Accepts "key = value" and "key=value"; anything else is ignored rather than
        // treated as an error, because a typo in a cosmetic file must not stop the boot.
        if (sscanf(line, " %31[a-zA-Z_] = %li", key, &value) != 2)
            continue;
        if (strcmp(key, "duration") == 0)
            cfg->duration_ms = (int)value;
        else if (strcmp(key, "background") == 0)
            cfg->background = (uint16_t)value;
        else if (strcmp(key, "skippable") == 0)
            cfg->skippable = value != 0;
    }
    fclose(fp);
}

// Minimal RIFF/WAVE reader. Leaves the file positioned at the start of the sample data and
// reports the format, or returns false and closes nothing the caller did not open.
static bool wav_open(FILE *fp, int *sample_rate, int *channels, int *bits, uint32_t *data_len)
{
    char riff[12];
    if (fread(riff, 1, 12, fp) != 12)
        return false;
    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0)
        return false;

    bool have_fmt = false;
    for (;;)
    {
        char id[4];
        uint32_t size;
        if (fread(id, 1, 4, fp) != 4 || fread(&size, 4, 1, fp) != 1)
            return false;

        if (memcmp(id, "fmt ", 4) == 0)
        {
            uint16_t format, ch, block, bps;
            uint32_t rate, byterate;
            if (size < 16)
                return false;
            if (fread(&format, 2, 1, fp) != 1 || fread(&ch, 2, 1, fp) != 1 ||
                fread(&rate, 4, 1, fp) != 1 || fread(&byterate, 4, 1, fp) != 1 ||
                fread(&block, 2, 1, fp) != 1 || fread(&bps, 2, 1, fp) != 1)
                return false;
            // Only uncompressed PCM. Anything else would need a decoder, and a boot jingle
            // is not worth carrying one for.
            if (format != 1 || (bps != 8 && bps != 16) || ch < 1 || ch > 2)
                return false;
            *sample_rate = (int)rate;
            *channels = ch;
            *bits = bps;
            have_fmt = true;
            if (size > 16)
                fseek(fp, size - 16, SEEK_CUR);
        }
        else if (memcmp(id, "data", 4) == 0)
        {
            if (!have_fmt)
                return false;
            *data_len = size;
            return true;
        }
        else
        {
            // Chunks are word aligned, so an odd size carries a pad byte.
            fseek(fp, size + (size & 1), SEEK_CUR);
        }
    }
}

// Plays the whole file, returning early if the user asks to skip. Runs on the calling task
// and blocks, which is what we want -- the logo should stay up for as long as the sound.
static void play_wav(const char *path, bool skippable, int min_duration_ms)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return;

    int rate = 0, channels = 0, bits = 0;
    uint32_t remaining = 0;
    if (!wav_open(fp, &rate, &channels, &bits, &remaining))
    {
        RG_LOGW("boot.wav is not 8/16-bit PCM, skipping");
        fclose(fp);
        return;
    }

    // Retune the output to the file rather than resample it. A boot sound is played once,
    // in isolation, so there is nothing to interfere with and no reason to write a
    // resampler for it.
    int previous_rate = rg_audio_get_sample_rate();
    rg_audio_set_sample_rate(rate);

    rg_audio_frame_t *frames = calloc(SOUND_CHUNK_FRAMES, sizeof(rg_audio_frame_t));
    uint8_t *raw = malloc(SOUND_CHUNK_FRAMES * 2 * 2);
    int64_t started = rg_system_timer();

    if (frames && raw)
    {
        size_t frame_bytes = (size_t)channels * (bits / 8);
        while (remaining >= frame_bytes)
        {
            if (skippable && rg_input_read_gamepad())
                break;

            size_t want = SOUND_CHUNK_FRAMES * frame_bytes;
            if (want > remaining)
                want = remaining - (remaining % frame_bytes);

            size_t got = fread(raw, 1, want, fp);
            if (got < frame_bytes)
                break;
            remaining -= got;

            size_t count = got / frame_bytes;
            for (size_t i = 0; i < count; ++i)
            {
                const uint8_t *src = raw + i * frame_bytes;
                int16_t l, r;
                if (bits == 8)
                {
                    // 8-bit WAV samples are unsigned and centred on 128.
                    l = (int16_t)((src[0] - 128) << 8);
                    r = (channels == 2) ? (int16_t)((src[1] - 128) << 8) : l;
                }
                else
                {
                    l = (int16_t)(src[0] | (src[1] << 8));
                    r = (channels == 2) ? (int16_t)(src[2] | (src[3] << 8)) : l;
                }
                frames[i].left = l;
                frames[i].right = r;
            }
            rg_audio_submit(frames, count);
        }
    }

    free(frames);
    free(raw);
    fclose(fp);

    // If the sound was shorter than the requested hold, wait out the difference so the logo
    // does not vanish the moment the jingle ends.
    int elapsed = (int)((rg_system_timer() - started) / 1000);
    if (!skippable || !rg_input_read_gamepad())
    {
        if (elapsed < min_duration_ms)
            rg_task_delay(min_duration_ms - elapsed);
    }

    rg_audio_set_sample_rate(previous_rate);
}

void bootsplash_show(void)
{
    bool have_image = file_exists(SPLASH_IMAGE);
    bool have_sound = file_exists(SPLASH_SOUND);

    // No assets, no delay. Someone who has not put anything in /boot should not pay a
    // couple of seconds of black screen for the privilege.
    if (!have_image && !have_sound)
        return;

    splash_config_t cfg;
    read_config(&cfg);

    rg_display_clear(cfg.background);

    if (have_image)
    {
        rg_image_t *img = rg_surface_load_image_file(SPLASH_IMAGE, 0);
        if (img)
        {
            int max_w = rg_display_get_info()->screen.width;
            int max_h = rg_display_get_info()->screen.height;
            int w = img->width, h = img->height;

            // Scale down to fit, preserving the aspect ratio. Never scale up: a pixel logo
            // blown up by a non-integer factor looks worse than a small sharp one.
            if (w > max_w || h > max_h)
            {
                int num = (max_w * h < max_h * w) ? max_w : max_h;
                int den = (max_w * h < max_h * w) ? w : h;
                w = w * num / den;
                h = h * num / den;
            }

            rg_gui_draw_image((max_w - w) / 2, (max_h - h) / 2, w, h, true, img);
            rg_surface_free(img);
        }
        else
        {
            RG_LOGW("Could not load %s", SPLASH_IMAGE);
        }
    }

    if (have_sound)
        play_wav(SPLASH_SOUND, cfg.skippable, cfg.duration_ms);
    else if (!cfg.skippable)
        rg_task_delay(cfg.duration_ms);
    else
        for (int waited = 0; waited < cfg.duration_ms && !rg_input_read_gamepad(); waited += 20)
            rg_task_delay(20);

    rg_display_clear(C_BLACK);
}
