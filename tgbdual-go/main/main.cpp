//============================================================================
// main.cpp - Retro-Go frontend for the TGB Dual GB/GBC/SGB core.
//
// TGB Dual is a C++ Game Boy emulator with broad mapper support, GBC colour,
// Super Game Boy borders, and link-cable infrastructure (single-player only
// in this port for now). This file replaces the gnuboy bridge
// (retro-core/main/main_gbc.c) with a stand-alone app that gives the C++
// core its own internal-RAM budget — retro-core is 100 % C today and adding
// the C++ ABI (vtables, static init, libstdc++) would overflow its sram_low.
//
// The bridge is modelled on the STM32 Game-and-Watch port
// (Core/Src/porting/gb_tgbdual/main_gb_tgbdual.cpp) for the core-specific
// boot, console-mode selection, and save-state logic, and on
// retro-core/main/main_gbc.c for the retro-go framework side.
//============================================================================

#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gb_core/gb.h"
#include "gb_core/renderer.h"
#include "gb_core/tgbdual_sgb.h"
#include "rg_renderer.h"

// Globals the renderer reads (declared extern in rg_renderer.cpp).
// Set from the main loop before each g_gb->run() batch.
uint16_t *g_display_target = NULL;
uint32_t  g_gamepad = 0;

#define AUDIO_SAMPLE_RATE  44100
#define GB_LCD_LINES       154   // 144 visible + 10 vblank = one frame

static rg_app_t *app;
static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;

static gb *g_gb = NULL;
static rg_renderer *render = NULL;

// Console mode: DMG (0), CGB (2), or SGB (3). Auto-detected from the cart
// header at boot; the user can cycle in the options menu for dual-mode carts.
static int gb_console_mode = GB_CONSOLE_DMG;
static bool sgb_border_enabled = true;

// --- Save / load state -----------------------------------------------------
// TGB Dual serialises to/from a flat memory buffer. We marshal through a
// generously-sized PSRAM allocation.
#define STATE_BUFFER_SIZE  (256 * 1024)

static bool save_state_handler(const char *filename)
{
    uint8_t *buf = (uint8_t *)rg_alloc(STATE_BUFFER_SIZE, MEM_SLOW);
    if (!buf) return false;
    g_gb->save_state_mem(buf);
    // Determine actual size: TGB Dual writes a header with the size.
    // For simplicity we write the full buffer; the load side reads what it needs.
    FILE *f = fopen(filename, "wb");
    bool ok = false;
    if (f)
    {
        ok = (fwrite(buf, 1, STATE_BUFFER_SIZE, f) == STATE_BUFFER_SIZE);
        fclose(f);
    }
    free(buf);
    return ok;
}

static bool load_state_handler(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > STATE_BUFFER_SIZE)
    {
        fclose(f);
        return false;
    }
    uint8_t *buf = (uint8_t *)rg_alloc(STATE_BUFFER_SIZE, MEM_SLOW);
    if (!buf) { fclose(f); return false; }
    bool ok = (fread(buf, 1, sz, f) == (size_t)sz);
    fclose(f);
    if (ok)
        g_gb->restore_state_mem(buf);
    free(buf);
    return ok;
}

static bool reset_handler(bool hard)
{
    g_gb->reset();
    return true;
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
        rg_display_submit(currentUpdate, 0);
}

// --- Options menu ----------------------------------------------------------
static rg_gui_event_t palette_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    // DMG palette cycling only (GBC/SGB use their own palettes).
    if (g_gb->get_rom()->get_info()->gb_type != 1)
    {
        strcpy(option->value, "N/A");
        return RG_DIALOG_VOID;
    }
    int max = g_gb->get_lcd()->get_palette_count() - 1;
    static int idx = 0;
    idx = g_gb->get_lcd()->get_current_palette();
    if (event == RG_DIALOG_PREV) idx = idx > 0 ? idx - 1 : max;
    if (event == RG_DIALOG_NEXT) idx = idx < max ? idx + 1 : 0;
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        g_gb->get_lcd()->set_palette(idx);
        rg_settings_set_number(NS_APP, "GbPalette", idx);
    }
    sprintf(option->value, "%d/%d", idx + 1, max + 1);
    return RG_DIALOG_VOID;
}

