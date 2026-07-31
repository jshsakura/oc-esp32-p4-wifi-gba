#!/usr/bin/env python3
"""Set up an SD card for a bring-up session.

The card spends most of its life inside the device, so the window where it is in a PC is
short and easy to waste -- copy the carts, forget the probe marker, boot, notice, pull the
card again. This does the whole list in one pass and prints what the next boot will do.

    python3 tools/prepare_sd.py /media/you/RETRO --carts ~/p8carts --probe
    python3 tools/prepare_sd.py /media/you/RETRO --boot p8:/sd/roms/p8/celeste.p8.png

Nothing here is destructive: it creates directories, copies files that are missing or
different, and writes boot.json only when asked.
"""

import argparse
import json
import os
import shutil
import sys

# The launcher's short_name for each system, from launcher/main/applications.c, mapped to the
# app partition that plays it. Only what --boot needs.
BOOT_TARGETS = {
    "p8": "fake08",
    "gba": "gbsp",
    "md": "gwenesis",
    "doom": "prboom-go",
    "msx": "fmsx",
}
for _core in ("nes", "snes", "gb", "gbc", "gw", "sms", "gg", "col", "pce", "lnx"):
    BOOT_TARGETS[_core] = "retro-core"

PROBE_MARKER = "retro-go/psram_exec_test"


def warn_about_non_ascii(names):
    """The card's filenames have to survive FatFs, and that depends on a build option.

    The firmware currently builds with CONFIG_FATFS_CODEPAGE_437 (US ASCII) and the default
    ANSI/OEM API encoding, so a name with Korean in it is not guaranteed to come back the way
    it went in -- which for a ROM means the launcher lists it and then cannot open it. Worth
    saying out loud at copy time rather than discovering it on a device with no display.
    """
    offenders = [n for n in names if any(ord(c) > 127 for c in n)]
    if not offenders:
        return
    print("Note: %d of these names are not ASCII, e.g. %s" % (len(offenders), offenders[0]))
    print("      The firmware builds with codepage 437 and ANSI/OEM encoding, so those names")
    print("      may not survive the trip. If the launcher lists a file it cannot open, that")
    print("      is this: either rename to ASCII, or build with CONFIG_FATFS_CODEPAGE_949 and")
    print("      CONFIG_FATFS_API_ENCODING_UTF_8. See docs/BRINGUP.md.")


def copy_into(src_dir, dest_dir):
    """Copy every file from src_dir into dest_dir, skipping ones already identical in size."""
    copied = skipped = 0
    os.makedirs(dest_dir, exist_ok=True)
    for name in sorted(os.listdir(src_dir)):
        src = os.path.join(src_dir, name)
        if not os.path.isfile(src):
            continue
        dest = os.path.join(dest_dir, name)
        if os.path.exists(dest) and os.path.getsize(dest) == os.path.getsize(src):
            skipped += 1
            continue
        shutil.copy2(src, dest)
        copied += 1
    return copied, skipped


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mount", help="where the card is mounted, e.g. /media/you/RETRO")
    parser.add_argument("--carts", metavar="DIR", help="copy PICO-8 carts from here into /roms/p8")
    parser.add_argument("--roms", metavar="SYSTEM=DIR", action="append", default=[],
                        help="copy ROMs into /roms/<system>, repeatable (e.g. --roms gba=~/gba)")
    parser.add_argument("--probe", action="store_true",
                        help="leave the marker that makes the next boot run the PSRAM instruction-fetch probe")
    parser.add_argument("--boot", metavar="SYSTEM:PATH",
                        help="preseed boot.json to launch a ROM without buttons or a display. "
                             "The OTA partition still has to be switched -- the command is printed.")
    parser.add_argument("--autosave", metavar="SECONDS", type=int,
                        help="set the auto-save interval in global.json (0, 15, 30, 60, 120 or 300)")
    args = parser.parse_args()

    mount = os.path.abspath(os.path.expanduser(args.mount))
    if not os.path.isdir(mount):
        sys.exit("Not a directory: %s" % mount)
    if not os.access(mount, os.W_OK):
        sys.exit("Not writable: %s" % mount)

    plan = []

    if args.carts:
        src = os.path.expanduser(args.carts)
        if not os.path.isdir(src):
            sys.exit("No such carts directory: %s" % src)
        copied, skipped = copy_into(src, os.path.join(mount, "roms", "p8"))
        plan.append("carts: %d copied, %d already there -> /roms/p8" % (copied, skipped))
        warn_about_non_ascii(os.listdir(src))

    for spec in args.roms:
        system, _, src = spec.partition("=")
        src = os.path.expanduser(src)
        if not system or not os.path.isdir(src):
            sys.exit("--roms wants SYSTEM=DIR, got: %s" % spec)
        copied, skipped = copy_into(src, os.path.join(mount, "roms", system))
        plan.append("%s: %d copied, %d already there -> /roms/%s" % (system, copied, skipped, system))

    if args.probe:
        marker = os.path.join(mount, PROBE_MARKER)
        os.makedirs(os.path.dirname(marker), exist_ok=True)
        open(marker, "w").close()
        plan.append("probe: /%s left for the next boot, which deletes it before it jumps" % PROBE_MARKER)

    if args.autosave is not None:
        path = os.path.join(mount, "retro-go", "config", "global.json")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        config = {}
        if os.path.exists(path):
            try:
                config = json.load(open(path))
            except ValueError:
                pass  # A card with a corrupt config is exactly when this should still work.
        config["AutoSaveSecs"] = args.autosave
        json.dump(config, open(path, "w"), indent=2, ensure_ascii=False)
        plan.append("auto-save: %ds (0 is off)" % args.autosave)

    partition = None
    if args.boot:
        system, _, rom = args.boot.partition(":")
        partition = BOOT_TARGETS.get(system)
        if not partition:
            sys.exit("Unknown system '%s'. Known: %s" % (system, ", ".join(sorted(BOOT_TARGETS))))
        if not rom.startswith("/sd/"):
            sys.exit("The ROM path is the device's, so it starts with /sd/ -- got: %s" % rom)
        path = os.path.join(mount, "retro-go", "config", "boot.json")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        # -1 means "new game": start the ROM without loading a state. Flags 0 for the same
        # reason -- RG_BOOT_RESUME would ask for a state that does not exist yet.
        json.dump({"BootName": system, "BootArgs": rom, "BootSlot": -1, "BootFlags": 0},
                  open(path, "w"), indent=2, ensure_ascii=False)
        plan.append("boot: %s from %s" % (system, rom))

    if not plan:
        sys.exit("Nothing asked for. See --help.")

    print("Card at %s:" % mount)
    for line in plan:
        print("  " + line)

    if partition:
        print("\nThe card only says which ROM. Which app runs it is OTA state in flash, so:")
        print("  python3 $IDF_PATH/components/app_update/otatool.py --port /dev/ttyACM0 \\")
        print("      switch_ota_partition --name %s" % partition)
        print("  (and --name launcher to go back)")


if __name__ == "__main__":
    main()
