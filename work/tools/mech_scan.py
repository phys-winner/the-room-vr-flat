"""Per-scene MonoBehaviour class counts (script PPtr read from raw object bytes), for the touch route plan.
Output: work/manifests/scene_mechanics.txt"""
"""Which scenes contain which interaction mechanics (MonoBehaviour script classes), for the touch route plan."""
import collections
import os
import re
import sys
from pathlib import Path

import UnityPy

data = str(Path(__file__).resolve().parents[2] / "decoded_original" / "assets" / "bin" / "Data")
WANT = re.compile(r".")
names = sorted(os.listdir(data))


def load(label):
    if os.path.exists(os.path.join(data, label)):
        return UnityPy.load(os.path.join(data, label))
    parts = sorted([n for n in names if n.startswith(label + ".split")], key=lambda n: int(n.rsplit("split", 1)[1]))
    return UnityPy.load(b"".join(open(os.path.join(data, n), "rb").read() for n in parts))


# MonoScript names from globalgamemanagers.assets (split)
scripts = {}
env = load("globalgamemanagers.assets")
for obj in env.objects:
    if obj.type.name == "MonoScript":
        t = obj.read_typetree()
        scripts[obj.path_id] = t["m_ClassName"]
print("monoscripts", len(scripts))
levels = ["level%d" % i for i in range(0, 17)]
for lv in levels:
    try:
        e = load(lv)
    except Exception as ex:
        print(lv, "load fail", ex)
        continue
    sf = e.file
    ext = [x.path for x in sf.externals]
    gi = [i + 1 for i, p in enumerate(ext) if p.endswith("globalgamemanagers.assets")]
    c = collections.Counter()
    for obj in e.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        try:
            raw = bytes(obj.get_raw_data())
            # m_GameObject PPtr (int32 file, int64 path), m_Enabled u8 + align 4, m_Script PPtr
            import struct as _s
            fid, pid = _s.unpack_from("<iq", raw, 16)
        except Exception:
            continue
        if fid in gi and pid in scripts:
            n = scripts[pid]
            if WANT.search(n):
                c[n] += 1
    print(lv, dict(sorted(c.items(), key=lambda x: -x[1])), flush=True)
