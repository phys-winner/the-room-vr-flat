#!/usr/bin/env python3
"""Machine-specific settings of the build and device tools. They stay out of Git: work/local.json (start from
work/local.example.json) or environment variables; a variable wins over the file.

  key          variable           used by
  java         JAVA               build_port.py, prepare_app.py (Java 17+; javac next to it)
  android_sdk  ANDROID_SDK_ROOT   build_port.py: platforms/android-36/android.jar
  ndk          ANDROID_NDK_ROOT   build_port.py: NDK 27.2.12479018 (clang, llvm-strip)
  build_tools  BUILD_TOOLS        build_port.py: build-tools 37.0.0 (aapt2, zipalign, lib/d8.jar)
  apktool      APKTOOL_JAR        prepare_app.py, build_port.py: apktool 3.0.3
  baksmali     BAKSMALI_JAR       build_port.py: baksmali 3.0.x
  apksigner    APKSIGNER_JAR      build_port.py: apksigner.jar
  sign_key     SIGN_KEY           build_port.py: signing key (.pk8)
  sign_cert    SIGN_CERT          build_port.py: signing certificate (.x509.pem)
  adb          ADB                device tools (default: adb on PATH)
  serial       RF_SERIAL          device tools: the one test device they may talk to
  ffmpeg       FFMPEG             tap_rec.py (default: ffmpeg on PATH)

Without work/local.json the build falls back to the porting workspace's own tool configuration when it exists
(<workspace>/.claude/skills/adapt-arm32-game: machine_config.json and tools/android), see build_port.py.
"""
import json
import os
import shutil
from pathlib import Path

GAME = Path(__file__).resolve().parents[2]
FILE = GAME / "work" / "local.json"
ENV = {"java": "JAVA", "android_sdk": "ANDROID_SDK_ROOT", "ndk": "ANDROID_NDK_ROOT", "build_tools": "BUILD_TOOLS",
       "apktool": "APKTOOL_JAR", "baksmali": "BAKSMALI_JAR", "apksigner": "APKSIGNER_JAR", "sign_key": "SIGN_KEY",
       "sign_cert": "SIGN_CERT", "adb": "ADB", "serial": "RF_SERIAL", "ffmpeg": "FFMPEG"}
_cache = None


def load():
    global _cache
    if _cache is None:
        _cache = json.loads(FILE.read_text(encoding="utf-8")) if FILE.is_file() else {}
    return _cache


def get(key, default=None):
    v = os.environ.get(ENV.get(key, ""), "") or load().get(key)
    return v if v else default


def path(key):
    v = get(key)
    return Path(v) if v else None


def adb():
    return get("adb") or shutil.which("adb") or "adb"


def serial():
    s = get("serial")
    if not s:
        raise SystemExit("no test device: set RF_SERIAL or \"serial\" in work/local.json (the tools talk to that one "
                         "adb serial only)")
    return s
