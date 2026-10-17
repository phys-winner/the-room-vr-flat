#!/usr/bin/env python3
"""Round-4 checks (user 2026-10-16, test device poco-x3-nfc: rooted, Android 16, Adreno 618, 1080x2400 @ 440 dpi).
Input is real multi-touch (work/tools/touch_ap.py: app_process MotionEvent injection); the developer console
(roomfix.dev) answers read-only questions and logs what the game did.

  r4_check.py shot TAG NAME
  r4_check.py hud NAME                      tap a HUD button (teleport, inventory, eyepiece, pause, turn_left, turn_right)
  r4_check.py tp TAG LOCATION               HUD teleport + tap on the location marker
  r4_check.py dev CMD...                    developer console commands, prints the answer
  r4_check.py swipe X1 Y1 X2 Y2 [MS]        normalised screen coordinates
  r4_check.py tap X Y                       normalised screen coordinates
  r4_check.py log [N]                       last log lines (without the periodic ones)
  r4_check.py layout                        HUD button positions on this display
  r4_check.py desk TAG                      tutorial from the balcony to the desk (snap turn, projector, teleports)
"""
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import devrun as d  # noqa: E402
import loc_check as lc  # noqa: E402
import touch_ap as ta  # noqa: E402

W, H = 2400, 1080


def hud_layout():
    """HudView.layoutButtons for this display: density and the cutout side from dumpsys (landscape)."""
    dens = int(re.search(r"Physical density: (\d+)", d.sh("wm density")).group(1))
    dp = dens / 160.0
    rot = d.sh("dumpsys input | grep -m1 -E 'SurfaceOrientation|orientation='")
    cut = re.search(r"insets=Rect\((\d+), (\d+) - (\d+), (\d+)\)",
                    d.sh("dumpsys display | grep -m1 -o 'cutout DisplayCutout{insets=Rect([^)]*)'"))
    inset = int(cut.group(2)) if cut else 0  # portrait top inset -> landscape left (rotation 90) or right (270)
    left_cut = "270" not in rot and "ROTATION_270" not in rot
    insetL, insetR = (inset, 0) if left_cut else (0, inset)
    m = 22 * dp
    right, left, bottom, top = W - insetR - m, insetL + m, H - m, 16 * dp
    rT, rS, gap = 33 * dp, 27 * dp, 14 * dp
    return {"teleport": (right - rT, bottom - rT), "inventory": (right - rT, bottom - 2 * rT - gap - rS),
            "eyepiece": (right - rT, bottom - 2 * rT - 2 * gap - 3 * rS), "pause": (left + 21 * dp, top + 21 * dp),
            "turn_left": (left + rS, bottom - rS), "turn_right": (left + 3 * rS + gap, bottom - rS)}


_HUD = None


def hud(name):
    global _HUD
    if _HUD is None:
        _HUD = hud_layout()
    x, y = _HUD[name]
    with ta.Session() as s:
        s.tap(x, y)
    time.sleep(0.4)


def tap(x, y, ms=80):
    with ta.Session() as s:
        s.tap(x * W, y * H, ms)
    time.sleep(0.2)


def swipe(x1, y1, x2, y2, ms=400):
    with ta.Session() as s:
        s.swipe(x1 * W, y1 * H, x2 * W, y2 * H, ms)
    time.sleep(0.2)


def dev(*cmds, settle=1.6):
    return lc.dev(*cmds, settle=settle)


def log_tail(n=40, skip=("dev: camera", "fps:", "xr: centre")):
    lines = d.read_ext("roomfix.log").splitlines()
    return [ln for ln in lines if not any(s in ln for s in skip)][-n:]


def wait_log(marker, timeout):
    return lc.wait_new(marker, timeout, 0)


def teleport(tag, name):
    lc.HUD.update(hud_layout())
    return lc.teleport(name, tag)


VOL = re.compile(r"vol '([^']*)' \S+ grab=1 poke=\d grabbable=(\S+) screen\(([-\d.]+) ([-\d.]+)\)")


def volume(name=None, grabbable=None):
    """Screen position of a visible grab volume (by object name or grabbable class)."""
    for ln in dev("vols"):
        m = VOL.search(ln)
        if m and (name is None or m.group(1) == name) and (grabbable is None or m.group(2) == grabbable):
            x, y = float(m.group(3)), float(m.group(4))
            if 0 < x < 1 and 0 < y < 1:
                return x, y
    return None


def tut_to_desk(tag):
    """Tutorial from the balcony: snap turn, projector (five slides), teleport to the desk - all by real touches."""
    lc.HUD.update(hud_layout())
    wait_log("SNAP_TURN_TUTORIAL", 60)
    hud("turn_right")
    time.sleep(3)
    teleport(tag, "ProjectorPosition")
    time.sleep(2)
    for _ in range(5):
        v = volume(grabbable="RotationGrabbable")
        if v:
            x, y = v[0] * W, v[1] * H
            with ta.Session() as s:
                s.down(0, x, y)
                time.sleep(0.5)
                s.move(0, x, y + 0.17 * H, ms=800)
                time.sleep(0.5)
                s.up(0)
        time.sleep(5.5)
    teleport(tag, "DeskPosition")
    time.sleep(3)


def main(a):
    cmd = a[0]
    if cmd == "shot":
        d.shot(a[1], a[2])
    elif cmd == "hud":
        hud(a[1])
    elif cmd == "tp":
        teleport(a[1], " ".join(a[2:]))
    elif cmd == "dev":
        for ln in dev(*a[1:]):
            print(ln)
    elif cmd == "swipe":
        swipe(*(float(v) for v in a[1:5]), *(int(v) for v in a[5:6]))
    elif cmd == "tap":
        tap(float(a[1]), float(a[2]))
    elif cmd == "log":
        print("\n".join(log_tail(int(a[1]) if len(a) > 1 else 40)))
    elif cmd == "layout":
        print(hud_layout())
    elif cmd == "desk":
        tut_to_desk(a[1])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
