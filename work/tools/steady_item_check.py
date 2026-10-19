#!/usr/bin/env python3
"""Reproduce a stationary pickup, then carry the item using real touches on the designated device."""
import json
import re
import sys
import time
from pathlib import Path

import devrun as d
import loc_check as l
import r4_check as r
import touch_ap as ta


def run(tag, volume):
    data = d.adb("exec-out", "screencap -p", binary=True)
    r.W, r.H = l.W, l.H = d.png_size(data)
    assert r.W > r.H
    v = next(v for v in l.volumes() if v["name"] == volume and (v["grab"] or v["poke"]))
    x, y = v["x"] * r.W, v["y"] * r.H
    assert .05 * r.W < x < .9 * r.W and .05 * r.H < y < .93 * r.H
    out = d.LOGS / tag
    out.mkdir(parents=True, exist_ok=True)
    phases = {}
    with ta.Session() as s:
        time.sleep(1)
        s.down(0, x, y)
        time.sleep(2.5)
        response = "\n".join(r.dev("held?", settle=.3))
        m = re.search(r"held '([^']+)'", response)
        if not m:
            raise SystemExit("No actual held item: " + response)
        name = m[1]
        def sample(phase, count):
            points = []
            for _ in range(count):
                lines = r.dev("objs InventoryItemGrabbable", settle=.45)
                pat = r"obj '" + re.escape(name) + r"' pos\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)"
                match = re.search(pat, "\n".join(lines))
                if not match:
                    raise SystemExit("Missing live item pose")
                points.append([float(a) for a in match.groups()])
                time.sleep(.037)
            span = [max(p[i] for p in points) - min(p[i] for p in points) for i in range(3)]
            phases[phase] = {"points": points, "span": span}
            d.shot(tag, phase)
            print(phase, span, flush=True)
        sample("still", 14)
        s.move(0, x + 160, y + 20, ms=400)
        time.sleep(.8)
        sample("after_move", 10)
        s.up(0)
    time.sleep(.5)
    phases["item"] = name
    phases["volume"] = volume
    (out / "samples.json").write_text(json.dumps(phases, indent=2), encoding="utf-8")
    d.pull(tag)
    assert max(phases["still"]["span"]) <= .03, "Stationary item still oscillates"
    assert max(phases["after_move"]["span"]) <= .03, "Carried item still oscillates"
    print("PASS: stationary and carried item stable to 1 cm native log precision")


if __name__ == "__main__":
    run(sys.argv[1], sys.argv[2])
