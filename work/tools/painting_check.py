#!/usr/bin/env python3
"""Real-touch PaintingPuzzle route; no device access during import or --self-test.

Prepare the scene, station, eyepiece and view first; every route point must already be visible.
  python work/tools/painting_check.py R062_desert1 END_PaintingPuzzle_01 --start
  python work/tools/painting_check.py R062_layout END_PaintingPuzzle_01 --inspect
  python work/tools/painting_check.py --self-test

Older helpers need explicitly measured --hold-range MIN MAX --hud-mode full|snap.
If DisplayFrames cannot identify the current cutout, pass measured --hud-insets LEFT TOP RIGHT BOTTOM.
--start calls only the authored StartPuzzle API. Solving uses touch_ap.Session and the real HUD slider.
"""
import argparse
import json
import math
import re
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ROUTES = ROOT / "work/evidence/R062_input_audit/PAINTING_ROUTES.json"
F = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
POSE = re.compile(rf"puzzle pose (\w+) '([^']*)' world\(({F}) ({F}) ({F})\) "
                  rf"screen\(({F}) ({F})\) visible=(\d) rotation\(({F}) ({F}) ({F}) ({F})\)")
PAINT = re.compile(rf"puzzle paint current='([^']*)' checkpoints=(\d+) rings=(\d+) blockers=(\d+) "
                   rf"connections=(\d+) activeRings=(\d+) collision=(\d+) radius=({F})")
SELECTION = re.compile(r"puzzle selection type=PaintingPuzzle query='([^']*)' matches=(\d+) rank=(\d+) mutation=(\d+)")
HOLD = re.compile(rf"puzzle hold visible=(\d+) current=({F}) min=({F}) max=({F}) hud=(0x[\da-fA-F]+|\d+)")
HINTS = {
    "WIT_PaintingPuzzle_Standard_01": "SolvePaintingPuzzle_Standard_01",
    "WIT_PaintingPuzzle_Standard_02": "SolvePaintingPuzzle_Standard_02",
    "WIT_PaintingPuzzle_wIntermediatePoints_01": "SolvePaintingPuzzlewIntermediatePoints01",
    "WIT_PaintingPuzzle_wIntermediatePoints_02": "SolvePaintingPuzzlewIntermediatePoints02",
    "WIT_PaintingPuzzle_Blockers_01": "SolvePaintingPuzzleBlockers01",
    "WIT_PaintingPuzzle_Blockers_02": "SolvePaintingPuzzleBlockers02",
    "WIT_PaintingPuzzle_Mix_01": "SolvePaintingPuzzleMixed01",
    "WIT_PaintingPuzzle_Mix_02": "SolvePaintingPuzzleMixed02",
    "WIT_PaintingPuzzle_END": "SolvePaintingPuzzle_End",
    "END_PaintingPuzzle_01": "SolvePaintingPuzzle",
    "END_PaintingPuzzle_02": "SolvePlynthPaintingPuzzle",
}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def parse_report(lines, name, allow_inactive=False):
    out = {"poses": {}, "checkpoints": {}, "lines": list(lines)}
    pending = None
    for line in lines:
        if "puzzle refused:" in line:
            raise RuntimeError(line.strip())
        m = SELECTION.search(line)
        if m:
            require("selection" not in out, "More than one Painting selection in one response")
            out["selection"] = dict(name=m[1], matches=int(m[2]), rank=int(m[3]), mutation=int(m[4]))
        m = PAINT.search(line)
        if m:
            out.update(current=m[1], checkpoints_count=int(m[2]), rings=int(m[3]), blockers=int(m[4]),
                       connections=int(m[5]), active_rings=int(m[6]), collision=int(m[7]), radius=float(m[8]))
        m = re.search(r"puzzle state type=PaintingPuzzle active=(\d) enabled=(\d)", line)
        if m:
            out["active"], out["enabled"] = map(int, m.groups())
        m = re.search(r"puzzle checkpoint index=(\d+) start=(\d) end=(\d) reached=(\d)", line)
        if m:
            pending = dict(zip(("index", "start", "end", "reached"), map(int, m.groups())))
        m = POSE.search(line)
        if m:
            p = dict(name=m[2], world=tuple(float(m[i]) for i in (3, 4, 5)),
                     screen=tuple(float(m[i]) for i in (6, 7)), visible=int(m[8]),
                     rotation=tuple(float(m[i]) for i in (9, 10, 11, 12)))
            if m[1] == "checkpoint":
                require(pending is not None, "Checkpoint pose has no native state record")
                require(p["name"] not in out["checkpoints"], "Ambiguous checkpoint name in native report")
                out["checkpoints"][p["name"]] = dict(p, **pending)
                pending = None
            else:
                out["poses"][m[1]] = p
        m = HOLD.search(line)
        if m:
            out["hold"] = dict(visible=int(m[1]), current=float(m[2]), minimum=float(m[3]),
                               maximum=float(m[4]), hud=int(m[5], 0))
        m = re.search(rf"puzzle camera world\(({F}) ({F}) ({F})\)", line)
        if m:
            out["camera"] = tuple(map(float, m.groups()))
        m = re.search(rf"head\?.*?cam\(({F}) ({F}) ({F})\)", line)
        if m and "camera" not in out:
            out["camera"] = tuple(map(float, m.groups()))
        m = re.search(r"hint: '([^']*)' step (\d+) of (\d+)", line)
        if m:
            out["hint"] = dict(id=m[1], step=int(m[2]), count=int(m[3]))
        m = re.search(r"dump: frame=\d+ screen=(\d+)x(\d+)", line)
        if m:
            out["screen_size"] = tuple(map(int, m.groups()))
    selection = out.get("selection", {})
    require(selection.get("name") == name and selection.get("matches") == 1 and selection.get("rank") == 2,
            "Native helper did not select exactly one exact named scene instance")
    require("current" in out and "camera" in out, "Incomplete native Painting state/camera response")
    require(allow_inactive or (out.get("active") == 1 and out.get("enabled") == 1),
            "Painting instance is not active and enabled")
    return out


