#!/usr/bin/env python3
"""Real-touch routes for TOUCH_GAMEPLAY_VERIFIED (device redmi-note8pro only).

Every game action is a real touch on the panel: multi-touch protocol-B events are written into the touchscreen
evdev node by work/tools/mtinject (root), so Android's InputReader/dispatcher, the window and the app see genuine
fingers. libroomfix's dev console is used read-only to locate targets on screen (locyaw, vols, head?) and to wait
for game log markers; it never performs the actions.

  touchroute.py tutorial TAG [--profile N]   new game on an empty profile -> tutorial safe solved by touch
"""
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import devrun as d  # noqa: E402
import route as r  # noqa: E402

PANEL_W, PANEL_H = 1080, 2340  # NVTCapacitiveTouchScreen natural (portrait) size
SCREEN_W, SCREEN_H = 2340, 1080
TOUCH_DEV = "/dev/input/event2"
VIEW_X0, VIEW_W = 0, 2340  # game view = whole panel since boot.config androidRenderOutsideSafeArea=1 (p060)

# HUD button centres (screen px) for the default HUD layout (HudView.layoutButtons, 2.75 dp/px)
HUD = {"teleport": (2189, 929), "inventory": (2189, 725), "eyepiece": (2189, 538), "pause": (194, 102),
       "turn_left": (211, 945), "turn_right": (398, 945)}


def rotation():
    out = d.sh("dumpsys window | grep mCurrentRotation")
    m = re.search(r"ROTATION_(\d+)", out)
    return int(m.group(1)) if m else 90


ROT = None


def inject(lines):
    global ROT
    if ROT is None:
        ROT = rotation()
    script = "\n".join(lines) + "\n"
    p = d.subprocess.run([d.ADB, "-s", d.SERIAL, "shell", f"su -c '/data/local/tmp/mtinject {TOUCH_DEV} {PANEL_W} {PANEL_H} {ROT}'"],
                         input=script.encode(), capture_output=True, timeout=120)
    if p.returncode != 0:
        raise SystemExit(f"mtinject failed: {p.stderr.decode(errors='replace')}")


class Session:
    """A live mtinject stream: fingers stay down between calls, so the route can look (shots, dev queries)
    and correct (closed loop) while a finger holds something."""

    def __init__(self):
        global ROT
        if ROT is None:
            ROT = rotation()
        self.p = d.subprocess.Popen([d.ADB, "-s", d.SERIAL, "shell",
                                     f"su -c '/data/local/tmp/mtinject {TOUCH_DEV} {PANEL_W} {PANEL_H} {ROT}'"],
                                    stdin=d.subprocess.PIPE, stdout=d.subprocess.DEVNULL, stderr=d.subprocess.DEVNULL)
        self.pos = {}

    def send(self, lines):
        self.p.stdin.write(("\n".join(lines) + "\n").encode())
        self.p.stdin.flush()

    def down(self, slot, x, y):
        self.pos[slot] = (int(x), int(y))
        self.send([f"down {slot} {int(x)} {int(y)}", "sync"])

    def move(self, slot, x, y, ms=500, steps=20):
        x0, y0 = self.pos[slot]
        for i in range(1, steps + 1):
            self.send([f"move {slot} {int(x0 + (x - x0) * i / steps)} {int(y0 + (y - y0) * i / steps)}", "sync"])
            time.sleep(ms / 1000.0 / steps)
        self.pos[slot] = (int(x), int(y))

    def up(self, slot):
        self.send([f"up {slot}", "sync"])
        self.pos.pop(slot, None)

    def close(self):
        for slot in list(self.pos):
            self.up(slot)
        self.p.stdin.close()
        self.p.wait(timeout=30)


def tap(x, y, ms=60):
    inject([f"down 0 {int(x)} {int(y)}", "sync", f"sleep {ms}", "up 0", "sync"])


def hold(x, y, ms):
    inject([f"down 0 {int(x)} {int(y)}", "sync", f"sleep {ms}", "up 0", "sync"])


def drag(points, step_ms=16, steps_per_seg=12, hold_start_ms=0, hold_end_ms=0, slot=0):
    """One finger through a polyline of screen points (interpolated, one frame-ish per step)."""
    x0, y0 = points[0]
    lines = [f"down {slot} {int(x0)} {int(y0)}", "sync"]
    if hold_start_ms:
        lines.append(f"sleep {hold_start_ms}")
    for (ax, ay), (bx, by) in zip(points, points[1:]):
        for i in range(1, steps_per_seg + 1):
            t = i / steps_per_seg
            lines += [f"move {slot} {int(ax + (bx - ax) * t)} {int(ay + (by - ay) * t)}", "sync", f"sleep {step_ms}"]
    if hold_end_ms:
        lines.append(f"sleep {hold_end_ms}")
    lines += [f"up {slot}", "sync"]
    inject(lines)


