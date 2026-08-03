#ifndef RG_RENDERER_H
#define RG_RENDERER_H
/*
 * Retro-Go renderer for the TGB Dual core.
 *
 * The core communicates with the platform through the abstract `renderer`
 * class (gb_core/renderer.h). This subclass bridges the core's output to
 * retro-go's display queue (rg_surface / rg_display_submit) and audio queue
 * (rg_audio_submit). Single-player only — link-cable support is a follow-up.
 */
#include "renderer.h"

#define GB_WIDTH  160
#define GB_HEIGHT 144

// SGB border is 256x224. We size the surface for the larger of the two so the
// same surface handles both modes without reallocation.
#define SGB_WIDTH  256
#define SGB_HEIGHT 224

class rg_renderer : public renderer
{
public:
    rg_renderer(int which);
    virtual ~rg_renderer() {}

    // --- renderer interface ---
    virtual void reset() {}
    virtual void refresh();
    virtual void render_screen(byte *buf, int width, int height, int depth);
    virtual int  check_pad();
    virtual word map_color(word gb_col);
    virtual byte get_time(int type);
    virtual void set_time(int type, byte dat);
    virtual word get_sensor(bool x_y) { (void)x_y; return 0; }
    virtual void set_bibrate(bool bibrate) { (void)bibrate; }

    // Called from main after g_gb->run() to submit the frame + audio that the
    // core pushed during the scanlines.
    void submit_frame();
    void submit_audio();

private:
    int m_which;
    dword m_fixed_time;
    dword m_cur_time;
    bool m_frame_ready;
    byte *m_frame_buf;
    int m_frame_w;
    int m_frame_h;
    int m_frame_depth;

    // Audio: the core's sound_renderer fills this stereo S16 buffer during
    // refresh(). 44100 Hz / 60 fps = 735 sample pairs per frame.
    static const int SAMPLES_PER_FRAME = 44100 / 60;
    short m_audio_stream[SAMPLES_PER_FRAME * 2];
    int m_audio_samples;
};

#endif // RG_RENDERER_H
