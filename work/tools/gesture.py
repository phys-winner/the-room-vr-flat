#!/usr/bin/env python3
"""Scripted one-finger gestures with screenshots while the finger is still down (real touch via mtinject).

  gesture.py TAG X Y  dx1,dy1[,ms]  [dx2,dy2[,ms] ...]   finger down at X,Y; each segment moves by (dx,dy) over
                                                        ms (default 900) and then holds 1.2 s while a shot is taken
"""
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import touchroute as t  # noqa: E402


def run(tag, x, y, segs, hold_ms=1200, settle_ms=300, cmds=()):
    """cmds: dev console commands (read-only queries such as peek) sent at every hold, after its shot"""
    lines = [f"down 0 {x} {y}", "sync", f"sleep {settle_ms}"]
    cx, cy = x, y
    times = []
    elapsed = settle_ms
    for dx, dy, ms in segs:
        n = max(1, ms // 25)
        for i in range(1, n + 1):
            lines += [f"move 0 {cx + dx * i // n} {cy + dy * i // n}", "sync", "sleep 25"]
        cx, cy = cx + dx, cy + dy
        elapsed += n * 25
        times.append(elapsed + hold_ms // 2)
        lines += [f"sleep {hold_ms}"]
        elapsed += hold_ms
    lines += ["up 0", "sync"]

    def shots():
        t0 = time.time()
        for i, at in enumerate(times):
            wait = at / 1000.0 - (time.time() - t0)
            if wait > 0:
                time.sleep(wait)
            t.shot(tag, f"seg{i + 1}")
            for c in cmds:
                t.d.main(["cmd", c])

    th = threading.Thread(target=shots)
    th.start()
    t.inject(lines)
    th.join()


def main(a):
    tag, x, y = a[0], int(a[1]), int(a[2])
    segs = []
    for s in a[3:]:
        v = [int(p) for p in s.split(",")]
        segs.append((v[0], v[1], v[2] if len(v) > 2 else 900))
    run(tag, x, y, segs)


if __name__ == "__main__":
    main(sys.argv[1:])