def pinch(cx, cy, d0, d1, ms=600, steps=30):
    """Two fingers on a horizontal line around (cx, cy), spread from distance d0 to d1 (px)."""
    lines = [f"down 0 {int(cx - d0 / 2)} {int(cy)}", f"down 1 {int(cx + d0 / 2)} {int(cy)}", "sync", "sleep 40"]
    for i in range(1, steps + 1):
        dd = d0 + (d1 - d0) * i / steps
        lines += [f"move 0 {int(cx - dd / 2)} {int(cy)}", f"move 1 {int(cx + dd / 2)} {int(cy)}", "sync", f"sleep {ms // steps}"]
    lines += ["up 0", "up 1", "sync"]
    inject(lines)


def vpx(nx, ny):
    """game-view normalised coordinates -> screen px"""
    return VIEW_X0 + nx * VIEW_W, ny * SCREEN_H


def hud(name):
    tap(*HUD[name])


# ---------------------------------------------------------------- read-only queries through the dev console
def devq(cmd, pattern, timeout=6.0):
    before = len(r.log_text().splitlines())
    d.main(["cmd", cmd])
    t0 = time.time()
    while time.time() - t0 < timeout:
        lines = r.log_text().splitlines()[before:]
        for ln in lines:
            m = re.search(pattern, ln)
            if m:
                return m, lines
        time.sleep(0.4)
    raise SystemExit(f"no answer to {cmd!r}")


def head():
    m, _ = devq("head?", r"head\? yaw (\S+) pitch (\S+)")
    return float(m.group(1)), float(m.group(2))


def look(dyaw, dpitch, hfov=100.0, vfov=59.2):
    """Swipe-look by real drags in the free middle area of the screen (drag right = turn right)."""
    px_per_deg_x = VIEW_W / hfov
    px_per_deg_y = SCREEN_H / vfov
    remaining_x, remaining_y = dyaw * px_per_deg_x, -dpitch * px_per_deg_y
    while abs(remaining_x) > 2 or abs(remaining_y) > 2:
        sx = max(-1400, min(1400, remaining_x))
        sy = max(-600, min(600, remaining_y))
        x0 = SCREEN_W / 2 - sx / 2
        y0 = SCREEN_H / 2 - sy / 2
        drag([(x0, y0), (x0 + sx, y0 + sy)], step_ms=12, steps_per_seg=20)
        remaining_x -= sx
        remaining_y -= sy
        time.sleep(0.3)


def face_loc(name, pitch=None):
    m, _ = devq(f"locyaw {name}", r"locyaw '" + re.escape(name) + r"' want (\S+) (\S+) cur (\S+) (\S+)")
    wy, wp, cy, cp = (float(m.group(i)) for i in range(1, 5))
    dy = (wy - cy + 180) % 360 - 180
    look(dy, (pitch if pitch is not None else wp) - cp)


def loc_screen(name):
    m, _ = devq(f"locyaw {name}", r"locyaw '" + re.escape(name) + r"' .* screen (\S+) (\S+) vis=(\d)")
    return float(m.group(1)), float(m.group(2)), m.group(3) == "1"


def volumes():
    before = len(r.log_text().splitlines())
    devq("vols", r"volumes: \d+ registered")
    time.sleep(0.6)
    lines = r.log_text().splitlines()[before:]
    out = []
    for ln in lines:
        m = re.search(r"vol '(.*)' (\S+) grab=(\d) poke=(\d) grabbable=(\S+) screen\((\S+) (\S+)\)", ln)
        if m:
            out.append({"name": m.group(1), "cls": m.group(2), "grabbable": m.group(5),
                        "x": float(m.group(6)), "y": float(m.group(7))})
    return out


def receivers():
    before = len(r.log_text().splitlines())
    devq("recv", r"receivers: \d+")
    time.sleep(0.6)
    out = []
    for ln in r.log_text().splitlines()[before:]:
        m = re.search(r"recv '(.*)' state=(-?\d+) screen\((\S+) (\S+)\) vis=(\d)", ln)
        if m:
            out.append({"name": m.group(1), "state": int(m.group(2)), "x": float(m.group(3)), "y": float(m.group(4)),
                        "vis": m.group(5) == "1"})
    return out


def find_vol(substr, cls=None):
    for v in volumes():
        if substr.lower() in v["name"].lower() and (cls is None or cls in (v["cls"], v["grabbable"])):
            return v
    return None


def wait(marker, timeout=90):
    print(f"wait {marker!r}: %.1fs" % r.wait_marker(marker, timeout))


