/* input.h - WonderSwan input interface.
 * WS.c's Interrupt() calls WsInputGetState() once per frame at vblank.
 * The front-end (main.c) provides the implementation, reading the device
 * gamepad and returning the WS button-register bitmask. */
#ifndef INPUT_H_
#define INPUT_H_

#include <stdint.h>

uint32_t WsInputGetState(void);

#endif /* INPUT_H_ */