def parse_insets(text, width, height):
    candidates = []
    frames = list(re.finditer(r"DisplayFrames w=(\d+) h=(\d+) r=(\d+)", text))
    for i, m in enumerate(frames):
        if (int(m[1]), int(m[2])) != (width, height):
            continue
        block = text[m.end():frames[i+1].start() if i+1 < len(frames) else m.end()+2400]
        cutout = re.search(r"(?:mDisplayCutout|DisplayCutout).*?insets=Rect\(\s*(\d+),\s*(\d+)\s*-\s*(\d+),\s*(\d+)\)", block)
        if cutout:
            candidates.append(tuple(map(int, cutout.groups())))
    values = set(candidates)
    require(len(values) == 1, "Current DisplayFrames cutout is unavailable/ambiguous; pass measured --hud-insets L T R B")
    return values.pop()


def density(text):
    values = re.findall(r"(?:Override|Physical) density:\s*(\d+)", text)
    require(bool(values), "wm density returned no usable display density")
    value = int(values[-1])/160
    require(.5 <= value <= 6, "Unexpected display density")
    return value


def layout(width, height, dp, insets, snap):
    left, top, right, bottom = insets
    require(min(insets) >= 0 and left+right < width and top+bottom < height, "Invalid measured cutout insets")
    # HudView.layoutButtons, with DisplayCutout safe insets; these are physical screen pixels.
    x, ytop = left+43*dp, top+108*dp
    ybottom = height-bottom-(120 if snap else 52)*dp
    require(0 < x < width and 0 < ytop < ybottom < height, "HUD distance track does not fit the measured screen")
    return dict(x=x, top=ytop, bottom=ybottom, dp=dp, insets=insets, snap=snap)


def hold_y(value, hold, hud):
    lo, hi = hold["minimum"], hold["maximum"]
    require(all(math.isfinite(x) for x in (value, lo, hi)) and hi > lo >= .05, "Invalid live hold range")
    require(lo <= value <= hi, f"Requested hold distance {value:.6f} is outside {lo:.6f}..{hi:.6f} m")
    return hud["bottom"]-(value-lo)/(hi-lo)*(hud["bottom"]-hud["top"])


