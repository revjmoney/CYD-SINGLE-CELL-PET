# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Talk to Cell Pet over USB serial: send commands, grab a screenshot.

usage: python -I tools/shot.py COM17 out.png [cmds] [--reset]
  cmds are sent first (e.g. "n" for the next stage, "T 160 120\n" for a tap),
  then 'S' grabs the frame. --reset reboots the board first and waits 8 s.
"""
import struct
import sys
import time

import serial
from PIL import Image


def main(argv):
    reset = "--reset" in argv
    args = [a for a in argv if a != "--reset"]
    port, out = args[0], args[1]
    cmds = args[2] if len(args) > 2 else ""
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 921600, 5
    s.dtr = s.rts = False  # opening the port must not reset the board
    s.open()
    if reset:
        s.rts = True
        time.sleep(0.1)
        s.rts = False
        time.sleep(8)
    time.sleep(0.2)
    if cmds:
        s.write(cmds.encode().decode("unicode_escape").encode())
        time.sleep(2.5)
    s.reset_input_buffer()
    s.write(b"S")
    buf = b""
    while b"SHOT" not in buf:
        chunk = s.read(1)
        if not chunk:
            raise SystemExit("no SHOT header")
        buf = (buf + chunk)[-4:]
    w, h = struct.unpack("<HH", s.read(4))
    raw = s.read(w * h * 3)
    if len(raw) != w * h * 3:
        raise SystemExit(f"short read {len(raw)}")
    Image.frombytes("RGB", (w, h), raw).save(out)
    s.write(b"i")
    time.sleep(0.3)
    print(s.read(s.in_waiting).decode(errors="replace").strip())


if __name__ == "__main__":
    main(sys.argv[1:])
