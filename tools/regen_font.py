#!/usr/bin/env python3
"""Keep the built-in Korean font in step with the translations.

The font is a subset: it contains exactly the characters translations.h uses. Add a Korean
string with a character the font does not have and it draws as nothing at all -- no error, no
missing-glyph box, just a gap. That is a bad failure to leave to whoever remembers to run a
command, so the build runs this instead.

It is called from CMake at configure time rather than build time, because the file it
produces is compiled in the same pass; regenerating it mid-build would only take effect on
the next one. translations.h is registered as a configure dependency, so editing it triggers
a reconfigure and the font is rebuilt before anything compiles.

Bails out quietly rather than failing the build when it cannot do the job -- no Pillow, no
source typeface on this machine. The generated .c is committed, so a build in that situation
uses the last good one and only misses characters added since. It says so on the way past.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

TRANSLATIONS = os.path.join(ROOT, "components", "retro-go", "translations.h")
OUTPUT_C = os.path.join(ROOT, "components", "retro-go", "fonts", "NotoKR14.c")
SYMBOL = "font_NotoKR14_data"
HEIGHT = "14"
NAME = "Noto KR 14"

# Whichever of these exists. Noto Sans CJK is the intent; the others are reasonable
# stand-ins on a machine that does not have it.
CANDIDATES = [
    ("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "1"),
    ("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc", "1"),
    ("/usr/share/fonts/opentype/noto/NotoSansKR-Regular.otf", "0"),
    ("/usr/share/fonts/truetype/nanum/NanumGothic.ttf", "0"),
]


def find_python_with_pillow():
    candidates = [sys.executable, shutil.which("python3"), "/usr/bin/python3", "/usr/bin/python"]
    seen = set()
    for exe in candidates:
        if not exe or exe in seen or not os.path.exists(exe):
            continue
        seen.add(exe)
        probe = subprocess.run([exe, "-c", "import PIL"], capture_output=True)
        if probe.returncode == 0:
            return exe
    return None


def note(msg):
    print(f"-- korean font: {msg}")


def main():
    if not os.path.exists(TRANSLATIONS):
        return 0

    # Nothing to do when the font is already newer than the strings it covers.
    if os.path.exists(OUTPUT_C):
        if os.path.getmtime(OUTPUT_C) >= os.path.getmtime(TRANSLATIONS):
            return 0

    # CMake invokes us with esp-idf's virtualenv python, which has no Pillow and should not
    # be made to. Look for an interpreter that does rather than assuming the one running us.
    python = find_python_with_pillow()
    if not python:
        note("translations changed but no python with Pillow was found; keeping the committed "
             "font. Any newly added Korean characters will draw blank until it is regenerated.")
        return 0

    source = next(((p, i) for p, i in CANDIDATES if os.path.exists(p)), None)
    if not source:
        note("translations changed but no source typeface found on this machine; keeping the "
             "committed font. Any newly added Korean characters will draw blank.")
        return 0

    path, index = source
    tmp = OUTPUT_C + ".tmp.font"
    cmd = [python, os.path.join(HERE, "make_font.py"),
           "--font", path, "--index", index, "--height", HEIGHT, "--name", NAME,
           "--out", tmp, "--c-array", SYMBOL, "--text-from", TRANSLATIONS]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        note(f"generation failed, keeping the committed font:\n{result.stderr.strip()}")
        for leftover in (tmp, tmp + ".c"):
            if os.path.exists(leftover):
                os.remove(leftover)
        return 0

    shutil.move(tmp + ".c", OUTPUT_C)
    os.remove(tmp)
    note(result.stdout.strip().splitlines()[-1] if result.stdout.strip() else "regenerated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
