#include <rg_system.h>
#include <string.h>
#include <stdlib.h>

#include "applications.h"
#include "gui.h"

// Layout scales with the display so it looks right on both 320x240 and 800x480.
// The base values target 240px height (the original ST7789 target); every other
// resolution grows proportionally from there.
#define HEADER_HEIGHT       (gui.height * 50 / 240)
#define LOGO_WIDTH          (gui.height * 46 / 240)
// Extra gap between list rows for the open, breathable layout the Analogue OS
// menu uses. Only applies above the 240px panel the original layout was tuned
// for, so existing 320x240 targets keep their original list density.
#define LINE_GAP            (RG_MAX((gui.height - 240) / 24, 0))
#define PREVIEW_HEIGHT      ((int)(gui.height * 0.70f))
#define PREVIEW_WIDTH       ((int)(gui.width * 0.50f))

retro_gui_t gui;

#define SETTING_SELECTED_TAB    "SelectedTab"
#define SETTING_START_SCREEN    "StartScreen"
#define SETTING_STARTUP_MODE    "StartupMode"
#define SETTING_LANGUAGE        "Language"
#define SETTING_COLOR_THEME     "ColorTheme"
#define SETTING_SHOW_PREVIEW    "ShowPreview"
#define SETTING_SCROLL_MODE     "ScrollMode"
#define SETTING_HIDE_TAB(name)  strcat((char[99]){"HideTab."}, (name))

static int max_visible_lines(const tab_t *tab, int *_line_height)
{
    int line_height = TEXT_RECT("ABC123", 0).height;
    if (_line_height) *_line_height = line_height;
    int row = RG_MAX(line_height + LINE_GAP, 1);
    return RG_MAX((gui.height - (HEADER_HEIGHT + 6) - (tab->navpath ? line_height : 0)) / row, 1);
}

void gui_init(bool cold_boot)
{
    gui = (retro_gui_t){
        .selected_tab = rg_settings_get_number(NS_APP, SETTING_SELECTED_TAB, 0),
        .startup_mode = rg_settings_get_number(NS_APP, SETTING_STARTUP_MODE, 0),
        .language     = rg_settings_get_number(NS_APP, SETTING_LANGUAGE, 0),
        .color_theme  = rg_settings_get_number(NS_APP, SETTING_COLOR_THEME, 0),
        .start_screen = rg_settings_get_number(NS_APP, SETTING_START_SCREEN, START_SCREEN_AUTO),
        .show_preview = rg_settings_get_number(NS_APP, SETTING_SHOW_PREVIEW, PREVIEW_MODE_SAVE_COVER),
        .scroll_mode  = rg_settings_get_number(NS_APP, SETTING_SCROLL_MODE, SCROLL_MODE_CENTER),
        .width        = rg_display_get_width(),
        .height       = rg_display_get_height(),
    };
    // Auto: Show carousel on cold boot, browser on warm boot (after cleanly exiting an emulator)
    gui.browse = gui.start_screen == START_SCREEN_BROWSER || (gui.start_screen == START_SCREEN_AUTO && !cold_boot);
    gui.theme = &gui.themes[gui.color_theme % RG_COUNT(gui.themes)];
    gui.http_lock = false;
    gui.low_memory_mode = rg_system_get_app()->lowMemoryMode;
    gui.surface = rg_surface_create(gui.width, gui.height, RG_PIXEL_565_LE, MEM_SLOW);
    gui_update_theme();
}

void gui_event(gui_event_t event, tab_t *tab)
{
    if (tab && tab->event_handler)
        (*tab->event_handler)(event, tab);
}

