#!/usr/bin/env python3
"""Device helper for the flat port. Only ever talks to the user-selected serial redmi-note8pro.

  devrun.py launch TAG [--secs N] [--shots 5,15,30] [--grant]   cold start + logcat + validated shots
  devrun.py shot TAG NAME                                      validated screenshot
  devrun.py tap X Y | swipe X1 Y1 X2 Y2 MS | key CODE          raw input (landscape pixels)
  devrun.py pull TAG                                           roomfix.log + crash buffer
  devrun.py push-lib                                           dev shortcut: replace installed libroomfix.so (root)
  devrun.py cmd "dump" ["look 30 -10" ...]                    dev console commands (one per argument)
  devrun.py stop

A screenshot counts only when the game pid is alive, the focused window is UnityPlayerActivity
and the image is landscape; otherwise it is stored with an INVALID suffix.
"""
import os
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

SERIAL = "redmi-note8pro"
ADB = "%LOCALAPPDATA%/Android/Sdk/platform-tools/adb.exe"
PKG = "com.FireproofStudios.TheRoomVRPlus"
ACT = "com.unity3d.player.UnityPlayerActivity"
GAME = Path(__file__).resolve().parents[2]
LOGS = GAME / "work" / "logs"
EXT = f"/sdcard/Android/data/{PKG}/files"
os.environ["MSYS_NO_PATHCONV"] = "1"


def adb(*args, check=False, capture=True, timeout=120, binary=False):
    r = subprocess.run([ADB, "-s", SERIAL, *args], capture_output=capture, timeout=timeout)
    if check and r.returncode != 0:
        raise SystemExit(f"adb {' '.join(args)} failed: {r.stderr.decode(errors='replace')}")
    if binary:
        return r.stdout
    return r.stdout.decode(errors="replace").strip() if capture else ""


def sh(cmd, **kw):
    return adb("shell", cmd, **kw)


def pid():
    return sh(f"pidof {PKG}")


def focus():
    # MIUI keeps reporting the launcher in `dumpsys window` mCurrentFocus while a game is in front;
    # the activity manager's resumed activity plus the input-focused window is reliable there.
    out = sh("dumpsys activity activities | grep -E 'mResumedActivity|topResumedActivity'")
    out += "\n" + sh("dumpsys input | grep -E 'FocusedWindow|FocusedApplication' | head -3")
    return out


def png_size(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return (0, 0)
    w, h = struct.unpack(">II", data[16:24])
    return (w, h)


def shot(tag, name):
    d = LOGS / tag
    d.mkdir(parents=True, exist_ok=True)
    data = adb("exec-out", "screencap -p", binary=True, timeout=30)
    w, h = png_size(data)
    p = pid()
    f = focus()
    valid = bool(p) and f"{PKG}/{ACT}" in f and w > h
    out = d / f"{name}{'' if valid else '_INVALID'}.png"
    out.write_bytes(data)
    print(f"shot {out.name}: {w}x{h} pid={p or '-'} valid={valid} bytes={len(data)}")
    return out, valid


def launch(tag, secs=40, shots=(5, 15, 30), grant=False):
    d = LOGS / tag
    d.mkdir(parents=True, exist_ok=True)
    (d / "crash_before.txt").write_text(sh("logcat -b crash -d"), encoding="utf-8")
    sh(f"am force-stop {PKG}")
    if grant:
        sh(f"pm grant {PKG} android.permission.WRITE_EXTERNAL_STORAGE")
        sh(f"pm grant {PKG} android.permission.READ_EXTERNAL_STORAGE")
    sh("logcat -c")
    t0 = sh("date +%s")
    logf = open(d / "logcat.txt", "wb")
    lc = subprocess.Popen([ADB, "-s", SERIAL, "logcat", "-v", "epoch", "-b", "main", "-b", "system", "-b", "crash"],
                          stdout=logf, stderr=subprocess.STDOUT)
    try:
        time.sleep(0.5)
        print(sh(f"am start -W -n {PKG}/{ACT}"))
        start = time.time()
        for s in sorted(shots):
            dt = s - (time.time() - start)
            if dt > 0:
                time.sleep(dt)
            shot(tag, f"t{s:03d}")
        rest = secs - (time.time() - start)
        if rest > 0:
            time.sleep(rest)
    finally:
        lc.terminate()
        logf.close()
    print(f"t0={t0} pid={pid() or '-'}")
    pull(tag)


def pull(tag):
    d = LOGS / tag
    d.mkdir(parents=True, exist_ok=True)
    log = sh(f"su -c 'cat {EXT}/roomfix.log'")
    (d / "roomfix.log").write_text(log, encoding="utf-8")
    (d / "crash_after.txt").write_text(sh("logcat -b crash -d"), encoding="utf-8")
    print(f"pulled roomfix.log ({len(log)} chars), crash_after {len((d / 'crash_after.txt').read_text(encoding='utf-8'))} chars")


def push_lib():
    # never overwrite the mapped file under a running process (that crashes the old instance)
    sh(f"am force-stop {PKG}")
    lib = GAME / "app" / "lib" / "arm64-v8a" / "libroomfix.so"
    adb("push", str(lib).replace("\\", "/"), "/data/local/tmp/libroomfix.so", check=True)
    path = sh(f"pm path {PKG}").replace("package:", "").strip()
    libdir = os.path.dirname(path) + "/lib/arm64"
    print(sh(f"su -c 'cp /data/local/tmp/libroomfix.so {libdir}/libroomfix.so && chown system:system {libdir}/libroomfix.so && chmod 755 {libdir}/libroomfix.so && restorecon {libdir}/libroomfix.so && sha256sum {libdir}/libroomfix.so'"))


def main(a):
    cmd = a[0]
    if cmd == "launch":
        secs, shots, grant = 40, (5, 15, 30), "--grant" in a
        if "--secs" in a:
            secs = int(a[a.index("--secs") + 1])
        if "--shots" in a:
            shots = tuple(int(x) for x in a[a.index("--shots") + 1].split(","))
        launch(a[1], secs, shots, grant)
    elif cmd == "shot":
        shot(a[1], a[2])
    elif cmd == "tap":
        sh(f"input tap {a[1]} {a[2]}")
    elif cmd == "swipe":
        sh(f"input swipe {a[1]} {a[2]} {a[3]} {a[4]} {a[5]}")
    elif cmd == "key":
        sh(f"input keyevent {a[1]}")
    elif cmd == "pull":
        pull(a[1])
    elif cmd == "push-lib":
        push_lib()
    elif cmd == "cmd":
        # one dev-console command per argument; printf keeps each on its own line
        body = "".join(f"{c}\\n" for c in a[1:])
        sh(f"printf '{body}' > {EXT}/roomfix.cmd.tmp && mv {EXT}/roomfix.cmd.tmp {EXT}/roomfix.cmd")
    elif cmd == "stop":
        sh(f"am force-stop {PKG}")
        print("pid after stop:", pid() or "-")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
