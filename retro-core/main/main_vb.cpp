//============================================================================
// main.cpp - Retro-Go frontend for the red-viper Virtual Boy emulator core.
//
// The red-viper core is C++ (video_soft.cpp) with C modules. This file is
// compiled as C++ and exposes a C entry point (app_main) for the retro-go
// launcher, mirroring stella-go/main/main.cpp.
//
// The Virtual Boy displays 384x224 pixels at 2 bits per pixel (4 red shades),
// stored column-major in the VIP display RAM. We render the LEFT eye only
// (VB_LEFT_EYE_ONLY) and convert to RGB565 for retro-go's display path.
// The V810 CPU runs in interpreter-only mode (the ARM DRC is excluded on
// RISC-V / ESP32-P4). Audio runs at the VSU's native 50000 Hz.
//============================================================================

extern "C" {
#include "shared.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_set.h"
#include "vb_sound.h"
#include "vb_dsp.h"
#include "replay.h"
}

// Virtual Boy display: 384x224, ~50.27 Hz (we use 50 for timing).
#define VB_WIDTH              384
#define VB_HEIGHT             224
#define VB_FPS                50

// Audio: the VB VSU generates at 50000 Hz (SAMPLE_RATE from vb_sound.h).
// At 50 fps that's 1000 stereo sample pairs per frame.
#define VB_AUDIO_RATE         SAMPLE_RATE
#define VB_AUDIO_PER_FRAME    (VB_AUDIO_RATE / VB_FPS)

// Each VIP framebuffer block is 0x8000 bytes; column stride is 32 words.
#define VB_FB_BLOCK_SIZE      0x8000
#define VB_COL_STRIDE_WORDS   32

// Save state format: "RVSS" v2 (matches red-viper's vb_gui.c).
#define VB_SS_ID              0x53535652u
#define VB_SS_VER             2u

static rg_app_t *app;
/* Frame surfaces in PSRAM, not internal RAM.
 *
 * This core was a standalone app, where MEM_FAST was free -- it was the only
 * emulator in the binary. Folded into retro-core it is one of twenty, and there
 * are 42KB of internal RAM left across all of them. Asking for more does not
 * fail loudly: rg_alloc warns "CAPS not fully met", hands back PSRAM anyway, and
 * the mismatch surfaces later as an assert in heap_caps_free. */
static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;

// ---- Platform stubs the core expects --------------------------------------
// The device path (GNW_VB_DEVICE) uses vb_dev_calloc/vb_dev_malloc for RAM
// regions instead of newlib malloc. On ESP32-P4 we map them to standard alloc.
extern "C" void *vb_dev_calloc(size_t nmemb, size_t size) { return calloc(nmemb, size); }
extern "C" void *vb_dev_malloc(size_t size) { return malloc(size); }

// vb_rom_mask is defined in v810_mem.c but not declared in any header
// (the reference main_vb.c declares it extern). Used for ROM address mirroring.
extern "C" unsigned int vb_rom_mask;

// tDSPCACHE normally lives in video.c (the GL renderer dispatcher, excluded).
VB_DSPCACHE tDSPCACHE;

// eye_count is referenced by video.c/video_hard.c (both excluded); define it
// here so the symbol resolves if anything takes its address.
int eye_count = 1;

// VIP framebuffer download is a hard-GL-renderer concern; the software path
// (video_soft.cpp) composites straight into V810_DISPLAY_RAM — no-op here.
extern "C" void video_download_vip(int drawn_fb) { (void)drawn_fb; }

// ---- Audio backend bridge -------------------------------------------------
// vb_sound.c calls sound_push_backend() when a SAMPLE_COUNT-frame stereo
// buffer completes. We accumulate the samples and drain them once per
// emulated frame into retro-go's audio mixer (same technique as the Lynx).
#define VB_AUDIO_MAX_ACCUM  2048

static int16_t s_audio_accum[VB_AUDIO_MAX_ACCUM * 2];
static int s_audio_count;

