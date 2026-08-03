#ifndef ROM_MANAGER_H
#define ROM_MANAGER_H
/*
 * fceumm-go compatibility stub for the ESP32-P4 retro-go port.
 *
 * The core's TARGET_GNW path includes this header unconditionally, but the
 * functions it declares (rom_manager_system / rom_manager_get_file) are only
 * called from #ifndef LINUX_EMU blocks — which we skip because LINUX_EMU is
 * defined. So the types need to exist for the parser, but the functions are
 * never linked. We declare them as weak no-ops just in case.
 */
#include <stdint.h>
#include <stddef.h>

typedef struct { const char *name; } rom_system_t;
typedef struct { uint8_t address[1]; } retro_emulator_file_t;
typedef struct { int dummy; } rom_manager_t;

static inline rom_system_t *rom_manager_system(rom_manager_t *rm, const char *name) {
    (void)rm; (void)name; return NULL;
}
static inline retro_emulator_file_t *rom_manager_get_file(const rom_system_t *sys, const char *name) {
    (void)sys; (void)name; return NULL;
}

#endif /* ROM_MANAGER_H */