def visible(p, width, height, hud):
    x, y = p["screen"]
    require(p["visible"] == 1 and all(math.isfinite(v) for v in p["world"]+p["screen"]),
            f"Point {p['name']} is behind the camera or invalid")
    require(.02 < x < .98 and .02 < y < .98, f"Point {p['name']} is offscreen/too near the edge: {x} {y}")
    px, py = x*width, y*height
    dp = hud["dp"]
    require(not (abs(px-hud["x"]) <= 34*dp and hud["top"]-30*dp <= py <= hud["bottom"]+30*dp),
            f"Point {p['name']} overlaps the HUD distance slider")
    require(not (px < hud["x"]+50*dp and py < hud["top"]-30*dp), f"Point {p['name']} overlaps Pause")
    require(not (px > width-hud["insets"][2]-100*dp and py > height-hud["insets"][3]-260*dp),
            f"Point {p['name']} overlaps the right HUD buttons")
    return px, py


def route_points(report, route):
    points = {route["start_id"]: report["poses"].get("start"), route["end_id"]: report["poses"].get("end")}
    for cp in route["checkpoints"]:
        if cp["id"] not in points:
            points[cp["id"]] = report["checkpoints"].get(cp["name"])
    require(all(points.values()), "Missing authored checkpoint(s) in the live helper report")
    require(report["checkpoints_count"] == len(route["checkpoints"])-2 and
            len(report["checkpoints"]) == report["checkpoints_count"] and
            report["rings"] == len(route["rings"]) and report["blockers"] == len(route["blockers"]),
            "Native initialization/counts differ from the authored route")
    require(bool(report["collision"]) == bool(route["collision_checks"]) and
            abs(report["radius"]-route["path_collision_radius"]) < 1e-6,
            "Native collision settings differ from the authored route")
    return [points[i] for i in route["route_ids"]]


def story_proof(route, before, after, new_lines, expected_saves):
    for line in new_lines:
        if any(s in line for s in expected_saves) and re.search(r"SaveProgress|checkpoint|saved", line, re.I):
            return line.strip()
        if re.search(r"OnPuzzleCompleted|OnOutroComplete", line) and ("Painting" in line or route["name"] in line):
            return line.strip()
    bh, ah = before.get("hint", {}), after.get("hint", {})
    if bh.get("id") == HINTS[route["name"]] and ah.get("id") and ah["id"] != bh["id"]:
        return f"Original HintManager advanced: {bh['id']} -> {ah['id']}"
    return None


