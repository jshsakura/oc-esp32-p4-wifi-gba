#!/usr/bin/env python3
"""Derive the oc-gba GPIO pinmap from the KiCad board, so firmware and PCB cannot drift apart.

The board is still being revised, so the target's pin defines are not authored by hand:
this reads the fabricated .kicad_pcb, maps each Waveshare header pad to the GPIO that
physically sits behind it, and reports the net wired to it.

    python3 tools/pinmap_from_kicad.py <board.kicad_pcb>            # show the pinmap
    python3 tools/pinmap_from_kicad.py <board.kicad_pcb> --check    # diff against config.h

--check exits non-zero when the board and components/retro-go/targets/oc-gba/config.h
disagree, which is the whole point: run it after every board revision.

The header pin -> GPIO table below is a property of the Waveshare ESP32-P4-WIFI6 module,
not of our board. It is transcribed from SPEC_GBA_P4.md section 5, where it is recorded as
measured from the physical module. KiCad's generic 2x20 footprint has the wrong row pitch
(2.54mm vs the real 17.78mm), so pad numbers are the only reliable link to a GPIO.
"""
import os
import re
import sys
from collections import defaultdict

# Waveshare ESP32-P4-WIFI6 2x20 header: pad number -> what the module exposes there.
HEADER_PIN_TO_GPIO = {
    1: 52, 2: 51, 4: 31, 5: 30, 6: 29, 7: 28, 9: 50, 10: 49,
    11: 5, 12: 4, 14: 3, 15: 2, 16: 8, 17: 7, 19: 24, 20: 25,
    21: 48, 22: 47, 24: 46, 25: 33, 26: 32, 27: 27, 29: 26,
    31: 23, 32: 22, 34: 21, 35: 20,
}
# Pads that are not programmable GPIO. The net wired to each is checked as a sanity test:
# if these do not line up, the pad numbering assumption is wrong and every GPIO is suspect.
HEADER_PIN_FIXED = {
    3: "GND", 8: "GND", 13: "GND", 18: "GND", 23: "GND", 28: "GND", 33: "GND", 38: "GND",
    30: None, 36: "+3V3", 37: None, 39: "VSYS", 40: None,
}

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG_H = os.path.join(REPO, "components", "retro-go", "targets", "oc-gba", "config.h")


def sexpr_blocks(text, tag):
    """Yield each balanced (tag ...) block. Quote-aware, since net names contain parens."""
    for m in re.finditer(r"\(" + tag + r"\s", text):
        depth, in_str, j = 0, False, m.start()
        while j < len(text):
            c = text[j]
            if in_str:
                if c == "\\":
                    j += 2
                    continue
                if c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    yield text[m.start():j + 1]
                    break
            j += 1


def read_pads(pcb_path):
    """{reference: {pad number: net name}} for every footprint on the board."""
    src = open(pcb_path, encoding="utf-8").read()
    pads = defaultdict(dict)
    values = {}
    for fp in sexpr_blocks(src, "footprint"):
        ref = re.search(r'\(property "Reference" "([^"]*)"', fp)
        if not ref:
            continue
        ref = ref.group(1)
        val = re.search(r'\(property "Value" "([^"]*)"', fp)
        values[ref] = val.group(1) if val else ""
        for pad in sexpr_blocks(fp, "pad"):
            num = re.match(r'\(pad "([^"]*)"', pad)
            net = re.search(r'\(net \d+ "([^"]*)"\)', pad)
            if num:
                pads[ref][num.group(1)] = net.group(1) if net else None
    return pads, values


def module_pinmap(pads, ref="U1"):
    """{net name: gpio number} for every header pad wired to something, plus any problems found."""
    if ref not in pads:
        raise SystemExit(f"no footprint {ref} on this board (have: {', '.join(sorted(pads))})")
    module = pads[ref]
    net_to_gpio, problems = {}, []

    for pin, expected in HEADER_PIN_FIXED.items():
        if expected is None:
            continue
        actual = module.get(str(pin))
        if actual != expected:
            problems.append(f"header pin {pin} should sit on {expected}, board has {actual!r}")

    for pin, gpio in HEADER_PIN_TO_GPIO.items():
        net = module.get(str(pin))
        if not net:
            continue
        if net in net_to_gpio:
            problems.append(f"net {net} is wired to both GPIO{net_to_gpio[net]} and GPIO{gpio}")
        net_to_gpio[net] = gpio
    return net_to_gpio, problems


def config_h_pins():
    """{net name: gpio} as currently written in the target, read from the trailing comment."""
    if not os.path.exists(CONFIG_H):
        return None
    found = {}
    for line in open(CONFIG_H, encoding="utf-8"):
        # e.g.  #define RG_GPIO_LCD_PCLK  GPIO_NUM_52  // net LCD_PCLK
        m = re.search(r"GPIO_NUM_(\d+).*//\s*net\s+(\S+)", line)
        if m:
            found[m.group(2)] = int(m.group(1))
    return found


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    pcb = sys.argv[1]
    check = "--check" in sys.argv[2:]

    pads, values = read_pads(pcb)
    net_to_gpio, problems = module_pinmap(pads)

    for p in problems:
        print(f"WARNING: {p}", file=sys.stderr)

    if not check:
        print(f"# {os.path.basename(pcb)} -- U1 ({values.get('U1', '?')})")
        for net, gpio in sorted(net_to_gpio.items(), key=lambda kv: kv[1]):
            print(f"GPIO_NUM_{gpio:<2}  {net}")
        for ref in ("U4", "U5", "U6", "U7", "U2"):
            if ref in pads:
                wired = {p: n for p, n in pads[ref].items() if n}
                print(f"\n# {ref} ({values.get(ref, '?')})")
                for p, n in sorted(wired.items(), key=lambda kv: int(kv[0]) if kv[0].isdigit() else 99):
                    print(f"  pin {p:>2}  {n}")
        return 0

    current = config_h_pins()
    if current is None:
        print(f"no target config at {CONFIG_H}", file=sys.stderr)
        return 1

    # config.h declares which nets the firmware drives; every one of those must exist on the
    # board at the stated GPIO. The reverse is not an error -- a board carries plenty the
    # firmware never touches, and since the display moved to MIPI DSI it no longer routes
    # any display signal through the header at all. Those show up as notes.
    drift, unclaimed = [], []
    for net, gpio in current.items():
        if net not in net_to_gpio:
            drift.append(f"{net}: config.h has GPIO{gpio}, board does not wire it")
        elif net_to_gpio[net] != gpio:
            drift.append(f"{net}: board has GPIO{net_to_gpio[net]}, config.h has GPIO{gpio}")
    for net, gpio in sorted(net_to_gpio.items(), key=lambda kv: kv[1]):
        if net not in current and not net.startswith("USB_"):
            unclaimed.append(f"{net} on GPIO{gpio}")

    for u in unclaimed:
        print(f"note: board wires {u}, firmware does not claim it")

    if drift or problems:
        for d in drift:
            print(f"DRIFT: {d}")
        print(f"\n{len(drift)} mismatch(es) between board and target config", file=sys.stderr)
        return 1
    print(f"config.h matches {os.path.basename(pcb)} ({len(current)} pins claimed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
