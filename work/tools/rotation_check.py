#!/usr/bin/env python3
"""Real-touch rotation check; scene, target and view must already be prepared.

rotation_check.py TAG NAME X Y DX DY [--cancel] | --self-test
Coordinates/deltas are normalized; NAME is an exact underscore identifier.
--cancel closes the injector with the finger down: its finally sends ACTION_CANCEL.
The console only reads native state; this helper never aims or activates a stage.
"""
import argparse
import json
import math
import re
import time


def state(lines, name):
    text = "\n".join(lines)
    selection = re.findall(r"puzzle selection type=RotationGrabbable query='([^']*)' matches=(\d+) rank=(\d+) mutation=(\d+)", text)
    grab = re.findall(r"puzzle grab class=RotationGrabbable state=(\d+) simulation=(\d+) held=(\d+) owner=(\S+)", text)
    angle = re.findall(r"puzzle rotation angle=([-\d.]+)", text)
    active = re.findall(r"puzzle state type=RotationGrabbable active=(\d) enabled=(\d)", text)
    assert selection == [(name, "1", "2", "0")], "Target is absent, ambiguous or not an exact read-only match"
    assert len(grab) == len(angle) == len(active) == 1, "Incomplete native rotation report"
    a, b, held, owner = grab[0]
    value = float(angle[0])
    assert math.isfinite(value), "Non-finite native angle"
    return dict(state=int(a), simulation=int(b), held=int(held), owner=owner, angle=value,
                active=int(active[0][0]), enabled=int(active[0][1]))


def change(before, after):
    return abs((after["angle"] - before["angle"] + 180) % 360 - 180)


def run(args):
    import devrun as d
    import loc_check as lc
    import touch_ap as ta

    assert re.fullmatch(r"[A-Za-z0-9_.-]+", args.tag) and args.tag not in (".", ".."), "Unsafe TAG"
    assert re.fullmatch(r"[A-Za-z0-9_]+", args.name) and args.name not in ("start", "arm", "show", "hide"), "Unsafe NAME"
    x, y, dx, dy = args.xy
    assert all(math.isfinite(v) for v in args.xy), "Coordinates must be finite"
    assert all(0 < v < 1 for v in (x, y, x + dx, y + dy)), "Touch endpoints must be inside the screen"
    out = d.LOGS / args.tag
    out.mkdir(parents=True, exist_ok=True)
    result = dict(status="INCOMPLETE", target=args.name, touch=args.xy, cancel=args.cancel, states={})

    def shot(label):
        path, valid = d.shot(args.tag, "rotation_" + label)
        assert valid, "Invalid screenshot/focus/process: " + label
        return d.png_size(path.read_bytes())

    def query(label):
        assert f"{d.PKG}/{d.ACT}" in d.focus(), "Game lost foreground"
        lines = lc.dev("puzzle RotationGrabbable " + args.name, settle=.6)
        report = state(lines, args.name)
        result["states"][label] = dict(report, lines=lines)
        return report

    try:
        width, height = shot("before")
        assert width > height > 0, "Expected landscape screenshot"
        result["screen"] = [width, height]
        before = query("before")
        assert before["active"] == before["enabled"] == 1 and before["state"] == before["simulation"] == 0, before
        assert not before["held"] and before["owner"] == "-", before
        with ta.Session() as session:
            session.down(0, x * width, y * height)
            time.sleep(.35)
            down = query("down")
            assert down["held"] and down["owner"] in ("left", "right"), down
            session.move(0, (x + dx) * width, (y + dy) * height, ms=800)
            held = query("dragged")
            shot("held")
            result["change_degrees"] = change(before, held)
            if not args.cancel:
                session.up(0)
                time.sleep(.4)
        time.sleep(.4)
        after = query("released")
        shot("after")
        assert not after["held"] and after["owner"] == "-", after
        assert result["change_degrees"] >= 5, result
        result["status"] = "PASS"
        print(f"PASS: {args.name} changed {result['change_degrees']:.2f} degrees; native hold cleared")
    except Exception as exc:
        result.update(status="FAIL", error=str(exc))
        raise
    finally:
        (out / "rotation_result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


def self_test():
    lines = ["puzzle selection type=RotationGrabbable query='Test' matches=1 rank=2 mutation=0",
             "puzzle state type=RotationGrabbable active=1 enabled=1",
             "puzzle grab class=RotationGrabbable state=0 simulation=0 held=1 owner=right",
             "puzzle rotation angle=359.000000 velocity=0.000000"]
    assert state(lines, "Test")["owner"] == "right"
    assert change(dict(angle=359), dict(angle=1)) == 2
    assert change(dict(angle=-10), dict(angle=10)) == 20
    try:
        state([line.replace("matches=1", "matches=2") for line in lines], "Test")
    except AssertionError:
        pass
    else:
        raise AssertionError("Ambiguous target was accepted")
    print("PASS: exact target/owner parser and wrapped angular change")


if __name__ == "__main__":
    import sys
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    else:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("tag")
        parser.add_argument("name")
        parser.add_argument("xy", nargs=4, type=float, metavar="COORD")
        parser.add_argument("--cancel", action="store_true")
        run(parser.parse_args())