def run(args):
    import devrun as d
    import loc_check as lc
    import touch_ap as ta

    data = json.loads(ROUTES.read_text(encoding="utf-8"))
    matches = [p for p in data["painting"] if p["name"] == args.name]
    require(len(matches) == 1, "NAME must exactly identify one of the 11 authored Painting routes")
    route = matches[0]
    require(re.fullmatch(r"[A-Za-z0-9_]+", args.name) is not None, "Unsafe/unsupported command name")
    require(re.fullmatch(r"[A-Za-z0-9_.-]+", args.tag) is not None, "TAG must be a simple local directory name")
    out = d.LOGS / args.tag
    out.mkdir(parents=True, exist_ok=True)
    result = dict(status="RUNNING", name=args.name, route_ids=route["route_ids"], states=[])
    log_start = lc.log_text()
    pid = d.pid()
    require(pid.isdigit(), "Game process is not running")

    def unity_log():
        return d.sh(f"logcat -d -v epoch --pid={int(pid)} -s Unity:I RoomFix:I")

    unity_before = set(unity_log().splitlines())
    saves = []
    for item in data["scenes"][route["scene"]]["flow"]["completion"]:
        targets = item["node"].get("defaultValues", {}).values()
        if not any(isinstance(v, dict) and v.get("resolved_name") == route["path"] for v in targets):
            continue
        saves = [x["element"]["defaultValues"]["%checkpointName"]["$content"] for x in item["downstream"]
                 if x["element"].get("member", {}).get("name") == "SaveProgress"]
    result["expected_saves"] = saves

    def shot(name):
        path, valid = d.shot(args.tag, name)
        require(valid, f"Invalid screenshot/focus/process at {name}")
        return d.png_size(path.read_bytes())

    def query(label, start=False, dump=False, completing=False):
        commands = (["dump"] if dump else []) + [f"puzzle PaintingPuzzle {args.name}"+(" start" if start else ""), "head?", "hint?"]
        report = parse_report(lc.dev(*commands, settle=.8), args.name, allow_inactive=completing)
        result["states"].append(dict(label=label, **report))
        return report

    def hold_state(report, initial=False):
        h = report.get("hold")
        if h is not None and (initial or h["visible"]):
            return h
        require(h is None, "Live native report says the brush/hold slider is no longer active")
        require(args.hold_range is not None and args.hud_mode is not None,
                "No live visible hold report; older candidates require measured --hold-range MIN MAX --hud-mode full|snap")
        lo, hi = args.hold_range
        return dict(visible=0 if initial else 1, current=math.dist(report["camera"], report["poses"]["brush"]["world"]),
                    minimum=lo, maximum=hi, hud=1 | (128 if args.hud_mode == "snap" else 0), fallback=True)

    session = None
    try:
        width, height = shot("painting_before")
        require(width > height > 0, "Expected a valid landscape screencap")
        before = query("before", start=args.start, dump=True)
        require(before.get("screen_size") == (width, height), "Unity screen dimensions differ from the screencap; viewport calibration required")
        if args.start:
            for attempt in range(12):
                time.sleep(.2)
                current = query(f"intro_{attempt}")
                old_points, new_points = route_points(before, route), route_points(current, route)
                stable = all(math.dist(a["world"], b["world"]) < .002 for a, b in zip(old_points, new_points))
                unit_scale = all(abs(math.dist(a["world"], b["world"])-math.dist(x, y)) < .003
                                 for a, b, x, y in zip(new_points, new_points[1:], route["route_puzzle_local"], route["route_puzzle_local"][1:]))
                before = current
                if stable and unit_scale:
                    break
            else:
                raise RuntimeError("Authored intro did not reach stable unit-scale waypoint geometry")
        dp = density(d.sh("wm density"))
        insets = tuple(args.hud_insets) if args.hud_insets else parse_insets(d.sh("dumpsys window displays"), width, height)
        initial_hold = hold_state(before, initial=True)
        hud = layout(width, height, dp, insets, bool(initial_hold["hud"] & 128))
        result["layout"] = dict(width=width, height=height, **hud)
        print("Measured HUD:", json.dumps(result["layout"], ensure_ascii=False))
        points = route_points(before, route)
        for p in points+[before["poses"]["brush"]]:
            visible(p, width, height, hud)
        require(before["current"] == "-" and before["connections"] == 0 and before["active_rings"] == 0,
                "Stage already has a live attempt; release/reset it before running this route")
        require(all(not cp["reached"] for cp in before["checkpoints"].values()), "Native checkpoint state is not clean")
        if args.inspect:
            result["status"] = "INSPECTED_NO_TOUCH"
            print("INSPECTED: native points and measured HUD registered; no finger events sent")
            return

        session = ta.Session()
        time.sleep(.5)
        session.down(0, *visible(before["poses"]["brush"], width, height, hud))
        time.sleep(.7)
        grabbed = query("grabbed")
        hold = hold_state(grabbed)
        require(hold["visible"] == 1 and hold["hud"] & 1, "Native hold slider is not visible")
        require(grabbed["current"] == points[0]["name"], "Brush did not start painting at the authored Start")
        require(bool(hold["hud"] & 128) == hud["snap"], "HUD snap/full layout changed during pickup")
        if hold.get("fallback"):
            m = re.search(rf"grab: PaintingPuzzleBrush .*?dist=({F}).*?hold ({F})\.\.({F})", lc.log_text()[len(log_start):])
            require(m is not None, "Older helper needs the original brush-grab depth/range log")
            require(abs(float(m[2])-hold["minimum"]) < .011 and abs(float(m[3])-hold["maximum"]) < .011,
                    "Explicit fallback range disagrees with the original grab log")
            hold["current"] = float(m[1])
        for p in points:
            hold_y(math.dist(p["world"], grabbed["camera"]), hold, hud)
        knob = hold_y(hold["current"], hold, hud)
        finger_y = round(knob, 1)
        # HudView.holdDown preserves this offset within 26 dp of the knob; taking its centre avoids a jump.
        grab_dy = knob-finger_y if abs(knob-finger_y) <= 26*dp else 0
        session.down(1, hud["x"], finger_y)
        time.sleep(.15)
        current_request = hold["current"]
        for k, target in enumerate(points[1:], 1):
            current_points = route_points(grabbed, route)
            target = current_points[k]
            px, py = visible(target, width, height, hud)
            brush = grabbed["poses"]["brush"]
            px += session.pos[0][0]-brush["screen"][0]*width
            py += session.pos[0][1]-brush["screen"][1]*height
            visible(dict(target, screen=(px/width, py/height)), width, height, hud)
            metres = math.dist(target["world"], grabbed["camera"])
            current_request = metres+current_request-math.dist(brush["world"], grabbed["camera"])
            sy = hold_y(current_request, hold, hud)-grab_dy
            session.move2(0, px, py, 1, hud["x"], sy, ms=650, steps=32)
            accepted = False
            for attempt in range(4):
                time.sleep(.2)
                after = query(f"waypoint_{k}_poll_{attempt}", completing=k == len(points)-1)
                ring_count = sum(any(i < k for i in indices) for indices in route["ring_hit_segment_indices"].values())
                if after["current"] == target["name"] and after["connections"] == k and after["active_rings"] >= ring_count:
                    accepted = True
                    break
                new_lines = lc.log_text()[len(log_start):].splitlines()
                if k == len(points)-1 and story_proof(route, before, after, new_lines, saves):
                    accepted = True
                    break
                require(after["current"] != "-" or k == len(points)-1, "Native attempt reset before this waypoint")
                require(after["current"] in (points[k-1]["name"], target["name"], "-"),
                        f"Unexpected checkpoint {after['current']!r}; route was not followed")
                if k == len(points)-1 and after["current"] == "-":
                    continue
                brush = after["poses"]["brush"]
                # Correct observed grab offset using real fingers, never the native depth/drag developer commands.
                sx, sy0 = session.pos[0]
                sx += (target["screen"][0]-brush["screen"][0])*width
                sy0 += (target["screen"][1]-brush["screen"][1])*height
                visible(dict(target, screen=(sx/width, sy0/height)), width, height, hud)
                live = hold_state(after)
                require(abs(live["minimum"]-hold["minimum"]) < 1e-6 and abs(live["maximum"]-hold["maximum"]) < 1e-6,
                        "Native hold range changed during the route")
                current_request = (current_request if live.get("fallback") else live["current"])+metres-math.dist(brush["world"], after["camera"])
                session.move2(0, sx, sy0, 1, hud["x"], hold_y(current_request, hold, hud)-grab_dy, ms=250, steps=16)
            shot(f"painting_waypoint_{k:02d}")
            if k < len(points)-1:
                require(accepted, f"Native checkpoint/ring progress did not reach waypoint {k}: {target['name']}")
            grabbed = after
        proof = None
        for n in range(12):
            after = query(f"completion_{n}", completing=True)
            native_new = lc.log_text()[len(log_start):].splitlines()
            unity_new = [ln for ln in unity_log().splitlines() if ln not in unity_before]
            new_lines = native_new+unity_new
            proof = story_proof(route, before, after, new_lines, saves)
            if proof:
                break
            time.sleep(.5)
        require(proof is not None, "UNCONFIRMED: no original completion/outro, expected SaveProgress, or corresponding HintManager transition")
        result.update(status="PASS", proof=proof)
        print("PASS:", proof)
    except Exception as exc:
        result.update(status="FAIL", error=str(exc))
        raise
    finally:
        if session is not None:
            try:
                for slot in list(session.pos)[::-1]:
                    session.up(slot)
            except (OSError, ValueError) as exc:
                result["touch_cleanup_error"] = str(exc)
            finally:
                session.close()
        result["roomfix_new"] = lc.log_text()[len(log_start):]
        result["unity_new"] = [ln for ln in unity_log().splitlines() if ln not in unity_before]
        (out / "painting_result.json").write_text(json.dumps(result, indent=2, ensure_ascii=False)+"\n", encoding="utf-8")
        print("Evidence:", out / "painting_result.json")


