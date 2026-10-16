#!/usr/bin/env python3
"""Build the port's own sources into the canonical app/ tree and package the APK.

  native : src/roomfix/*.c  -> app/lib/arm64-v8a/libroomfix.so   (NDK clang)
  java   : src/java/**.java -> app/smali/port/**                  (javac -> d8 -> baksmali)
  apk    : app/ -> out/base.apk  (apktool b -> zipalign -P 16 4 -> apksigner sign -> verify)

usage: build_port.py [native] [java] [apk] [--release] [--standalone]   (default: native java)
  --release     release build without logs: -DRF_RELEASE=1 (no rf_log output, no roomfix.log, no developer console,
                the process muted in logcat) and port.compat.BuildFlags.RELEASE = true (no Java log calls).
                Without it the build is a developer/player build (bounded roomfix.log, console with roomfix.dev).
  --standalone  package with the built-in apktool/zipalign/apksigner sequence even inside the porting workspace

Tools come from work/local.json or environment variables (see local_config.py and work/local.example.json). Without
those settings the porting workspace's own configuration is used when it exists (<workspace>/.claude/skills/
adapt-arm32-game/machine_config.json and <workspace>/tools/android); its apk_build.py then packages the APK with the
workspace's mandatory test signer.
"""
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import local_config as lc  # noqa: E402

GAME = Path(__file__).resolve().parents[2]
PROJECT = GAME.parent
SKILL = PROJECT / ".claude" / "skills" / "adapt-arm32-game"
EXE = ".exe" if os.name == "nt" else ""
LOCAL_KEYS = ("java", "android_sdk", "ndk", "build_tools", "apktool", "baksmali", "apksigner", "sign_key", "sign_cert")


class Tools:
    """Resolved tool paths: 'local' (work/local.json / environment) or 'workspace' (porting workspace config)."""

    def __init__(self):
        if any(lc.get(k) for k in LOCAL_KEYS):
            self.mode = "local"
            missing = [k for k in LOCAL_KEYS if not lc.get(k)]
            if missing:
                raise SystemExit(f"work/local.json: missing {', '.join(missing)} (see work/local.example.json)")
            self.java = lc.path("java")
            sdk, ndk, bt = lc.path("android_sdk"), lc.path("ndk"), lc.path("build_tools")
            self.apktool, self.baksmali = lc.path("apktool"), lc.path("baksmali")
            self.apksigner, self.key, self.cert = lc.path("apksigner"), lc.path("sign_key"), lc.path("sign_cert")
        elif (SKILL / "scripts" / "machine_config.py").is_file():
            self.mode = "workspace"
            sys.path.insert(0, str(SKILL / "scripts"))
            import machine_config  # noqa: E402
            cfg = machine_config.load()
            self.java = Path(cfg["java_exe"])
            sdk = Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk"
            ndk = sdk / "ndk" / cfg["ndk_version"]
            tools = PROJECT / "tools" / "android"
            bt = tools / "sdk" / "build-tools" / cfg["build_tools_version"]
            self.apktool, self.baksmali = tools / "apktool.jar", tools / "baksmali.jar"
            sign = Path(cfg["signer"]["dir"])
            self.apksigner, self.key, self.cert = (sign / cfg["signer"]["apksigner"], sign / cfg["signer"]["key"],
                                                   sign / cfg["signer"]["cert"])
            self.tool_root = tools
        else:
            raise SystemExit("no tool configuration: copy work/local.example.json to work/local.json and fill it in")
        self.javac = self.java.with_name("javac" + EXE)
        self.android_jar = sdk / "platforms" / "android-36" / "android.jar"
        host = {"Windows": "windows-x86_64", "Linux": "linux-x86_64", "Darwin": "darwin-x86_64"}[platform.system()]
        llvm = ndk / "toolchains" / "llvm" / "prebuilt" / host / "bin"
        self.clang, self.strip = llvm / ("clang" + EXE), llvm / ("llvm-strip" + EXE)
        self.d8 = bt / "lib" / "d8.jar"
        self.aapt2, self.zipalign = bt / ("aapt2" + EXE), bt / ("zipalign" + EXE)


T = None


def tools():
    global T
    if T is None:
        T = Tools()
    return T


