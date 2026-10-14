#!/usr/bin/env python3
"""Look through the hole of a note at a glowing target, by real touch (device redmi-note8pro only).

The VR player holds the note up (eyepiece on) and looks through its hole at something only the eyepiece shows.
Here finger 0 holds the note (InventoryItemProxy -> the held note), finger 1 turns the view onto the target and
finger 0 then brings the hole over it; one live touch stream (touchroute.Session), closed loop on screenshots:
the hole's glowing rim is fitted with a circle (often only an arc is on screen), the target is its glow.

  note_through.py TAG NOTE_PROXY TARGET   e.g. p071 EGP_Note_02 EGP_holo_symbols_gog   (eyepiece already on)
"""
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mech_route as m  # noqa: E402
import touchroute as t  # noqa: E402

PX_DEG_X, PX_DEG_Y = t.VIEW_W / 100.0, t.SCREEN_H / 59.2


def index_of(cls, name):
    return [i for i, o in enumerate(m.objs(cls)) if o["name"] == name][0]


def projected(cls, name):
    o = next(o for o in m.objs(cls) if o["name"] == name)
    return t.vpx(o["x"], o["y"])


def pixels(tag, shot):
    return np.asarray(Image.open(t.d.LOGS / tag / f"{shot}.png").convert("RGB")).astype(int)


def hole(a):
    """centre of the note's hole: algebraic circle fit to its green rim"""
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    ys, xs = np.nonzero((g > 140) & (g - b > 50) & (g - r > 80))
    if len(xs) < 30:
        return None
    lhs = np.stack([xs, ys, np.ones_like(xs)], 1).astype(float)
    d, e, _ = np.linalg.lstsq(lhs, -(xs.astype(float) ** 2 + ys.astype(float) ** 2), rcond=None)[0]
    return -d / 2, -e / 2


def glow(a, near, win=150):
    """centre of the cyan glow of the target around its projected position"""
    x0, y0 = int(max(0, near[0] - win)), int(max(0, near[1] - win))
    sub = a[y0:int(near[1] + win), x0:int(near[0] + win)]
    r, g, b = sub[..., 0], sub[..., 1], sub[..., 2]
    ys, xs = np.nonzero((g > 150) & (b > 140) & (abs(g - b) < 45) & (g - r > 90))
    return (x0 + xs.mean(), y0 + ys.mean()) if len(xs) > 10 else near


def run(tag, note_name, target_name, cls="GogglesVisibility"):
    m.face(cls, index_of(cls, target_name), box=(0.47, 0.53, 0.45, 0.55))
    yt, pt = t.head()
    note = m.face("InventoryItemProxy", index_of("InventoryItemProxy", note_name), box=(0.4, 0.6, 0.4, 0.7))
    yn, pn = t.head()
    rx, ry = ((yt - yn + 180) % 360 - 180) * PX_DEG_X, -(pt - pn) * PX_DEG_Y
    n0 = len(t.r.log_text().splitlines())
    s = t.Session()
    s.down(0, *t.vpx(note["x"], note["y"]))
    time.sleep(1.8)  # the hand takes the note
    while abs(rx) > 2 or abs(ry) > 2:  # finger 1 turns the view (strokes of at most 1400 x 500 px)
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
    tg = projected(cls, target_name)
    s.move(0, tg[0] + 600, tg[1] + 40, ms=600)  # held up, the hole sits left of the grab point
    time.sleep(1.0)
    for it in range(6):
        t.shot(tag, f"hole{it}")
        a = pixels(tag, f"hole{it}")
        h, tg = hole(a), glow(a, projected(cls, target_name))
        print("iter", it, "finger", s.pos[0], "hole", h, "target", tg)
        if not h:
            break
        ex, ey = tg[0] - h[0], tg[1] - h[1]
        if abs(ex) < 12 and abs(ey) < 12:
            break
        fx, fy = s.pos[0]
        s.move(0, max(5, min(2335, fx + ex)), max(5, min(1075, fy + ey)), ms=500)
        time.sleep(1.0)
    time.sleep(5.0)
    t.shot(tag, "held")
    t.d.main(["cmd", "hint?"])
    time.sleep(1.0)
    s.close()
    for ln in t.r.log_text().splitlines()[n0:]:
        if any(k in ln for k in ("grab:", "hud:", "hint:")):
            print(ln[-170:])


if __name__ == "__main__":
    if len(sys.argv) < 4:
        raise SystemExit(__doc__)
    run(*sys.argv[1:4])
