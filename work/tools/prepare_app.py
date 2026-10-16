#!/usr/bin/env python3
"""Recreate decoded_original/ and the canonical app/ tree from the player's own copy of the original APK.

The repository holds only the port's own work (no game files). This script rebuilds what the port works on:
  1. checks the APK: size and SHA-256 recorded in work/manifests/original.json (The Room VR: A Dark Matter,
     PICO build 1.02, versionCode 5011);
  2. decodes it once with apktool 3.0.3 into decoded_original/ (framework dir work/tool-cache/apktool-framework)
     and compares every file with work/manifests/decoded_original.json;
  3. copies decoded_original/ to app/ and applies patches/app.patch (app_patch.py);
  4. moves the files the port removes out of app/ (prune_app.py) and writes the binary asset edits
     (patch_shaders.py, patch_textures.py, patch_buildsettings.py - each checks its source bytes first);
  5. builds the port's own code into app/ (build_port.py native java [--release]).
Then `build_port.py apk [--release]` packages out/base.apk.

usage: prepare_app.py APK [--release] [--force]
  --force  replace an existing app/ (an existing decoded_original/ is reused after the manifest check)

Tools: java and apktool from work/local.json / the environment (local_config.py) or the porting workspace.
Python packages for step 4: UnityPy, lz4, numpy, texture2ddecoder, astc-encoder-py.
"""
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_port  # noqa: E402

GAME = Path(__file__).resolve().parents[2]
TOOLS = GAME / "work" / "tools"
MANIFESTS = GAME / "work" / "manifests"
DEC = GAME / "decoded_original"
APP = GAME / "app"
FRAME = GAME / "work" / "tool-cache" / "apktool-framework"
# apktool.yml records the input file name (apkFileName), so its hash depends on how the player named the APK
NAME_DEPENDENT = {"apktool.yml"}


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def run(argv):
    print("+", " ".join(str(a) for a in argv), flush=True)
    if subprocess.run([str(a) for a in argv]).returncode != 0:
        raise SystemExit("command failed")


def py(script, *args):
    run([sys.executable, "-X", "utf8", "-B", TOOLS / script, *args])


def check_apk(apk):
    want = [e for e in json.loads((MANIFESTS / "original.json").read_text(encoding="utf-8"))["entries"]
            if e["type"] == "file"][0]
    size = apk.stat().st_size
    if size != want["size"]:
        raise SystemExit(f"{apk}: {size} bytes, expected {want['size']} (the PICO build 1.02 APK)")
    got = sha(apk)
    if got != want["sha256"]:
        raise SystemExit(f"{apk}: sha256 {got}, expected {want['sha256']}")
    print(f"APK ok: {size} bytes, sha256 {got}")


def check_decode():
    ref = json.loads((MANIFESTS / "decoded_original.json").read_text(encoding="utf-8"))["entries"]
    want = {e["path"]: e for e in ref if e["type"] == "file"}
    have = {p.relative_to(DEC).as_posix(): p for p in DEC.rglob("*") if p.is_file()}
    missing = sorted(set(want) - set(have))
    extra = sorted(set(have) - set(want))
    differ, eol = [], []
    for rel in sorted(set(want) & set(have)):
        if have[rel].stat().st_size == want[rel]["size"] and sha(have[rel]) == want[rel]["sha256"]:
            continue
        if rel in NAME_DEPENDENT:
            continue
        b = have[rel].read_bytes()
        alt = b.replace(b"\r\n", b"\n") if b"\r\n" in b else b.replace(b"\n", b"\r\n")
        (eol if hashlib.sha256(alt).hexdigest() == want[rel]["sha256"] else differ).append(rel)
    print(f"decode vs reference: {len(want)} files, {len(missing)} missing, {len(extra)} extra, "
          f"{len(eol)} differ only in line endings, {len(differ)} differ")
    for rel in (missing + extra + differ)[:20]:
        print("  ", rel)
    if missing or extra or differ:
        raise SystemExit("decoded_original/ does not match work/manifests/decoded_original.json (apktool 3.0.3?)")


def main(a):
    if not a or a[0].startswith("--"):
        raise SystemExit(__doc__)
    apk, release, force = Path(a[0]), "--release" in a, "--force" in a
    check_apk(apk)
    t = build_port.tools()
    if not DEC.exists():
        FRAME.mkdir(parents=True, exist_ok=True)
        run([t.java, "-jar", t.apktool, "d", apk, "-o", DEC, "-p", FRAME, "-f"])
    check_decode()
    if APP.exists():
        if not force:
            raise SystemExit("app/ exists; --force replaces it")
        shutil.rmtree(APP)
    shutil.copytree(DEC, APP)
    print("app/: fresh copy of decoded_original/")
    py("app_patch.py", "apply")
    py("prune_app.py")
    py("patch_shaders.py")
    py("patch_textures.py")
    py("patch_buildsettings.py")
    py("build_port.py", "native", "java", *(["--release"] if release else []))
    for script in ("patch_shaders.py", "patch_textures.py", "patch_buildsettings.py"):
        py(script, "--check")
    py("app_patch.py", "check")
    print("app/ ready: build_port.py apk" + (" --release" if release else ""))


if __name__ == "__main__":
    main(sys.argv[1:])
