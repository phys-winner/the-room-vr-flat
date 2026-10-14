#!/usr/bin/env python3
"""Mali compatibility fixes for the game's GLES3 shader programs (DEVICE_EFFECTS, work/device_effects.md).

The PICO build only ever ran on Adreno. Two kinds of its GLSL break on Mali:

  glow_hash  FastGlow*: the dither hash dot(vec2(171119, 2312319), clipXY) is stored in a mediump temporary.
             On Mali fp16 it overflows to inf, fract(inf) = NaN, NaN * _Dither(0) stays NaN and min(NaN, 1)
             returns 1, so the fresnel glow becomes a hard, uniformly lit disc. The temporary is declared highp.
  varying    SHADOWCASTER passes of the Dissolve / VertColor variants read a varying (vs_TEXCOORD0 /
             vs_COLOR0) that their vertex stage never writes (HLSLcc dropped it). Adreno links such programs,
             Mali rejects them (L0007). The vertex stage now declares the varying and writes a neutral value
             (texcoords zero, vertex colour white as for a mesh without colours, so cutout follows the texture);
             the pass is only compiled by the shader warm-up (shadows are off in every quality level and the
             depth texture is only requested with the stripped AO / volumetrics).

Source files come from decoded_original (immutable) and are written to app/assets/bin/Data. Only the Shader
object bytes change: the file keeps its header, metadata layout and 4096-byte data offset; the GLES3 blob is
rebuilt with the edited subprogram code and recompressed with LZ4.

usage: patch_shaders.py [--check]     --check: verify app/ copies, write nothing
"""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

import lz4.block
import UnityPy

GAME = Path(__file__).resolve().parents[2]
SRC = GAME / "decoded_original" / "assets" / "bin" / "Data"
DST = GAME / "app" / "assets" / "bin" / "Data"
MANIFEST = GAME / "work" / "manifests" / "shader_patches.json"
GLES3 = 9  # ShaderCompilerPlatform GLES3Plus

HASH = re.compile(r"(\w+)\.x = dot\(vec2\(171119\.0, 2312319\.0\)")
OUT_DECL = re.compile(r"^\s*(?:flat\s+)?out\s+(?:(?:highp|mediump|lowp)\s+)?\w+\s+(vs_\w+)\s*;", re.M)
IN_DECL = re.compile(r"^\s*(flat\s+)?in\s+(?:(?:highp|mediump|lowp)\s+)?(\w+)\s+(vs_\w+)\s*;", re.M)
ZERO = {"float": "0.0", "vec2": "vec2(0.0)", "vec3": "vec3(0.0)", "vec4": "vec4(0.0)"}
WHITE = {"float": "1.0", "vec2": "vec2(1.0)", "vec3": "vec3(1.0)", "vec4": "vec4(1.0)"}


def sha(b):
    return hashlib.sha256(b).hexdigest()


def fix_code(code):
    """Return (new_code, [fix descriptions]) for one GLES3 subprogram (vertex + fragment in one source)."""
    if "#ifdef VERTEX" not in code or "#ifdef FRAGMENT" not in code:
        return code, []
    cut = code.index("#ifdef FRAGMENT")
    vs, fs = code[:cut], code[cut:]
    fixes = []

    m = HASH.search(fs)
    if m:
        var = m.group(1)
        decl = re.compile(r"^mediump (vec\d|float) %s;$" % re.escape(var), re.M)
        fs, n = decl.subn(r"highp \1 %s;" % var, fs)
        assert n == 1, ("glow hash temporary declaration", var)
        fixes.append("glow_hash:%s" % var)

    outs = set(OUT_DECL.findall(vs))
    for flat, typ, name in IN_DECL.findall(fs):
        if name in outs:
            continue
        assert typ in ZERO and not flat, (typ, name)
        main = "void main()\n{\n"
        assert vs.count(main) == 1
        value = (WHITE if name.startswith("vs_COLOR") else ZERO)[typ]
        vs = vs.replace(main, "out highp %s %s;\n%s    %s = %s;\n" % (typ, name, main, name, value))
        fixes.append("varying:%s" % name)
    return vs + fs, fixes


def parse_sub(raw, o):
    """Offsets inside one blob subprogram (Unity 2019.4 layout, version 201806140)."""
    ver, typ = struct.unpack_from("<ii", raw, o)
    assert ver == 201806140, ver
    p = o + 24  # version, type, 4 stats
    for _ in range(2):  # keywords, local keywords
        n = struct.unpack_from("<i", raw, p)[0]
        p += 4
        for _ in range(n):
            sl = struct.unpack_from("<i", raw, p)[0]
            p = (p + 4 + sl + 3) & ~3
    return typ, p  # p: program code length field


