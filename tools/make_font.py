#!/usr/bin/env python3
"""Build a Retro-Go .font file from a TrueType font, without a GUI.

Upstream ships tools/font_converter.py, which is a Tkinter application and emits a C source
file to be compiled in. That is the wrong shape twice over for Korean: covering Hangul means
thousands of glyphs, which nobody wants baked into every build, and a font that has to be
made by hand on a desktop is a font that never gets rebuilt when the translations grow.

This writes the binary format rg_font_load_from_file() reads, so the result goes on the SD
card at /retro-go/fonts/ and is picked up at boot.

    python3 tools/make_font.py --font /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc \\
                               --height 16 --name "Noto KR" --out ko.font \\
                               --text-from components/retro-go/translations.h

--text-from subsets to exactly the characters that appear in a file, which for a UI is a few
hundred syllables rather than the 11,172 that exist. Regenerate it when the translations
change; a missing glyph draws as nothing, so it fails quietly and confusingly otherwise.

File layout, from rg_font.c and rg_gui.c:

    header, 24 bytes
        char     name[16]
        uint8    type        0 monospace, 1 proportional
        uint8    width       median advance
        uint8    height      tallest glyph
        uint8    (padding)
        uint32   glyph count, little endian
    then one record per glyph, packed, terminated by a record whose code is 0:
        uint16   codepoint, little endian
        uint8    yOffset, uint8 width, uint8 height, uint8 xOffset, uint8 xDelta
        uint8    bitmap[ceil(width * height / 8)]

The bitmap is one bit per pixel with no per-row padding: bit index is x + y * width, most
significant bit first within each byte.
"""
import argparse
import struct
import sys
import unicodedata

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("This needs Pillow: pip install --user Pillow")

# The renderer shifts each row into a uint32 and indexes rows by yOffset + y, so a glyph
# cannot be wider than 32 pixels or reach below the font's height.
MAX_WIDTH = 32

ASCII = range(0x20, 0x7F)


def collect_codepoints(args):
    points = set(ASCII)
    if args.text_from:
        for path in args.text_from:
            with open(path, encoding="utf-8") as f:
                for ch in f.read():
                    cp = ord(ch)
                    # Anything above the BMP cannot be stored: the codepoint field is 16 bits.
                    if cp > 0xFFFF or ch.isspace():
                        continue
                    if cp > 0x7F:
                        points.add(cp)
    for spec in args.range or []:
        lo, _, hi = spec.partition("-")
        points.update(range(int(lo, 0), int(hi or lo, 0) + 1))
    return sorted(points)


def render_glyph(font, cp, height):
    """Rasterise one codepoint to (bitmap rows, metrics), or None if it has no ink."""
    ch = chr(cp)
    # Render onto a canvas with room around the cell so accents and descenders are not clipped.
    pad = height
    img = Image.new("L", (height * 3 + pad * 2, height * 3 + pad * 2), 0)
    draw = ImageDraw.Draw(img)
    draw.text((pad, pad), ch, font=font, fill=255)

    bbox = img.getbbox()
    try:
        advance = int(round(font.getlength(ch)))
    except AttributeError:
        advance = height

    if bbox is None:
        # No ink, space being the usual case. Keep it: the renderer still needs the advance.
        return [], 0, 0, 0, 0, max(1, advance)

    x0, y0, x1, y1 = bbox
    w, h = x1 - x0, y1 - y0
    if w > MAX_WIDTH:
        return None

    rows = []
    px = img.load()
    for y in range(y0, y1):
        row = 0
        for x in range(x0, x1):
            if px[x, y] >= 128:
                row |= 1 << (x - x0)
        rows.append(row)

    # yOffset is measured from the top of the line box, which is where we drew from.
    return rows, y0 - pad, w, h, max(0, x0 - pad), max(w, advance)


def pack_glyph(cp, rows, y_off, w, h, x_off, x_delta):
    bits = bytearray()
    acc = 0
    nbits = 0
    for y in range(h):
        for x in range(w):
            acc = (acc << 1) | ((rows[y] >> x) & 1)
            nbits += 1
            if nbits == 8:
                bits.append(acc)
                acc, nbits = 0, 0
    if nbits:
        # Trailing bits sit in the high end of the final byte, matching the reader's mask.
        bits.append(acc << (8 - nbits))

    return struct.pack("<HBBBBB", cp, y_off & 0xFF, w, h, x_off & 0xFF, x_delta) + bytes(bits)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--font", required=True, help="TTF/OTF/TTC to rasterise")
    ap.add_argument("--index", type=int, default=0, help="face index inside a .ttc")
    ap.add_argument("--height", type=int, default=16, help="pixel height")
    ap.add_argument("--name", default="Font", help="name shown in the font menu, max 15 chars")
    ap.add_argument("--out", required=True)
    ap.add_argument("--text-from", nargs="*", help="files whose characters to include")
    ap.add_argument("--range", nargs="*", help="extra codepoint ranges, eg 0xAC00-0xD7A3")
    args = ap.parse_args()

    font = ImageFont.truetype(args.font, args.height, index=args.index)
    codepoints = collect_codepoints(args)
    if not codepoints:
        sys.exit("Nothing to include -- pass --text-from or --range")

    glyphs = bytearray()
    widths = []
    tallest = 0
    kept = skipped = 0

    # Two passes. The renderer does output[yOffset + y] into a buffer of exactly font->height
    # rows, with no bounds check, so every glyph has to fit inside the declared height or it
    # writes past the end. Rasterising at a given pixel size does not guarantee that -- a
    # descender or an accent easily reaches past it -- so measure everything first, then
    # declare the height that is actually true and shift the glyphs to match.
    rendered = []
    for cp in codepoints:
        out = render_glyph(font, cp, args.height)
        if out is None:
            skipped += 1
            continue
        rendered.append((cp, *out))

    inked = [g for g in rendered if g[4] > 0]  # h > 0
    top = min(g[2] for g in inked) if inked else 0          # lowest yOffset
    bottom = max(g[2] + g[4] for g in inked) if inked else args.height
    real_height = bottom - top

    if real_height > 255:
        sys.exit(f"glyphs span {real_height} rows, which does not fit the 8-bit height field")

    for cp, rows, y_off, w, h, x_off, x_delta in rendered:
        # A glyph with no ink -- space, mostly -- has no meaningful offset, and normalising
        # its zero against a positive `top` would wrap it to 255 through the 8-bit field.
        # Harmless today because a zero-height glyph draws no rows, but it is wrong data and
        # would be an out-of-bounds write the moment it were not zero-height.
        adjusted = 0 if h == 0 else y_off - top
        glyphs += pack_glyph(cp, rows, adjusted, w, h, x_off, x_delta)
        widths.append(x_delta)
        kept += 1
    tallest = real_height

    glyphs += struct.pack("<HBBBBB", 0, 0, 0, 0, 0, 0)  # terminator

    widths.sort()
    median = widths[len(widths) // 2] if widths else args.height
    proportional = 1 if widths and widths[0] != widths[-1] else 0

    name = args.name.encode("utf-8")[:15]
    header = name + b"\0" * (16 - len(name))
    header += struct.pack("<BBBBI", proportional, median, tallest, 0, kept)

    with open(args.out, "wb") as f:
        f.write(header + glyphs)

    print(f"{args.out}: {kept} glyphs, {len(header) + len(glyphs)} bytes, "
          f"height {tallest} (asked for {args.height}), median width {median}, "
          f"{'proportional' if proportional else 'monospace'}")
    if skipped:
        print(f"  {skipped} glyph(s) skipped for being wider than {MAX_WIDTH}px")


if __name__ == "__main__":
    main()
