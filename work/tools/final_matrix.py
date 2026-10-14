#!/usr/bin/env python3
"""Final 5.8 matrix, lifecycle and soak on the published APK (player mode), device redmi-note8pro only.

Readiness is read from logcat (tag RoomFix) without su: on this phone every Magisk su call makes the system run
dex2oat for framework.jar, which would load the CPU during cold starts. su is used only between runs (developer
flag removal, re-arming the first-start gesture card with help=0).

  final_matrix.py TAG [--soak MINUTES] [--only setup,aspects,lifecycle,density,soak,nav]

  setup      cold start at the native size, profile 2, NEW GAME / CONTINUE into the level (a save exists after it)
  aspects    per logical size (16:9, 2:1, native 19.5:9, 4:3): cold start with the gesture card re-armed -> card
             shot -> tap closes -> main menu shot -> CONTINUE -> level shot + look drag -> Back = pause menu
  lifecycle  lifecycle.py rows (Home, screen off/on, other app) in the level
  nav        3-button navigation: cold start in that mode -> level -> bars revealed/hidden + look drag -> Back
  density    live density 360 / 520 and font scale 1.3 in the level: shot + look drag (restored afterwards)
  soak       MINUTES in the level with a look drag every 30 s: same pid, PSS before/after, crash buffer

Taps are real touches on the panel (mtinject). With `wm size` the logical display is letterboxed in the middle of the
panel at the same scale (hor+), so menu elements near the centre keep their panel positions.
"""
import re
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import touchroute as t  # noqa: E402
import lifecycle as lc  # noqa: E402

d = t.d
PROFILE2 = (1211, 559)
CONTINUE = (1319, 636)    # main menu CONTINUE GAME / NEW GAME row (panel px)
CENTRE = (1170, 540)
SIZES = [("16x9_ref", "1080x1920"), ("2x1", "1080x2160"), ("native_19.5x9", None), ("4x3", "1080x1440")]
RESULTS = []


def logcat():
    return subprocess.run([d.ADB, "-s", d.SERIAL, "logcat", "-d", "-s", "RoomFix:I"], capture_output=True,
                          text=True, errors="replace").stdout


def wait_lc(marker, timeout, n=1):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if logcat().count(marker) >= n:
            return time.time() - t0
        if not d.pid() and time.time() - t0 > 15:  # `am start` without -W returns before the process exists
            raise SystemExit(f"process died waiting for {marker!r}")
        time.sleep(2.0)
    raise SystemExit(f"timeout {timeout}s waiting for {marker!r}")


def frame(tag, name):
    d.shot(tag, name)
    return np.asarray(Image.open(d.LOGS / tag / f"{name}.png").convert("L").resize((390, 180))).astype(float)


def look_diff(tag, name):
    a = frame(tag, name)
    t.look(20, 0)
    time.sleep(1.0)
    b = frame(tag, f"{name}_look")
    t.look(-20, 0)
    time.sleep(0.6)
    return float(np.abs(a - b).mean())


def cold_to_level(tag, name, card):
    d.sh(f"am force-stop {d.PKG}")
    time.sleep(2)
    flags = d.EXT + "/roomfix.dev " + d.EXT + "/roomfix.cmd"
    d.sh(f"su -c 'rm -f {flags}'")
    if card:
        d.sh(f"su -c \"sed -i 's/^help=1$/help=0/' {d.EXT}/port_settings.cfg\"")
    d.sh("logcat -c")
    t0 = time.time()
    d.sh(f"am start -n {d.PKG}/{d.ACT}")
    wait_lc("menu: screen 'Select Profile Screen'", 300)
    menu_s = time.time() - t0
    time.sleep(2.0)
    t.tap(*PROFILE2)
    wait_lc("menu: screen 'Main Menu Screen'", 30)
    if card:
        wait_lc("help: gesture card (first start", 20)
    time.sleep(2.5)
    shown = "help: gesture card (first start" in logcat()  # also on a fresh install (help=0 from the start)
    if shown:
        d.shot(tag, f"{name}_card")
        t.tap(*CENTRE)
        time.sleep(1.5)
    d.shot(tag, f"{name}_menu")
    t.tap(*CONTINUE)
    wait_lc("player ready", 240)
    level_s = time.time() - t0
    time.sleep(6)
    hooks = re.search(r"hooks ready: (\d+) ok, (\d+) failed; calls (\d+)/(\d+) verified \(dev=(\d)\)", logcat())
    return menu_s, level_s, shown, hooks.group(0) if hooks else "hooks line missing"


def pause_check(tag, name):
    d.sh("input keyevent 4")
    time.sleep(2.5)
    d.shot(tag, f"{name}_pause")
    paused = "menu: screen 'Pause Menu Screen'" in logcat().split("player ready")[-1]
    d.sh("input keyevent 4")
    time.sleep(2.0)
    return paused


def setup(tag):
    menu_s, level_s, _, hooks = cold_to_level(tag, "setup", card=False)
    RESULTS.append(("setup", f"menu {menu_s:.1f}s level {level_s:.1f}s; {hooks}"))