tab_t *gui_add_tab(const char *name, const char *desc, void *arg, void *event_handler)
{
    RG_ASSERT_ARG(name && desc);

    tab_t *tab = calloc(1, sizeof(tab_t));

    snprintf(tab->name, sizeof(tab->name), "%s", name);
    snprintf(tab->desc, sizeof(tab->desc), "%s", desc);
    sprintf(tab->status[1].left, "Loading...");

    tab->event_handler = event_handler;
    tab->initialized = false;
    tab->enabled = !rg_settings_get_number(NS_APP, SETTING_HIDE_TAB(name), 0);
    tab->has_roms = true; // default visible; per-system tabs clear this on scan
    tab->arg = arg;
    tab->listbox = (listbox_t){
        .items = calloc(10, sizeof(listbox_item_t)),
        .capacity = 10,
        .length = 0,
        .cursor = 0,
        .sort_mode = SORT_TEXT_ASC,
    };

    gui.tabs[gui.tabs_count++] = tab;

    RG_LOGI("Tab '%s' added at index %d\n", tab->name, gui.tabs_count - 1);

    return tab;
}

void gui_init_tab(tab_t *tab)
{
    if (!tab || tab->initialized)
        return;

    tab->initialized = true;
    // tab->status[0] = 0;

    gui_event(TAB_INIT, tab);
    gui_scroll_list(tab, SCROLL_SET, tab->listbox.cursor);
}

void gui_deinit_tab(tab_t *tab)
{
    if (!tab || !tab->initialized)
        return;

    // FIXME: Maybe the other images should be freed too?
    gui_event(TAB_DEINIT, tab);
    gui_set_preview(tab, NULL);
    gui_resize_list(tab, 10);

    tab->initialized = false;
}

tab_t *gui_get_tab(int index)
{
    return (index >= 0 && index < gui.tabs_count) ? gui.tabs[index] : NULL;
}

void gui_invalidate(void)
{
    for (size_t i = 0; i < gui.tabs_count; ++i)
        gui_deinit_tab(gui.tabs[i]);
    // Kick the user out of the tab and only re-init upon manual re-entry
    gui.browse = false;
    // gui_init_tab(gui_get_current_tab());
}

rg_image_t *gui_get_image(const char *type, const char *subtype)
{
    char name[64];

    if (gui.low_memory_mode)
        return NULL;

    if (subtype && *subtype)
        snprintf(name, sizeof(name), "%s_%s.png", type, subtype);
    else
        snprintf(name, sizeof(name), "%s.png", type);

    // Try to get image from theme
    rg_image_t *img = rg_gui_get_theme_image(name);
    if (img)
        return img;

    // Then fallback to built-in images
    for (const binfile_t **img = builtin_images; *img; img++)
    {
        if (strcmp((*img)->name, name) == 0)
            return rg_surface_load_image((*img)->data, (*img)->size, 0);
    }

    return NULL;
}

tab_t *gui_get_current_tab(void)
{
    tab_t *tab = gui_get_tab(gui.selected_tab);
    if (!tab)
        RG_LOGE("current tab is NULL!");
    return tab;
}

tab_t *gui_set_current_tab(int index)
{
    tab_t *prev_tab = gui_get_tab(gui.selected_tab);
    tab_t *curr_tab;

    index %= (int)gui.tabs_count;

    if (index < 0)
        index += gui.tabs_count;

    gui.selected_tab = index;

    curr_tab = gui_get_tab(gui.selected_tab);

    if (prev_tab && prev_tab != curr_tab)
    {
        // FIXME: We should recompress the images rather than fully free them, because if a custom theme is
        //        used then it means that we're constantly reloading from SD Card which is very slow...
        rg_surface_free(prev_tab->background), prev_tab->background = NULL;
        // rg_surface_free(prev_tab->banner), prev_tab->banner = NULL;
        // rg_surface_free(prev_tab->logo), prev_tab->logo = NULL;
    }

    return curr_tab;
}

void gui_set_status(tab_t *tab, const char *left, const char *right)
{
    if (!tab)
        tab = gui_get_current_tab();
    if (tab && left)
        strcpy(tab->status[1].left, left);
    if (tab && right)
        strcpy(tab->status[1].right, right);
}

