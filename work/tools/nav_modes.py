#!/usr/bin/env python3
"""5.8 navigation rows: 3-button and gesture navigation over the running game (player mode).

In each mode: an edge swipe that reveals the system bars over the immersive game, then the bars hide again and a
look drag must still turn the view (no stuck touch, input transform unchanged); finally Back (nav button or edge
gesture) must reach the game (pause menu).

  nav_modes.py TAG
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aspect_matrix as am  # noqa: E402
import touchroute as t  # noqa: E402


def edge_swipe_bottom():
    t.d.sh("input swipe 1170 1075 1170 700 250")


def run_mode(tag, name):
    time.sleep(2)
    edge_swipe_bottom()
    time.sleep(1.0)
    t.shot(tag, f"{name}_bars_revealed")
    time.sleep(4.0)
    am.look_check(tag, f"{name}_after_bars")
    t.d.sh("input keyevent KEYCODE_BACK")
    time.sleep(2.5)
    t.shot(tag, f"{name}_back")
    t.d.sh("input keyevent KEYCODE_BACK")
    time.sleep(2.0)


def main(tag):
    old_fsg = t.d.sh("settings get global force_fsg_nav_bar")
    try:
        t.d.sh("settings put global force_fsg_nav_bar 0")
        time.sleep(3)
        print("3-button:", t.d.sh("settings get global force_fsg_nav_bar"))
        run_mode(tag, "buttons")
        t.d.sh("settings put global force_fsg_nav_bar 1")
        time.sleep(3)
        print("gestures:", t.d.sh("settings get global force_fsg_nav_bar"))
        run_mode(tag, "gestures")
    finally:
        t.d.sh(f"settings put global force_fsg_nav_bar {old_fsg.strip() or 1}")


if __name__ == "__main__":
    main(sys.argv[1])
