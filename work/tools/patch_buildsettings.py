#!/usr/bin/env python3
"""Stage 8 C4 (operator choice 2026-10-14): hide Unity's "trial version" corner mark.

The PICO build's BuildSettings (globalgamemanagers) carries hasPublishingRights=false, so the player draws the
"trial version" mark; in the headset it sat on the unused mirror display, on a phone it is in the corner of the
picture. The flag is flipped in place (same object size), from the immutable decoded_original copy into app/.

  patch_buildsettings.py          write app/assets/bin/Data/globalgamemanagers + work/manifests/buildsettings_patch.json
  patch_buildsettings.py --check  verify app/ against the manifest
"""
import hashlib
import json
import sys
from pathlib import Path

import UnityPy

GAME = Path(__file__).resolve().parents[2]
SRC = GAME / "decoded_original" / "assets" / "bin" / "Data" / "globalgamemanagers"
DST = GAME / "app" / "assets" / "bin" / "Data" / "globalgamemanagers"
MANIFEST = GAME / "work" / "manifests" / "buildsettings_patch.json"
EXPECT_SRC = "80deb16d888b16ec3ae4"  # sha256 prefix of the original file


def sha(b):
    return hashlib.sha256(b).hexdigest()


def build():
    src = SRC.read_bytes()
    assert sha(src).startswith(EXPECT_SRC), "unexpected original globalgamemanagers"
    env = UnityPy.load(str(SRC))
    obj = next(o for o in env.objects if o.type.name == "BuildSettings")
    tree = obj.read_typetree()
    assert tree["hasPublishingRights"] is False and tree["m_Version"] == "2019.4.40f1", "preimage"
    old = bytes(obj.get_raw_data())
    assert bytes(obj.save_typetree(obj.read_typetree())) == old, "type tree writer round trip"
    tree["hasPublishingRights"] = True
    new = bytes(obj.save_typetree(tree))
    assert len(new) == len(old) and sum(a != b for a, b in zip(old, new)) == 1, "one byte flips"
    at = src.find(old)
    assert at > 0 and src.find(old, at + 1) < 0, "object bytes located once"
    out = src[:at] + new + src[at + len(old):]
    info = {"file": "assets/bin/Data/globalgamemanagers", "object": "BuildSettings", "path_id": obj.path_id,
            "field": "hasPublishingRights false -> true", "offset": at, "src_sha256": sha(src), "out_sha256": sha(out)}
    return info, out


def main():
    info, out = build()
    if "--check" in sys.argv:
        ok = sha(DST.read_bytes()) == info["out_sha256"] == json.loads(MANIFEST.read_text())["out_sha256"]
        print("buildsettings patch:", "OK" if ok else "MISMATCH")
        sys.exit(0 if ok else 1)
    DST.write_bytes(out)
    MANIFEST.write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(info, indent=2))


if __name__ == "__main__":
    main()