def patch_blob(raw):
    n = struct.unpack_from("<i", raw, 0)[0]
    if n == 0:
        return raw, []
    ents = [struct.unpack_from("<iii", raw, 4 + 12 * i) for i in range(n)]
    assert ents[0][0] == 4 + 12 * n and ents[-1][0] + ents[-1][1] == len(raw)
    subs, changes = [], []
    for i, (o, l, seg) in enumerate(ents):
        assert seg == 0 and o % 4 == 0 and l % 4 == 0
        assert i == 0 or o == ents[i - 1][0] + ents[i - 1][1]
        typ, p = parse_sub(raw, o)
        cl = struct.unpack_from("<i", raw, p)[0]
        code = raw[p + 4:p + 4 + cl]
        rest = raw[(p + 4 + cl + 3) & ~3:o + l]
        new_code, fixes = fix_code(code.decode("utf-8"))
        if fixes:
            nc = new_code.encode("utf-8")
            body = raw[o:p] + struct.pack("<i", len(nc)) + nc
            body += b"\0" * (-len(body) % 4) + rest
            subs.append(body)
            changes.append({"subprogram": i, "fixes": fixes, "code_sha256_before": sha(code), "code_sha256_after": sha(nc)})
        else:
            subs.append(raw[o:o + l])
    if not changes:
        return raw, []
    out = bytearray(struct.pack("<i", n))
    pos = 4 + 12 * n
    for s in subs:
        out += struct.pack("<iii", pos, len(s), 0)
        pos += len(s)
    for s in subs:
        out += s
    return bytes(out), changes


def patch_file(name):
    src = (SRC / name).read_bytes()
    env = UnityPy.load(str(SRC / name))
    sf = env.file
    objs = list(env.objects)
    shaders = [o for o in objs if o.type.name == "Shader"]
    if not shaders:
        return None
    obj = shaders[0]
    tree = obj.read_typetree()
    if GLES3 not in tree["platforms"]:
        return None
    k = tree["platforms"].index(GLES3)
    assert len(tree["platforms"]) == 1 and len(tree["offsets"][k]) == 1
    blob = bytes(tree["compressedBlob"])
    off, clen, dlen = tree["offsets"][k][0], tree["compressedLengths"][k][0], tree["decompressedLengths"][k][0]
    raw = lz4.block.decompress(blob[off:off + clen], uncompressed_size=dlen)
    new_raw, changes = patch_blob(raw)
    if not changes:
        return None
    assert len(objs) == 1, ("shader file with several objects; the splice below needs extending", name)
    comp = lz4.block.compress(new_raw, mode="high_compression", compression=12, store_size=False)
    assert lz4.block.decompress(comp, uncompressed_size=len(new_raw)) == new_raw
    tree["compressedBlob"] = list(comp)
    tree["offsets"][k] = [0]
    tree["compressedLengths"][k] = [len(comp)]
    tree["decompressedLengths"][k] = [len(new_raw)]
    old_obj = bytes(obj.get_raw_data())
    assert bytes(obj.save_typetree(obj.read_typetree())) == old_obj, "type tree writer round trip"
    new_obj = bytes(obj.save_typetree(tree))

    # splice: header + metadata unchanged except file size and the object's byte size
    h = sf.header
    start = h.data_offset
    assert h.version == 21 and len(src) == start + len(old_obj) and src[start:] == old_obj
    meta = bytearray(src[:start])
    struct.pack_into(">I", meta, 4, start + len(new_obj))
    entry = struct.pack("<qII", obj.path_id, 0, len(old_obj))
    at = meta.find(entry)
    assert at > 0 and meta.find(entry, at + 1) < 0, "object table entry"
    struct.pack_into("<I", meta, at + 12, len(new_obj))
    out = bytes(meta) + new_obj
    return {
        "file": name,
        "shader": tree["m_ParsedForm"]["m_Name"],
        "src_sha256": sha(src),
        "out_sha256": sha(out),
        "blob_bytes": [len(raw), len(new_raw)],
        "changes": changes,
    }, out


def shader_files():
    for p in sorted(SRC.iterdir()):
        if p.is_file() and re.fullmatch(r"[0-9a-f]{32}", p.name):
            yield p.name


def main():
    check = "--check" in sys.argv
    results = []
    bad = 0
    for name in shader_files():
        r = patch_file(name)
        if not r:
            continue
        info, data = r
        results.append(info)
        dst = DST / name
        if check:
            ok = dst.exists() and sha(dst.read_bytes()) == info["out_sha256"]
            bad += not ok
            print(("ok   " if ok else "DIFF ") + name, info["shader"])
        else:
            dst.write_bytes(data)
            fixes = sorted({f.split(":")[0] for c in info["changes"] for f in c["fixes"]})
            print(name, info["shader"], len(info["changes"]), "subprograms", ",".join(fixes))
    if not check:
        MANIFEST.parent.mkdir(parents=True, exist_ok=True)
        MANIFEST.write_text(json.dumps(results, indent=1) + "\n", encoding="utf-8", newline="\n")
        print("patched files", len(results), "->", MANIFEST.relative_to(GAME))
    elif bad:
        raise SystemExit("%d patched shader files differ" % bad)


if __name__ == "__main__":
    main()
