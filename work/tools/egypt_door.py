#!/usr/bin/env python3
"""Egypt door bolts by real touch (UnlockDoorBolts), device redmi-note8pro only.

Hint: "lift the bottom two bolts and hook them onto the supports, then twist the top bolt free of its latch and
pull it over". Reached with the dev test aid `tpto CoffinDoor_Position`; every action is a real one-finger drag.
Screen paths come from the bolts' geometry seen from CoffinDoor_Position at pitch -50 (handle tip at rest, swung
out towards the player, lifted, swung back over the support).

  egypt_door.py bottom TAG      both bottom bolts
  egypt_door.py top TAG DY DX   top bolt: twist by DY px across the bar, then pull by DX px along it
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gesture as g  # noqa: E402
import touchroute as t  # noqa: E402

LEFT = (699, 579, [(67, 69, 600), (-128, -224, 900), (-78, -73, 700), (16, -52, 500)])
RIGHT = (1633, 581, [(-70, 71, 600), (125, -224, 900), (81, -75, 700), (-15, -54, 500)])


def report(n0, keys=("peek", "grab", "hint")):
    for ln in t.r.log_text().splitlines()[n0:]:
        if any(k in ln for k in keys) and "fps" not in ln and "dev>" not in ln:
            print(ln[-170:])


def go_door(pitch):
    t.d.main(["cmd", "tpto CoffinDoor_Position"])
    time.sleep(5)
    yaw, cur = t.head()
    t.look(-yaw, pitch - cur)
    time.sleep(1)


def bottom(tag):
    go_door(-50)
    n0 = len(t.r.log_text().splitlines())
    for side, (x, y, segs) in (("left", LEFT), ("right", RIGHT)):
        pin = "Left_Pin" if side == "left" else "Right_Pin"
        g.run(f"{tag}_{side}", x, y, segs, hold_ms=2600,
              cmds=[f"peek RotationGrabbable f118 {pin}", f"peek SliderGrabbable fe8 {pin}"])
        time.sleep(2.5)
    for c in ["peek RotationGrabbable f118 _Pin", "peek SliderGrabbable fe8 _Pin", "hint?"]:
        t.d.main(["cmd", c])
        time.sleep(0.8)
    t.shot(tag, "bottom_after")
    report(n0)


def top(tag, dy, dx):
    go_door(-6)
    v = t.find_vol("EGP_Door_Lock")
    x, y = t.vpx(v["x"], v["y"])
    n0 = len(t.r.log_text().splitlines())
    g.run(f"{tag}_top", int(x), int(y), [(0, dy, 700), (dx, 0, 1200)], hold_ms=2600,
          cmds=["peek RotationGrabbable f118 Door_Lock", "peek SliderGrabbable fe8 Door_Lock"])
    time.sleep(2.5)
    for c in ["peek RotationGrabbable f118 Door_Lock", "peek SliderGrabbable fe8 Door_Lock", "hint?"]:
        t.d.main(["cmd", c])
        time.sleep(0.8)
    t.shot(tag, "top_after")
    report(n0)


def main(a):
    if a[0] == "bottom":
        bottom(a[1])
    elif a[0] == "top":
        top(a[1], int(a[2]), int(a[3]))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
