#!/usr/bin/env python3
"""Annotated arm64 disassembly of libil2cpp.so methods using Il2CppDumper script.json.

usage: il2dis.py NAME_SUBSTRING_OR_0xADDR [--n=COUNT] [--exact] [--lib=PATH]
  Resolves BL targets to method names and adrp+ldr/add pairs to metadata globals
  (TypeInfo / MethodInfo / StringLiteral). Prints the whole function (until the next
  known method start) unless --n is given.
"""
import bisect
import json
import re
import sys
from pathlib import Path

from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
DEF_LIB = ROOT / "decoded_original" / "lib" / "arm64-v8a" / "libil2cpp.so"
SCRIPT = ROOT / "work" / "il2cppdumper" / "script.json"
_cache = {}


def load(lib):
    if "meth" in _cache:
        return _cache
    d = json.loads(SCRIPT.read_text(encoding="utf-8"))
    meth = {}
    for m in d["ScriptMethod"]:
        meth.setdefault(m["Address"], m["Name"])
    glob = {}
    for m in d["ScriptMetadata"]:
        glob[m["Address"]] = m["Name"]
    for m in d["ScriptMetadataMethod"]:
        glob[m["Address"]] = m["Name"]
    for m in d["ScriptString"]:
        glob[m["Address"]] = "str:" + repr(m["Value"])[:80]
    addrs = sorted(set(meth) | set(d.get("Addresses", [])))
    data = Path(lib).read_bytes()
    elf = ELFFile(open(lib, "rb"))
    segs = [(s["p_vaddr"], s["p_offset"], s["p_filesz"]) for s in elf.iter_segments() if s["p_type"] == "PT_LOAD"]
    import struct
    rel = {}
    for sec in elf.iter_sections():
        if sec.name == ".rela.dyn":
            off = sec["sh_offset"]; n = sec["sh_size"] // 24
            buf = data[off:off + n * 24]
            for i in range(n):
                r_off, r_info, r_add = struct.unpack_from("<QQq", buf, i * 24)
                if (r_info & 0xffffffff) == 1027:
                    rel[r_off] = r_add
    _cache.update(meth=meth, glob=glob, addrs=addrs, data=data, segs=segs, byname=d["ScriptMethod"], rel=rel)
    return _cache


def v2o(c, va):
    for v, o, sz in c["segs"]:
        if v <= va < v + sz:
            return va - v + o
    return None


def find(c, key, exact=False):
    if re.fullmatch(r"0x[0-9a-fA-F]+", key):
        return [(int(key, 16), c["meth"].get(int(key, 16), "?"))]
    res = []
    for m in c["byname"]:
        if (m["Name"] == key) if exact else (key in m["Name"]):
            res.append((m["Address"], m["Name"]))
    return res


def disasm(c, start, count=None):
    i = bisect.bisect_right(c["addrs"], start)
    end = c["addrs"][i] if i < len(c["addrs"]) else start + 0x400
    if count:
        end = start + 4 * count
    off = v2o(c, start)
    code = c["data"][off:off + (end - start)]
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    regs = {}
    out = []
    for ins in md.disasm(code, start):
        note = ""
        ops = ins.op_str
        if ins.mnemonic == "adrp":
            r, imm = ops.split(", ")
            regs[r] = int(imm.lstrip("#"), 16)
        elif ins.mnemonic in ("ldr", "add", "ldrsw", "str") and "#" in ops:
            m = re.match(r"(\w+), \[(\w+), #(-?0x[0-9a-f]+|-?\d+)\]", ops)
            m2 = re.match(r"(\w+), (\w+), #(0x[0-9a-f]+|\d+)$", ops)
            mm = m or m2
            if mm and mm.group(2) in regs:
                tgt = regs[mm.group(2)] + int(mm.group(3), 0)
                if tgt in c["rel"] and mm.group(1) != "xzr":
                    g = c["rel"][tgt]
                    note = c["glob"].get(g) or ("&" + c["meth"][g] if g in c["meth"] else "*" + hex(g))
                elif tgt in c["glob"]:
                    note = c["glob"][tgt]
                elif tgt in c["meth"]:
                    note = "&" + c["meth"][tgt]
                else:
                    note = hex(tgt)
                if ins.mnemonic != "str" and mm.group(1) != mm.group(2):
                    regs.pop(mm.group(1), None)
        if ins.mnemonic in ("bl", "b") and ops.startswith("#"):
            t = int(ops[1:], 16)
            if t in c["meth"]:
                note = c["meth"][t]
        out.append(f"{ins.address:08x}: {ins.mnemonic:7s} {ops:40s} {('; ' + note) if note else ''}")
    return out


def main(argv):
    lib = DEF_LIB
    count = None
    exact = False
    keys = []
    for a in argv:
        if a.startswith("--n="):
            count = int(a[4:])
        elif a == "--exact":
            exact = True
        elif a.startswith("--lib="):
            lib = a[6:]
        else:
            keys.append(a)
    c = load(lib)
    for key in keys:
        hits = find(c, key, exact)
        if not hits:
            print("no match:", key)
        for addr, name in hits[:8]:
            print(f"==== {name} @ {addr:#x}")
            print("\n".join(disasm(c, addr, count)))
        if len(hits) > 8:
            print(f"... {len(hits) - 8} more matches")


if __name__ == "__main__":
    main(sys.argv[1:])
