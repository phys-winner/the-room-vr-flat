#!/usr/bin/env python3
"""5.8 aspect / density / font-scale rows on the running game (player mode), device redmi-note8pro only.

The logical display is changed with `wm size` / `wm density` / font_scale (the activity declares these config
changes, Unity resizes its surface); the same camera is shot for each variant, and a HUD tap (pause) must open
a look drag must turn the view at every variant (input transform of the letterboxed logical display). Everything is restored at the end.

  aspect_matrix.py TAG
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import touchroute as t  # noqa: E402

SIZES = [("16x9_ref", "1080x1920"), ("2x1", "1080x2160"), ("native_19.5x9", None), ("4x3", "1080x1440")]


def look_check(tag, name):
    """a one-finger look drag in the middle of the panel (the centre of every letterboxed logical display) must
    turn the view: touch reaches the game through the scaled input transform"""
    import numpy as np
    from PIL import Image

    def g(n):
        t.shot(tag, n)
        return np.asarray(Image.open(t.d.LOGS / tag / f"{n}.png").convert("L").resize((300, 140))).astype(float)

    a = g(name)
    t.look(20, 0)
    time.sleep(1.0)
    b = g(f"{name}_look")
    t.look(-20, 0)
    time.sleep(0.8)
    print(f"  {name}: look-diff {float(np.abs(a - b).mean()):.1f}")


def main(tag):
    try:
        for name, size in SIZES:
            t.d.sh(f"wm size {size}" if size else "wm size reset")
            time.sleep(5)
            print(name, " | ".join(t.d.sh("wm size").splitlines()))
            look_check(tag, name)
        t.d.sh("wm size reset")
        time.sleep(4)
        for name, dens in (("density_360", "360"), ("density_520", "520")):
            t.d.sh(f"wm density {dens}")
            time.sleep(5)
            look_check(tag, name)
        t.d.sh("wm density reset")
        time.sleep(4)
        t.d.sh("settings put system font_scale 1.3")
        time.sleep(5)
        look_check(tag, "font_1.3")
    finally:
        t.d.sh("wm size reset")
        t.d.sh("wm density reset")
        t.d.sh("settings put system font_scale 1.0")


if __name__ == "__main__":
    main(sys.argv[1])
