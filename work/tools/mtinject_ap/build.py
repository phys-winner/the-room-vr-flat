#!/usr/bin/env python3
"""Build the no-root multi-touch injector: MtInject.java -> work/tools/mtinject_ap/mtinject.dex.

Device side:  adb push mtinject.dex /data/local/tmp/mtinject.dex
              adb shell CLASSPATH=/data/local/tmp/mtinject.dex app_process /system/bin port.tools.MtInject
"""
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import build_port as bp  # noqa: E402


def main():
    t = bp.tools()
    with tempfile.TemporaryDirectory(dir=bp.GAME / "work") as td:
        td = Path(td)
        classes = td / "classes"
        classes.mkdir()
        bp.run([t.javac, "-source", "8", "-target", "8", "-Xlint:-options", "-bootclasspath", t.android_jar,
                "-d", classes, HERE / "MtInject.java"])
        dex = td / "dex"
        dex.mkdir()
        bp.run([t.java, "-cp", t.d8, "com.android.tools.r8.D8", "--min-api", "24", "--release",
                "--lib", t.android_jar, "--output", dex, *sorted(classes.rglob("*.class"))])
        out = HERE / "mtinject.dex"
        out.write_bytes((dex / "classes.dex").read_bytes())
    print(f"mtinject.dex {out.stat().st_size} B sha256 {bp.sha(out)}")


if __name__ == "__main__":
    main()
