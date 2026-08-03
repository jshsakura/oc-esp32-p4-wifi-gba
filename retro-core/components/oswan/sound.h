/* sound.h - oswan APU sound-backend stubs.
 * WSApu.c calls these at the boundaries of each HBlank audio sample. The
 * SDL backend (not compiled here) would lock/unlock its audio thread; on
 * retro-go the front-end drains the APU ring buffer (sndbuffer) directly,
 * so these are empty stubs defined in main.c. */
#ifndef SOUND_H_
#define SOUND_H_

void Sound_APU_Start(void);
void Sound_APU_End(void);
void Sound_APUClose(void);
void Pause_Sound(void);

#endif /* SOUND_H_ */
