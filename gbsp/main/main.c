#include <rg_system.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../components/gbsp-libretro/common.h"
#include "../components/gbsp-libretro/memmap.h"
#include "../components/gbsp-libretro/gba_memory.h"
#include "../components/gbsp-libretro/gba_cc_lut.h"
#include "../components/gbsp-libretro/main.h"
#include "../components/gbsp-libretro/cpu.h"
#include "../components/jit_dev/jit_core.h"

#define AUDIO_SAMPLE_RATE (GBA_SOUND_FREQUENCY)
#define AUDIO_BUFFER_LENGTH (AUDIO_SAMPLE_RATE / 60 + 1)

#include "bios.h"

/* JIT configuration */
static bool jit_enabled = true;  /* Enable JIT, but all instructions use interpreter */
static bool jit_initialized = false;
static int jit_debug_counter = 0;
#define JIT_DEBUG_INTERVAL 60

u32 idle_loop_target_pc = 0xFFFFFFFF;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
u32 translation_gate_targets = 0;
boot_mode selected_boot_mode = boot_game;

u32 skip_next_frame = 0;
int sprite_limit = 1;

gbsp_memory_t *gbsp_memory;

static rg_surface_t *updates[2];
static rg_surface_t *currentUpdate;
static rg_app_t *app;

void netpacket_poll_receive()
{
}

void netpacket_send(uint16_t client_id, const void *buf, size_t len)
{
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    return rg_surface_save_image_file(currentUpdate, filename, width, height);
}

// --- GBA color correction -------------------------------------------------
// GBA games were tuned for the dark original LCD and look flat on a modern IPS.
// A gentle gamma boost (0.85) on the 240x160 frame buffer restores the contrast
// the artists intended, at 1/9 the cost of doing it in the scaled display path.
static uint16_t gba_lut5[32], gba_lut6[64];

static void gba_lut_init(void)
{
    for (int i = 0; i < 32; i++)
        gba_lut5[i] = RG_MIN(31, (int)(powf((float)i / 31.f, 0.85f) * 31.f + 0.5f));
    for (int i = 0; i < 64; i++)
        gba_lut6[i] = RG_MIN(63, (int)(powf((float)i / 63.f, 0.85f) * 63.f + 0.5f));
}

static void gba_color_correct(rg_surface_t *surf)
{
    if (!surf || !surf->data)
        return;
    uint16_t *data = surf->data;
    int pixels = surf->width * surf->height;
    for (int i = 0; i < pixels; i++)
    {
        uint16_t px = data[i];
        data[i] = (gba_lut5[(px >> 11) & 0x1F] << 11)
                | (gba_lut6[(px >> 5) & 0x3F] << 5)
                | gba_lut5[px & 0x1F];
    }
}

static bool gba_color_enabled(void)
{
    return rg_settings_get_number(NS_APP, "GbaColor", 0);
}

static void gba_maybe_correct(rg_surface_t *surf)
{
    static bool lut_ready = false;
    if (!lut_ready)
    {
        gba_lut_init();
        lut_ready = true;
    }
    if (gba_color_enabled())
        gba_color_correct(surf);
}

// Menu toggle for the color correction, shown under "Emulator options".
static rg_gui_event_t gba_color_cb(rg_gui_option_t *option, rg_gui_event_t event)
{
    bool on = rg_settings_get_number(NS_APP, "GbaColor", 0);
    if (event == RG_DIALOG_PREV || event == RG_DIALOG_NEXT)
    {
        on = !on;
        rg_settings_set_number(NS_APP, "GbaColor", on);
        rg_settings_commit();
        return RG_DIALOG_REDRAW;
    }
    strcpy(option->value, on ? _("On") : _("Off"));
    return RG_DIALOG_VOID;
}

static void options_handler(rg_gui_option_t *dest)
{
    int end = 0;
    while (dest[end].label || dest[end].value || dest[end].arg || dest[end].flags || dest[end].update_cb)
        end++;
    dest[end] = (rg_gui_option_t){0, _("GBA Color"), "-", RG_DIALOG_FLAG_NORMAL, &gba_color_cb};
    dest[end + 1] = (rg_gui_option_t)RG_DIALOG_END;
}

static bool save_state_handler(const char *filename)
{
    return false;
}

static bool load_state_handler(const char *filename)
{
    return false;
}

static bool reset_handler(bool hard)
{
    return true;
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_REDRAW)
    {
        gba_maybe_correct(currentUpdate);
        rg_display_submit(currentUpdate, 0);
    }
}

