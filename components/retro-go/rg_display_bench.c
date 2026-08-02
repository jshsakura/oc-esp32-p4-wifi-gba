/**
 * How long does one frame's blit actually take, per system, on this board?
 *
 * The per-system table in docs/ROADMAP.md was measured by running real ROMs. That needs a
 * card with the right cartridges, buttons to pick one, and a screen to see what you picked.
 * This asks the same question with none of those: it builds a surface the size a given
 * system produces, submits it as fast as the display task will take it, and divides the
 * display task's own busy counter by the number of frames.
 *
 * It measures the real path -- rg_display_submit() into write_update(), whichever branch
 * that takes -- so the number moves when the blit changes, which is the entire point.
 *
 * Runs when /retro-go/display_bench exists on the card, and also when no panel answered --
 * a board with no screen is a board being brought up, and its numbers are the ones nobody
 * can read off a display. The marker is deleted before the run starts, so an answer can
 * never turn into a device that benchmarks forever.
 */
#include "rg_system.h"
#include "rg_display.h"
#include "rg_surface.h"

#include <string.h>

#define BENCH_MARKER RG_BASE_PATH_CONFIG "/display_bench"
#define BENCH_FRAMES 60

static const struct
{
    const char *name;
    int width, height;
} bench_sources[] = {
    {"PICO-8", 128, 128},
    {"GBA", 240, 160},
    {"NES/SNES", 256, 224},
    {"Mega Drive", 320, 224},
    {"Game & Watch", 320, 240},
};

static void bench_one(const char *name, int width, int height)
{
    // 8-bit paletted, which is what most cores hand over and the only format the PPA cannot
    // take directly -- so this is the expensive case, not the flattering one.
    rg_surface_t *surface = rg_surface_create(width, height, RG_PIXEL_PAL565_BE, MEM_SLOW);
    if (!surface)
    {
        RG_LOGE("%s: could not allocate a %dx%d surface", name, width, height);
        return;
    }

    for (int i = 0; i < 256; ++i)
        surface->palette[i] = (uint16_t)((i << 8) | (255 - i));

    // Content that differs every line and every frame: a still picture would let the
    // per-line checksums skip the work and report a blit that never happened.
    uint8_t *pixels = surface->data;

    int ppa_before = 0, cpu_before = 0;
    int64_t convert_before = 0, transfer_before = 0;
    rg_display_ppa_status(&ppa_before, &cpu_before);
    rg_display_ppa_split(&convert_before, &transfer_before);
    rg_display_sync(true);
    rg_display_counters_t before = rg_display_get_counters();
    int64_t wall_start = rg_system_timer();

    for (int frame = 0; frame < BENCH_FRAMES; ++frame)
    {
        for (int y = 0; y < height; ++y)
            memset(pixels + (size_t)y * surface->stride, (uint8_t)(y + frame), width);
        rg_display_submit(surface, 0);
    }
    rg_display_sync(true);

    int64_t wall = rg_system_timer() - wall_start;
    rg_display_counters_t after = rg_display_get_counters();
    int frames = after.totalFrames - before.totalFrames;
    if (frames < 1)
        frames = 1;

    int ppa_done = 0, ppa_skipped = 0;
    const char *why = rg_display_ppa_status(&ppa_done, &ppa_skipped);
    int on_cpu = ppa_skipped - cpu_before;
    int64_t convert_after = 0, transfer_after = 0;
    rg_display_ppa_split(&convert_after, &transfer_after);
    RG_LOGI("%-13s %3dx%-3d  blit %6.2f ms   expand %5.2f ms   dma %5.2f ms   ppa %d cpu %d%s%s",
            name, width, height,
            (after.busyTime - before.busyTime) / 1000.0f / frames,
            (convert_after - convert_before) / 1000.0f / frames,
            (transfer_after - transfer_before) / 1000.0f / frames,
            ppa_done - ppa_before, on_cpu,
            on_cpu ? "  <- " : "", on_cpu ? why : "");

    rg_surface_free(surface);
}

void rg_display_bench_run_if_requested(void)
{
    bool asked = rg_storage_exists(BENCH_MARKER);
    if (!asked && rg_display_has_panel())
        return;

    // Before, not after: if one of these sizes brings the device down, the next boot must
    // come up clean rather than repeat it.
    if (asked)
        rg_storage_delete(BENCH_MARKER);

    RG_LOGI("=== display blit benchmark (%d frames each, 8-bit paletted source) ===", BENCH_FRAMES);
    for (size_t i = 0; i < RG_COUNT(bench_sources); ++i)
        bench_one(bench_sources[i].name, bench_sources[i].width, bench_sources[i].height);

    // What does the quarter turn cost by itself? A DMA writing a rotated picture writes down
    // a column, so it touches a new cache line per pixel instead of filling one -- the same
    // reason the CPU transpose was the expensive half of the old blit. Priced on the one
    // source whose output (480x480) fits the framebuffer both ways round.
    rg_display_ppa_set_rotation(false);
    bench_one("^ unrotated", 128, 128);
    rg_display_ppa_set_rotation(true);
    RG_LOGI("=== display blit benchmark done ===");
}
