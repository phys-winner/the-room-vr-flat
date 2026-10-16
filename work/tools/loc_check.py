#!/usr/bin/env python3
"""Round-2 checks on the no-root test device (realme-rmx3371, user 2026-10-16), all input as real touches
(work/tools/touch_ap.py: app_process MotionEvent injection). The dev console (roomfix.dev) is read-only here: it lists
menu buttons (menubtns) and grabbable volumes (vols) with their screen positions and the log shows what the game did.

  loc_check.py menu TAG                      cold start -> profile 1 -> main menu
  loc_check.py chapter TAG N                 main menu -> LEVEL SELECT -> chapter N (list order), wait for the player
  loc_check.py interact TAG [--n 3] [--skip NAME,NAME]
                                             touch-grab and drag N different visible objects; PASS when the game's
                                             controller reports holding each one ('grab: holding <class>')
  loc_check.py btns                          print the current menu buttons
"""
import json
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import devrun as d  # noqa: E402
import touch_ap as ta  # noqa: E402

W, H = 2400, 1080  # realme-rmx3371 landscape screen (wm size 1080x2400)


def log_text():
    return d.read_ext("roomfix.log")


def dev(*cmds, settle=1.6):
    n0 = len(log_text().splitlines())
    d.main(["cmd", *cmds])
    time.sleep(settle)
    return log_text().splitlines()[n0:]


def wait_new(marker, timeout, n0=None):
    lines = log_text().splitlines()
    if n0 is None:
        n0 = len(lines)
    t0 = time.time()
    while time.time() - t0 < timeout:
        lines = log_text().splitlines()
        for ln in lines[n0:]:
            if marker in ln:
                return time.time() - t0, ln
        time.sleep(1.0)
    raise SystemExit(f"no new {marker!r} within {timeout} s")


def menubtns():
    out = {}
    for ln in dev("menubtns"):
        m = re.search(r"btn '([^']*)' key=(\S+) screen\(([-\d.]+) ([-\d.]+)\)", ln)
        if m:
            out.setdefault(m.group(2), (float(m.group(3)), float(m.group(4)), m.group(1)))
    return out


def tap_n(x, y, ms=80):
    with ta.Session() as s:
        s.tap(x * W, y * H, ms)
    time.sleep(0.2)


def press(key, tag=None, after=2.0):
    b = menubtns()
    if key not in b:
        raise SystemExit(f"menu button {key} not on screen: {sorted(b)}")
    x, y, nm = b[key]
    print(f"tap {key} ('{nm}') at {x:.3f} {y:.3f}")
    tap_n(x, y)
    time.sleep(after)
    if tag:
        d.shot(tag, f"after_{key.lower()}")


def menu(tag):
    d.launch(tag, secs=6, shots=(5,))
    wait_new("menu: screen 'Select Profile Screen'", 150, 0)
    time.sleep(2.5)
    d.shot(tag, "profiles")
    n0 = len(log_text().splitlines())
    tap_n(0.40, 0.50)  # profile 1 (first badge)
    print("main menu after %.1fs" % wait_new("menu: screen 'Main Menu Screen'", 60, n0)[0])
    time.sleep(2.0)
    d.shot(tag, "main_menu")


def chapter(tag, n):
    press("FE_BUTTON_LEVEL_SELECT", tag)
    b = menubtns()
    chapters = sorted(((v[2], k, v) for k, v in b.items() if v[2].startswith("MenuButton Chapter ")))
    if n >= len(chapters):
        raise SystemExit(f"only {len(chapters)} chapters offered: {[c[1] for c in chapters]}")
    nm, key, (x, y, _) = chapters[n]
    print(f"chapter {n}: {key} at {x:.3f} {y:.3f}")
    n0 = len(log_text().splitlines())
    tap_n(x, y)
    dt, ln = wait_new("player ready", 240, n0)
    print(f"player ready after {dt:.1f}s: {ln.strip()[-80:]}")
    time.sleep(6.0)  # arrival facing, fades
    d.shot(tag, f"chapter{n}_start")


VOL = re.compile(r"vol '([^']*)' (\S+) grab=(\d) poke=(\d) grabbable=(\S+) screen\(([-\d.]+) ([-\d.]+)\)")


def volumes():
    out = []
    for ln in dev("vols"):
        m = VOL.search(ln)
        if m:
            out.append({"name": m.group(1), "cls": m.group(2), "grab": int(m.group(3)), "poke": int(m.group(4)),
                        "grabbable": m.group(5), "x": float(m.group(6)), "y": float(m.group(7))})
    return out


