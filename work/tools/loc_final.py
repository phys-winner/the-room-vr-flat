#!/usr/bin/env python3
"""R2-7 on one APK: in every chapter (location) three objects are grabbed by real touch (user request 2026-10-16:
"on every location check that three objects can be interacted with"; no full play-through).

Recipes found by sweeping each chapter (loc_check.py sweep / volsall): the game's own teleport by touch (HUD button +
marker), the view aimed with the dev console's `look` (navigation only), every grab a real finger (touch_ap.py).
Views are relative to the arrival yaw of the location. PASS per object = the game's controller reports holding it.

  loc_final.py TAG [chapter ...]     default: all six, front end -> chapter -> objects -> pause -> quit
"""
import json
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import devrun as d  # noqa: E402
import loc_check as L  # noqa: E402
import menu_cycle as M  # noqa: E402

# chapter index -> list of steps: ("tp", location) | ("hud", button) | ("wait", marker) | ("grab", name, dyaw, pitch) |
# ("pulls", n) (projector lever: n pulls, one per slide)
RECIPES = {
    0: ("Tutorial", [("wait", "SNAP_TURN_TUTORIAL"), ("hud", "turn_right"), ("wait", "TELEPORT_TUTORIAL"), ("tp", "ProjectorPosition"),
                     ("grab", "Collider", 0, -30), ("pulls", 4), ("wait", "TUTORIAL_TELEPORT_TO_DESK"),
                     ("tp", "DeskPosition"), ("grab", "TUT_Desk_Note", 0, -35), ("grab", "Collider", -30, -35)]),
    1: ("Egypt", [("tp", "CoffinDoor_Position"), ("grab", "EGP_Door_Lock", 28, 0),
                  ("grab", "EGP_Door_Right_Pin", 14, -42), ("grab", "EGP_Door_Left_Pin", 14, -42)]),
    2: ("Chapel", [("tp", "LeftRopePosition"), ("grab", "Rope01Grabbable", -5, -45), ("grab", "Rope02Grabbable", -5, -45),
                   ("tp", "HandlePosition"), ("grab", "Collider", 0, -50)]),
    3: ("Witch", [("tp", "PlayerPosition_Table"), ("grab", "WIT_Ingrdrum_Pourer", 0, -25),
                  ("grab", "WIT_IngrdrumTurner_GrabbableCollider", 9, 0), ("tp", "PlayerPosition_Candles"),
                  ("grab", "WIT_Can_Switch_001", 0, -35)]),
    4: ("Desert", [("tp", "DeskPosition"), ("grab", "DES_Crypt_Door_Key_Right_hidden", 0, -40), ("tp", "DoorCogsPosition"),
                   ("grab", "DES_GearBarWedge", 0, -40), ("hud", "inventory"), ("grab", "GogglesGrabbable_Rotation", None, None),
                   ("hud", "inventory")]),
    5: ("Epilogue", [("hud", "inventory"), ("grab", "Hints Tab 1 Grabbable", None, None), ("hud", "inventory"),
                     ("tp", "ProjectorPos"), ("grab", "jnt_Root", -58, -40)]),
}


def arrival_yaw():
    arr = [ln for ln in L.log_text().splitlines() if "view: arrived" in ln]
    m = re.search(r"yaw ([-\d.]+)", arr[-1]) if arr else None
    return float(m.group(1)) if m else L.head()[0]


def grab(tag, name, dyaw, pitch, y0, idx):
    """Aim, then one finger on the named object: down, drag, lift. PASS when the game holds it."""
    if dyaw is not None:
        L.turn_to(y0 + dyaw, pitch)
    vs = [v for v in L.volumes() if v["name"] == name]
    if not vs:
        return {"object": name, "pass": False, "why": "not on screen"}
    v = min(vs, key=lambda v: (v["x"] - 0.5) ** 2 + (v["y"] - 0.5) ** 2)
    n0 = len(L.log_text().splitlines())
    x, y = v["x"] * L.W, v["y"] * L.H
    d.shot(tag, f"{idx:02d}_{name[:24]}_before")
    with L.ta.Session() as s:
        s.down(0, x, y)
        time.sleep(0.6)
        s.move(0, x + 0.05 * L.W, y + 0.07 * L.H, ms=700)
        time.sleep(0.4)
        d.shot(tag, f"{idx:02d}_{name[:24]}_held")
        s.move(0, x - 0.03 * L.W, y + 0.02 * L.H, ms=500)
        time.sleep(0.3)
        s.up(0)
    time.sleep(1.0)
    new = [ln for ln in L.log_text().splitlines()[n0:] if "grab:" in ln]
    held = [ln for ln in new if "holding" in ln and "holding nothing" not in ln] or [ln for ln in new if " kind=" in ln]
    return {"object": name, "class": v["grabbable"], "screen": [v["x"], v["y"]], "pass": bool(held),
            "log": (held or new or ["(no grab log)"])[0].strip()[-150:]}


def ensure_main_menu():
    """The front end may stand on the profile screen (after a chapter or the game's end): profile 1 -> main menu."""
    if "FE_BUTTON_LEVEL_SELECT" in L.menubtns():
        return
    n0 = len(L.log_text().splitlines())
    L.tap_n(0.40, 0.50)
    L.wait_new("menu: screen 'Main Menu Screen'", 60, n0)
    time.sleep(2.5)


def run_chapter(tag, n):
    title, steps = RECIPES[n]
    ensure_main_menu()
    L.chapter(tag, n)
    n_start = len(L.log_text().splitlines())  # markers of this chapter run only
    time.sleep(2)
    y0 = arrival_yaw()
    results, idx = [], 0
    for st in steps:
        if st[0] == "tp":
            if not L.teleport(st[1], tag):
                results.append({"object": f"teleport {st[1]}", "pass": False})
            time.sleep(1.5)
            y0 = arrival_yaw()
        elif st[0] == "hud":
            L.hud(st[1])
            time.sleep(2.0)
        elif st[0] == "wait":
            L.wait_new(st[1], 90, n_start)
            time.sleep(1.5)
        elif st[0] == "pulls":
            for k in range(st[1]):
                time.sleep(6)
                r = grab(tag, "Collider", 0, -30, y0, 90 + k)
                print("  slide pull", k + 2, "PASS" if r["pass"] else "FAIL")
        elif st[0] == "grab":
            idx += 1
            r = grab(tag, st[1], st[2], st[3], y0, idx)
            results.append(r)
            print(f"  {'PASS' if r['pass'] else 'FAIL'} {st[1]} | {r.get('log', r.get('why', ''))[-110:]}")
    ok = sum(1 for r in results if r.get("pass") and not r["object"].startswith("teleport"))
    print(f"{title}: {ok} objects held by touch")
    return {"chapter": title, "objects": results, "passed": ok}


def main(a):
    tag = a[0]
    chapters = [int(x) for x in a[1:]] or list(RECIPES)
    out = []
    for n in chapters:
        out.append(run_chapter(f"{tag}_{n}", n))
        if not M.quit_to_menu(f"{tag}_{n}"):
            raise SystemExit("front end not reached / crash")
    (d.LOGS / f"{tag}_summary.json").write_text(json.dumps(out, indent=1, ensure_ascii=False), encoding="utf-8")
    for c in out:
        print(c["chapter"], c["passed"], [r["object"] for r in c["objects"] if r.get("pass")])


if __name__ == "__main__":
    main(sys.argv[1:])
