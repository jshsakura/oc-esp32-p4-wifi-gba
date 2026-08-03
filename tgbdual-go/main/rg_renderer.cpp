/*
 * rg_renderer.cpp — Retro-Go renderer for the TGB Dual core.
 *
 * Adapted from the libretro dmy_renderer (single-player paths) and the
 * Game-and-Watch gw_renderer (map_color, RTC). The core calls render_screen
 * and refresh during g_gb->run(); we buffer the output and submit it to
 * retro-go from the main loop.
 */
#include "rg_renderer.h"

#include <rg_system.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

// Globals that the main module sets; the renderer reads them to avoid pulling
// retro-go headers into the core component.
extern uint16_t *g_display_target;
extern uint32_t  g_gamepad;

rg_renderer::rg_renderer(int which)
{
    m_which = which;
    m_fixed_time = (dword)time(NULL);
    m_cur_time = m_fixed_time;
    m_frame_ready = false;
    m_frame_buf = NULL;
    m_frame_w = 0;
    m_frame_h = 0;
    m_frame_depth = 0;
    m_audio_samples = 0;
}

// GB colour format is RGB555 (xRRRRRGGGGGBBBBB). Retro-go surfaces are RGB565
// (RRRRRGGGGGGBBBBB). The green channel gains a bit by spreading bit 9 (the
// GB green MSB) into the new bit 5 position.
word rg_renderer::map_color(word gb_col)
{
    return ((gb_col & 0x001f) << 11) |
           ((gb_col & 0x03e0) <<  1) |
           ((gb_col & 0x0200) >>  4) |
           ((gb_col & 0x7c00) >> 10);
}

void rg_renderer::render_screen(byte *buf, int width, int height, int depth)
{
    // Buffer the pointer; the actual pixel copy happens in submit_frame() so
    // we can target whichever double-buffer surface is current.
    m_frame_buf = buf;
    m_frame_w = width;
    m_frame_h = height;
    m_frame_depth = depth;
    m_frame_ready = true;
}

void rg_renderer::submit_frame()
{
    if (!m_frame_ready || !m_frame_buf || !g_display_target)
        return;

    // The core already applied map_color, so the buffer is RGB565. Straight
    // memcpy into the surface, row by row (stride matches width for 565).
    int pitch = m_frame_w * ((m_frame_depth + 7) / 8);
    int rowsize = m_frame_w * 2;  // 16-bit pixels
    uint16_t *dst = g_display_target;
    byte *src = m_frame_buf;
    for (int y = 0; y < m_frame_h; y++)
    {
        memcpy(dst, src, rowsize);
        dst += m_frame_w;
        src += pitch;
    }
    m_frame_ready = false;
}

void rg_renderer::refresh()
{
    // The core's sound system calls this once per frame (at vblank). Render
    // SAMPLES_PER_FRAME stereo S16 pairs into our buffer; submit_audio()
    // hands them to retro-go from the main loop.
    if (snd_render)
    {
        snd_render->render(m_audio_stream, SAMPLES_PER_FRAME);
        m_audio_samples = SAMPLES_PER_FRAME;
    }
}

void rg_renderer::submit_audio()
{
    if (m_audio_samples <= 0)
        return;
    rg_audio_submit((const rg_audio_frame_t *)m_audio_stream, m_audio_samples);
    m_audio_samples = 0;
}

int rg_renderer::check_pad()
{
    // GB pad bit order: 0=A 1=B 2=Select 3=Start 4=Down 5=Up 6=Left 7=Right
    uint32_t pad = g_gamepad;
    int state = 0;
    if (pad & RG_KEY_A)      state |= (1 << 0);
    if (pad & RG_KEY_B)      state |= (1 << 1);
    if (pad & RG_KEY_SELECT) state |= (1 << 2);
    if (pad & RG_KEY_START)  state |= (1 << 3);
    if (pad & RG_KEY_DOWN)   state |= (1 << 4);
    if (pad & RG_KEY_UP)     state |= (1 << 5);
    if (pad & RG_KEY_LEFT)   state |= (1 << 6);
    if (pad & RG_KEY_RIGHT)  state |= (1 << 7);
    return state;
}

byte rg_renderer::get_time(int type)
{
    m_fixed_time = (dword)time(NULL);
    dword now = m_fixed_time - m_cur_time;
    switch (type)
    {
        case 8:  return (byte)(now % 60);
        case 9:  return (byte)((now / 60) % 60);
        case 10: return (byte)((now / 3600) % 24);
        case 11: return (byte)((now / 86400) & 0xff);
        case 12: return (byte)((now / (256 * 86400)) & 1);
    }
    return 0;
}

void rg_renderer::set_time(int type, byte dat)
{
    m_fixed_time = (dword)time(NULL);
    dword adj = m_fixed_time - m_cur_time;
    switch (type)
    {
        case 8:  adj = (adj/60)*60 + (dat%60); break;
        case 9:  adj = (adj/3600)*3600 + (dat%60)*60 + (adj%60); break;
        case 10: adj = (adj/86400)*86400 + (dat%24)*3600 + (adj%3600); break;
        case 11: adj = (adj/(256*86400))*(256*86400) + dat*86400 + (adj%86400); break;
        case 12: adj = (dat&1)*256*86400 + (adj%(256*86400)); break;
    }
    m_cur_time = m_fixed_time - adj;
}