def shot(tag, name):
    d.shot(tag, name)


def key_into_lock(k, h, rad=110):
    """One finger: drag an inventory item onto a receiver, wait for the hand-over, then turn it clockwise."""
    import math
    (kx, ky), (hx, hy) = k, h
    pts = [(kx, ky), ((kx + hx) / 2, (ky + hy) / 2), (hx - rad, hy)]
    lines = [f"down 0 {int(kx)} {int(ky)}", "sync", "sleep 250"]
    for (ax, ay), (bx, by) in zip(pts, pts[1:]):
        for i in range(1, 16):
            t_ = i / 15
            lines += [f"move 0 {int(ax + (bx - ax) * t_)} {int(ay + (by - ay) * t_)}", "sync", "sleep 20"]
    lines += ["sleep 1600"]  # item inserted: the grab hands over to the lock, libroomfix re-grabs it
    for i in range(1, 41):  # clockwise half circle over the top of the receiver
        a = math.pi + math.pi * i / 40
        lines += [f"move 0 {int(hx + rad * math.cos(a))} {int(hy + rad * math.sin(a))}", "sync", "sleep 25"]
    lines += ["sleep 600", "up 0", "sync"]
    inject(lines)


# ---------------------------------------------------------------- routes
def continue_game(tag, profile=3):
    r.route_menu(tag)
    wait("menu: screen 'Select Profile Screen'", 60)
    time.sleep(1.5)
    tap(*{1: vpx(0.396, 0.493), 2: vpx(0.501, 0.493), 3: vpx(0.606, 0.493)}[profile])
    wait("menu: screen 'Main Menu Screen'", 30)
    time.sleep(1.5)
    tap(1302, 635)  # CONTINUE GAME
    wait("player ready", 150)
    time.sleep(3)


def teleport_to(name, pitch=-20):
    hud("teleport")
    time.sleep(1.0)
    face_loc(name, pitch=pitch)
    time.sleep(1.0)
    x, y, vis = loc_screen(name)
    tap(*vpx(x, y))
    time.sleep(3.5)


def twist(cx, cy, degrees, rad=90, start_deg=180, step_deg=3, hold_ms=250, end_ms=400):
    """Finger down on a knob centre, out to the side, then around it (positive = clockwise on screen)."""
    import math
    sx, sy = cx + rad * math.cos(math.radians(start_deg)), cy + rad * math.sin(math.radians(start_deg))
    lines = [f"down 0 {int(cx)} {int(cy)}", "sync", f"sleep {hold_ms}"]
    for i in range(1, 9):
        lines += [f"move 0 {int(cx + (sx - cx) * i / 8)} {int(cy + (sy - cy) * i / 8)}", "sync", "sleep 20"]
    n = max(1, int(abs(degrees) / step_deg))
    for i in range(1, n + 1):
        a = math.radians(start_deg + degrees * i / n)
        lines += [f"move 0 {int(cx + rad * math.cos(a))} {int(cy + rad * math.sin(a))}", "sync", "sleep 25"]
    lines += [f"sleep {end_ms}", "up 0", "sync"]
    inject(lines)


def crank(kx, ky, cx, cy, turns=2.0, step_deg=6, step_ms=22, hold_ms=300):
    """Finger down on a crank knob at (kx, ky), then circles around the axis (cx, cy); positive = clockwise."""
    import math
    r = math.hypot(kx - cx, ky - cy)
    a0 = math.atan2(ky - cy, kx - cx)
    lines = [f"down 0 {int(kx)} {int(ky)}", "sync", f"sleep {hold_ms}"]
    n = int(abs(turns) * 360 / step_deg)
    for i in range(1, n + 1):
        a = a0 + math.copysign(math.radians(step_deg * i), turns)
        lines += [f"move 0 {int(cx + r * math.cos(a))} {int(cy + r * math.sin(a))}", "sync", f"sleep {step_ms}"]
    lines += ["sleep 300", "up 0", "sync"]
    inject(lines)