int16_t input_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    // RG_LOGI("%u, %u, %u, %u", port, device, index, id);
    uint32_t joystick = rg_input_read_gamepad();
    int16_t val = 0;
    if (joystick & RG_KEY_DOWN) val |= (1 << RETRO_DEVICE_ID_JOYPAD_DOWN);
    if (joystick & RG_KEY_UP) val |= (1 << RETRO_DEVICE_ID_JOYPAD_UP);
    if (joystick & RG_KEY_LEFT) val |= (1 << RETRO_DEVICE_ID_JOYPAD_LEFT);
    if (joystick & RG_KEY_RIGHT) val |= (1 << RETRO_DEVICE_ID_JOYPAD_RIGHT);
    if (joystick & RG_KEY_START) val |= (1 << RETRO_DEVICE_ID_JOYPAD_START);
    if (joystick & RG_KEY_SELECT) val |= (1 << RETRO_DEVICE_ID_JOYPAD_SELECT);
    if (joystick & RG_KEY_B) val |= (1 << RETRO_DEVICE_ID_JOYPAD_B);
    if (joystick & RG_KEY_A) val |= (1 << RETRO_DEVICE_ID_JOYPAD_A);
    return val;
}

void set_fastforward_override(bool fastforward)
{
}

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
    // app = rg_system_init(AUDIO_SAMPLE_RATE * 0.7, &handlers, NULL);
    // rg_system_set_overclock(2);

    updates[0] = rg_surface_create(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT + 1, RG_PIXEL_565_LE, MEM_FAST);
    updates[0]->height = GBA_SCREEN_HEIGHT;
    // updates[1] = rg_surface_create(GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT + 1, RG_PIXEL_565_LE, MEM_FAST);
    // updates[1]->height = GBA_SCREEN_HEIGHT;
    currentUpdate = updates[0];

    gba_screen_pixels = currentUpdate->data;

    gbsp_memory = rg_alloc(sizeof(*gbsp_memory), MEM_ANY);

    libretro_supports_bitmasks = true;
    retro_set_input_state(input_cb);
    init_gamepak_buffer();
    init_sound();

    if (load_bios(RG_BASE_PATH_BIOS "/gba_bios.bin") != 0)
        memcpy(bios_rom, open_gba_bios_rom, sizeof(bios_rom));

    memset(gamepak_backup, 0xff, sizeof(gamepak_backup));
    if (load_gamepak(NULL, app->romPath, FEAT_DISABLE, FEAT_DISABLE, SERIAL_MODE_DISABLED) != 0)
    {
        RG_PANIC("Could not load the game file.");
    }

    reset_gba();

    if (jit_enabled) {
        int ret = jit_init(NULL);
        if (ret == 0) {
            jit_initialized = true;
        } else {
            RG_LOGW("JIT initialization failed, using interpreter");
            jit_initialized = false;
        }
    }

    while (true)
    {
        // RG_TIMER_INIT();

        rg_audio_sample_t mixbuffer[AUDIO_BUFFER_LENGTH];
        uint32_t joystick = rg_input_read_gamepad();

        if (joystick & (RG_KEY_MENU | RG_KEY_OPTION))
        {
            if (joystick & RG_KEY_MENU)
                rg_gui_game_menu();
            else
                rg_gui_options_menu();
        }

        int64_t start_time = rg_system_timer();

        update_input();
        rumble_frame_reset();

        clear_gamepak_stickybits();
        
        /* Try JIT execution first, fallback to interpreter */
        bool jit_executed = false;
        if (jit_initialized) {
            extern u32 reg[64];
            extern u32 update_gba(int remaining_cycles);
            s32 cycles_remaining = execute_cycles;
            u32 update_ret = 0;
            int block_count = 0;
            int fallback_count = 0;
            
            jit_debug_counter++;
            bool debug_this_frame = (jit_debug_counter % JIT_DEBUG_INTERVAL == 0);
            
            // 临时注释 - 之前的调试日志
            // if (debug_this_frame) {
            //     RG_LOGI("JIT frame start: PC=0x%08x, cycles=%d, thumb=%d", 
            //             (unsigned int)reg[15], cycles_remaining, (reg[16] & 0x20) != 0);
            // }
            
            while (1) {
                /* Check CPU halt state or need more cycles */
                if (reg[CPU_HALT_STATE] != CPU_ACTIVE || cycles_remaining <= 0) {
                    update_ret = update_gba(cycles_remaining);
                    if (completed_frame(update_ret)) {
                        // 临时注释 - 之前的调试日志
                        // if (debug_this_frame) {
                        //     RG_LOGI("JIT frame done: blocks=%d, fallbacks=%d, cycles_left=%d", 
                        //             block_count, fallback_count, cycles_remaining);
                        // }
                        break;
                    }
                    cycles_remaining = cycles_to_run(update_ret);
                    
                    /* If still in halt state, continue waiting */
                    if (reg[CPU_HALT_STATE] != CPU_ACTIVE) {
                        continue;
                    }
                }
                
                /* Execute JIT blocks */
                uint32_t pc = reg[15];
                bool is_thumb = (reg[16] & 0x20) != 0;
                
                block_entry_t *block = jit_block_lookup(pc, is_thumb);
                if (block == NULL) {
                    block = jit_block_compile(pc, is_thumb);
                    if (block == NULL) {
                        jit_mark_pc_failed(pc, is_thumb);
                        // 临时注释 - 之前的调试日志
                        // if (debug_this_frame && fallback_count < 5) {
                        //     RG_LOGW("JIT compile failed: PC=0x%08x, thumb=%d", (unsigned int)pc, is_thumb);
                        // }
                    }
                }
                
                if (block && block->valid && block->native_code) {
                    gba_cpu_state_t jit_cpu;
                    memcpy(jit_cpu.reg, reg, sizeof(jit_cpu.reg));
                    jit_cpu.cycles = cycles_remaining;
                    jit_cpu.cycles_target = cycles_remaining;
                    
                    /* 从reg[REG_CPSR]提取标志位到jit_cpu */
                    jit_cpu.n_flag = (reg[REG_CPSR] >> 31) & 1;
                    jit_cpu.z_flag = (reg[REG_CPSR] >> 30) & 1;
                    jit_cpu.c_flag = (reg[REG_CPSR] >> 29) & 1;
                    jit_cpu.v_flag = (reg[REG_CPSR] >> 28) & 1;
                    
                    uint32_t new_pc = jit_execute_block(block, &jit_cpu);
                    
                    // 保留 - 新增的PC追踪日志
                    if (debug_this_frame && block_count < 10) {
                        RG_LOGI("JIT执行: PC=0x%08x -> 0x%08x, block_pc=0x%08x", 
                                (unsigned int)pc, (unsigned int)new_pc, (unsigned int)block->pc);
                    }
                    
                    memcpy(reg, jit_cpu.reg, sizeof(jit_cpu.reg));
                    reg[15] = new_pc;
                    
                    /* 将jit_cpu标志位同步回reg[REG_CPSR] */
                    reg[REG_CPSR] = (reg[REG_CPSR] & 0x0FFFFFFF) |
                                    ((jit_cpu.n_flag & 1) << 31) |
                                    ((jit_cpu.z_flag & 1) << 30) |
                                    ((jit_cpu.c_flag & 1) << 29) |
                                    ((jit_cpu.v_flag & 1) << 28);
                    
                    int32_t consumed = cycles_remaining - (int32_t)jit_cpu.cycles;
                    if (consumed <= 0) consumed = 1;
                    cycles_remaining -= consumed;
                    
                    jit_executed = true;
                    block_count++;
                    
                    if (cycles_remaining <= 0) {
                        update_ret = update_gba(cycles_remaining);
                        if (completed_frame(update_ret)) {
                            // 临时注释 - 之前的调试日志
                            // if (debug_this_frame) {
                            //     RG_LOGI("JIT frame done: blocks=%d, fallbacks=%d, cycles_left=%d", 
                            //             block_count, fallback_count, cycles_remaining);
                            //     RG_LOGI("JIT stats: total_blocks=%lu, jit_hits=%lu, interpreter_fallbacks=%lu", 
                            //             (unsigned long)g_jit_stats.total_blocks, 
                            //             (unsigned long)g_jit_stats.jit_hits, 
                            //             (unsigned long)g_jit_stats.interpreter_fallbacks);
                            // }
                            break;
                        }
                        cycles_remaining = cycles_to_run(update_ret);
                        
                        if (reg[CPU_HALT_STATE] != CPU_ACTIVE) {
                            continue;
                        }
                    }
                } else {
                    /* Block compilation failed, use interpreter for this frame */
                    // 临时注释 - 之前的调试日志
                    // if (debug_this_frame) {
                    //     RG_LOGW("JIT fallback to interpreter: PC=0x%08x", (unsigned int)pc);
                    // }
                    execute_arm(execute_cycles);
                    fallback_count++;
                    break;
                }
            }
        }
        
        if (!jit_executed) {
            execute_arm(execute_cycles);
        }
        // RG_TIMER_LAP("execute_arm");

        if (!skip_next_frame)
        {
            gba_maybe_correct(currentUpdate);
            rg_display_submit(currentUpdate, 0);
        }

        size_t frames_count = sound_read_samples((s16 *)mixbuffer, AUDIO_BUFFER_LENGTH);
        // RG_TIMER_LAP("sound_read_samples");

        rg_system_tick(rg_system_timer() - start_time);

        rg_audio_submit(mixbuffer, frames_count);
        // RG_TIMER_LAP("rg_audio_submit");

        if (skip_next_frame == 0)
            skip_next_frame = app->frameskip;
        else if (skip_next_frame > 0)
            skip_next_frame--;
    }

    RG_PANIC("GBsP Ended");
}
