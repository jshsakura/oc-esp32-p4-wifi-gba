#include "nofrendo_module.h"

static const nofrendo_module_api_t api = {
    .abi_version = NOFRENDO_MODULE_ABI_VERSION,
    .nes_getptr = nes_getptr,
    .nes_init = nes_init,
    .nes_setvidbuf = nes_setvidbuf,
    .nes_shutdown = nes_shutdown,
    .nes_insertcart = nes_insertcart,
    .nes_loadfile = nes_loadfile,
    .nes_emulate = nes_emulate,
    .nes_reset = nes_reset,
    .rom_loadmem = rom_loadmem,
    .state_load = state_load,
    .state_save = state_save,
    .input_update = input_update,
    .ppu_setopt = ppu_setopt,
    .ppu_getopt = ppu_getopt,
    .buildpalette = nofrendo_buildpalette,
};

int main(int argc, char **argv)
{
    if (argc > 0 && argv && argv[0])
        *(const nofrendo_module_api_t **)argv[0] = &api;
    return 0;
}
