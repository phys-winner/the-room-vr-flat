#!/usr/bin/env python3
"""Film a menu tap: screenrecord streams raw H.264 to stdout (exec-out; this Realme refuses screenrecord output files)
while a real touch taps (touch_ap), then ffmpeg cuts the frames around the tap so every frame of the menu click
(~13 frames after the lift) can be checked for the pointer beam.

  tap_rec.py TAG NAME X Y      screen px; writes NAME.h264 and NAME_fNN.png (20 fps around the tap)
"""
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import devrun as d  # noqa: E402
import local_config  # noqa: E402
import touch_ap as ta  # noqa: E402

FFMPEG = local_config.get("ffmpeg") or shutil.which("ffmpeg") or "ffmpeg"


def main(a):
    tag, name, x, y = a[0], a[1], float(a[2]), float(a[3])
    out = d.LOGS / tag
    out.mkdir(parents=True, exist_ok=True)
    raw = out / f"{name}.h264"
    with open(raw, "wb") as f:
        rec = subprocess.Popen([d.ADB, "-s", d.SERIAL, "exec-out",
                                "screenrecord --output-format=h264 --time-limit 4 --bit-rate 12000000 -"], stdout=f)
        time.sleep(1.2)
        with ta.Session() as s:
            s.down(0, x, y)
            time.sleep(0.06)
            s.up(0)
        rec.wait(timeout=30)
    subprocess.run([FFMPEG, "-loglevel", "error", "-y", "-i", str(raw), "-ss", "0.9", "-t", "1.2", "-vf", "fps=20,scale=1200:-1",
                    str(out / f"{name}_f%02d.png")], check=True)
    frames = sorted(out.glob(f"{name}_f*.png"))
    print(f"{raw.name}: {raw.stat().st_size} B, {len(frames)} frames around the tap")


if __name__ == "__main__":
    main(sys.argv[1:])
