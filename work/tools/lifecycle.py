#!/usr/bin/env python3
"""5.8 lifecycle rows on the release build in player mode (no developer flag), device redmi-note8pro only.

Each row: an Android event, then the game must come back with the same view, HUD, working touch (a one-finger
look drag changes the frame) and running audio (an active AudioTrack of the game's pid).

  lifecycle.py TAG   (game already running in a level)
"""
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import touchroute as t  # noqa: E402


def frame(tag, name):
    t.shot(tag, name)
    return np.asarray(Image.open(t.d.LOGS / tag / f"{name}.png").convert("L").resize((390, 180))).astype(float)


def audio_playing(pid):
    out = t.d.sh("dumpsys audio | grep -E 'AudioPlaybackConfiguration' | grep -E 'u/pid:[0-9]+/" + pid + " '")
    return "state:started" in out, out.strip()[:160]


def check(tag, name):
    time.sleep(3.0)
    pid = t.d.pid()
    a = frame(tag, f"{name}_back")
    t.look(25, 0)
    time.sleep(1.0)
    b = frame(tag, f"{name}_look")
    t.look(-25, 0)
    playing, line = audio_playing(pid)
    diff = float(np.abs(a - b).mean())
    print(f"{name}: pid={pid} look-diff={diff:.1f} audio={'started' if playing else 'NOT started'} | {line}")
    return pid, diff, playing


def main(tag):
    pkg, act = t.d.PKG, t.d.ACT
    rows = []
    frame(tag, "start")
    # Home and back
    t.d.sh("input keyevent KEYCODE_HOME"); time.sleep(5)
    t.d.sh(f"am start -n {pkg}/{act}")
    rows.append(("home_resume",) + check(tag, "home"))
    # screen off / on
    t.d.sh("input keyevent KEYCODE_POWER"); time.sleep(5)
    t.d.sh("input keyevent KEYCODE_WAKEUP"); time.sleep(1)
    t.d.sh("wm dismiss-keyguard"); time.sleep(1)
    t.d.sh(f"am start -n {pkg}/{act}")
    rows.append(("screen_off_on",) + check(tag, "screen"))
    # another app in front, then back through recents
    t.d.sh("am start -a android.settings.SETTINGS"); time.sleep(4)
    t.d.sh(f"am start -n {pkg}/{act}")
    rows.append(("other_app",) + check(tag, "otherapp"))
    for r in rows:
        print(r)


if __name__ == "__main__":
    main(sys.argv[1])
