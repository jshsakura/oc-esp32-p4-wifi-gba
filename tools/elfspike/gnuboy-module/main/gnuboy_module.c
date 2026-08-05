#include "gnuboy_module.h"

static const gnuboy_module_api_t api = {
    .abi_version = GNUBOY_MODULE_ABI_VERSION,
    .init = gnuboy_init,
    .load_bios = gnuboy_load_bios,
    .load_bios_file = gnuboy_load_bios_file,
    .free_bios = gnuboy_free_bios,
    .load_rom = gnuboy_load_rom,
    .load_rom_file = gnuboy_load_rom_file,
    .free_rom = gnuboy_free_rom,
    .reset = gnuboy_reset,
    .run = gnuboy_run,
    .sram_dirty = gnuboy_sram_dirty,
    .load_bank = gnuboy_load_bank,
    .set_pad = gnuboy_set_pad,
    .set_framebuffer = gnuboy_set_framebuffer,
    .set_soundbuffer = gnuboy_set_soundbuffer,
    .get_time = gnuboy_get_time,
    .set_time = gnuboy_set_time,
    .get_hwtype = gnuboy_get_hwtype,
    .set_hwtype = gnuboy_set_hwtype,
    .get_palette = gnuboy_get_palette,
    .set_palette = gnuboy_set_palette,
    .load_sram = gnuboy_load_sram,
    .save_sram = gnuboy_save_sram,
    .load_state = gnuboy_load_state,
    .save_state = gnuboy_save_state,
};

int main(int argc, char **argv)
{
    if (argc > 0 && argv && argv[0])
        *(const gnuboy_module_api_t **)argv[0] = &api;
    return 0;
}
