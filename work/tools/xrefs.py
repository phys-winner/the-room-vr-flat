#!/usr/bin/env python3
"""Fast BL/B call-site index for libil2cpp.so (numpy), with method names from script.json.

usage: xrefs.py NAME_SUBSTRING_OR_0xADDR [--exact] [--max=N]
Prints every direct bl/b call site of the matching method(s), grouped by the calling method.
The index is cached in work/il2cppdumper/bl_index.npz.
"""
import bisect
import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
LIB = ROOT / "decoded_original" / "lib" / "arm64-v8a" / "libil2cpp.so"
SCRIPT = ROOT / "work" / "il2cppdumper" / "script.json"
CACHE = ROOT / "work" / "il2cppdumper" / "bl_index.npz"
TEXT_START, TEXT_END = 0xE34260, 0x1168170 + 0x1A12720  # .text .. end of "il2cpp" section (vaddr == offset)


def index():
    if CACHE.exists():
        z = np.load(CACHE)
        return z["src"], z["dst"]
    data = np.frombuffer(LIB.read_bytes()[TEXT_START:TEXT_END], dtype="<u4")
    pcs = TEXT_START + 4 * np.arange(len(data), dtype=np.int64)
    isbl = (data & 0xFC000000) == 0x94000000
    isb = (data & 0xFC000000) == 0x14000000
    sel = isbl | isb
    imm = (data[sel] & 0x3FFFFFF).astype(np.int64)
    imm = np.where(imm & 0x2000000, imm - 0x4000000, imm)
    src = pcs[sel]
    dst = src + imm * 4
    order = np.argsort(dst, kind="stable")
    src, dst = src[order], dst[order]
    np.savez(CACHE, src=src, dst=dst)
    return src, dst


def main(argv):
    exact = "--exact" in argv
    mx = 200
    keys = []
    for a in argv:
        if a.startswith("--max="):
            mx = int(a[6:])
        elif not a.startswith("--"):
            keys.append(a)
    d = json.loads(SCRIPT.read_text(encoding="utf-8"))
    meth = {}
    for m in d["ScriptMethod"]:
        meth.setdefault(m["Address"], m["Name"])
    starts = sorted(meth)
    src, dst = index()

    def owner(pc):
        i = bisect.bisect_right(starts, pc) - 1
        return (starts[i], meth[starts[i]]) if i >= 0 else (0, "?")

    for key in keys:
        if key.startswith("0x"):
            targets = [(int(key, 16), meth.get(int(key, 16), "?"))]
        else:
            targets = sorted({(m["Address"], m["Name"]) for m in d["ScriptMethod"]
                              if (m["Name"] == key if exact else key in m["Name"])})
        for addr, name in targets:
            lo = np.searchsorted(dst, addr, "left")
            hi = np.searchsorted(dst, addr, "right")
            sites = src[lo:hi]
            print(f"== {name} @ {addr:#x}: {len(sites)} call sites")
            groups = {}
            for pc in sites[:mx]:
                o = owner(int(pc))
                groups.setdefault(o, []).append(int(pc))
            for (oa, on), pcs in sorted(groups.items()):
                print(f"   {on} @ {oa:#x}: " + ", ".join(f"{p:#x}" for p in pcs))


if __name__ == "__main__":
    main(sys.argv[1:])
