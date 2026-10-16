#!/usr/bin/env python3
"""Real multi-touch on a device without root (round-2 test device realme-rmx3371).

work/tools/mtinject_ap/mtinject.dex runs as the adb shell user through app_process and injects MotionEvents with
stable pointer ids (InputManagerGlobal.injectInputEvent); coordinates are pixels of the screen in its current
rotation, i.e. the same pixels as a screencap. Same stdin protocol as the evdev tool work/tools/mtinject.

  touch_ap.py tap X Y [MS]
  touch_ap.py swipe X1 Y1 X2 Y2 [MS]
  touch_ap.py pinch CX CY D0 D1 [MS]            two fingers apart horizontally from distance D0 to D1
"""
import hashlib
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import devrun as d  # noqa: E402

DEX_LOCAL = Path(__file__).resolve().parent / "mtinject_ap" / "mtinject.dex"
DEX_DEVICE = "/data/local/tmp/mtinject.dex"
CMD = f"CLASSPATH={DEX_DEVICE} app_process /system/bin port.tools.MtInject"


def ensure_dex():
    want = hashlib.sha256(DEX_LOCAL.read_bytes()).hexdigest()
    if want not in d.sh(f"sha256sum {DEX_DEVICE} 2>/dev/null"):
        d.adb("push", str(DEX_LOCAL).replace("\\", "/"), DEX_DEVICE, check=True)
    return want


class Session:
    """A live injector: fingers stay down between calls (closed-loop routes can look and correct while holding)."""

    def __init__(self):
        ensure_dex()
        self.p = subprocess.Popen([d.ADB, "-s", d.SERIAL, "shell", CMD], stdin=subprocess.PIPE,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.pos = {}

    def send(self, lines):
        self.p.stdin.write(("\n".join(lines) + "\n").encode())
        self.p.stdin.flush()

    def down(self, slot, x, y):
        self.pos[slot] = (float(x), float(y))
        self.send([f"down {slot} {x:.1f} {y:.1f}"])

    def move(self, slot, x, y, ms=400, steps=20):
        x0, y0 = self.pos[slot]
        for i in range(1, steps + 1):
            self.send([f"move {slot} {x0 + (x - x0) * i / steps:.1f} {y0 + (y - y0) * i / steps:.1f}", "sync"])
            time.sleep(ms / 1000.0 / steps)
        self.pos[slot] = (float(x), float(y))

    def move2(self, a, xa, ya, b, xb, yb, ms=400, steps=20):
        """Two fingers move together (pinch / two-finger pan)."""
        ax, ay = self.pos[a]
        bx, by = self.pos[b]
        for i in range(1, steps + 1):
            t = i / steps
            self.send([f"move {a} {ax + (xa - ax) * t:.1f} {ay + (ya - ay) * t:.1f}",
                       f"move {b} {bx + (xb - bx) * t:.1f} {by + (yb - by) * t:.1f}", "sync"])
            time.sleep(ms / 1000.0 / steps)
        self.pos[a] = (float(xa), float(ya))
        self.pos[b] = (float(xb), float(yb))

    def up(self, slot):
        self.send([f"up {slot}"])
        self.pos.pop(slot, None)

    def tap(self, x, y, ms=80, slot=0):
        self.down(slot, x, y)
        time.sleep(ms / 1000.0)
        self.up(slot)

    def swipe(self, x1, y1, x2, y2, ms=400, slot=0):
        self.down(slot, x1, y1)
        time.sleep(0.05)
        self.move(slot, x2, y2, ms)
        time.sleep(0.05)
        self.up(slot)

    def close(self):
        try:
            self.send(["quit"])
            self.p.stdin.close()
            self.p.wait(timeout=10)
        except Exception:
            self.p.kill()
        err = self.p.stderr.read().decode(errors="replace").strip() if self.p.stderr else ""
        if err:
            print("mtinject:", err)

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()


def main(a):
    with Session() as s:
        if a[0] == "tap":
            s.tap(float(a[1]), float(a[2]), float(a[3]) if len(a) > 3 else 80)
        elif a[0] == "swipe":
            s.swipe(float(a[1]), float(a[2]), float(a[3]), float(a[4]), float(a[5]) if len(a) > 5 else 400)
        elif a[0] == "pinch":
            cx, cy, d0, d1 = (float(v) for v in a[1:5])
            ms = float(a[5]) if len(a) > 5 else 500
            s.down(0, cx - d0 / 2, cy)
            s.down(1, cx + d0 / 2, cy)
            time.sleep(0.05)
            s.move2(0, cx - d1 / 2, cy, 1, cx + d1 / 2, cy, ms)
            time.sleep(0.05)
            s.up(1)
            s.up(0)
        else:
            raise SystemExit(__doc__)
        time.sleep(0.2)


if __name__ == "__main__":
    main(sys.argv[1:])