def run(argv, **kw):
    print("+", " ".join(str(a) for a in argv), flush=True)
    r = subprocess.run([str(a) for a in argv], **kw)
    if r.returncode != 0:
        raise SystemExit(f"command failed ({r.returncode})")
    return r


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def build_native(release):
    t = tools()
    run([sys.executable, "-X", "utf8", "-B", GAME / "work" / "tools" / "gen_hooks.py"])
    out = GAME / "app" / "lib" / "arm64-v8a" / "libroomfix.so"
    out.parent.mkdir(parents=True, exist_ok=True)
    srcs = sorted((GAME / "src" / "roomfix").glob("*.c"))
    flags = ["--target=aarch64-linux-android24", "-O2", "-fPIC", "-shared", "-std=gnu11",
             "-Wall", "-Wextra", "-Wno-unused-parameter", "-fvisibility=hidden",
             "-mbranch-protection=standard", "-ffunction-sections", "-fdata-sections",
             "-Wl,--gc-sections", "-Wl,-z,max-page-size=16384", "-Wl,--build-id=sha1",
             "-Wl,-z,relro", "-Wl,-z,now"]
    if release:
        flags.append("-DRF_RELEASE=1")
    run([t.clang, *flags, "-I", GAME / "src" / "roomfix", *srcs, "-o", out, "-llog", "-ldl", "-landroid", "-lm"])
    run([t.strip, "--strip-unneeded", out])
    print(f"native: {out} {out.stat().st_size} B sha256 {sha(out)}")


def build_java(release):
    t = tools()
    (GAME / "work").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=GAME / "work") as td:
        td = Path(td)
        src = td / "src"
        shutil.copytree(GAME / "src" / "java", src)
        if release:
            flags = src / "port" / "compat" / "BuildFlags.java"
            text = flags.read_text(encoding="utf-8")
            if text.count("RELEASE = false;") != 1:
                raise SystemExit(f"{flags}: expected one 'RELEASE = false;'")
            flags.write_text(text.replace("RELEASE = false;", "RELEASE = true;"), encoding="utf-8")
        srcs = sorted(src.rglob("*.java"))
        classes = td / "classes"
        classes.mkdir()
        run([t.javac, "-source", "8", "-target", "8", "-Xlint:-options", "-bootclasspath", t.android_jar,
             "-d", classes, *srcs])
        dex = td / "dex"
        dex.mkdir()
        cls = sorted(classes.rglob("*.class"))
        run([t.java, "-cp", t.d8, "com.android.tools.r8.D8", "--min-api", "24", "--release",
             "--lib", t.android_jar, "--output", dex, *cls])
        smali = td / "smali"
        run([t.java, "-jar", t.baksmali, "d", dex / "classes.dex", "-o", smali])
        dst = GAME / "app" / "smali" / "port"
        if dst.exists():
            shutil.rmtree(dst)
        shutil.copytree(smali / "port", dst)
    n = len(list(dst.rglob("*.smali")))
    print(f"java: {n} smali files -> {dst}")


def build_apk_standalone():
    t = tools()
    out = GAME / "out"
    out.mkdir(exist_ok=True)
    frame = GAME / "work" / "tool-cache" / "apktool-framework"
    frame.mkdir(parents=True, exist_ok=True)
    unsigned, aligned, final = out / "base-unsigned.apk", out / "base-aligned.apk", out / "base.apk"
    try:
        run([t.java, "-jar", t.apktool, "b", GAME / "app", "-f", "--aapt", t.aapt2, "--frame-path", frame,
             "-o", unsigned])
        run([t.zipalign, "-f", "-P", "16", "4", unsigned, aligned])
        run([t.java, "-jar", t.apksigner, "sign", "--key", t.key, "--cert", t.cert, "--out", final, aligned])
        run([t.java, "-jar", t.apksigner, "verify", "--verbose", "--print-certs", final])
        run([t.zipalign, "-c", "-P", "16", "4", final])
    finally:
        for p in (unsigned, aligned):
            p.unlink(missing_ok=True)
    print(f"apk: {final} {final.stat().st_size} B sha256 {sha(final)}")


def build_apk(standalone):
    t = tools()
    if t.mode == "workspace" and not standalone:
        run([sys.executable, "-X", "utf8", "-B", SKILL / "scripts" / "apk_build.py", "--project-root", GAME,
             "--tool-root", t.tool_root, "--java", t.java, "--target", "base", "--timeout", "3600"])
    else:
        build_apk_standalone()


def main(argv):
    opts = {"--release", "--standalone"}
    unknown = [a for a in argv if a.startswith("--") and a not in opts]
    if unknown:
        raise SystemExit(f"unknown option(s) {unknown}\n{__doc__}")
    release, standalone = "--release" in argv, "--standalone" in argv
    steps = [a for a in argv if not a.startswith("--")] or ["native", "java"]
    actions = {"native": lambda: build_native(release), "java": lambda: build_java(release),
               "apk": lambda: build_apk(standalone)}
    for s in steps:
        if s not in actions:
            raise SystemExit(f"unknown step {s!r}\n{__doc__}")
        actions[s]()


if __name__ == "__main__":
    main(sys.argv[1:])
