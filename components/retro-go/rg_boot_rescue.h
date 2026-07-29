#pragma once

#include <stdbool.h>
#include <stdint.h>

// Boot-loop rescue.
//
// The failure this exists for: something the firmware touches on the way up kills it, the
// chip resets, and it touches the same thing again. A bad auto-resume ROM, a setting that
// crashes the core that reads it, a half-written save. The device looks bricked, and every
// reboot lands in exactly the same place, because nothing remembers that the last two
// attempts also died here.
//
// So the boot counter lives in RTC memory, which survives a reset -- software, panic or
// watchdog -- but not a real loss of power. On a handheld with a hard power switch that is
// exactly the behaviour you want: a crash loop keeps counting, and a human flipping the
// switch off and on starts over.
//
// The counter is cleared only once the firmware has proven itself, either by running for a
// while or by shutting down deliberately. A boot that hangs never gets to clear it. That is
// the whole trick: absence of success is the signal, not presence of failure, so it catches
// hangs as readily as crashes.
//
// Ported from the Game & Watch retro-go fork, which uses an RTC backup register for the
// same purpose.

// Call as early in startup as possible, before anything that could fail.
void rg_boot_rescue_note_start(void);

// True when enough consecutive boots have died without proving themselves alive. The caller
// is expected to then skip whatever is most likely to be the culprit -- stored settings and
// the auto-resume -- and offer the user a way out.
bool rg_boot_rescue_due(void);

// How many boots in a row have failed to prove themselves, including this one.
int rg_boot_rescue_count(void);

// Declare this boot a success. Called automatically once the system has been alive long
// enough, and on any deliberate exit, restart or shutdown.
void rg_boot_rescue_clear(void);

// Feed the uptime check. Cheap; safe to call from the monitoring loop.
void rg_boot_rescue_tick(void);
