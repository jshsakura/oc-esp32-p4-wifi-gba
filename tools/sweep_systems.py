#!/usr/bin/env python3
"""Run every system on the device and write down what it does.

Build, flash, boot, capture, repeat. One system per pass, because the ROM picker
is a compile-time flag (RG_BENCH_ROM_DIR, see rg_system.c) -- which it has to be:
this board has no buttons and its SD card is inside the case, so a system cannot
be chosen at runtime at all.

    python3 tools/sweep_systems.py                # everything with a ROM
    python3 tools/sweep_systems.py nes snes md    # just these

Reports us/frame, not BUSY%. BUSY drifts as the board warms -- measured 67% to
78% on one unchanged binary over a session -- while FPS rises with it, so
busyTime/ticks is the figure that survives. The first sample of every capture is
dropped: it is always 0 0.
"""
import os
import re
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = os.getenv("RG_PORT", "/dev/ttyACM0")
TARGET = os.getenv("RG_TOOL_TARGET", "oc-gba-devkit")
IDF_PY = os.path.expanduser("~/.espressif/python_env/idf5.5_py3.14_env/bin/python")

# system -> (app, partition offset). The offsets come from the packed image's
# table; rerun `rg_tool.py build-img` and re-read them if the app set changes.
SYSTEMS = {
    "nes":         ("fceumm-go",   0x1030000),
    "snes":        ("retro-core", 0x110000),
    "gb":          ("tgbdual-go",  0x1230000),
    "gbc":         ("tgbdual-go",  0x1230000),
    "sms":         ("retro-core", 0x110000),
    "gg":          ("retro-core", 0x110000),
    "sg1":         ("retro-core", 0x110000),
    "col":         ("retro-core", 0x110000),
    "pce":         ("retro-core", 0x110000),
    "gw":          ("retro-core", 0x110000),
    "a26":         ("retro-core", 0x110000),
    "a78":         ("retro-core", 0x110000),
    "ngp":         ("retro-core", 0x110000),
    "supervision": ("retro-core", 0x110000),
    "poke":        ("retro-core", 0x110000),
    "wsc":         ("retro-core", 0x110000),
    "vb":          ("retro-core", 0x110000),
    "videopac":    ("retro-core", 0x110000),
    "zxs":         ("retro-core", 0x110000),
    "gamecom":     ("retro-core", 0x110000),
    "md":          ("gwenesis",     0x530000),
    "gba":         ("gbsp",         0x7f0000),
    "msx":         ("fmsx",         0x6c0000),
    "sm":          ("sm-go",        0xef0000),
    "segacd":      ("picodrive-go", 0xb00000),
    "cpc":         ("caprice32-go", 0xd90000),
    "tama":        ("tamalib-go",   0xca0000),
}


def sh(cmd, **kw):
    return subprocess.run(cmd, shell=True, cwd=REPO, capture_output=True, text=True, **kw)


def sweep(system):
    app, offset = SYSTEMS[system]
    env = f'RG_BENCH_ROM_DIR={system} RG_BENCH_ROM_MATCH=.'
    build = sh(f'bash -lc "source ~/esp/esp-idf/export.sh >/dev/null 2>&1 && '
               f'{env} python3 rg_tool.py build {app} --target {TARGET}"')
    if "All done" not in build.stdout:
        return "BUILD FAILED", ""

    sh(f'sudo -n {IDF_PY} -m esptool --chip esp32p4 --port {PORT} -b 921600 '
       f'write_flash 0x{offset:x} {app}/build/{app}.bin')
    sh(f'sudo -n bash -lc "source ~/esp/esp-idf/export.sh >/dev/null 2>&1 && '
       f'env IDF_PATH=\\$IDF_PATH PYTHONPATH=\\$IDF_PATH/components/partition_table '
       f'{IDF_PY} \\$IDF_PATH/components/app_update/otatool.py --port {PORT} '
       f'switch_ota_partition --name {app}"')
    cap = sh(f'sudo -n python3 tools/serial_capture.py 26')
    log = cap.stdout

    if "Guru Meditation" in log or "assert failed" in log:
        return "CRASH", log
    if "ROM load failed" in log:
        why = re.search(r"ROM load failed: ([^\r\n]*)", log)
        return "NO ROM: " + (why.group(1) if why else "?"), log
    if "BENCH: nothing in" in log:
        return "NO ROM: nothing on the card", log

    rows = re.findall(r"BUSY:(\d+)%, FPS:(\d+)", log)[1:]  # first sample is 0 0
    if not rows:
        return "NO FRAMES", log
    busy = sum(int(b) for b, _ in rows) / len(rows)
    fps = sum(int(f) for _, f in rows) / len(rows)
    us = busy * 10000 / fps if fps else 0
    return f"{fps:5.1f} fps   BUSY {busy:4.1f}%   {us:6.0f} us/frame", log


def main():
    want = sys.argv[1:] or list(SYSTEMS)
    outdir = os.getenv("RG_SWEEP_OUT", "/tmp/rg_sweep")
    os.makedirs(outdir, exist_ok=True)
    results = {}
    for s in want:
        if s not in SYSTEMS:
            print(f"{s:<12} unknown system", flush=True)
            continue
        t0 = time.time()
        verdict, log = sweep(s)
        results[s] = verdict
        if log:
            open(f"{outdir}/{s}.log", "w").write(log)
        print(f"{s:<12} {verdict}   ({time.time()-t0:.0f}s)", flush=True)

    print("\n=== sweep ===")
    for s, v in results.items():
        print(f"{s:<12} {v}")


if __name__ == "__main__":
    main()
