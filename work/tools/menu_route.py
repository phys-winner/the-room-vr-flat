#!/usr/bin/env python3
"""Front-end menu helpers for routes (device redmi-note8pro only): find a menu button by its GameObject name in
the current menu screen (dev `menutree`, read-only) and tap it with a real touch.

The menu tree prints camera-space positions; the game camera uses the port's hor+ projection (horizontal FOV
101.3 deg at 2340x1080, vertical 58.7 deg), so screen = centre + (x/z, -y/z) scaled by the half-FOV tangents.
"""
import math
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import touchroute as t  # noqa: E402

TAN_H = math.tan(math.radians(101.3 / 2))
TAN_V = math.tan(math.radians(58.7 / 2))


BUTTON_PREFIXES = ("MenuButton", "Button [")


def buttons(depth=2, prefixes=BUTTON_PREFIXES):
    n0 = len(t.r.log_text().splitlines())
    t.d.main(["cmd", f"menutree {depth}"])
    time.sleep(1.5)
    out = {}
    for ln in t.r.log_text().splitlines()[n0:]:
        m = re.search(r"tree\s+'([^']+)' act=(\d) cam\((\S+) (\S+) (\S+)\)", ln)
        if m and m.group(1).startswith(prefixes):
            x, y, z = (float(m.group(i)) for i in (3, 4, 5))
            sx = 0.5 + (x / z) / (2 * TAN_H)
            sy = 0.5 - (y / z) / (2 * TAN_V)
            out[m.group(1)] = {"active": m.group(2) == "1", "x": sx * t.SCREEN_W, "y": sy * t.SCREEN_H}
    return out


def press(name, wait=2.0, depth=2):
    b = buttons(depth).get(name)
    if not b or not b["active"]:
        raise SystemExit(f"menu button {name!r} not shown: {b}")
    t.tap(b["x"], b["y"])
    time.sleep(wait)
    return b
