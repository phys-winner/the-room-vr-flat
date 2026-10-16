#!/usr/bin/env python3
"""Final 5.8 matrix, lifecycle and soak for the round-2 APK on realme-rmx3371 (no root).

Real touches through touch_ap (app_process injection, logical display pixels); the dev console (roomfix.dev) only reads
(menubtns, head?) so menu positions are found at every display size. Logical sizes by `wm size` (restored at the end).

  matrix_abb.py TAG [--only aspects,lifecycle,soak] [--soak MINUTES]

  aspects    16:9, 2:1, native 20:9, 4:3: cold start -> profile -> main menu (shot) -> Options (shot) -> back ->
             LEVEL SELECT -> Tutorial -> remark (shot) -> look swipe changes the head yaw -> Back = pause (shot) -> QUIT
  lifecycle  in the tutorial: Home + resume, screen off/on: same pid, look swipe still turns the view, gyro sensors
             registered again (dumpsys sensorservice)
  soak       MINUTES in the tutorial with both gyroscope options on and a look swipe every 30 s: same pid, PSS, crash
"""
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import devrun as d  # noqa: E402
import loc_check as L  # noqa: E402
import touch_ap as ta  # noqa: E402

PKG = d.PKG
EXT = d.EXT
SIZES = [("16x9", "1080x1920"), ("2x1", "1080x2160"), ("native", None), ("4x3", "1080x1440")]
RESULTS = []


def logical():
    out = d.sh("wm size")
    m = re.search(r"Override size: (\d+)x(\d+)", out) or re.search(r"Physical size: (\d+)x(\d+)", out)
    a, b = int(m.group(1)), int(m.group(2))
    return max(a, b), min(a, b)  # landscape


def tap(nx, ny):
    w, h = logical()
    with ta.Session() as s:
        s.tap(nx * w, ny * h)
    time.sleep(0.3)


def swipe_look():
    w, h = logical()
    y0, _ = L.head()
    with ta.Session() as s:
        s.swipe(w * 0.35, h * 0.55, w * 0.6, h * 0.55, 400)
    time.sleep(0.5)
    y1, _ = L.head()
    return y1 - y0


def buttons():
    out = {}
    for ln in L.dev("menubtns"):
        m = re.search(r"btn '([^']*)' key=(\S+) screen\(([-\d.]+) ([-\d.]+)\)", ln)
        if m:
            out[m.group(1)] = (float(m.group(3)), float(m.group(4)))
    return out


def wait(marker, timeout, n0):
    return L.wait_new(marker, timeout, n0)


def cold_start(tag):
    d.sh(f"am force-stop {PKG}")
    time.sleep(1)
    d.sh(f"am start -W -n {PKG}/{d.ACT}")
    wait("menu: screen 'Select Profile Screen'", 150, 0)
    time.sleep(2.5)
    n0 = len(L.log_text().splitlines())
    tap(0.40, 0.50)
    wait("menu: screen 'Main Menu Screen'", 60, n0)
    time.sleep(2.5)


def row(name, ok, note):
    RESULTS.append((name, ok, note))
    print(f"{'PASS' if ok else 'FAIL'} {name}: {note}", flush=True)


def to_tutorial(tag, label):
    b = buttons()
    n0 = len(L.log_text().splitlines())
    tap(*b["MenuButton Level Select"])
    time.sleep(2.5)
    b = buttons()
    tap(*b["MenuButton Chapter 0"])
    wait("player ready", 120, n0)
    wait("message: 'TUTORIAL_INTRO'", 60, n0)
    time.sleep(1.2)
    d.shot(tag, f"{label}_remark")
    return n0


def quit_from_pause(tag, label):
    d.sh("input keyevent 4")
    time.sleep(2.5)
    d.shot(tag, f"{label}_pause")
    b = buttons()
    tap(*b["Button [Quit]"])
    time.sleep(2.5)
    n0 = len(L.log_text().splitlines())
    tap(*buttons()["Button [Yes]"])
    wait("menu: screen 'Select Profile Screen'", 120, n0)
    time.sleep(2)