void gui_update_theme(void)
{
    // Load our four color schemes from gui theme
    // Variant 0 is the Analogue OS-inspired dark-minimal scheme: pure black
    // background, near-white unselected text, and an inverted (white bar / black
    // text) selection row. See themes/default/theme.json.
    gui.themes[0].background = rg_gui_get_theme_color("launcher_1", "background", C_BLACK);
    gui.themes[0].foreground = rg_gui_get_theme_color("launcher_1", "foreground", C_WHITE);
    gui.themes[0].list.standard_bg = rg_gui_get_theme_color("launcher_1", "list_standard_bg", C_TRANSPARENT);
    gui.themes[0].list.standard_fg = rg_gui_get_theme_color("launcher_1", "list_standard_fg", C_WHITE);
    gui.themes[0].list.selected_bg = rg_gui_get_theme_color("launcher_1", "list_selected_bg", C_WHITE);
    gui.themes[0].list.selected_fg = rg_gui_get_theme_color("launcher_1", "list_selected_fg", C_BLACK);

    gui.themes[1].background = rg_gui_get_theme_color("launcher_2", "background", C_BLACK);
    gui.themes[1].foreground = rg_gui_get_theme_color("launcher_2", "foreground", C_SNOW);
    gui.themes[1].list.standard_bg = rg_gui_get_theme_color("launcher_2", "list_standard_bg", C_TRANSPARENT);
    gui.themes[1].list.standard_fg = rg_gui_get_theme_color("launcher_2", "list_standard_fg", C_GRAY);
    gui.themes[1].list.selected_bg = rg_gui_get_theme_color("launcher_2", "list_selected_bg", C_TRANSPARENT);
    gui.themes[1].list.selected_fg = rg_gui_get_theme_color("launcher_2", "list_selected_fg", C_GREEN);

    gui.themes[2].background = rg_gui_get_theme_color("launcher_3", "background", C_BLACK);
    gui.themes[2].foreground = rg_gui_get_theme_color("launcher_3", "foreground", C_SNOW);
    gui.themes[2].list.standard_bg = rg_gui_get_theme_color("launcher_3", "list_standard_bg", C_TRANSPARENT);
    gui.themes[2].list.standard_fg = rg_gui_get_theme_color("launcher_3", "list_standard_fg", C_GRAY);
    gui.themes[2].list.selected_bg = rg_gui_get_theme_color("launcher_3", "list_selected_bg", C_WHITE);
    gui.themes[2].list.selected_fg = rg_gui_get_theme_color("launcher_3", "list_selected_fg", C_BLACK);

    gui.themes[3].background = rg_gui_get_theme_color("launcher_4", "background", C_BLACK);
    gui.themes[3].foreground = rg_gui_get_theme_color("launcher_4", "foreground", C_SNOW);
    gui.themes[3].list.standard_bg = rg_gui_get_theme_color("launcher_4", "list_standard_bg", C_TRANSPARENT);
    gui.themes[3].list.standard_fg = rg_gui_get_theme_color("launcher_4", "list_standard_fg", C_DARK_GRAY);
    gui.themes[3].list.selected_bg = rg_gui_get_theme_color("launcher_4", "list_selected_bg", C_WHITE);
    gui.themes[3].list.selected_fg = rg_gui_get_theme_color("launcher_4", "list_selected_fg", C_BLACK);

    // Flush our image cache to make sure the new images are loaded next time
    for (size_t i = 0; i < gui.tabs_count; ++i)
    {
        tab_t *tab = gui.tabs[i];
        rg_surface_free(tab->background), tab->background = NULL;
        tab->background_suppressed = false; // a theme change re-evaluates backgrounds
        rg_surface_free(tab->banner), tab->banner = NULL;
        rg_surface_free(tab->logo), tab->logo = NULL;
    }
}

