/*
 * Host side of the runtime-core-loading spike. Copied into an app's main/ to run
 * (see docs/APP_PARTITION_CEILING.md); kept here so it survives the app being
 * reverted.
 *
 * Runs the same benchmark twice: once compiled into this firmware, once as a
 * -fPIC shared object that elf_loader relocates into PSRAM and calls. The pair
 * prices what a runtime-loaded core actually costs, PIC and PSRAM together,
 * which is the honest unit -- a loaded core would be both.
 *
 * The module is embedded rather than read from SD only because this board's card
 * lives inside the case. The loader path is identical either way.
 */
#include <rg_system.h>
#include <string.h>

#include "esp_elf.h"

extern const uint8_t bench_so_start[] asm("_binary_bench_so_start");
extern const uint8_t bench_so_end[]   asm("_binary_bench_so_end");

uint32_t elfspike_bench(uint32_t seed);

static int64_t time_static(uint32_t *out)
{
    int64_t t0 = rg_system_timer();
    *out = elfspike_bench(0x12345678u);
    return rg_system_timer() - t0;
}

static int64_t time_module(uint32_t *out, int *err)
{
    esp_elf_t elf;
    char *argv[1] = {NULL};

    *err = esp_elf_init(&elf);
    if (*err != 0)
        return -1;

    /* Relocation is load-time and deliberately not counted in the comparison:
     * it happens once per core launch, where milliseconds disappear next to
     * reading a ROM. It is logged because how it scales with module size is a
     * separate thing worth knowing. */
    int64_t tr0 = rg_system_timer();
    *err = esp_elf_relocate(&elf, bench_so_start);
    int64_t reloc = rg_system_timer() - tr0;
    if (*err != 0)
    {
        esp_elf_deinit(&elf);
        return -1;
    }
    RG_LOGW("ELFSPIKE: relocate took %lld us", reloc);

    int64_t t0 = rg_system_timer();
    int rc = esp_elf_request(&elf, 0, 0, argv);
    int64_t dt = rg_system_timer() - t0;

    *out = (uint32_t)rc;
    esp_elf_deinit(&elf);
    return dt;
}

void elfspike_run(void)
{
    uint32_t sum_s = 0, sum_m = 0;
    int err = 0;

    RG_LOGW("ELFSPIKE: module is %u bytes", (unsigned)(bench_so_end - bench_so_start));

    /* A/B/A, same reason as every other measurement on this board: the part
     * warms up, and one ordered pair cannot tell drift from effect. */
    int64_t a1 = time_static(&sum_s);
    int64_t b1 = time_module(&sum_m, &err);
    int64_t a2 = time_static(&sum_s);
    int64_t b2 = time_module(&sum_m, &err);

    if (err != 0 || b1 < 0 || b2 < 0)
    {
        RG_LOGE("ELFSPIKE: module load failed, err=%d", err);
        return;
    }
    if (sum_s != sum_m)
    {
        RG_LOGE("ELFSPIKE: checksum mismatch, static=0x%08x module=0x%08x -- not "
                "the same work, timings mean nothing", (unsigned)sum_s, (unsigned)sum_m);
        return;
    }

    int64_t a = (a1 + a2) / 2, b = (b1 + b2) / 2;
    RG_LOGW("ELFSPIKE: static/host  %lld us  (%lld, %lld)", a, a1, a2);
    RG_LOGW("ELFSPIKE: loaded/PSRAM %lld us  (%lld, %lld)", b, b1, b2);
    RG_LOGW("ELFSPIKE: loaded costs %lld%% more   checksum 0x%08x",
            a ? ((b - a) * 100) / a : 0, (unsigned)sum_s);
}
