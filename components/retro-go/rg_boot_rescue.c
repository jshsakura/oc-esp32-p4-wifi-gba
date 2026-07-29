#include "rg_boot_rescue.h"
#include "rg_system.h"

#ifdef ESP_PLATFORM
#include <esp_attr.h>
#endif

// Three attempts, matching the Game & Watch fork: two boots have already died and this is
// the third. Two is too eager -- a single brownout while writing a save can cost one boot
// without anything actually being wrong.
#define RESCUE_THRESHOLD 3

// How long the firmware has to survive before its boot counts as a success. Long enough to
// have finished mounting storage, read settings and resumed a game, since those are the
// steps a rescue is meant to skip. A core that crashes five minutes into a game is a
// different problem and the panic handler already sends that one back to the launcher.
#define RESCUE_ALIVE_US (15 * 1000000)

// Not a version number, just an unlikely value. RTC memory comes up as whatever the last
// power cycle left behind, so the counter is only believable when this sits next to it.
#define RESCUE_MAGIC 0x0CB0071E

#ifdef ESP_PLATFORM
// NOINIT rather than DATA: the startup code must not zero these, or a reset would wipe the
// very thing they exist to remember.
RTC_NOINIT_ATTR static uint32_t rescue_magic;
RTC_NOINIT_ATTR static uint32_t rescue_count;
#else
static uint32_t rescue_magic;
static uint32_t rescue_count;
#endif

static bool cleared_this_boot = false;
static int64_t boot_time_us = 0;

void rg_boot_rescue_note_start(void)
{
    boot_time_us = rg_system_timer();

    if (rescue_magic != RESCUE_MAGIC)
    {
        // Either a genuine power cycle or the first boot after flashing. Either way the
        // previous contents are garbage and this boot starts the count.
        rescue_magic = RESCUE_MAGIC;
        rescue_count = 0;
    }

    rescue_count++;
    cleared_this_boot = false;

    if (rescue_count > 1)
        RG_LOGW("Boot attempt %d without a successful boot in between", (int)rescue_count);
}

bool rg_boot_rescue_due(void)
{
    return rescue_magic == RESCUE_MAGIC && rescue_count >= RESCUE_THRESHOLD;
}

int rg_boot_rescue_count(void)
{
    return (rescue_magic == RESCUE_MAGIC) ? (int)rescue_count : 0;
}

void rg_boot_rescue_clear(void)
{
    if (cleared_this_boot)
        return;
    if (rescue_count > 1)
        RG_LOGI("Boot declared successful, clearing rescue counter (was %d)", (int)rescue_count);
    rescue_magic = RESCUE_MAGIC;
    rescue_count = 0;
    cleared_this_boot = true;
}

void rg_boot_rescue_tick(void)
{
    if (cleared_this_boot || boot_time_us == 0)
        return;
    if (rg_system_timer() - boot_time_us >= RESCUE_ALIVE_US)
        rg_boot_rescue_clear();
}
