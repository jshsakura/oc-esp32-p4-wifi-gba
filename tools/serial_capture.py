#!/usr/bin/env python3
"""Reset the board and capture its console for a fixed time.

Every measurement in docs/BRINGUP.md was taken with this. idf.py monitor is interactive and
cannot be scripted; this resets the same way esptool does, reads for a while, and prints what
it got, so a build-flash-measure loop fits in one command.

    python3 tools/serial_capture.py 25 > /tmp/boot.log
    python3 tools/serial_capture.py 25 | grep -v "0x2[01] failed"

The expander NACKs on a bare dev board are normal -- see BRINGUP 0.4.
"""
import sys
import time

import serial


def reset_and_capture(port="/dev/ttyACM0", seconds=25, baud=115200):
    s = serial.Serial(port, baud, timeout=0.2)
    # The CH343 bridge wires RTS to EN and DTR to the boot pin. Hold EN low briefly with the
    # boot pin released, so the chip comes up running rather than in the download loader.
    s.setDTR(False)
    s.setRTS(True)
    time.sleep(0.12)
    s.setRTS(False)

    end = time.time() + seconds
    out = bytearray()
    while time.time() < end:
        out += s.read(4096)
    s.close()
    return bytes(out)


if __name__ == "__main__":
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 25
    port = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyACM0"
    sys.stdout.buffer.write(reset_and_capture(port, secs))