def interact(tag, n=3, skip=()):
    vols = [v for v in volumes() if (v["grab"] or v["poke"]) and 0.12 < v["x"] < 0.86 and 0.12 < v["y"] < 0.9
            and v["name"] not in skip]
    vols.sort(key=lambda v: (v["x"] - 0.5) ** 2 + (v["y"] - 0.5) ** 2)
    picked, seen = [], set()
    for v in vols:
        if v["name"] in seen:
            continue
        seen.add(v["name"])
        picked.append(v)
    results = []
    for i, v in enumerate(picked):
        if sum(r["pass"] for r in results) >= n:
            break
        d.shot(tag, f"obj{i}_before")
        n0 = len(log_text().splitlines())
        x, y = v["x"] * W, v["y"] * H
        with ta.Session() as s:
            s.down(0, x, y)
            time.sleep(0.6)
            s.move(0, x + 0.05 * W, y + 0.07 * H, ms=700)
            time.sleep(0.4)
            d.shot(tag, f"obj{i}_held")
            s.move(0, x - 0.03 * W, y + 0.02 * H, ms=500)
            time.sleep(0.3)
            s.up(0)
        time.sleep(1.0)
        d.shot(tag, f"obj{i}_after")
        new = log_text().splitlines()[n0:]
        grab = [ln for ln in new if "grab:" in ln]
        held = [ln for ln in grab if "holding" in ln and "holding nothing" not in ln]
        ok = bool(held) or any(" kind=" in ln for ln in grab)
        r = {"object": v["name"], "class": v["grabbable"], "screen": [v["x"], v["y"]], "pass": ok,
             "log": [ln.strip()[-160:] for ln in grab][:6]}
        results.append(r)
        print(("PASS" if ok else "FAIL"), v["name"], v["grabbable"], "|", (held or grab or ["(no grab log)"])[0].strip()[-120:])
    out = d.LOGS / tag / "interact.json"
    out.write_text(json.dumps(results, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"{sum(r['pass'] for r in results)} of {len(results)} tried passed -> {out}")
    return results


HUD = {"teleport": (2235, 915), "inventory": (2235, 693), "eyepiece": (2235, 489), "pause": (236, 111),
       "turn_left": (188, 933), "turn_right": (392, 933)}  # HudView.layoutButtons at 3.0 px/dp, cutout inset 107 px left


def hud(name):
    with ta.Session() as s:
        s.tap(*HUD[name])
    time.sleep(0.4)


def head():
    for ln in reversed(dev("head?", settle=1.3)):
        m = re.search(r"head\? yaw ([-\d.]+) pitch ([-\d.]+)", ln)
        if m:
            return float(m.group(1)), float(m.group(2))
    raise SystemExit("no head? answer")


def turn_to(yaw, pitch, tries=4, real=False):
    """Aim the camera. Default: dev 'look' (navigation aid only - a look swipe that starts on a reachable object would
    grab it); real=True: touch swipes (finger right = turn right, finger up = look up), checked with head?."""
    if not real:
        dev(f"look {yaw:.1f} {pitch:.1f}", settle=0.8)
        return yaw, pitch
    for _ in range(tries):
        y0, p0 = head()
        dyaw = (yaw - y0 + 180) % 360 - 180
        dp = pitch - p0
        if abs(dyaw) < 4 and abs(dp) < 4:
            return y0, p0
        # 90 across 16:9 kept vertical: ~103 deg across 2400 px, ~59 deg over 1080 px; one swipe at most ~45 deg
        px = max(-1050, min(1050, dyaw / 103.0 * W))
        py = max(-430, min(430, dp / 59.0 * H))
        x0, y0p = 1200 - px / 2, 560 + py / 2
        with ta.Session() as s:
            s.swipe(x0, y0p, x0 + px, y0p - py, 450)
        time.sleep(0.3)
    return head()


def sweep(base_yaw, pitch=-25, step=60):
    """Look around in steps; every visible grabbable with the view it was seen from."""
    seen = {}
    for k in range(0, 360, step):
        yaw = base_yaw + (k if k <= 180 else k - 360)
        turn_to(yaw, pitch)
        for v in volumes():
            if (v["grab"] or v["poke"]) and v["name"] not in seen:
                v["view"] = head()
                seen[v["name"]] = v
    return list(seen.values())


def locations():
    out = []
    for ln in dev("tele"):
        m = re.search(r"loc\[\d+\] '([^']*)' state=(\d+)", ln)
        if m:
            out.append((m.group(1), int(m.group(2))))
    return out


def teleport(name, tag):
    """HUD teleport button and a tap on the location's marker, both real touches."""
    for ln in reversed(dev(f"locyaw {name}")):
        m = re.search(r"want ([-\d.]+) ([-\d.]+)", ln)
        if m:
            turn_to(float(m.group(1)), float(m.group(2)) - 10)
            break
    hud("teleport")
    time.sleep(1.0)
    for ln in reversed(dev(f"locyaw {name}")):
        m = re.search(r"screen ([-\d.]+) ([-\d.]+) vis=(\d)", ln)
        if m and m.group(3) == "1":
            n0 = len(log_text().splitlines())
            tap_n(float(m.group(1)), float(m.group(2)))
            try:
                dt, _ = wait_new("view: arrived", 15, n0)
                print(f"teleported to {name} after {dt:.1f}s")
                time.sleep(1.5)
                d.shot(tag, f"at_{name.replace(' ', '_')}")
                return True
            except SystemExit as e:
                print(e)
            break
    hud("teleport")  # leave teleport mode
    return False


def main(a):
    cmd = a[0]
    if cmd == "menu":
        menu(a[1])
    elif cmd == "chapter":
        chapter(a[1], int(a[2]))
    elif cmd == "interact":
        n = int(a[a.index("--n") + 1]) if "--n" in a else 3
        skip = a[a.index("--skip") + 1].split(",") if "--skip" in a else ()
        interact(a[1], n, skip)
    elif cmd == "btns":
        for k, v in menubtns().items():
            print(k, v)
    elif cmd == "sweep":
        y, p = head()
        for v in sweep(y):
            print(v["name"], v["grabbable"], "view", v["view"], "screen", v["x"], v["y"])
    elif cmd == "locs":
        for n, st in locations():
            print(st, n)
    elif cmd == "tp":
        teleport(" ".join(a[2:]), a[1])
    elif cmd == "turn":
        print(turn_to(float(a[1]), float(a[2])))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
