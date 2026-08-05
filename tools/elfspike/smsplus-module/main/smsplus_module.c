#include "smsplus_module.h"

static const smsplus_module_api_t api = {
    .abi_version = SMSPLUS_MODULE_ABI_VERSION,
    .smsplus = &smsplus,
    .sms = &sms,
    .coleco = &coleco,
    .snd = &snd,
    .reset_config = system_reset_config,
    .load_rom = load_rom,
    .load_rom_file = load_rom_file,
    .poweron = system_poweron,
    .poweroff = system_poweroff,
    .reset = system_reset,
    .shutdown = system_shutdown,
    .frame = system_frame,
    .save_state = system_save_state,
    .load_state = system_load_state,
    .palette_sync = palette_sync,
    .copy_palette = render_copy_palette,
};

int main(int argc, char **argv)
{
    if (argc > 0 && argv && argv[0])
        *(const smsplus_module_api_t **)argv[0] = &api;
    return 0;
}
