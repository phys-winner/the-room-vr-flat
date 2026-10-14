#!/usr/bin/env python3
"""Mechanic checks by real touch (TOUCH_GAMEPLAY_VERIFIED / VR_ACTIONS_COVERED), device redmi-note8pro only.

Reaching a mechanic may use the dev test aid `tpto <PlayerLocation>` (the game's Player.TeleportTo, also for a
location the story has not unlocked yet); the action itself is always real multi-touch through mtinject.

  mech_route.py rope TAG       Egypt: pull the hanging rope at PullRopeWheel_Position (hand over hand)
"""
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import touchroute as t  # noqa: E402


def objs(cls):
    before = len(t.r.log_text().splitlines())
    t.d.main(["cmd", f"objs {cls}"])
    time.sleep(1.2)
    out = []
    for ln in t.r.log_text().splitlines()[before:]:
        m = re.search(r"obj '(.*)' pos\((\S+) (\S+) (\S+)\) screen\((\S+) (\S+)\) vis=(\d)", ln)
        if m:
            out.append({"name": m.group(1), "pos": tuple(float(m.group(i)) for i in (2, 3, 4)),
                        "x": float(m.group(5)), "y": float(m.group(6)), "vis": m.group(7) == "1"})
    return out


def face(cls, index=0, box=(0.3, 0.7, 0.15, 0.6), tries=8):
    """turn the view (real drags) until objs(cls)[index] is inside the screen box"""
    for _ in range(tries):
        o = objs(cls)[index]
        x0, x1, y0, y1 = box
        if o["vis"] and x0 < o["x"] < x1 and y0 < o["y"] < y1:
            return o
        if not o["vis"]:
            t.look(120, 0)
        else:
            dyaw = max(-60.0, min(60.0, (o["x"] - 0.5) * 60))
            dpitch = max(-30.0, min(30.0, -(o["y"] - (y0 + y1) / 2) * 25))
            t.look(dyaw, dpitch)
        time.sleep(0.8)
    return objs(cls)[index]


def log_since(n0, skip=("fps:", "carry:", "obj '")):
    for ln in t.r.log_text().splitlines()[n0:]:
        if not any(s in ln for s in skip):
            print(ln[-170:])


def rope(tag):
    t.d.main(["cmd", "tpto PullRopeWheel_Position"])
    time.sleep(5)
    o = face("RopeGrabbable", 1, box=(0.35, 0.65, 0.1, 0.45))
    print("rope grab point", o)
    n0 = len(t.r.log_text().splitlines())
    t.shot(tag, "rope_before")
    for i in range(3):  # three hand-over-hand pulls
        o = objs("RopeGrabbable")[1]
        x, y = t.vpx(o["x"], o["y"])
        t.drag([(x, y), (x, y + 280), (x, y + 560)], step_ms=18, steps_per_seg=18, hold_start_ms=250, hold_end_ms=250)
        time.sleep(1.2)
        t.shot(tag, f"rope_pull{i + 1}")
    log_since(n0)


def main(a):
    cmd, tag = a[0], a[1]
    {"rope": rope}[cmd](tag)


if __name__ == "__main__":
    main(sys.argv[1:])