void gui_save_config(void)
{
    rg_settings_set_number(NS_APP, SETTING_SELECTED_TAB, gui.selected_tab);
    rg_settings_set_number(NS_APP, SETTING_START_SCREEN, gui.start_screen);
    rg_settings_set_number(NS_APP, SETTING_SHOW_PREVIEW, gui.show_preview);
    rg_settings_set_number(NS_APP, SETTING_SCROLL_MODE, gui.scroll_mode);
    rg_settings_set_number(NS_APP, SETTING_COLOR_THEME, gui.color_theme);
    rg_settings_set_number(NS_APP, SETTING_STARTUP_MODE, gui.startup_mode);
    for (int i = 0; i < gui.tabs_count; i++)
        rg_settings_set_number(NS_APP, SETTING_HIDE_TAB(gui.tabs[i]->name), !gui.tabs[i]->enabled);
    rg_settings_commit();
}

listbox_item_t *gui_get_selected_item(tab_t *tab)
{
    if (tab && gui.browse)
    {
        listbox_t *list = &tab->listbox;
        if (list->cursor >= 0 && list->cursor < list->length)
            return &list->items[list->cursor];
    }
    return NULL;
}

static int list_comp_text_asc(const listbox_item_t *a, const listbox_item_t *b)
{
    return a->group == b->group ? strcasecmp(a->text, b->text) : ((int)a->group - b->group);
}

static int list_comp_text_desc(const listbox_item_t *a, const listbox_item_t *b)
{
    return a->group == b->group ? strcasecmp(b->text, a->text) : ((int)a->group - b->group);
}

static int list_comp_id_asc(const listbox_item_t *a, const listbox_item_t *b)
{
    return a->group == b->group ? ((int)a->order - b->order) : ((int)a->group - b->group);
}

static int list_comp_id_desc(const listbox_item_t *a, const listbox_item_t *b)
{
    return a->group == b->group ? ((int)b->order - a->order) : ((int)a->group - b->group);
}

void gui_sort_list(tab_t *tab)
{
    void *comp[] = {&list_comp_id_asc, &list_comp_id_desc, &list_comp_text_asc, &list_comp_text_desc};
    size_t sort_mode = tab->listbox.sort_mode - 1;

    if (!tab->listbox.length || sort_mode > RG_COUNT(comp) - 1)
        return;

    qsort((void*)tab->listbox.items, tab->listbox.length, sizeof(listbox_item_t), comp[sort_mode]);
}

void gui_resize_list(tab_t *tab, int new_size)
{
    listbox_t *list = &tab->listbox;

    if (new_size == list->length)
        return;

    // Always grow but only shrink past a certain threshold
    if (new_size >= list->capacity || list->capacity - new_size >= 20)
    {
        list->capacity = new_size + 10;
        list->items = realloc(list->items, list->capacity * sizeof(listbox_item_t));
        RG_LOGI("Resized list '%s' from %d to %d items (new capacity: %d)\n",
            tab->name, list->length, new_size, list->capacity);
    }

    for (int i = list->length; i < list->capacity; i++)
        memset(&list->items[i], 0, sizeof(listbox_item_t));

    list->length = new_size;

    if (list->cursor >= new_size)
        list->cursor = new_size ? new_size - 1 : 0;
}