extern "C" bool sound_init_backend(int16_t *bufs[])
{
    (void)bufs;
    return true; // MUST succeed: vb_sound_init frees wave bufs on failure (UAF)
}

extern "C" void sound_close_backend(void)  {}
extern "C" void sound_pause_backend(void)  {}
extern "C" void sound_resume_backend(void) {}

// Called by vb_sound.c when a SAMPLE_COUNT stereo buffer is complete.
extern "C" bool sound_push_backend(int16_t *buf)
{
    int n = SAMPLE_COUNT;
    if (s_audio_count + n > VB_AUDIO_MAX_ACCUM)
        n = VB_AUDIO_MAX_ACCUM - s_audio_count;
    if (n > 0)
    {
        memcpy(&s_audio_accum[s_audio_count * 2], buf, (size_t)n * 2 * sizeof(int16_t));
        s_audio_count += n;
    }
    return true; // non-zero so the core advances to the next wave buffer
}

// Downmix accumulated stereo frames to mono rg_audio_frame_t and resample
// onto the fixed per-frame buffer.
static void vb_audio_drain(rg_audio_frame_t *out, int len)
{
    int gen = s_audio_count;
    if (gen <= 0)
    {
        memset(out, 0, (size_t)len * sizeof(rg_audio_frame_t));
        return;
    }
    for (int i = 0; i < len; i++)
    {
        int idx = (int)(((int64_t)i * gen) / len);
        if (idx >= gen) idx = gen - 1;
        int32_t mono = ((int32_t)s_audio_accum[idx * 2] + (int32_t)s_audio_accum[idx * 2 + 1]) >> 1;
        out[i].left = (int16_t)mono;
        out[i].right = (int16_t)mono;
    }
    s_audio_count = 0;
}

// ---- ROM loading ----------------------------------------------------------
static uint8_t *load_rom(const char *path, uint32_t *out_size)
{
    uint8_t *data = NULL;
    size_t size = 0;

    if (rg_extension_match(path, "zip"))
    {
        if (!rg_storage_unzip_file(path, NULL, (void **)&data, &size, 0))
            return NULL;
    }
    else
    {
        FILE *fp = fopen(path, "rb");
        if (!fp)
            return NULL;
        fseek(fp, 0, SEEK_END);
        size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        data = (uint8_t *)malloc(size);
        if (!data || fread(data, 1, size, fp) != size)
        {
            free(data);
            data = NULL;
        }
        fclose(fp);
    }

    *out_size = (uint32_t)size;
    return data;
}

// Cheap FNV-1a stamp for savestate/ROM matching (self-consistent, not a CRC).
static uint32_t vb_rom_stamp(const uint8_t *rom, uint32_t len)
{
    uint32_t h = 2166136261u ^ len;
    uint32_t step = len > 4096 ? len / 4096 : 1;
    for (uint32_t i = 0; i < len; i += step)
    {
        h ^= rom[i];
        h *= 16777619u;
    }
    return h;
}

// ---- Save / load state (path-based, matches red-viper vb_gui.c v2 format) --
static bool save_state_handler(const char *filename)
{
    FILE *f = fopen(filename, "wb");
    if (!f)
        return false;

    bool ok = false;
    do {
        uint32_t id = VB_SS_ID, ver = VB_SS_VER, crc = (uint32_t)tVBOpt.CRC32;
        #define WBUF(ptr, sz) if (fwrite(ptr, 1, sz, f) != (size_t)(sz)) break;
        #define WVAR(V)        WBUF(&(V), sizeof(V))

        WVAR(id);
        WVAR(ver);
        WVAR(crc);

        // CPU registers
        WVAR(vb_state->v810_state.P_REG);
        WVAR(vb_state->v810_state.S_REG);
        WVAR(vb_state->v810_state.PC);
        WVAR(vb_state->v810_state.cycles);
        WVAR(vb_state->v810_state.except_flags);

        // VIP registers (with size prefix for forward-compat)
        uint32_t sz = (uint32_t)sizeof(vb_state->tVIPREG);
        WVAR(sz);
        WBUF(&vb_state->tVIPREG, sz);

        // Hardware control registers
        sz = (uint32_t)sizeof(vb_state->tHReg);
        WVAR(sz);
        WBUF(&vb_state->tHReg, sz);

        // Audio registers
        sz = (uint32_t)sizeof(sound_state);
        WVAR(sz);
        WBUF(&sound_state, sz);

        // RAM regions
        #define WRITE_MEM(area) \
            sz = (uint32_t)(vb_state->area.highaddr + 1 - vb_state->area.lowaddr); \
            WVAR(sz); \
            WBUF(vb_state->area.pmemory, sz);
        WRITE_MEM(V810_DISPLAY_RAM);
        WRITE_MEM(V810_SOUND_RAM);
        WRITE_MEM(V810_VB_RAM);
        WRITE_MEM(V810_GAME_RAM);
        #undef WRITE_MEM
        #undef WVAR
        #undef WBUF

        ok = true;
    } while (0);

    fclose(f);
    return ok;
}