def aspects(tag):
    try:
        for name, size in SIZES:
            d.sh(f"am force-stop {d.PKG}")
            d.sh(f"wm size {size}" if size else "wm size reset")
            time.sleep(3)
            menu_s, level_s, shown, hooks = cold_to_level(tag, name, card=True)
            diff = look_diff(tag, f"{name}_level")
            paused = pause_check(tag, name)
            RESULTS.append((name, f"{d.sh('wm size').splitlines()[-1]}; menu {menu_s:.1f}s level {level_s:.1f}s; "
                                  f"card {shown}; look-diff {diff:.1f}; Back->pause {paused}; {hooks}"))
    finally:
        d.sh(f"am force-stop {d.PKG}")  # no live size change under the running game (size-compat mode)
        d.sh("wm size reset")


def lifecycle(tag):
    rows = []
    lc.frame(tag, "lc_start")
    d.sh("input keyevent KEYCODE_HOME")
    time.sleep(5)
    d.sh(f"am start -n {d.PKG}/{d.ACT}")
    rows.append(("home_resume",) + lc.check(tag, "lc_home"))
    d.sh("input keyevent KEYCODE_POWER")
    time.sleep(5)
    d.sh("input keyevent KEYCODE_WAKEUP")
    time.sleep(1)
    d.sh("wm dismiss-keyguard")
    time.sleep(1)
    d.sh(f"am start -n {d.PKG}/{d.ACT}")
    rows.append(("screen_off_on",) + lc.check(tag, "lc_screen"))
    d.sh("am start -a android.settings.SETTINGS")
    time.sleep(4)
    d.sh(f"am start -n {d.PKG}/{d.ACT}")
    rows.append(("other_app",) + lc.check(tag, "lc_otherapp"))
    for r in rows:
        RESULTS.append((f"lifecycle {r[0]}", f"pid {r[1]} look-diff {r[2]:.1f} audio {'started' if r[3] else 'NOT'}"))


def nav(tag):
    old = d.sh("settings get global force_fsg_nav_bar").strip() or "1"
    try:
        d.sh(f"am force-stop {d.PKG}")
        d.sh("settings put global force_fsg_nav_bar 0")
        time.sleep(3)
        menu_s, level_s, _, _ = cold_to_level(tag, "nav_buttons", card=False)
        d.sh("input swipe 1170 1075 1170 700 250")
        time.sleep(1.0)
        d.shot(tag, "nav_buttons_bars_revealed")
        time.sleep(4.0)
        diff = look_diff(tag, "nav_buttons_after_bars")
        paused = pause_check(tag, "nav_buttons")
        RESULTS.append(("3-button navigation", f"level {level_s:.1f}s; look-diff after bars {diff:.1f}; Back->pause {paused}"))
    finally:
        d.sh(f"am force-stop {d.PKG}")
        d.sh(f"settings put global force_fsg_nav_bar {old}")
        time.sleep(3)


def density(tag):
    try:
        for name, cmd in (("density_360", "wm density 360"), ("density_520", "wm density 520"),
                          ("font_1.3", "settings put system font_scale 1.3")):
            d.sh(cmd)
            time.sleep(5)
            pid = d.pid()
            diff = look_diff(tag, name)
            RESULTS.append((name, f"pid {pid} look-diff {diff:.1f}"))
            d.sh("wm density reset")
            d.sh("settings put system font_scale 1.0")
            time.sleep(3)
    finally:
        d.sh("wm density reset")
        d.sh("settings put system font_scale 1.0")


def pss():
    out = d.sh(f"dumpsys meminfo {d.PKG} | grep -E 'TOTAL PSS|TOTAL:'")
    m = re.search(r"TOTAL(?: PSS)?:?\s+(\d+)", out)
    return int(m.group(1)) if m else -1


def soak(tag, minutes):
    pid0, p0, t0 = d.pid(), pss(), time.time()
    d.shot(tag, "soak_start")
    n = 0
    while time.time() - t0 < minutes * 60:
        time.sleep(30)
        if d.pid() != pid0:
            break
        t.look(15 if n % 2 == 0 else -15, 0)
        n += 1
    pid1, p1 = d.pid(), pss()
    d.shot(tag, "soak_end")
    crash = d.sh("logcat -b crash -d")
    RESULTS.append(("soak", f"{(time.time() - t0) / 60:.1f} min, {n} look drags; pid {pid0} -> {pid1}; "
                            f"PSS {p0} -> {p1} KB; crash buffer {len(crash)} chars"))


def main(argv):
    tag = argv[0]
    minutes = float(argv[argv.index("--soak") + 1]) if "--soak" in argv else 10.0
    only = argv[argv.index("--only") + 1].split(",") if "--only" in argv else \
        ["setup", "aspects", "lifecycle", "density", "soak", "nav"]
    (d.LOGS / tag).mkdir(parents=True, exist_ok=True)
    (d.LOGS / tag / "crash_before.txt").write_text(d.sh("logcat -b crash -d"), encoding="utf-8")
    steps = {"setup": lambda: setup(tag), "aspects": lambda: aspects(tag), "lifecycle": lambda: lifecycle(tag),
             "nav": lambda: nav(tag), "density": lambda: density(tag), "soak": lambda: soak(tag, minutes)}
    try:
        for s in only:
            if s in ("lifecycle", "density", "soak") and (not d.pid() or "player ready" not in logcat()):
                cold_to_level(tag, f"{s}_entry", card=False)
            steps[s]()
            print(RESULTS[-1], flush=True)
    finally:
        out = "\n".join(f"{k}: {v}" for k, v in RESULTS)
        (d.LOGS / tag / "matrix.txt").write_text(out + "\n", encoding="utf-8")
        print(out)


if __name__ == "__main__":
    main(sys.argv[1:])
