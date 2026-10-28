# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Turns the frames from tools/film.py into docs/media/cellpet.gif (for the README)
and docs/media/cellpet.mp4, with ffmpeg on PATH.

usage: python -I tools/make_clip.py build/film
"""
import glob
import os
import shutil
import subprocess
import sys
import tempfile

src = sys.argv[1]
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
media = os.path.join(root, "docs", "media")
os.makedirs(media, exist_ok=True)
order = ["round1", "quip", "jelly", "jellyquip"]
frames = [f for name in order for f in sorted(glob.glob(os.path.join(src, f"{name}_*.png")))]
if not frames:
    raise SystemExit("no frames")
tmp = tempfile.mkdtemp()
try:
    for i, f in enumerate(frames):
        shutil.copy(f, os.path.join(tmp, f"f{i:04d}.png"))
    seq = os.path.join(tmp, "f%04d.png")
    gif = os.path.join(media, "cellpet.gif")
    mp4 = os.path.join(media, "cellpet.mp4")
    # 20 fps is true speed (each frame is one 50 ms step); the GIF uses one palette built from the whole clip
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", "20", "-i", seq, "-vf",
                    "split[a][b];[a]palettegen=max_colors=192:stats_mode=full[p];[b][p]paletteuse=dither=bayer:bayer_scale=4",
                    "-loop", "0", gif], check=True)
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", "20", "-i", seq, "-vf",
                    "scale=640:480:flags=neighbor", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20",
                    "-movflags", "+faststart", mp4], check=True)
    print(f"{len(frames)} frames: {gif} {os.path.getsize(gif) // 1024} KB, {mp4} {os.path.getsize(mp4) // 1024} KB")
finally:
    shutil.rmtree(tmp)