static bool load_state_handler(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
        return false;

    bool ok = false;
    // Use do-while(0)+break instead of goto: C++ forbids jumping over the
    // cpu_state initializer below.
    do {
        uint32_t id = 0, ver = 0, crc = 0;
        if (fread(&id, 1, 4, f) != 4) break;
        if (fread(&ver, 1, 4, f) != 4) break;
        if (id != VB_SS_ID || ver != VB_SS_VER) break;
        if (fread(&crc, 1, 4, f) != 4) break;
        if (crc != (uint32_t)tVBOpt.CRC32) break; // savestate must match this ROM

        // Read CPU state into a scratch copy; commit only if full read succeeds.
        cpu_state new_state = vb_state->v810_state;
        if (fread(new_state.P_REG, 1, sizeof(new_state.P_REG), f) != sizeof(new_state.P_REG)) break;
        if (fread(new_state.S_REG, 1, sizeof(new_state.S_REG), f) != sizeof(new_state.S_REG)) break;
        if (fread(&new_state.PC, 1, sizeof(new_state.PC), f) != sizeof(new_state.PC)) break;
        if (fread(&new_state.cycles, 1, sizeof(new_state.cycles), f) != sizeof(new_state.cycles)) break;
        if (fread(&new_state.except_flags, 1, sizeof(new_state.except_flags), f) != sizeof(new_state.except_flags)) break;

        uint32_t sz;
        // VIP registers — size must match this build's struct (v2 only).
        if (fread(&sz, 1, 4, f) != 4) break;
        if (sz != (uint32_t)sizeof(vb_state->tVIPREG)) break;
        if (fread(&vb_state->tVIPREG, 1, sz, f) != sz) break;

        // Hardware control registers
        if (fread(&sz, 1, 4, f) != 4) break;
        if (sz != (uint32_t)sizeof(vb_state->tHReg)) break;
        if (fread(&vb_state->tHReg, 1, sz, f) != sz) break;

        // Audio registers
        if (fread(&sz, 1, 4, f) != 4) break;
        if (sz != (uint32_t)sizeof(sound_state)) break;
        if (fread(&sound_state, 1, sz, f) != sz) break;

        // RAM regions
        #define READ_MEM(area) \
            if (fread(&sz, 1, 4, f) != 4) break; \
            if (sz != (uint32_t)(vb_state->area.highaddr + 1 - vb_state->area.lowaddr)) break; \
            if (fread(vb_state->area.pmemory, 1, sz, f) != sz) break;
        READ_MEM(V810_DISPLAY_RAM);
        READ_MEM(V810_SOUND_RAM);
        READ_MEM(V810_VB_RAM);
        READ_MEM(V810_GAME_RAM);
        #undef READ_MEM

        vb_state->v810_state = new_state; // commit
        ok = true;
    } while (0);

    fclose(f);
    return ok;
}

static bool reset_handler(bool hard)
{
    v810_reset();
    clearCache();
    vb_state->tVIPREG.frametime = videoProcessingTime();
    return true;
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
    {
        rg_display_submit(currentUpdate, 0);
    }
}