void gui_scroll_list(tab_t *tab, scroll_whence_t mode, int arg)
{
    listbox_t *list = &tab->listbox;
    int list_length = list->length;
    int old_cursor = list->cursor;
    int new_cursor = RG_MAX(RG_MIN(old_cursor, list_length - 1), 0);

    if (list_length == 0)
    {
        // new_cursor = -1;
        new_cursor = 0;
    }
    else if (mode == SCROLL_SET)
    {
        new_cursor = arg;
    }
    else if (mode == SCROLL_LINE)
    {
        new_cursor += arg;
        // In line mode we wrap around
        if (new_cursor > list_length - 1)
            new_cursor = 0;
        else if (new_cursor < 0)
            new_cursor = list_length - 1;
    }
    else if (mode == SCROLL_PAGE)
    {
        new_cursor += arg * max_visible_lines(tab, NULL);
        // In page mode we stop at the edges
        if (new_cursor > list_length - 1)
            new_cursor = list_length - 1;
        else if (new_cursor < 0)
            new_cursor = 0;
    }

    // Check for invalid cursor
    if (new_cursor < 0 || new_cursor > list_length - 1)
    {
        RG_LOGW("Invalid cursor position: %d, list length: %d", new_cursor, list_length);
        new_cursor = 0; // -1;
    }

    if (list_length > 0 && list->items[new_cursor].arg)
        sprintf(tab->status[0].left, "%d / %d", (new_cursor + 1) % 10000, list_length % 10000);
    else
        strcpy(tab->status[0].left, "List empty");

    // if (new_cursor != old_cursor)
    {
        list->cursor = new_cursor;
        gui_event(TAB_SCROLL, tab);
    }
}

void gui_redraw(void)
{
    rg_display_sync(true);
    rg_gui_set_surface(gui.surface);

    tab_t *tab = gui_get_current_tab();
    if (!tab)
    {
        RG_LOGW("No tab to redraw...");
    }
    else if (gui.browse)
    {
        gui_draw_background(tab, 4);
        gui_draw_header(tab, 0);
        gui_draw_status(tab);
        gui_draw_list(tab);
        gui_draw_preview(tab);
        gui_draw_playtime(tab);
    }
    else
    {
        gui_draw_background(tab, 0);
        gui_draw_header(tab, (gui.height - HEADER_HEIGHT) / 2);
        // gui_draw_tab_indicator();
    }

    rg_gui_set_surface(NULL);
    rg_display_submit(gui.surface, 0);
}

void gui_draw_preview(tab_t *tab)
{
    if (!tab->preview)
        return;

    // Fit the cover art inside the preview box preserving its aspect ratio,
    // rather than stretching it to fill the box. Portrait box art (the common
    // case) would otherwise be squashed wide. The letterbox fills with the
    // theme background, which on the dark-minimal theme is black.
    float scale = RG_MIN((float)PREVIEW_WIDTH / tab->preview->width,
                         (float)PREVIEW_HEIGHT / tab->preview->height);
    int width = (int)(tab->preview->width * scale);
    int height = (int)(tab->preview->height * scale);

    // Centre the fitted cover in the right-half preview region so it reads as
    // intentional negative space rather than art crammed into the corner.
    // Horizontal: centred in [width/2, width]. Vertical: centred below the header.
    int px = gui.width / 2 + (gui.width / 2 - width) / 2;
    int py = HEADER_HEIGHT + (gui.height - HEADER_HEIGHT - height) / 2;
    rg_gui_draw_image(px, py, width, height, true, tab->preview);
}

void gui_draw_playtime(tab_t *tab)
{
    // Show the selected game's accumulated playtime under the preview area,
    // centred in the right half. Stored by the emulator on exit, keyed by the
    // ROM basename (see rg_system_switch_app).
    listbox_item_t *item = gui_get_selected_item(tab);
    if (!item || !item->arg)
        return;

    retro_file_t *file = item->arg;
    char key[48];
    snprintf(key, sizeof(key), "Playtime.%s", file->name);
    int seconds = rg_settings_get_number(NS_GLOBAL, key, 0);
    if (seconds <= 0)
        return;

    char buf[24];
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    if (h > 0)
        snprintf(buf, sizeof(buf), "%dh %dm", h, m);
    else if (m > 0)
        snprintf(buf, sizeof(buf), "%dm", m);
    else
        snprintf(buf, sizeof(buf), "%ds", seconds);

    int y = gui.height - TEXT_RECT(buf, 0).height - 6;
    rg_gui_draw_text(gui.width / 2, y, gui.width / 2, buf, C_DIM_GRAY, C_TRANSPARENT, RG_TEXT_ALIGN_CENTER);
}