def aspects(tag):
    try:
        for label, size in SIZES:
            d.sh(f"wm size {size}" if size else "wm size reset")
            time.sleep(2)
            cold_start(tag)
            d.shot(tag, f"{label}_main_menu")
            fr = [ln for ln in L.log_text().splitlines() if "menu: framing" in ln][-1]
            b = buttons()
            tap(*b["MenuButton Options"])
            time.sleep(3)
            d.shot(tag, f"{label}_options")
            fo = [ln for ln in L.log_text().splitlines() if "menu: framing" in ln][-1]
            d.sh("input keyevent 4")
            time.sleep(2.5)
            to_tutorial(tag, label)
            dy = swipe_look()
            quit_from_pause(tag, label)
            crash = d.sh("logcat -b crash -d")
            row(f"aspect {label} ({'x'.join(map(str, logical()))})", abs(dy) > 3 and PKG not in crash,
                f"menu {fr.split('-> ')[-1]}; options {fo.split('-> ')[-1]}; look swipe {dy:+.1f} deg")
    finally:
        d.sh("wm size reset")


def sensors_registered():
    out = d.sh("dumpsys sensorservice")
    return len(re.findall(r"(?:Gyroscope Non-wakeup|linear_acceleration) 0x[0-9a-f]+ \| status: active", out))


def set_gyro(on):
    d.sh(f"am force-stop {PKG}")
    cfg = d.sh(f"cat {EXT}/port_settings.cfg")
    cfg = re.sub(r"gpos=\d", f"gpos={int(on)}", cfg)
    cfg = re.sub(r"grot=\d", f"grot={int(on)}", cfg)
    d.sh(f"printf '{cfg.strip()}\\n' > {EXT}/port_settings.cfg")


def lifecycle(tag):
    set_gyro(True)
    cold_start(tag)
    to_tutorial(tag, "lifecycle")
    pid0 = d.pid()
    s0 = sensors_registered()
    d.sh("input keyevent 3")  # Home
    time.sleep(4)
    s_bg = sensors_registered()
    d.sh(f"am start -n {PKG}/{d.ACT}")
    time.sleep(5)
    s1 = sensors_registered()
    dy = swipe_look()
    d.shot(tag, "lifecycle_after_home")
    row("lifecycle Home -> resume", d.pid() == pid0 and abs(dy) > 3 and s0 >= 2 and s_bg == 0 and s1 >= 2,
        f"pid {pid0} -> {d.pid()}, gyro sensors {s0} -> background {s_bg} -> {s1}, look swipe {dy:+.1f}")
    d.sh("input keyevent 26")  # screen off
    time.sleep(4)
    d.sh("input keyevent 26")  # on
    time.sleep(2)
    d.sh("input keyevent 82")  # unlock (no secure lock on the test device)
    time.sleep(4)
    dy = swipe_look()
    d.shot(tag, "lifecycle_after_screen")
    row("lifecycle screen off/on", d.pid() == pid0 and abs(dy) > 3, f"pid {d.pid()}, look swipe {dy:+.1f}")


def pss():
    out = d.sh(f"dumpsys meminfo {PKG}")
    m = re.search(r"TOTAL PSS:\s+(\d+)", out) or re.search(r"TOTAL\s+(\d+)", out)
    return int(m.group(1)) if m else -1


def soak(tag, minutes):
    if not d.pid():
        set_gyro(True)
        cold_start(tag)
        to_tutorial(tag, "soak")
    pid0, p0 = d.pid(), pss()
    t_end = time.time() + minutes * 60
    n = 0
    while time.time() < t_end:
        time.sleep(30)
        swipe_look()
        n += 1
    p1 = pss()
    crash = d.sh("logcat -b crash -d")
    d.shot(tag, "soak_end")
    row(f"soak {minutes} min (gyroscope on)", d.pid() == pid0 and PKG not in crash,
        f"pid {pid0} -> {d.pid()}, PSS {p0} -> {p1} kB, {n} look swipes, crash buffer {len(crash)} chars")


def main(a):
    tag = a[0]
    only = a[a.index("--only") + 1].split(",") if "--only" in a else ["aspects", "lifecycle", "soak"]
    minutes = float(a[a.index("--soak") + 1]) if "--soak" in a else 10
    d.sh("logcat -b crash -c")
    if "aspects" in only:
        aspects(tag)
    if "lifecycle" in only:
        lifecycle(tag)
    if "soak" in only:
        soak(tag, minutes)
    set_gyro(False)
    (d.LOGS / tag / "results.txt").write_text("\n".join(f"{'PASS' if ok else 'FAIL'} | {n} | {note}" for n, ok, note in RESULTS),
                                              encoding="utf-8")
    print("\n".join(f"{'PASS' if ok else 'FAIL'} | {n} | {note}" for n, ok, note in RESULTS))


if __name__ == "__main__":
    main(sys.argv[1:])