// ---- Input ----------------------------------------------------------------
// VB controller has two D-pads plus L/R triggers. We map the physical D-pad
// to the LEFT pad (most games) and A/B to VB A/B. Start/Select map directly.
static uint16_t read_input(void)
{
    uint32_t pad = rg_input_read_gamepad();
    uint16_t k = 0;
    if (pad & RG_KEY_UP)     k |= VB_LPAD_U;
    if (pad & RG_KEY_DOWN)   k |= VB_LPAD_D;
    if (pad & RG_KEY_LEFT)   k |= VB_LPAD_L;
    if (pad & RG_KEY_RIGHT)  k |= VB_LPAD_R;
    if (pad & RG_KEY_A)      k |= VB_KEY_A;
    if (pad & RG_KEY_B)      k |= VB_KEY_B;
    if (pad & RG_KEY_START)  k |= VB_KEY_START;
    if (pad & RG_KEY_SELECT) k |= VB_KEY_SELECT;
    // Shoulder buttons map to L/R triggers
    if (pad & RG_KEY_L)      k |= VB_KEY_L;
    if (pad & RG_KEY_R)      k |= VB_KEY_R;
    return k;
}

// ---- Framebuffer conversion -----------------------------------------------
// VB left-eye framebuffer: 2 bits/pixel, column-major (8 px per 16-bit word,
// 32 words per column). Convert to a row-major RGB565 surface (red shades).
static void convert_framebuffer(rg_surface_t *surf)
{
    int dfb = vb_state->tVIPREG.tDisplayedFB;

    // Composite the just-drawn frame if a new drawing cycle is ready.
    // Same condition as red-viper's linux soft path.
    if (vb_state->tVIPREG.tFrame == 0 && !vb_state->tVIPREG.drawing &&
        (vb_state->tVIPREG.XPCTRL & XPEN))
    {
        if (tDSPCACHE.CharCacheInvalid)
            update_texture_cache_soft();
        video_soft_render(!dfb);
        tDSPCACHE.CharCacheInvalid = false;
        memset(tDSPCACHE.CharacterCache, 0, sizeof(tDSPCACHE.CharacterCache));
    }

    // Left-eye framebuffer base (block 0 or 1 within the left-eye region).
    const uint16_t *vb_fb = (const uint16_t *)(
        vb_state->V810_DISPLAY_RAM.pmemory + VB_FB_BLOCK_SIZE * dfb);

    // Build the 4 red shades as RGB565 from the VIP brightness registers.
    int bri[4];
    bri[0] = 0;
    bri[1] = vb_state->tVIPREG.BRTA;
    bri[2] = vb_state->tVIPREG.BRTB;
    bri[3] = vb_state->tVIPREG.BRTA + vb_state->tVIPREG.BRTB + vb_state->tVIPREG.BRTC;

    uint16_t pal565[4];
    for (int v = 0; v < 4; v++)
    {
        int b = bri[v] * 2;
        if (b > 255) b = 255;
        pal565[v] = (uint16_t)((b >> 3) << 11); // red channel only
    }

    uint16_t *dst = (uint16_t *)surf->data;
    int stride = surf->stride / 2;

    // Column-major source: for each output row, read the same bit position
    // across all 384 columns. word_idx = y/8, shift = (y%8)*2.
    for (int y = 0; y < VB_HEIGHT; y++)
    {
        int word_idx = y >> 3;
        int shift = (y & 7) * 2;
        const uint16_t *row_base = vb_fb + word_idx;
        for (int x = 0; x < VB_WIDTH; x++)
        {
            dst[x] = pal565[(row_base[x * VB_COL_STRIDE_WORDS] >> shift) & 3];
        }
        dst += stride;
    }
}