def self_test():
    fixture = """1.0 puzzle selection type=PaintingPuzzle query='END_PaintingPuzzle_01' matches=1 rank=2 mutation=0
1.0 puzzle state type=PaintingPuzzle active=1 enabled=1
1.0 puzzle paint current='Start' checkpoints=1 rings=0 blockers=0 connections=0 activeRings=0 collision=0 radius=0.000000
1.0 puzzle pose start 'Start' world(1.000000 2.000000 3.000000) screen(0.400000 0.500000) visible=1 rotation(0.000000 0.000000 0.000000 1.000000)
1.0 puzzle checkpoint index=0 start=0 end=0 reached=0 radius=0.010000 collisionDistance=0.035000
1.0 puzzle pose checkpoint 'Checkpoint 03' world(1.000000 2.100000 3.000000) screen(0.400000 0.450000) visible=1 rotation(0.000000 0.000000 0.000000 1.000000)
1.0 puzzle hold visible=1 current=1.000000 min=0.200000 max=1.600000 hud=129
1.0 head? yaw 0.0 pitch 0.0 eyes 0.00 m from the centre cam(0.000 0.000 0.000)
1.0 hint: 'SolvePaintingPuzzle' step 1 of 3
"""
    p = parse_report(fixture.splitlines(), "END_PaintingPuzzle_01")
    assert p["hold"]["hud"] == 129 and p["checkpoints"]["Checkpoint 03"]["reached"] == 0
    assert p["camera"] == (0, 0, 0) and p["current"] == "Start"
    try:
        parse_report(fixture.replace("matches=1", "matches=2").splitlines(), "END_PaintingPuzzle_01")
    except RuntimeError:
        pass
    else:
        raise AssertionError("Ambiguous selection was accepted")
    inset_fixture = "DisplayFrames w=2340 h=1080 r=1\n mDisplayCutout=DisplayCutout{insets=Rect(76, 0 - 0, 0)}"
    ins = parse_insets(inset_fixture, 2340, 1080)
    dp = density("Physical density: 440\nOverride density: 440")
    h = layout(2340, 1080, dp, ins, False)
    assert h["x"] == 194.25 and h["top"] == 297 and h["bottom"] == 937
    assert layout(2340, 1080, dp, ins, True)["bottom"] == 750
    assert hold_y(.2, p["hold"], h) == 937 and hold_y(1.6, p["hold"], h) == 297
    ky = hold_y(1, p["hold"], h)
    finger = round(ky, 1)
    assert abs((finger+(ky-finger))-ky) < 1e-9
    try:
        hold_y(2, p["hold"], h)
    except RuntimeError:
        pass
    else:
        raise AssertionError("Out-of-range depth was accepted")
    assert story_proof({"name": "END_PaintingPuzzle_01"}, p, dict(p, hint={"id": "MoveToTempleChamber"}), [], [])
    assert not story_proof({"name": "END_PaintingPuzzle_01"}, p, p, ["grab: holding PaintingPuzzleBrush"], [])
    print("PASS: offline helper parser, uniqueness, HUD density/cutout, knob offset, depth bounds and original progress checks")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tag", nargs="?")
    ap.add_argument("name", nargs="?")
    ap.add_argument("--start", action="store_true")
    ap.add_argument("--inspect", action="store_true")
    ap.add_argument("--hold-range", nargs=2, type=float, metavar=("MIN", "MAX"))
    ap.add_argument("--hud-mode", choices=("full", "snap"))
    ap.add_argument("--hud-insets", nargs=4, type=int, metavar=("LEFT", "TOP", "RIGHT", "BOTTOM"))
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.tag or not args.name:
        ap.error("TAG and exact NAME are required")
    try:
        run(args)
    except (RuntimeError, OSError, ValueError) as exc:
        raise SystemExit(f"FAIL: {exc}") from exc


if __name__ == "__main__":
    main()