def tutorial(tag, profile=3):
    # an empty profile: remove its save (test device policy allows clearing the task's app data)
    d.sh(f"su -c 'rm -f {d.EXT}/{profile - 1}.save'")
    r.route_menu(tag)
    wait("menu: screen 'Select Profile Screen'", 60)
    time.sleep(1.5)
    tap(*{1: vpx(0.396, 0.493), 2: vpx(0.501, 0.493), 3: vpx(0.606, 0.493)}[profile])
    wait("menu: screen 'Main Menu Screen'", 30)
    time.sleep(1.5)
    shot(tag, "t0_main_menu")
    tap(1302, 635)  # NEW GAME / CONTINUE GAME (same row)
    wait("player ready", 150)
    wait("tutorial: 'SNAP_TURN_TUTORIAL_TITLE'", 60)
    time.sleep(2.0)
    shot(tag, "t1_snap_turn")
    hud("turn_right")
    wait("'TELEPORT_TUTORIAL_TITLE'", 30)
    time.sleep(2.0)
    shot(tag, "t2_teleport")
    hud("teleport")
    time.sleep(1.0)
    face_loc("ProjectorPosition", pitch=-20)
    time.sleep(1.0)
    x, y, vis = loc_screen("ProjectorPosition")
    shot(tag, "t2b_projector_marker")
    tap(*vpx(x, y))
    wait("'TUTORIAL_OPERATE_LEVER_TITLE'", 30)
    time.sleep(2.0)
    shot(tag, "t3_lever")
    for k in range(1, 6):
        h = next(v for v in volumes() if v["grabbable"] == "RotationGrabbable" and v["y"] > 0.55)
        x, y = vpx(h["x"], h["y"])
        drag([(x, y), (x, y + 140), (x, y + 260)], step_ms=16, steps_per_seg=10, hold_start_ms=180, hold_end_ms=300)
        if k == 1:
            shot(tag, "t3b_after_first_pull")
        wait(f"VIEW_SLIDE_{k}_MESSAGE", 20)
        time.sleep(7.5)
    wait("'TUTORIAL_TELEPORT_TO_DESK_TITLE'", 30)
    time.sleep(1.5)
    shot(tag, "t4_teleport_desk")
    hud("teleport")
    time.sleep(1.0)
    face_loc("DeskPosition", pitch=-20)
    time.sleep(1.0)
    x, y, vis = loc_screen("DeskPosition")
    tap(*vpx(x, y))
    wait("'TUTORIAL_READ_NOTE_TITLE'", 30)
    time.sleep(2.0)
    shot(tag, "t5_read_note")
    look(0, -12)
    time.sleep(0.8)
    note = find_vol("TUT_Desk_Note")
    print("note", note)
    hold(*vpx(note["x"], note["y"]), 3500)
    wait("'TUTORIAL_PICK_UP_KEY_TITLE'", 30)
    time.sleep(2.0)
    shot(tag, "t6_pick_up_key")
    vols = volumes()
    for v in vols:
        print(v)
    drawer = min((v for v in vols if v["grabbable"] == "SliderGrabbable"), key=lambda v: abs(v["x"] - 0.2) + abs(v["y"] - 0.75))
    x, y = vpx(drawer["x"], drawer["y"])
    drag([(x, y), (x - 15, y + 90), (x - 30, y + 180)], step_ms=16, steps_per_seg=10, hold_start_ms=180, hold_end_ms=400)
    time.sleep(1.5)
    shot(tag, "t6b_drawer_open")
    key = next((v for v in volumes() if "key" in v["name"].lower()), None)
    print("key", key)
    x, y = vpx(key["x"], key["y"])
    inject([f"down 0 {int(x)} {int(y)}", "sync"])
    wait("'TUTORIAL_RELEASE_KEY_TITLE'", 20)
    time.sleep(1.5)
    shot(tag, "t7_release_key")
    inject(["up 0", "sync"])
    wait("TUTORIAL_FOUND_KEY", 20)
    time.sleep(1.5)
    hud("teleport")
    time.sleep(1.0)
    face_loc("SafePosition", pitch=-20)
    time.sleep(1.0)
    x, y, vis = loc_screen("SafePosition")
    tap(*vpx(x, y))
    wait("'TUTORIAL_OPEN_INVENTORY_TITLE'", 30)
    time.sleep(2.0)
    shot(tag, "t8_open_inventory")
    hud("inventory")
    time.sleep(1.5)
    shot(tag, "t8b_inventory")
    key = next((v for v in volumes() if "key" in v["name"].lower() and v["grabbable"] == "InventoryItemGrabbable"), None)
    rec = [v for v in receivers() if v["vis"]]
    print("key", key, "receivers", rec)
    hole = next(v for v in rec if "key" in v["name"].lower())  # the keyhole, not the inventory's own receiver
    key_into_lock(vpx(key["x"], key["y"]), vpx(hole["x"], hole["y"]))
    wait("TUTORIAL_SAFE_TENTACLES_MESSAGE", 30)
    time.sleep(2.0)
    shot(tag, "t9_safe_open")


if __name__ == "__main__":
    cmd, tag = sys.argv[1], sys.argv[2]
    if cmd == "tutorial":
        prof = int(sys.argv[sys.argv.index("--profile") + 1]) if "--profile" in sys.argv else 3
        tutorial(tag, prof)
    else:
        raise SystemExit(__doc__)
