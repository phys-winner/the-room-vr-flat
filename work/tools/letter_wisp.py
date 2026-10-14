#!/usr/bin/env python3
"""Hub between chapters: hold the letter with the eyepiece on, look at the wisp through its hole (real touch).

The wisp ('Wisp Ball Ghost' particles) only exists while the letter is held; finger 0 holds the letter, finger 1
turns the view onto the wisp, finger 0 brings the hole over it (closed loop: hole rim circle fit vs. the wisp's
projected position from the renderer list).

  letter_wisp.py TAG LX LY DYAW DPITCH     letter at screen px (LX, LY); rough turn to the wisp in degrees
"""
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import note_through as nt  # noqa: E402
import touchroute as t  # noqa: E402


def wisp():
    n0 = len(t.r.log_text().splitlines())
    t.d.main(["cmd", "rlist Wisp Ball Ghost"])
    time.sleep(1.2)
    for ln in t.r.log_text().splitlines()[n0:]:
        m = re.search(r"r 'Wisp Ball Ghost' .* screen\((\S+) (\S+)\) vis=1", ln)
        if m:
            return t.vpx(float(m.group(1)), float(m.group(2)))
    return None


def run(tag, lx, ly, dyaw, dpitch):
    n0 = len(t.r.log_text().splitlines())
    s = t.Session()
    s.down(0, lx, ly)
    time.sleep(2.0)
    rx, ry = dyaw * nt.PX_DEG_X, -dpitch * nt.PX_DEG_Y
    while abs(rx) > 2 or abs(ry) > 2:
        sx, sy = max(-1400, min(1400, rx)), max(-500, min(500, ry))
        x1, y1 = 1170 - sx / 2, 600 - sy / 2
        s.down(1, x1, y1)
        time.sleep(0.08)
        s.move(1, x1 + sx, y1 + sy, ms=600)
        time.sleep(0.1)
        s.up(1)
        time.sleep(0.3)
        rx, ry = rx - sx, ry - sy
    time.sleep(1.0)
    for it in range(8):
        t.shot(tag, f"w{it}")
        h, w = nt.hole(nt.pixels(tag, f"w{it}")), wisp()
        print("iter", it, "finger", s.pos[0], "hole", h, "wisp", w)
        if not w:
            break
        if not h:  # hole off screen: bring the letter towards the wisp first
            fx, fy = s.pos[0]
            s.move(0, (fx + w[0]) / 2, (fy + w[1]) / 2, ms=500)
            time.sleep(0.8)
            continue
        ex, ey = w[0] - h[0], w[1] - h[1]
        if abs(ex) < 12 and abs(ey) < 12:
            break
        fx, fy = s.pos[0]
        s.move(0, max(5, min(2335, fx + ex)), max(5, min(1075, fy + ey)), ms=500)
        time.sleep(0.8)
    time.sleep(5.0)
    t.shot(tag, "held")
    s.close()
    time.sleep(2.0)
    for ln in t.r.log_text().splitlines()[n0:]:
        if any(k in ln for k in ("grab:", "scene ->", "player ready", "message:")):
            print(ln[-160:])


if __name__ == "__main__":
    if len(sys.argv) < 6:
        raise SystemExit(__doc__)
    run(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5]))
