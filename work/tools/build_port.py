#!/usr/bin/env python3
"""Build the port's own sources into the canonical app/ tree.

  native : src/roomfix/*.c  -> app/lib/arm64-v8a/libroomfix.so   (NDK clang from machine config)
  java   : src/java/**.java -> app/smali/port/**                  (javac -> d8 -> baksmali)
  apk    : bundled apk_build.py (apktool b -> zipalign -P 16 4 -> portable signer -> verify)

usage: build_port.py [native] [java] [apk] [--dev]   (default: native java)
  --dev  adds -DRF_DEV=1 (diagnostic code paths); release builds must not use it.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

GAME = Path(__file__).resolve().parents[2]
PROJECT = GAME.parent
SKILL = PROJECT / ".claude" / "skills" / "adapt-arm32-game"
sys.path.insert(0, str(SKILL / "scripts"))
import machine_config  # noqa: E402

CFG = machine_config.load()
JAVA = Path(CFG["java_exe"])
JAVAC = JAVA.with_name("javac.exe")
SDK = Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk"
NDK = SDK / "ndk" / CFG["ndk_version"]
LLVM = NDK / "toolchains" / "llvm" / "prebuilt" / "windows-x86_64" / "bin"
CLANG = LLVM / "clang.exe"
STRIP = LLVM / "llvm-strip.exe"
TOOLS = PROJECT / "tools" / "android"
BT = TOOLS / "sdk" / "build-tools" / CFG["build_tools_version"]
ANDROID_JAR = SDK / "platforms" / "android-36" / "android.jar"


def run(argv, **kw):
    print("+", " ".join(str(a) for a in argv), flush=True)
    r = subprocess.run([str(a) for a in argv], **kw)
    if r.returncode != 0:
        raise SystemExit(f"command failed ({r.returncode})")


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def build_native(dev):
    run([sys.executable, "-X", "utf8", "-B", GAME / "work" / "tools" / "gen_hooks.py"])
    out = GAME / "app" / "lib" / "arm64-v8a" / "libroomfix.so"
    srcs = sorted((GAME / "src" / "roomfix").glob("*.c"))
    flags = ["--target=aarch64-linux-android24", "-O2", "-fPIC", "-shared", "-std=gnu11",
             "-Wall", "-Wextra", "-Wno-unused-parameter", "-fvisibility=hidden",
             "-mbranch-protection=standard", "-ffunction-sections", "-fdata-sections",
             "-Wl,--gc-sections", "-Wl,-z,max-page-size=16384", "-Wl,--build-id=sha1",
             "-Wl,-z,relro", "-Wl,-z,now"]
    if dev:
        flags.append("-DRF_DEV=1")
    run([CLANG, *flags, "-I", GAME / "src" / "roomfix", *srcs, "-o", out, "-llog", "-ldl", "-landroid", "-lm"])
    run([STRIP, "--strip-unneeded", out])
    print(f"native: {out} {out.stat().st_size} B sha256 {sha(out)}")


def build_java():
    srcs = sorted((GAME / "src" / "java").rglob("*.java"))
    with tempfile.TemporaryDirectory(dir=GAME / "work") as td:
        td = Path(td)
        classes = td / "classes"
        classes.mkdir()
        run([JAVAC, "--release", "8", "-Xlint:-options", "-cp", ANDROID_JAR, "-d", classes, *srcs]) if False else \
            run([JAVAC, "-source", "8", "-target", "8", "-Xlint:-options", "-bootclasspath", ANDROID_JAR, "-d", classes, *srcs])
        dex = td / "dex"
        dex.mkdir()
        cls = sorted(classes.rglob("*.class"))
        run([JAVA, "-cp", BT / "lib" / "d8.jar", "com.android.tools.r8.D8", "--min-api", "24", "--release",
             "--lib", ANDROID_JAR, "--output", dex, *cls])
        smali = td / "smali"
        run([JAVA, "-jar", TOOLS / "baksmali.jar", "d", dex / "classes.dex", "-o", smali])
        dst = GAME / "app" / "smali" / "port"
        if dst.exists():
            shutil.rmtree(dst)
        shutil.copytree(smali / "port", dst)
    n = len(list(dst.rglob("*.smali")))
    print(f"java: {n} smali files -> {dst}")


def build_apk():
    run([sys.executable, "-X", "utf8", "-B", SKILL / "scripts" / "apk_build.py", "--project-root", GAME,
         "--tool-root", TOOLS, "--java", JAVA, "--target", "base", "--timeout", "3600"])


def main(argv):
    dev = "--dev" in argv
    steps = [a for a in argv if not a.startswith("--")] or ["native", "java"]
    for s in steps:
        {"native": lambda: build_native(dev), "java": build_java, "apk": build_apk}[s]()


if __name__ == "__main__":
    main(sys.argv[1:])
