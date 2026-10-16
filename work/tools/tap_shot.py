#!/usr/bin/env python3
"""Tap and photograph the next frames (the menu click runs ~13 frames after the finger lifts): shows whether the
menu pointer beam is drawn. Real touch through touch_ap; screencaps straight after the lift.

  tap_shot.py TAG NAME X Y [N]   screen px; N screencaps (default 3)
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import devrun as d  # noqa: E402
import touch_ap as ta  # noqa: E402


def main(a):
    tag, name, x, y = a[0], a[1], float(a[2]), float(a[3])
    n = int(a[4]) if len(a) > 4 else 3
    out = d.LOGS / tag
    out.mkdir(parents=True, exist_ok=True)
    with ta.Session() as s:
        s.down(0, x, y)
        time.sleep(0.06)
        s.up(0)
        t0 = time.time()
        for i in range(n):
            data = d.adb("exec-out", "screencap -p", binary=True, timeout=30)
            (out / f"{name}_{i}.png").write_bytes(data)
            print(f"{name}_{i}.png at +{(time.time() - t0) * 1000:.0f} ms, {len(data)} B")


if __name__ == "__main__":
    main(sys.argv[1:])
