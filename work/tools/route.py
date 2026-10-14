#!/usr/bin/env python3
"""Scripted routes for the flat port (device redmi-note8pro only).

  route.py tut TAG [--new]   cold start -> profile 2 -> Continue/New Game -> Sitting -> wait for the Player in 'Tut'
  route.py menu TAG          cold start -> wait for MainMenu

Readiness is read from libroomfix's own log markers ("scene -> 'X'", "player ready"), polled through root,
so the route never relies on blind delays for loading. Logcat is captured to work/logs/TAG/logcat.txt by a
detached adb process recorded in work/logs/TAG/logcat.pid (stop it with `route.py stop TAG`).
"""
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import devrun as d  # noqa: E402


def log_text():
    return d.sh(f"su -c 'cat {d.EXT}/roomfix.log'")


def wait_marker(marker, timeout, after=0.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if marker in log_text():
            if after:
                time.sleep(after)
            return time.time() - t0
        if not d.pid():
            raise SystemExit(f"process died while waiting for {marker!r}")
        time.sleep(1.0)
    raise SystemExit(f"timeout {timeout}s waiting for {marker!r}")


def start(tag):
    out = d.LOGS / tag
    out.mkdir(parents=True, exist_ok=True)
    (out / "crash_before.txt").write_text(d.sh("logcat -b crash -d"), encoding="utf-8")
    d.sh(f"am force-stop {d.PKG}")
    # dev console on: the flag file lives in the app's external files dir (created by the app on first start)
    d.sh(f"su -c 'test -d {d.EXT} && touch {d.EXT}/roomfix.dev && chown $(stat -c %u:%g {d.EXT}) {d.EXT}/roomfix.dev'")
    d.sh("logcat -c")
    f = open(out / "logcat.txt", "wb")
    p = subprocess.Popen([d.ADB, "-s", d.SERIAL, "logcat", "-v", "epoch", "-b", "main", "-b", "system", "-b", "crash"],
                         stdout=f, stderr=subprocess.STDOUT, creationflags=getattr(subprocess, "DETACHED_PROCESS", 0))
    (out / "logcat.pid").write_text(str(p.pid))
    print(d.sh(f"am start -W -n {d.PKG}/{d.ACT}").splitlines()[-2:])


def stop(tag):
    pf = d.LOGS / tag / "logcat.pid"
    if pf.exists():
        try:
            subprocess.run(["taskkill", "/F", "/PID", pf.read_text().strip()], capture_output=True)
        except OSError:
            pass
        pf.unlink()
    d.pull(tag)


def tap_n(x, y):  # normalised landscape screen coords -> pixels (2340x1080 panel)
    d.sh(f"input tap {int(x * 2340)} {int(y * 1080)}")


def route_menu(tag):
    start(tag)
    print("main menu after %.1fs" % wait_marker("scene -> 'MainMenu'", 150, after=8))


def route_tut(tag, new=False):
    # libroomfix turns the camera to every menu screen (rf_menu.c), so screen positions are stable;
    # coordinates are normalised to the full 2340x1080 panel
    route_menu(tag)
    wait_marker("menu: screen 'Select Profile Screen'", 60, after=1.5)
    tap_n(0.5175, 0.5176)   # profile 2
    wait_marker("menu: screen 'Main Menu Screen'", 30, after=1.5)
    tap_n(0.5564, 0.588)    # Continue game / New game (same row)
    # the posture wizard (Sitting/Standing) is answered by libroomfix (rf_menu.c)
    print("player in level after %.1fs" % wait_marker("player ready", 150, after=4))


def run_steps(tag, steps):
    """steps: wait:<marker>[@timeout] | cmd:<dev command> | sleep:<s> | shot:<name> | tap:<x>,<y> (normalised)"""
    for st in steps:
        kind, _, arg = st.partition(":")
        if kind == "wait":
            marker, _, to = arg.partition("@")
            print(f"wait {marker!r}: %.1fs" % wait_marker(marker, float(to or 90)))
        elif kind == "cmd":
            d.main(["cmd", *arg.split(";")])
        elif kind == "sleep":
            time.sleep(float(arg))
        elif kind == "shot":
            d.shot(tag, arg)
        elif kind == "tap":
            x, y = (float(v) for v in arg.split(","))
            tap_n(x, y)
        else:
            raise SystemExit(f"bad step {st}")


TUT_TO_PROJECTOR = [
    "wait:tutorial: 'SNAP_TURN_TUTORIAL_TITLE' / 'SNAP_TURN_TUTORIAL'", "sleep:1", "cmd:snap 1",
    "wait:'TELEPORT_TUTORIAL_TITLE'", "cmd:look -125 -15;tmode 1", "sleep:2", "cmd:ttap 0.43 0.57",
    "wait:'TUTORIAL_OPERATE_LEVER_TITLE'", "cmd:look -40 -25", "sleep:2",
]

PULL_LEVER = ["cmd:grab 0.53 0.548", "sleep:0.6", "cmd:drag 0.53 0.62", "sleep:0.3", "cmd:drag 0.53 0.70", "sleep:0.3",
              "cmd:drag 0.53 0.78", "sleep:0.8", "cmd:release"]
TUT_TO_DESK = TUT_TO_PROJECTOR + sum(([*PULL_LEVER, f"wait:VIEW_SLIDE_{k}_MESSAGE", "sleep:7"] for k in range(1, 6)), []) + [
    "wait:'TUTORIAL_TELEPORT_TO_DESK_TITLE'", "cmd:faceloc DeskPosition;tmode 1", "sleep:2", "cmd:ttaploc DeskPosition",
    "sleep:4",
]

TUT_TO_SAFE = TUT_TO_DESK + [
    "wait:'TUTORIAL_READ_NOTE_TITLE'", "cmd:look 0 -35", "sleep:2", "cmd:grab 0.513 0.544", "sleep:3", "cmd:release",
    "wait:'TUTORIAL_PICK_UP_KEY_TITLE'", "cmd:grab 0.312 0.538", "sleep:0.6", "cmd:drag 0.31 0.58", "sleep:0.3",
    "cmd:drag 0.30 0.63", "sleep:0.3", "cmd:drag 0.29 0.68", "sleep:0.3", "cmd:drag 0.28 0.73", "sleep:0.8",
    "cmd:release", "sleep:2", "cmd:grab 0.311 0.560", "sleep:2",
    "wait:'TUTORIAL_RELEASE_KEY_TITLE'", "cmd:release", "wait:'TUTORIAL_FOUND_KEY'",
    "cmd:faceloc SafePosition;tmode 1", "sleep:2", "cmd:ttaploc SafePosition",
    "wait:'TUTORIAL_OPEN_INVENTORY_TITLE'", "sleep:2", "cmd:look 0 -10", "sleep:1",
]


if __name__ == "__main__":
    cmd, tag = sys.argv[1], sys.argv[2]
    if cmd == "steps":
        run_steps(tag, sys.argv[3:])
    elif cmd == "projector":
        route_tut(tag)
        run_steps(tag, TUT_TO_PROJECTOR + sys.argv[3:])
    elif cmd == "safe":
        route_tut(tag)
        run_steps(tag, TUT_TO_SAFE + sys.argv[3:])
    elif cmd == "desk":
        route_tut(tag)
        run_steps(tag, TUT_TO_DESK + sys.argv[3:])
    elif cmd == "tut":
        route_tut(tag, "--new" in sys.argv)
    elif cmd == "menu":
        route_menu(tag)
    elif cmd == "stop":
        stop(tag)
    else:
        raise SystemExit(__doc__)