void gui_draw_background(tab_t *tab, int shade)
{
    // We can't losslessly change shade, must reload!
    if (tab->background && tab->background_shade > 0 && tab->background_shade != shade)
    {
        rg_surface_free(tab->background);
        tab->background = NULL;
    }

    if (!tab->background && !tab->background_suppressed)
    {
        tab->background = gui_get_image("background", tab->name); // Try background_<tabname>.png
        if (!tab->background)
            tab->background = gui_get_image("background", NULL); // Fallback to a background.png
        tab->background_shade = 0;
        // The bundled backgrounds were drawn for 320x240. On a larger panel they
        // upscale into a soft, muddy wash that fights the minimal aesthetic, so
        // drop them in favour of the solid theme colour unless they are close to
        // the native resolution. A high-res theme can still supply its own art.
        if (tab->background && tab->background->width < gui.width * 3 / 4)
        {
            rg_surface_free(tab->background);
            tab->background = NULL;
            tab->background_suppressed = true; // do not re-decode this every redraw
        }
        if (tab->background && (tab->background->width != gui.width || tab->background->height != gui.height))
        {
            rg_image_t *temp = rg_surface_resize(tab->background, gui.width, gui.height);
            if (temp)
            {
                rg_surface_free(tab->background);
                tab->background = temp;
            }
        }
    }

    if (tab->background && tab->background_shade != shade && shade > 0)
    {
        rg_image_t *img = tab->background;
        for (int y = 0; y < img->height; ++y)
        {
            uint16_t *line = img->data + y * img->stride;
            for (int x = 0; x < img->width; ++x)
            {
                int pixel = line[x];
                int r = ((pixel >> 11) & 0x1F) / shade;
                int g = ((pixel >> 5) & 0x3F) / shade;
                int b = ((pixel) & 0x1F) / shade;
                line[x] = ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | ((b & 0x1F) << 0);
            }
        }
        tab->background_shade = shade;
    }

    if (tab->background)
        rg_gui_draw_image(0, 0, gui.width, gui.height, false, tab->background);
    else
        rg_gui_draw_rect(0, 0, gui.width, gui.height, 0, 0, gui.theme->background);
}

void gui_draw_header(tab_t *tab, int offset)
{
    if (!tab->banner)
        tab->banner = gui_get_image("banner", tab->name);
    if (!tab->logo)
        tab->logo = gui_get_image("logo", tab->name);

    // The bundled logos/banners were drawn for 320x240. On a larger panel they
    // upscale soft, which fights the minimal aesthetic, so when the art is too
    // low-res we fall back to a clean text system name -- the Analogue OS approach.
    bool hires_art = tab->logo && tab->logo->width >= LOGO_WIDTH * 3 / 4;

    if (hires_art)
    {
        rg_gui_draw_image(0, offset, LOGO_WIDTH, HEADER_HEIGHT, false, tab->logo);
        if (tab->banner)
            rg_gui_draw_image(LOGO_WIDTH + 1, offset + 8, 0, HEADER_HEIGHT - 8, false, tab->banner);
        else
            rg_gui_draw_text(LOGO_WIDTH + 8, offset + 8, 0, tab->desc, gui.theme->foreground, C_TRANSPARENT, RG_TEXT_BIGGER);
    }
    else
    {
        // Minimal text header: system name inset to line up with the list gutter.
        // Native font size (no RG_TEXT_BIGGER) -- BIGGER only stretches glyphs
        // vertically, which distorts the title. The 24px face is already large
        // and crisp on this panel.
        int x = gui.width / 24;
        rg_gui_draw_text(x, offset + HEADER_HEIGHT / 4, 0, tab->desc,
                         gui.theme->foreground, C_TRANSPARENT, 0);
    }
}

