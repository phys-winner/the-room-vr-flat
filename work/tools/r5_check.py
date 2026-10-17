#!/usr/bin/env python3
"""Round-5 checks (operator 2026-10-17: eyes at the centre after a teleport, no movement when turning, the position
gauge; test device poco-x3-nfc). Input is real multi-touch (touch_ap.py); screenshots are taken while the fingers
still push. The developer console (roomfix.dev) only reads state: `state` needs a developer build.

  r5_check.py push TAG NAME KIND AMOUNT [AT]   two-finger move, a shot AT s into it; KIND pinch (spread by AMOUNT
                                                px), panx / pany (both fingers by AMOUNT px)
  r5_check.py state                            view? + bounds?: the eyes in tracking space and in the world
  r5_check.py start TAG                        50-s cold start: logcat lines of the game pid, Unity/port tags
  r5_check.py soak TAG SECONDS                 two-finger moves and looks every ~3 s; same pid, PSS before / after
  r5_check.py sheet OUT COLS L T R B SCALE IMG...   contact sheet of one crop box from several screenshots
"""
import re
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import devrun as d  # noqa: E402
import loc_check as lc  # noqa: E402
import touch_ap as ta  # noqa: E402

W, H = 2400, 1080


def state():
    out = {}
    for ln in lc.dev("view?", "bounds?", settle=1.6):
        m = re.search(r"view\? eyes \(([-\d.]+) ([-\d.]+) ([-\d.]+)\) .*?, ([-\d.]+) m;", ln)
        if m:
            out["eye_ts"] = tuple(float(v) for v in m.groups()[:3])
            out["off_m"] = float(m.group(4))
            out["view"] = ln.strip()[ln.find("view?"):]
        m = re.search(r"eyes\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) ([-\d.]+) m from it, ([-\d.]+) above", ln)
        if m:
            out["eye_w"] = tuple(float(v) for v in m.groups()[:3])
            out["h_from_loc"] = float(m.group(4))
            out["above_rig"] = float(m.group(5))
    return out


def two(s, a0, b0, a1, b1, ms=900, steps=30):
    s.down(0, *a0)
    s.down(1, *b0)
    time.sleep(0.08)
    s.move2(0, a1[0], a1[1], 1, b1[0], b1[1], ms=ms, steps=steps)
    time.sleep(0.1)
    s.up(1)
    s.up(0)


def push(tag, name, kind, amount, at=1.6, ms=3000, cx=1200, cy=620):
    if kind == "pinch":
        a0, b0, a1, b1 = (cx - 350, cy), (cx + 350, cy), (cx - 350 - amount / 2, cy), (cx + 350 + amount / 2, cy)
    elif kind == "panx":
        a0, b0, a1, b1 = (cx - 150, cy), (cx + 150, cy), (cx - 150 + amount, cy), (cx + 150 + amount, cy)
    else:
        a0, b0, a1, b1 = (cx - 150, cy), (cx + 150, cy), (cx - 150, cy + amount), (cx + 150, cy + amount)
    shot = threading.Thread(target=lambda: (time.sleep(at), d.shot(tag, name)))
    with ta.Session() as s:
        time.sleep(1.0)  # injector warm-up on poco-x3-nfc
        s.down(0, *a0)
        s.down(1, *b0)
        time.sleep(0.08)
        shot.start()
        s.move2(0, a1[0], a1[1], 1, b1[0], b1[1], ms=ms, steps=60)
        shot.join()
        s.up(1)
        s.up(0)
    time.sleep(0.3)


def start(tag):
    d.launch(tag, secs=50, shots=(5, 30, 48))
    pid = d.pid()
    lines = (d.LOGS / tag / "logcat.txt").read_text(encoding="utf-8", errors="replace").splitlines()
    mine = [ln for ln in lines if re.match(r"\s*[\d.]+\s+%s\s" % pid, ln)] if pid else []
    tags = {}
    for ln in mine:
        m = re.match(r"\s*[\d.]+\s+\d+\s+\d+\s+\w\s+([^:]+):", ln)
        if m:
            tags[m.group(1).strip()] = tags.get(m.group(1).strip(), 0) + 1
    port = sum(v for k, v in tags.items() if re.search(r"Unity|RoomFix|roomfix|PICO|PXR|IL2CPP|il2cpp", k))
    print(f"pid {pid}: {len(mine)} logcat lines, Unity/RoomFix/PICO/IL2CPP lines {port}")
    for k, v in sorted(tags.items(), key=lambda kv: -kv[1])[:12]:
        print(f"  {v:4d} {k}")


def pss(pid):
    m = re.search(r"TOTAL PSS:\s+(\d+)", d.sh(f"dumpsys meminfo {pid} | grep -E 'TOTAL PSS'"))
    return int(m.group(1)) if m else -1


def soak(tag, secs):
    moves = [((950, 600), (1250, 600), (1350, 600), (1650, 600)), ((1350, 600), (1650, 600), (950, 600), (1250, 600)),
             ((1000, 600), (1400, 600), (800, 600), (1600, 600)), ((800, 600), (1600, 600), (1000, 600), (1400, 600)),
             ((1050, 700), (1350, 700), (1050, 450), (1350, 450)), ((1050, 450), (1350, 450), (1050, 700), (1350, 700))]
    pid = d.pid()
    p0, t0, n = pss(pid), time.time(), 0
    with ta.Session() as s:
        time.sleep(1.0)
        while time.time() - t0 < secs:
            two(s, *moves[n % len(moves)])
            if n % len(moves) == len(moves) - 1:
                s.swipe(1000, 560, 1400, 560, 400)
            n += 1
            time.sleep(2.0)
    time.sleep(1.0)
    d.shot(tag, "soak_end")
    p1 = pss(d.pid())
    print(f"soak {time.time() - t0:.0f} s, {n} moves, pid {pid} -> {d.pid()}, PSS {p0} -> {p1} KB")


def sheet(out, cols, box, scale, imgs):
    from PIL import Image, ImageDraw  # noqa: PLC0415
    ims = [Image.open(p).convert("RGB").crop(box) for p in imgs]
    w, h = int(ims[0].width * scale), int(ims[0].height * scale)
    rows = (len(ims) + cols - 1) // cols
    sh = Image.new("RGB", (cols * w, rows * h), (40, 40, 40))
    for i, im in enumerate(ims):
        x, y = (i % cols) * w, (i // cols) * h
        sh.paste(im.resize((w, h), Image.LANCZOS), (x, y))
        ImageDraw.Draw(sh).rectangle([x, y, x + w - 1, y + h - 1], outline=(90, 90, 90))
    sh.save(out, quality=88)
    print(out, sh.size)


def main(a):
    if not a:
        raise SystemExit(__doc__)
    if a[0] == "push":
        push(a[1], a[2], a[3], float(a[4]), float(a[5]) if len(a) > 5 else 1.6)
    elif a[0] == "state":
        print(state())
    elif a[0] == "start":
        start(a[1])
    elif a[0] == "soak":
        soak(a[1], float(a[2]))
    elif a[0] == "sheet":
        sheet(a[1], int(a[2]), tuple(int(v) for v in a[3:7]), float(a[7]), a[8:])
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
