# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Films Cell Pet off the board for the README: freezes the pond (serial Z), then
steps it 50 ms at a time ('.') and grabs a screenshot ('S') after each step, so
the frames play back at true speed at 20 fps however slow the serial link is.

usage: python -I tools/film.py COM17 outdir
  Leaves the board on a fresh drop (it crowns and evolves a test pet to film the jellyfish).
"""
import os
import struct
import sys
import time

import serial
from PIL import Image

port, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
s = serial.Serial()
s.port, s.baudrate, s.timeout = port, 921600, 3
s.dtr = s.rts = False
s.open()


frozen = False


def cmd(c, wait=0.4):
    global frozen
    if c == b"Z":
        frozen = not frozen
    s.write(c)
    time.sleep(wait)
    s.reset_input_buffer()


def grab(path, what=b"S", tries=3):
    for _ in range(tries):
        s.reset_input_buffer()
        s.write(what)
        buf = b""
        while b"SHOT" not in buf:
            ch = s.read(1)
            if not ch:
                break
            buf = (buf + ch)[-4:]
        else:
            w, h = struct.unpack("<HH", s.read(4))
            raw = b""
            while len(raw) < w * h * 3:
                got = s.read(w * h * 3 - len(raw))
                if not got:  # a dropped byte would otherwise wait forever with the pond frozen
                    break
                raw += got
            if len(raw) == w * h * 3:
                Image.frombytes("RGB", (w, h), raw).save(path)
                return
        time.sleep(1)
    if frozen:
        s.write(b"Z")  # don't leave the pond frozen
    raise SystemExit("screenshot failed: " + path)


def film(name, frames):
    for i in range(frames):
        s.write(b".")
        time.sleep(0.05)
        grab(os.path.join(out, f"{name}_{i:03d}.png"))
    print(name, frames, "frames", flush=True)


grab(os.path.join(out, "settings.png"), b"P0\n")
grab(os.path.join(out, "about.png"), b"P1\n")
print("menus", flush=True)

cmd(b"Z")  # freeze
film("round1", 60)
cmd(b"q")  # a silly achievement pops up
film("quip", 24)
cmd(b"Z")  # run again to crown and evolve a pet for the jellyfish
cmd(b"w", 3)
for _ in range(4):
    cmd(b"n", 1)
cmd(b"F 0.9 0.8 0.9\n", 1)  # a green, hunting, plump jellyfish
time.sleep(32)  # let the stage-change pop-ups from the fast-forward play out first
cmd(b"K", 0.2)  # bring it to the middle
cmd(b"Z")
film("jelly", 110)
cmd(b"q")
film("jellyquip", 30)
cmd(b"Z")  # unfreeze
cmd(b"r", 2)  # and leave a fresh drop
print("done", flush=True)
