#include "pce_go_module.h"

static const pce_go_module_api_t api = {
    .abi_version = PCE_GO_MODULE_ABI_VERSION,
    .init = InitPCE,
    .load_card = LoadCard,
    .load_file = LoadFile,
    .reset = ResetPCE,
    .run = RunPCE,
    .shutdown = ShutdownPCE,
    .load_state = LoadState,
    .save_state = SaveState,
    .palette = PalettePCE,
    .psg_update = psg_update,
};

int main(int argc, char **argv)
{
    if (argc > 0 && argv && argv[0])
        *(const pce_go_module_api_t **)argv[0] = &api;
    return 0;
}
