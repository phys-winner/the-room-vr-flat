#!/usr/bin/env python3
"""Chapter routes for TOUCH_GAMEPLAY_VERIFIED (device redmi-note8pro only).

Reaches later chapters the way a player can: the pause menu's authored secret (tap the GAME PAUSED title five
times → SKIP LEVEL), all by real touch through work/tools/mtinject (touchroute.py). The dev console is used only to
wait for log markers and to list targets (vols, recv, locyaw, rlist).

  chapter_route.py skip TAG [--profile N] [--times K]   continue the profile, skip K chapters, wait for the level
  chapter_route.py survey TAG                            list grabbables / receivers / locations of the current scene
"""
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import touchroute as t  # noqa: E402

PAUSE_TITLE = (1170, 336)  # GAME PAUSED title (pause menu screen, full-screen render)


def pause_menu_buttons():
    """screen px of the pause menu buttons from the dev menu tree (read-only)."""
    before = len(t.r.log_text().splitlines())
    t.d.main(["cmd", "menutree 3"])
    time.sleep(1.5)
    out = {}
    for ln in t.r.log_text().splitlines()[before:]:
        m = re.search(r"tree\s+'Button \[([^\]]+)\]' act=(\d)", ln)
        if m:
            out[m.group(1)] = m.group(2) == "1"
    return out


def wait_new(marker, timeout):
    """wait for a new occurrence of a log marker (the log keeps earlier ones)"""
    n0 = t.r.log_text().count(marker)
    t0 = time.time()
    while time.time() - t0 < timeout:
        if t.r.log_text().count(marker) > n0:
            return time.time() - t0
        time.sleep(1.0)
    raise SystemExit(f"no new {marker!r} within {timeout} s")


def skip_level(tag):
    t.hud("pause")
    time.sleep(2.5)
    for _ in range(5):
        t.tap(*PAUSE_TITLE)
        time.sleep(1.4)
    time.sleep(1.0)
    t.shot(tag, "pause_skip_visible")
    # SKIP LEVEL sits under RESTART LEVEL (pause menu layout, R014): third button
    t.tap(1170, 603)
    time.sleep(2.5)
    t.shot(tag, "skip_confirm")
    t.tap(1090, 597)  # ATTENTION! Are you sure you want to skip the level? -> YES
    time.sleep(2.0)
    t.shot(tag, "after_skip_yes")


def survey(tag):
    before = len(t.r.log_text().splitlines())
    for c in ("vols", "recv", "zooms"):
        t.d.main(["cmd", c])
        time.sleep(1.5)
    lines = t.r.log_text().splitlines()[before:]
    for ln in lines:
        if any(k in ln for k in ("vol '", "recv '", "zoom", "volumes:", "receivers:")):
            print(ln.strip()[-200:])


def main(a):
    cmd, tag = a[0], a[1]
    if cmd == "skip":
        prof = int(a[a.index("--profile") + 1]) if "--profile" in a else 3
        times = int(a[a.index("--times") + 1]) if "--times" in a else 1
        t.continue_game(tag, prof)
        for i in range(times):
            n0 = t.r.log_text().count("player ready")
            skip_level(tag)
            t0 = time.time()
            while t.r.log_text().count("player ready") <= n0 and time.time() - t0 < 300:
                time.sleep(2.0)
            print("level ready after %.0f s" % (time.time() - t0))
            time.sleep(5)
            t.shot(tag, f"chapter_{i + 1}")
            scene = [ln for ln in t.r.log_text().splitlines() if "scene ->" in ln][-1:]
            print("now in", scene)
    elif cmd == "survey":
        survey(tag)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