// ---- Entry point ----------------------------------------------------------
extern "C" void vb_main(void)
{
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
        .memRead = NULL,
        .memWrite = NULL,
        .options = NULL,
        .about = NULL,
    };

    app = rg_system_reinit(VB_AUDIO_RATE, &handlers, NULL);

    // VB runs at ~50.27 Hz, not the retro-go default 60 Hz.
    rg_system_set_tick_rate(VB_FPS);

    // Double-buffered RGB565 surfaces at native VB resolution (384x224).
    // retro-go's display path scales this to the physical screen.
    updates[0] = rg_surface_create(VB_WIDTH, VB_HEIGHT, RG_PIXEL_565_LE, MEM_SLOW);
    updates[1] = rg_surface_create(VB_WIDTH, VB_HEIGHT, RG_PIXEL_565_LE, MEM_SLOW);
    currentUpdate = updates[0];

    // --- Load ROM ---
    uint32_t rom_size = 0;
    uint8_t *rom_data = load_rom(app->romPath, &rom_size);
    if (!rom_data || rom_size == 0)
    {
        // CRITICAL: alert + exit, NO panic (don't crash the device).
        rg_gui_alert(_("Error"), _("Could not load the game file."));
        rg_system_exit();
    }

    // --- Initialise the emulator ---
    setDefaults();
    is_multiplayer = false;
    v810_init();
    replay_init();

    // Point the CPU at our loaded ROM buffer (pointer-based, no 16MB alloc).
    V810_ROM1.pmemory  = rom_data;
    V810_ROM1.lowaddr  = 0x07000000;
    V810_ROM1.size     = rom_size;
    V810_ROM1.highaddr = 0x07000000 + rom_size - 1;
    V810_ROM1.off      = (size_t)rom_data - 0x07000000;
    vb_rom_mask        = (unsigned int)(rom_size - 1);
    tVBOpt.CRC32       = vb_rom_stamp(rom_data, rom_size);

    v810_reset();
    clearCache();

    // Bootstrap: RENDERMODE must be RM_CPUONLY (software VIP), and frametime
    // must be set so the VIP drawing progress tracker works (without it the
    // draw never completes and the displayed framebuffer never flips).
    tVBOpt.RENDERMODE = RM_CPUONLY;
    vb_state->tVIPREG.frametime = videoProcessingTime();

    vb_sound_init();

    if (app->bootFlags & RG_BOOT_RESUME)
        rg_emu_load_state(app->saveSlot);

    long skipFrames = 0;
    bool slowFrame = false;

    while (1)
    {
        uint32_t joystick = rg_input_read_gamepad();

        if (joystick & (RG_KEY_MENU | RG_KEY_OPTION))
        {
            if (joystick & RG_KEY_MENU)
                rg_gui_game_menu();
            else
                rg_gui_options_menu();
        }

        int64_t startTime = rg_system_timer();
        bool drawFrame = !skipFrames;

        // --- Input ---
        uint16_t k = read_input();
        vb_state->tHReg.SLB = (BYTE)(k & 0xFF);
        vb_state->tHReg.SHB = (BYTE)((k >> 8) & 0xFF);

        // --- Emulate one frame ---
        v810_run();

        if (drawFrame)
        {
            convert_framebuffer(currentUpdate);
            slowFrame = !rg_display_sync(false);
            rg_display_submit(currentUpdate, 0);
            currentUpdate = updates[currentUpdate == updates[0]];
        }

        // --- Audio: drain accumulated VSU frames into the mixer ---
        rg_audio_frame_t mixbuf[VB_AUDIO_PER_FRAME];
        vb_audio_drain(mixbuf, VB_AUDIO_PER_FRAME);

        rg_system_tick(rg_system_timer() - startTime);
        rg_audio_submit(mixbuf, VB_AUDIO_PER_FRAME);

        // Frame skipping to keep emulation in real time.
        if (skipFrames == 0)
        {
            int elapsed = rg_system_timer() - startTime;
            if (app->frameskip > 0)
                skipFrames = app->frameskip;
            else if (elapsed > app->frameTime + 1500)
                skipFrames = 1;
            else if (drawFrame && slowFrame)
                skipFrames = 1;
        }
        else if (skipFrames > 0)
        {
            skipFrames--;
        }
    }

    RG_PANIC("Virtual Boy Ended");
}
