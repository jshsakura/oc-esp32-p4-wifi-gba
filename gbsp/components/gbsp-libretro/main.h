/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>

#define TIMER_INACTIVE                0
#define TIMER_PRESCALE                1
#define TIMER_CASCADE                 2

#define TIMER_NO_IRQ                  0
#define TIMER_TRIGGER_IRQ             1

#define TIMER_DS_CHANNEL_NONE         0
#define TIMER_DS_CHANNEL_A            1
#define TIMER_DS_CHANNEL_B            2
#define TIMER_DS_CHANNEL_BOTH         3

typedef struct
{
  s32 count;
  u32 reload;
  u32 prescale;
  fixed8_24 frequency_step;
  u32 direct_sound_channels;
  u32 irq;
  u32 status;
} timer_type;

typedef enum
{
  no_frameskip = 0,
  auto_frameskip,
  auto_threshold_frameskip,
  fixed_interval_frameskip
} frameskip_type;

typedef enum
{
  auto_detect = 0,
  builtin_bios,
  official_bios
} bios_type;

typedef enum
{
  boot_game = 0,
  boot_bios
} boot_mode;

extern u32 frame_counter;
extern u32 cpu_ticks;
extern u32 execute_cycles;
extern u32 skip_next_frame;

extern u32 flush_ram_count;

extern char main_path[512];

u16 rand_gen();
void rand_seed(u32 data);

#define cycles_to_run(c) ((c) & 0x7FFF)
#define completed_frame(c) ((c) & 0x80000000)
u32 function_cc update_gba(int remaining_cycles);
void reset_gba(void);

void init_main(void);

void game_name_ext(char *src, char *buffer, char *extension);

bool main_check_savestate(const u8 *src);
unsigned main_write_savestate(u8* ptr);
bool main_read_savestate(const u8 *src);

#define MAX_TRANSLATION_GATES 3

extern u32 idle_loop_target_pc;
/* When to burn the slice at idle_loop_target_pc.
 *
 * ALWAYS is the classic table semantic: the pc is inside a loop that only an
 * interrupt can release, so any arrival means waiting.
 *
 * WHEN_NE exists for the OTHER shape of wait — a raster poll,
 * `ldrh rN,[VCOUNT]; cmp rN,#line; bne back` — whose callers routinely burst
 * through it while the compare already matches (Super Robot Taisen D delays by
 * CALLING the poll N times; on hardware ~120 of those calls fit inside the
 * matching scanline). Park the target on the BRANCH and burn the slice only
 * while Z says it will loop back; a pass-through costs its natural handful of
 * cycles instead of a whole slice, which is the difference between an intro
 * that takes six frames and one that takes seven hundred. */
#define IDLE_COND_ALWAYS   0u
#define IDLE_COND_WHEN_NE  1u
extern u32 idle_loop_cond;
extern u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
extern u32 translation_gate_targets;

extern u32 num_skipped_frames;
extern int dynarec_enable;
extern boot_mode selected_boot_mode;
extern int sprite_limit;

#ifdef TRACE_EVENTS
  #define trace_update_gba(remcyc)   \
    printf("update_gba: %d remaining cycles\n", (remcyc));
#else  /* TRACE_EVENTS */
  #define trace_update_gba(x)
#endif

#endif