static rg_gui_event_t sgb_border_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        sgb_border_enabled = !sgb_border_enabled;
        rg_settings_set_number(NS_APP, "SgbBorder", sgb_border_enabled ? 1 : 0);
    }
    strcpy(option->value, sgb_border_enabled ? _("On") : _("Off"));
    return RG_DIALOG_VOID;
}

static void options_handler(rg_gui_option_t *dest)
{
    int end = 0;
    while (dest[end].label || dest[end].value || dest[end].arg || dest[end].flags || dest[end].update_cb)
        end++;
    dest[end] = (rg_gui_option_t){0, _("Palette"), "-", RG_DIALOG_FLAG_NORMAL, &palette_cb};
    dest[end + 1] = (rg_gui_option_t){0, _("SGB Border"), "-", RG_DIALOG_FLAG_NORMAL, &sgb_border_cb};
    dest[end + 2] = (rg_gui_option_t)RG_DIALOG_END;
}

// --- ROM loading -----------------------------------------------------------
static byte *load_rom(const char *path, uint32_t *out_size)
{
    byte *data = NULL;
    size_t size = 0;

    if (rg_extension_match(path, "zip"))
    {
        if (!rg_storage_unzip_file(path, NULL, (void **)&data, &size, 0))
            return NULL;
    }
    else
    {
        FILE *fp = fopen(path, "rb");
        if (!fp) return NULL;
        fseek(fp, 0, SEEK_END);
        size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        data = (byte *)malloc(size);
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

// --- Detect console mode from ROM header -----------------------------------
static int detect_console_mode(uint8_t rom_cgb_flag, uint8_t sgb_flag, uint8_t old_licensee)
{
    bool is_gbc_only = (rom_cgb_flag & 0xC0) == 0xC0;
    bool sgb_compatible = (sgb_flag == 0x03) && (old_licensee == 0x33) && !is_gbc_only;

    if (is_gbc_only)
        return GB_CONSOLE_CGB;
    if (sgb_compatible)
        return GB_CONSOLE_SGB;
    return GB_CONSOLE_DMG;
}

// --- App entry point --------------------------------------------------------
extern "C" void app_main(void);

void app_main(void)
{
    const rg_handlers_t handlers = {
        .loadState = &load_state_handler,
        .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .event = &event_handler,
        .options = &options_handler,
    };

    app = rg_system_init(AUDIO_SAMPLE_RATE, &handlers, NULL);

    // GB is 160x144; SGB border is 256x224. Allocate the larger size so one
    // surface handles both modes. RGB565 little-endian for the display queue.
    updates[0] = rg_surface_create(SGB_WIDTH, SGB_HEIGHT, RG_PIXEL_565_LE, MEM_FAST);
    updates[1] = rg_surface_create(SGB_WIDTH, SGB_HEIGHT, RG_PIXEL_565_LE, MEM_FAST);
    currentUpdate = updates[0];

    // Read ROM header to detect console mode.
    uint8_t rom_cgb_flag = 0, sgb_flag = 0, old_licensee = 0;
    {
        FILE *hf = fopen(app->romPath, "rb");
        if (hf)
        {
            if (fseek(hf, 0x143, SEEK_SET) == 0) fread(&rom_cgb_flag, 1, 1, hf);
            if (fseek(hf, 0x146, SEEK_SET) == 0) fread(&sgb_flag, 1, 1, hf);
            if (fseek(hf, 0x14B, SEEK_SET) == 0) fread(&old_licensee, 1, 1, hf);
            fclose(hf);
        }
    }
    gb_console_mode = detect_console_mode(rom_cgb_flag, sgb_flag, old_licensee);

    // Load ROM into memory (goes to PSRAM for large ROMs via the allocator).
    uint32_t rom_size = 0;
    byte *rom_data = load_rom(app->romPath, &rom_size);
    if (!rom_data)
        rg_system_rom_load_failed(_("Could not load the game file."));

    // Create the renderer + GB instance. The gb constructor takes
    // (renderer, enable_sound, enable_link).
    render = new rg_renderer(0);
    g_gb = new gb(render, true, false);

    // Apply user's console mode preference if the cart supports it.
    int saved_mode = (int)rg_settings_get_number(NS_APP, "GbSystem", gb_console_mode);
    if (saved_mode != gb_console_mode)
    {
        // Only override if the cart supports the requested mode.
        bool can_dmg = (rom_cgb_flag & 0xC0) != 0xC0;
        bool can_sgb = (sgb_flag == 0x03) && (old_licensee == 0x33) && can_dmg;
        if (saved_mode == GB_CONSOLE_DMG && can_dmg) gb_console_mode = saved_mode;
        else if (saved_mode == GB_CONSOLE_SGB && can_sgb) gb_console_mode = saved_mode;
        else if (saved_mode == GB_CONSOLE_CGB) gb_console_mode = saved_mode;
    }
    g_gb->set_console_mode(gb_console_mode);

    sgb_border_enabled = (int)rg_settings_get_number(NS_APP, "SgbBorder", 1) != 0;

    if (!g_gb->load_rom(rom_data, rom_size, NULL, 0, true))
        rg_system_rom_load_failed(_("ROM load failed."));

    // Load SRAM (battery-backed save).
    if (g_gb->get_rom()->has_battery())
    {
        const char *sram_path = rg_emu_get_path(RG_PATH_SAVE_SRAM, app->romPath);
        if (sram_path)
        {
            FILE *sf = fopen(sram_path, "rb");
            if (sf)
            {
                int ss = g_gb->get_rom()->get_sram_size();
                fread(g_gb->get_rom()->get_sram(), ss, 1, sf);
                fclose(sf);
            }
        }
    }

    // Apply DMG palette preference.
    if (g_gb->get_rom()->get_info()->gb_type == 1)
    {
        int pal = (int)rg_settings_get_number(NS_APP, "GbPalette", 0);
        g_gb->get_lcd()->set_palette(pal);
    }

    // Resume from save state.
    if (app->bootFlags & RG_BOOT_RESUME)
        rg_emu_load_state(app->saveSlot);

    rg_system_set_tick_rate(60);

    int skipFrames = 0;

    while (true)
    {
        g_gamepad = rg_input_read_gamepad();

        if (g_gamepad & (RG_KEY_MENU | RG_KEY_OPTION))
        {
            // Save SRAM before entering menu.
            if (g_gb->get_rom()->has_battery())
            {
                const char *sram_path = rg_emu_get_path(RG_PATH_SAVE_SRAM, app->romPath);
                if (sram_path)
                {
                    FILE *sf = fopen(sram_path, "wb");
                    if (sf)
                    {
                        fwrite(g_gb->get_rom()->get_sram(),
                               g_gb->get_rom()->get_sram_size(), 1, sf);
                        fclose(sf);
                    }
                }
            }
            if (g_gamepad & RG_KEY_MENU)
                rg_gui_game_menu();
            else
                rg_gui_options_menu();
        }

        int64_t startTime = rg_system_timer();
        bool drawFrame = !skipFrames;

        // Point the renderer at the current display surface.
        if (drawFrame)
        {
            currentUpdate = updates[currentUpdate == updates[0]];
            g_display_target = (uint16_t *)currentUpdate->data;
        }

        // Run one full frame: 154 scanlines (144 visible + 10 vblank).
        // The core calls render_screen and refresh during vblank.
        for (int line = 0; line < GB_LCD_LINES; line++)
            g_gb->run();

        if (drawFrame)
        {
            render->submit_frame();
            // Set the surface dimensions to match what the core rendered.
            // GB: 160x144, SGB with border: 256x224.
            int w = GB_WIDTH, h = GB_HEIGHT;
            if (gb_console_mode == GB_CONSOLE_SGB && sgb_border_enabled)
                w = SGB_WIDTH, h = SGB_HEIGHT;
            currentUpdate->width = w;
            currentUpdate->height = h;
            rg_display_submit(currentUpdate, 0);
        }

        render->submit_audio();

        rg_system_tick(rg_system_timer() - startTime);

        if (skipFrames == 0)
            skipFrames = app->frameskip;
        else if (skipFrames > 0)
            skipFrames--;
    }
}
