#pragma once

// Boot splash, supplied by the user rather than by us.
//
// A boot animation with a jingle is half of what makes a handheld feel like the machine it
// is shaped like, and the obvious ones belong to their owners. So nothing is shipped: the
// firmware looks for files on the SD card and shows whatever it finds, and a card without
// them boots straight through with no added delay.
//
//   /sd/boot/logo.png    any size; centred, and scaled down if it is bigger than the screen
//   /sd/boot/boot.wav    16-bit PCM, mono or stereo, played while the logo is up
//   /sd/boot/boot.cfg    optional "key = value" lines:
//                          duration = 2500     how long to hold the logo, milliseconds
//                          background = 0      RGB565 fill behind it
//                          skippable = 1       any button cuts it short
//
// Whatever is there is used; anything missing is skipped. With a sound but no image the
// screen stays on the background colour for the length of the sound, which is a legitimate
// way to do it.
void bootsplash_show(void);
