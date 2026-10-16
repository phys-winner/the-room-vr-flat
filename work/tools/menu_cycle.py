#!/usr/bin/env python3
"""Front end <-> chapter round trips by real touch (R027: the crash after returning from a replayed chapter).

  menu_cycle.py TAG CHAPTER [N]   from the main menu: LEVEL SELECT -> chapter -> pause -> QUIT -> YES -> profile 1 ->
                                  main menu; wait 15 s there and check the process and the crash buffer; N rounds
"""
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import devrun as d  # noqa: E402
import loc_check as L  # noqa: E402


def buttons():
    out = {}
    for ln in L.dev("menubtns"):
        m = re.search(r"btn '([^']*)' key=(\S+) screen\(([-\d.]+) ([-\d.]+)\)", ln)
        if m:
            out[m.group(1)] = (float(m.group(3)), float(m.group(4)))
    return out


def quit_to_menu(tag):
    L.hud("pause")
    time.sleep(2.5)
    b = buttons()
    L.tap_n(*b["Button [Quit]"])
    time.sleep(2.5)
    n0 = len(L.log_text().splitlines())
    L.tap_n(*buttons()["Button [Yes]"])
    L.wait_new("menu: screen 'Select Profile Screen'", 120, n0)
    time.sleep(2.5)
    n0 = len(L.log_text().splitlines())
    L.tap_n(0.40, 0.50)  # profile 1
    L.wait_new("menu: screen 'Main Menu Screen'", 60, n0)
    time.sleep(15)
    pid = d.pid()
    crash = d.sh("logcat -b crash -d")
    d.shot(tag, "main_menu_after_return")
    print(f"back in the main menu: pid={pid or '-'} crash buffer {len(crash)} chars")
    return bool(pid) and "TheRoomVRPlus" not in crash


def main(a):
    tag, chapter = a[0], int(a[1])
    rounds = int(a[2]) if len(a) > 2 else 1
    for r in range(rounds):
        L.chapter(tag, chapter)
        if not quit_to_menu(tag):
            raise SystemExit(f"round {r + 1}: FAILED")
        print(f"round {r + 1}: PASS")
        time.sleep(2)


if __name__ == "__main__":
    main(sys.argv[1:])
