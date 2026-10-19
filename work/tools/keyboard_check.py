#!/usr/bin/env python3
"""Real-touch organ regression on the designated device, with the revealed keyboard already active.

keyboard_check.py TAG | --self-test
Reads original key angles/owners and actuator state; never activates a stage or writes puzzle progress.
"""
import json
import re
import sys
import time

import devrun as d
import loc_check as l
import r4_check as r
import touch_ap as ta

# Nine objects remain in Keys[] but have activeSelf=false in the original PICO scene.
INACTIVE = {"CHP_OrganConsole_" + n for n in
            ("A1_Sharp", "B1", "C0", "D3", "F3", "F3_Sharp", "G0", "G3", "G3_Sharp")}


def key_contacts(lines):
    contacts, name = {}, None
    for line in lines:
        key = re.search(r"puzzle pose key '([^']+)'", line)
        if key:
            name = key[1]
        point = re.search(r"puzzle pose keyContact .* screen\(([-\d.]+) ([-\d.]+)\) visible=(\d)", line)
        if point:
            assert name and point[3] == "1", line
            x, y = float(point[1]), float(point[2])
            assert 0 < x < 1 and 0 < y < 1, line
            contacts[name] = {"x": x, "y": y}
    return contacts


def key_angles(lines):
    angles, name = {}, None
    for line in lines:
        key = re.search(r"puzzle pose key '([^']+)'", line)
        if key:
            name = key[1]
        angle = re.search(r"puzzle rotation angle=([-\d.]+)", line)
        if name and angle:
            angles[name] = float(angle[1])
    return angles


def key_state(lines):
    text = "\n".join(lines)
    grab = re.search(r"puzzle grab class=RotationGrabbable state=(\d+) simulation=(\d+) held=(\d+) owner=(\S+)", text)
    angle = re.search(r"puzzle rotation angle=([-\d.]+)", text)
    assert grab and angle, "Missing native key state/owner (requires the current developer build)"
    return {"state": int(grab[1]), "simulation": int(grab[2]), "held": int(grab[3]),
            "owner": grab[4], "angle": float(angle[1])}


def verify(lines, pressed, black):
    state = key_state(lines)
    assert state["state"] == state["simulation"] == 0, state
    assert bool(state["held"]) == pressed, state
    assert (state["angle"] >= 5 if pressed else abs(state["angle"]) < .1), state
    field = "f88" if black else "f84"
    actuator = [x for x in lines if "peek RotationActivatedAuto" in x and f"{field}=" in x]
    assert len(actuator) == 1, actuator
    value = float(re.search(rf"{field}=([-\d.]+)", actuator[0])[1])
    assert (value >= .95 if pressed else value <= .05), actuator
    return state


def self_test():
    assert key_contacts(["puzzle pose key 'test'", "puzzle pose keyContact 'pivot' screen(0.2 0.6) visible=1"]) == {
        "test": {"x": .2, "y": .6}}
    assert key_angles(["puzzle pose key 'test'", "puzzle rotation angle=15.000000"]) == {"test": 15}
    lines = ["puzzle grab class=RotationGrabbable state=0 simulation=0 held=1 owner=left",
             "puzzle rotation angle=15.000000 velocity=0.000000",
             "peek RotationActivatedAutoRotateModifier 'CHP_OrganConsole_A2_Sharp': f88=1.000 b8C=1"]
    assert verify(lines, True, True)["owner"] == "left"
    lines = ["puzzle grab class=RotationGrabbable state=0 simulation=0 held=0 owner=-",
             "puzzle rotation angle=0.000000 velocity=0.000000",
             "peek RotationActivatedAutoMoveModifier 'CHP_OrganConsole_B2': f84=0.000 b8C=0"]
    assert verify(lines, False, False)["owner"] == "-"
    print("PASS: native key/actuator report parser")