void gui_draw_tab_indicator(void)
{
    char buffer[64] = {0};
    memset(buffer, '-', gui.tabs_count);
    rg_gui_draw_text(RG_GUI_CENTER, RG_GUI_BOTTOM, 0, buffer, C_DIM_GRAY, C_TRANSPARENT, RG_TEXT_BIGGER|RG_TEXT_MONOSPACE);
    memset(buffer, ' ', gui.tabs_count);
    buffer[gui.selected_tab] = '-';
    rg_gui_draw_text(RG_GUI_CENTER, RG_GUI_BOTTOM, 0, buffer, gui.theme->foreground, C_TRANSPARENT, RG_TEXT_BIGGER|RG_TEXT_MONOSPACE);
}

void gui_draw_status(tab_t *tab)
{
    // Anchor the status row to the font height, not a fixed constant: the old
    // HEADER_HEIGHT-16 was tuned for an ~8px font and collided with the first
    // list row once the font grew. Keeping the whole row inside the header band
    // leaves the list area clear below it.
    const int line_height = TEXT_RECT("A", 0).height;
    const int status_x = gui.width / 24;
    const int status_y = HEADER_HEIGHT - line_height - 4;
    char *txt_left = tab->status[tab->status[1].left[0] ? 1 : 0].left;
    char *txt_right = tab->status[tab->status[1].right[0] ? 1 : 0].right;

    rg_gui_draw_text(status_x, status_y, gui.width - status_x, txt_right, gui.theme->foreground, C_TRANSPARENT, RG_TEXT_ALIGN_RIGHT);
    rg_gui_draw_text(status_x, status_y, 0, txt_left, gui.theme->foreground, C_TRANSPARENT, RG_TEXT_ALIGN_LEFT);
    rg_gui_draw_icons();
}

void gui_draw_list(tab_t *tab)
{
    rg_color_t fg[2] = {gui.theme->list.standard_fg, gui.theme->list.selected_fg};
    rg_color_t bg[2] = {gui.theme->list.standard_bg, gui.theme->list.selected_bg};

    const listbox_t *list = &tab->listbox;
    int line_height, top = HEADER_HEIGHT + 6;
    int lines = max_visible_lines(tab, &line_height);
    int line_offset = 0;

    // The list sits inside horizontal margins so the selection bar can breathe
    // away from the screen edges, the way the Analogue OS menu insets its rows.
    int list_x = gui.width / 24;          // ~4% gutter each side
    int list_w = gui.width - list_x * 2;
    // Reserve the right half for the cover whenever preview mode is on and the
    // preview can actually be loaded, even before the current item's art arrives,
    // so the list width does not jump when it does. In low-memory mode previews
    // are never loaded, so keep the full width there.
    if (!gui.low_memory_mode && gui.show_preview != PREVIEW_MODE_NONE)
        list_w = gui.width / 2 - list_x;
    int bar_pad = RG_MAX(list_w / 40, 4); // inner padding of the selection bar
    int radius = RG_MAX(line_height / 4, 2);

    if (tab->navpath)
    {
        char buffer[64];
        snprintf(buffer, 63, "[%s]",  tab->navpath);
        top += rg_gui_draw_text(list_x, top, list_w, buffer, gui.theme->foreground, C_TRANSPARENT, 0).height;
    }

    top += ((gui.height - top) - (lines * (line_height + LINE_GAP))) / 2;

    if (gui.scroll_mode == SCROLL_MODE_PAGING)
    {
        line_offset = (list->cursor / lines) * lines;
    }
    else // (gui.scroll_mode == SCROLL_MODE_CENTER)
    {
        line_offset = list->cursor - (lines / 2);
    }

    for (int i = 0; i < lines; i++)
    {
        int idx = line_offset + i;
        int selected = idx == list->cursor;
        char *label = (idx >= 0 && idx < list->length) ? list->items[idx].text : "";

        // Draw the selection bar first, then the text on top with a transparent
        // background so the rounded bar shows through around the glyphs.
        if (selected && bg[selected] != C_TRANSPARENT)
            rg_gui_draw_rounded_rect(list_x, top, list_w, line_height, radius, bg[selected]);

        rg_gui_draw_text(list_x + bar_pad, top, list_w - bar_pad * 2, label, fg[selected], C_TRANSPARENT, 0);
        top += line_height + LINE_GAP;
    }
}

