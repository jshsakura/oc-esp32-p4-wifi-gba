// PICO-8 for oc-gba, on top of FAKE-08.
//
// FAKE-08 ships its own main() in source/main.cpp for platforms that boot into a shell. Here
// the launcher has already chosen a cart and rebooted into this partition, so that file is
// left out of the build and this is the entry point instead: retro-go comes up first, then the
// same Host/PicoRam/Audio/Vm construction FAKE-08 does everywhere else.
//
// Everything device-specific lives in the backend at
// external/fake-08/platform/esp32p4-retro-go/, which is written to be upstreamable.

#include <rg_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>

#include <string>

#include "Audio.h"
#include "PicoRam.h"
#include "RetroGoHost.h"
#include "host.h"
#include "logger.h"
#include "vm.h"

// Lua's parser is recursive descent and a cart is a Lua source file, so loading one nests
// about as deep as the expressions in it. The 8KB main task is not enough: the first cart
// loaded on hardware tripped the stack protector before ever drawing a frame. So the VM runs
// on its own task with room to work, and app_main only supervises.
#define VM_TASK_STACK_SIZE (48 * 1024)

static rg_app_t *app;
static Host *host;
static Vm *vm;

// PICO-8 has no save states -- a cart's persistent data goes through cartdata(), which the
// core writes itself. So loadState/saveState stay unset rather than pretending.

static bool screenshot_handler(const char *filename, int width, int height)
{
    rg_surface_t *screen = retrogo_host_screen();
    return screen && rg_surface_save_image_file(screen, filename, width, height);
}

static bool reset_handler(bool hard)
{
    (void)hard;
    if (!vm)
        return false;
    // Reloading the cart is what "reset" means for a fantasy console: the VM is rebuilt from
    // the cart image, which is the same thing PICO-8's own ctrl-R does.
    vm->CloseCart();
    if (app->romPath && *app->romPath)
        vm->LoadCart(app->romPath, true);
    else
        vm->LoadBiosCart();
    vm->vm_run();
    return true;
}

static void vm_task(void *arg)
{
    (void)arg;

    host = new Host(0, 0);
    PicoRam *memory = new PicoRam();
    memory->Reset();
    Audio *audio = new Audio(memory);

    Logger_Initialize(host->logFilePrefix());

    vm = new Vm(host, memory, nullptr, nullptr, audio);

    host->setUpPaletteColors();
    host->oneTimeSetup(audio);
    host->setTargetFps(60);

    vm->SetCartList(host->listcarts());

    if (app->romPath && *app->romPath)
    {
        RG_LOGI("Loading cart: %s", app->romPath);
        vm->LoadCart(app->romPath, true);
    }
    else
    {
        // No cart named: FAKE-08's BIOS cart, which is its own cart browser. That is what the
        // PICO-8 tab launched with nothing selected lands on.
        RG_LOGI("No cart given, loading the BIOS cart");
        vm->LoadBiosCart();
    }

    vm->vm_run();

    vm->GameLoop();

    // GameLoop only returns when the cart or the user asked to stop, so the way out is the
    // same as every other retro-go app's: back to the launcher.
    vm->CloseCart();
    host->oneTimeCleanup();
    Logger_Exit();

    rg_system_exit();
}

extern "C" void app_main(void)
{
    const rg_handlers_t handlers = {
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
    };

    // 22050 because that is the rate PICO-8 mixes at and FAKE-08's synth is built around.
    app = rg_system_init(22050, &handlers, NULL);

    // Not rg_task_create, because that would put this stack in PSRAM: FreeRTOS takes task
    // stacks from the ordinary heap, this target allows stacks in external memory
    // (CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM), and anything over
    // CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (16KB) is served from there. A Lua interpreter
    // touches its stack constantly, so it gets internal memory on purpose.
    //
    // Worth stating plainly for whoever reads this next: this was not what made the first
    // hardware run 150x too slow. Moving the stack internal changed the measurement by
    // nothing at all. The cost was the display blit -- see docs/BRINGUP.md.
    TaskHandle_t vmTask = NULL;
    if (xTaskCreatePinnedToCoreWithCaps(&vm_task, "p8_vm", VM_TASK_STACK_SIZE, NULL, RG_TASK_PRIORITY_2, &vmTask, 0,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS)
        RG_PANIC("Failed to start the PICO-8 VM task");

    // Kept alive rather than returned from: rg_system_init registered this task for its
    // statistics, and letting it be deleted would leave that pointing at nothing.
    while (true)
        rg_task_delay(1000);
}
