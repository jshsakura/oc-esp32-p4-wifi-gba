# Flashing and first boot

Two targets exist. `oc-gba` is the handheld; `oc-gba-devkit` is the same firmware for a bare
Waveshare ESP32-P4-WIFI6 dev board, and is the one to use until the carrier board is built.

The dev board is not a stand-in for the real thing so much as most of it: the panel is MIPI
DSI and plugs into the module's own FPC connector, and the microSD slot, the ES8311 codec and
the radio all live on the module too. What the carrier board adds is buttons, the battery
path and the shell.

## Build

```sh
. ~/esp/esp-idf/export.sh
python3 rg_tool.py --target oc-gba-devkit build-img
```

That writes `retro-go_<rev>_oc-gba-devkit.img`, about 3.9 MB, containing the bootloader, the
partition table, and the three apps: `launcher`, `retro-core` and `gbsp`.

## Flash

```sh
python3 rg_tool.py --target oc-gba-devkit --port /dev/ttyACM0 install
```

Or with esptool directly, if you prefer to see what is being written:

```sh
python -m esptool --chip esp32p4 -b 921600 write_flash 0x0 retro-go_<rev>_oc-gba-devkit.img
```

Then `python3 rg_tool.py --target oc-gba-devkit --port /dev/ttyACM0 monitor` for the log.

## Wiring for the dev board

Buttons are active low: connect each to ground. All ten are on header pins the parallel LCD
bus used to occupy, before the panel became DSI, so none of them clash with the module's SD,
codec or USB pins.

| Key | GPIO | | Key | GPIO |
|---|---|---|---|---|
| UP | 20 | | A | 26 |
| DOWN | 21 | | B | 27 |
| LEFT | 22 | | START | 28 |
| RIGHT | 23 | | SELECT | 29 |
| | | | L / R | 30 / 31 |

Wiring two TCA9554 breakouts to GPIO7/8 at addresses 0x20 and 0x21 instead gives the exact
input path the handheld uses. Both maps are live at once and an expander that does not answer
contributes nothing, so there is nothing to switch between.

MENU is `START` + `SELECT`, and OPTION is `START` + `L`. There is no dedicated button for
either because a GBA has ten and games use all ten.

## SD card

FAT32. ROMs go in `/roms/<system>/`, matching stock retro-go.

An optional boot splash, which ships with nothing and shows whatever you supply:

```
/boot/logo.png     any size; centred, scaled down to fit, never scaled up
/boot/boot.wav     16-bit PCM, mono or stereo
/boot/boot.cfg     duration = 2500
                   background = 0
                   skippable = 1
```

With neither file present the boot does not pause at all. The splash only plays on a real
power-on, not when returning to the launcher from a game.

## If it will not boot

Hold `START` + `SELECT` + `L` + `R` while powering on for recovery mode, which starts with
settings disabled so a bad setting cannot lock you out.

You should rarely need it. Three boots in a row that fail to stay up for fifteen seconds
trigger the rescue screen by themselves, and that boot skips stored settings and the stored
boot target -- between them the likeliest reason a device stops booting. The counter lives in
RTC memory, so it survives the device resetting itself but not you switching it off, which is
the behaviour you want on a handheld with a hard power switch.

A crash writes `/sd/crash.log` and returns to the launcher rather than to whatever crashed.

## Known unknowns

None of this has run on hardware yet. In rough order of likelihood:

- **The panel stays dark.** The ST7701S init sequence is the generic 480x800 one the base
  driver shipped with, not the D310N9362V0's. See the notes in `targets/oc-gba/config.h` for
  which registers to suspect from which symptom.
- **The image is upside down.** The rotation direction is a guess until the panel is
  physically in a shell; `st7701.h` names the one line to flip.
- **Red and blue are swapped.** Frame buffer byte order, one line in the same file.
- **No sound.** The codec is configured through Espressif's driver, so the likely culprits
  are MCLK not reaching it or the amplifier enable being inverted, not the register values.

## Korean

The firmware ships no Korean font. Hangul is thousands of glyphs and the typeface would be
somebody else's; both are reasons to keep it on the card rather than in every build. Put a
`.font` file in `/retro-go/fonts/` and it is loaded at boot -- the log says how many it found.

`tools/make_font.py` builds one from any TrueType font, headlessly:

```sh
python3 tools/make_font.py \
    --font /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc --index 1 \
    --height 14 --name "Noto KR 14" --out ko14.font \
    --text-from components/retro-go/translations.h
```

`--text-from` subsets to exactly the characters that file uses, which is why the result is
about 15KB rather than megabytes. **Regenerate it whenever the Korean translations grow**: a
character with no glyph draws as nothing at all, so the failure is silent.

Note that the asked-for height is a starting point, not the result -- the tool measures where
the ink actually lands and declares that instead, because the renderer indexes a buffer of
exactly `height` rows with no bounds check.