void gui_set_preview(tab_t *tab, rg_image_t *preview)
{
    if (!tab)
        return;

    if (tab->preview)
        rg_surface_free(tab->preview);

    tab->preview = preview;
}

void gui_load_preview(tab_t *tab)
{
    listbox_item_t *item = gui_get_selected_item(tab);
    bool show_missing_cover = false;
    uint32_t order;

    gui_set_preview(tab, NULL);

    if (!item || !item->arg || gui.low_memory_mode)
        return;

    switch (gui.show_preview)
    {
        case PREVIEW_MODE_COVER_SAVE:
            show_missing_cover = true;
            order = 0x4123;
            break;
        case PREVIEW_MODE_SAVE_COVER:
            show_missing_cover = true;
            order = 0x1234;
            break;
        case PREVIEW_MODE_COVER_ONLY:
            show_missing_cover = true;
            order = 0x0123;
            break;
        case PREVIEW_MODE_SAVE_ONLY:
            show_missing_cover = false;
            order = 0x0004;
            break;
        default:
            show_missing_cover = false;
            order = 0x0000;
    }

    retro_file_t *file = item->arg;
    retro_app_t *app = file->app;
    uint32_t errors = 0;

    while (order && !tab->preview)
    {
        char path[RG_PATH_MAX + 1];
        size_t path_len = 0;
        int type = order & 0xF;

        order >>= 4;

        // Give up on any button press to improve responsiveness
        if ((gui.joystick |= rg_input_read_gamepad()))
            break;

        if (file->missing_cover & (1 << type))
            continue;

        if (type == 0x1 && app->use_crc_covers && application_get_file_crc32(file)) // Game cover (old format)
            path_len = snprintf(path, RG_PATH_MAX, "%s/%X/%08X.art", app->paths.covers, (int)(file->checksum >> 28), (int)file->checksum);
        else if (type == 0x2 && app->use_crc_covers && application_get_file_crc32(file)) // Game cover (png)
            path_len = snprintf(path, RG_PATH_MAX, "%s/%X/%08X.png", app->paths.covers, (int)(file->checksum >> 28), (int)file->checksum);
        else if (type == 0x3) // Game cover (based on filename)
        {
            path_len = snprintf(path, RG_PATH_MAX, "%s/%s", app->paths.covers, file->name);
            if (path_len < RG_PATH_MAX - 3) // Don't bother if we already have an overflow
                strcpy(path + path_len - strlen(rg_extension(file->name) ?: ""), "png");
        }
        else if (type == 0x4 && file->saves > 0) // Save state screenshot (png)
        {
            snprintf(path, RG_PATH_MAX, "%s/%s", file->folder, file->name);
            uint8_t last_used_slot = rg_emu_get_last_used_slot(path);
            if (last_used_slot != 0xFF)
            {
                char *preview = rg_emu_get_path(RG_PATH_SCREENSHOT + last_used_slot, path);
                path_len = snprintf(path, RG_PATH_MAX, "%s", preview);
                free(preview);
            }
        }

        if (path_len > 0 && path_len < RG_PATH_MAX)
        {
            RG_LOGD("Looking for %s", path);
            gui_set_preview(tab, rg_surface_load_image_file(path, 0));
            // if (!tab->preview && rg_storage_exists(path))
            //     errors++;
        }

        file->missing_cover |= (tab->preview ? 0 : 1) << type;
    }

    if (!tab->preview && file->checksum && (show_missing_cover || errors))
    {
        RG_LOGI("No image found for '%s'\n", file->name);
        gui_set_status(tab, NULL, errors ? "Bad cover" : "No cover");
        // gui_draw_status(tab);
        // tab->preview = gui_get_image("cover", file->app);
    }
}