def main(tag):
    assert f"{d.PKG}/{d.ACT}" in d.focus(), "Game is not in the foreground"
    r.W, r.H = l.W, l.H = d.png_size(d.adb("exec-out", "screencap -p", binary=True))
    assert r.W > r.H
    l.HUD.update(r.hud_layout())
    out = d.LOGS / tag
    out.mkdir(parents=True, exist_ok=True)
    rows, results = [], []
    status = "INCOMPLETE"
    original_fps = int(re.search(r"^fps=(\d+)\r?$", d.read_ext("port_settings.cfg"), re.M)[1])

    def query(name, pressed, isolate=False):
        assert f"{d.PKG}/{d.ACT}" in d.focus(), "Game lost foreground"
        black = name.endswith("_Sharp")
        kind, field = ("Rotate", "f88") if black else ("Move", "f84")
        lines = r.dev(f"puzzle RotationGrabbable {name}",
                      f"peek RotationActivatedAuto{kind}Modifier {field},b8C {name}",
                      *(["puzzle KeyboardActivator CHP_OrganConsole"] if isolate else []), settle=.5)
        rows.append({"key": name, "pressed": pressed, "lines": lines})
        if isolate:
            angles = key_angles(lines)
            assert len(angles) == 46, "Missing neighbour key state"
            assert all(abs(v) < 5 for k, v in angles.items() if k != name), angles
        return verify(lines, pressed, black)

    try:
        lines = r.dev("puzzle KeyboardActivator CHP_OrganConsole")
        assert any("keyboard functional=1 keysActive=1 keys=46" in x for x in lines), "Keyboard stage is not active"
        names = set(re.findall(r"puzzle pose key '([^']+)'", "\n".join(lines)))
        contacts = key_contacts(lines)
        volumes = {v["name"]: v for v in l.volumes() if v["poke"] and v["name"] in names}
        assert len(names) == 46 and names - volumes.keys() == INACTIVE, (len(names), names - volumes.keys())
        for name in sorted(INACTIVE):
            lines = r.dev(f"puzzle GameObject {name}", settle=.4)
            rows.append({"key": name, "lines": lines})
            assert any("gameObject activeSelf=0 activeInHierarchy=0" in x for x in lines), name
        results.append({"case": "nine inactive original key objects", "status": "PASS"})
        names -= INACTIVE
        assert names <= contacts.keys(), "Missing original key contact points"
        volumes = contacts  # collider centres can project behind a neighbouring thick key
        d.shot(tag, "keyboard_before")
        with ta.Session() as s:
            time.sleep(1)
            for name in sorted(names):
                v = volumes[name]
                s.down(0, v["x"] * r.W, v["y"] * r.H)
                time.sleep(.35)
                pressed = query(name, True, isolate=True)
                s.up(0)
                time.sleep(.6)
                query(name, False)
                results.append({"key": name, "pressed": pressed, "status": "PASS"})
                print(f"PASS {len(results)-1}/37 {name}", flush=True)

            white, black = "CHP_OrganConsole_B2", "CHP_OrganConsole_A2_Sharp"
            def down(slot, name):
                v = volumes[name]
                s.down(slot, v["x"] * r.W, v["y"] * r.H)
                time.sleep(.35)

            down(0, white)
            down(1, black)
            a, b = query(white, True), query(black, True)
            assert a["owner"] != b["owner"], (a, b)
            s.down(7, .8 * r.W, .35 * r.H)
            s.move(7, .8 * r.W, .45 * r.H, ms=350)
            s.up(7)
            query(white, True)
            query(black, True)
            s.up(0)
            time.sleep(.6)
            query(white, False)
            query(black, True)
            down(0, white)
            query(white, True)
            s.up(1)
            time.sleep(.6)
            query(black, False)
            query(white, True)
            s.up(0)
            time.sleep(.6)
            query(white, False)
            results.append({"case": "black+white, both release orders, recontact, third pointer", "status": "PASS"})

            for fps_option in (0, 2):
                r.dev(f"fpsopt {fps_option}", settle=.5)
                for next_key in (white, "CHP_OrganConsole_G1"):
                    for gap in (0, 16, 40):
                        down(0, white)
                        down(1, black)
                        current = key_contacts(r.dev("puzzle KeyboardActivator CHP_OrganConsole", settle=.4))
                        v = current[next_key]
                        assert f"{d.PKG}/{d.ACT}" in d.focus()
                        # One device-side batch preserves the short gap without an ADB foreground round-trip.
                        s.send(["up 0", f"sleep {gap}", f"down 0 {v['x'] * r.W:.1f} {v['y'] * r.H:.1f}"])
                        time.sleep(.35)
                        query(next_key, True)
                        query(black, True)
                        if next_key != white:
                            query(white, False)
                        s.up(0)
                        s.up(1)
                        time.sleep(.6)
                        query(next_key, False)
                        query(black, False)
                        results.append({"case": "fast recontact", "key": next_key, "fps_option": fps_option,
                                        "gap_ms": gap, "status": "PASS"})
                down(0, white)
                down(1, black)
                v = volumes["CHP_OrganConsole_G1"]
                assert f"{d.PKG}/{d.ACT}" in d.focus()
                s.send(["up 0", f"down 2 {v['x'] * r.W:.1f} {v['y'] * r.H:.1f}", "up 2"])
                s.pos.pop(0, None)
                time.sleep(.5)
                query(white, False)
                query("CHP_OrganConsole_G1", False)
                query(black, True)
                s.up(1)
                time.sleep(.6)
                results.append({"case": "pending contact cancelled before acquisition", "fps_option": fps_option,
                                "status": "PASS"})
        status = "PASS"
        d.shot(tag, "keyboard_after")
    finally:
        r.dev(f"fpsopt {original_fps}", settle=.5)
        (out / "keyboard_result.json").write_text(json.dumps({"status": status, "results": results, "states": rows},
                                                             ensure_ascii=False, indent=2), encoding="utf-8")
        d.pull(tag)
    print("PASS: 37 active keys, 9 original inactive objects and independent two-hand chord", flush=True)


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    elif len(sys.argv) == 2:
        main(sys.argv[1])
    else:
        raise SystemExit(__doc__)
