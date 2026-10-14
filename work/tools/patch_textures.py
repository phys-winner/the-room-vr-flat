#!/usr/bin/env python3
"""TEXTURE_PORTABILITY: re-encode the build's non-universal textures (work/texture_portability.md).

The only one is Nav_pole_arrow_grad (sharedassets5.assets, path_id 214): 256x256, 1 mip, DXT5 Crunched (format
29). Mali/Adreno phones have no S3TC, so Unity falls back to CPU decompression into RGBA32 at load. It is decoded
from the original crunch data (crunch -> DXT5 -> RGBA, Unity row order) and stored as ASTC 4x4 (format 48, the
format of the build's other ASTC 4x4 textures), which reproduces the decoded source exactly (checked here).

The serialized file is edited in place, not re-written: the object's bytes are replaced, every later object is
shifted by the 8-aligned growth (object table byte_start values updated), the header file size updated, and the
file is split again into the original 1 MiB .splitN parts. Source: decoded_original (immutable); output:
app/assets/bin/Data. Manifest: work/manifests/texture_patches.json.

usage: patch_textures.py [--check]
"""
import hashlib
import json
import math
import struct
import sys
from pathlib import Path

import astc_encoder as ae
import numpy as np
import texture2ddecoder as td
import UnityPy

GAME = Path(__file__).resolve().parents[2]
SRC = GAME / "decoded_original" / "assets" / "bin" / "Data"
DST = GAME / "app" / "assets" / "bin" / "Data"
MANIFEST = GAME / "work" / "manifests" / "texture_patches.json"
SPLIT = 1 << 20
FMT_DXT5_CRUNCHED, FMT_ASTC_4X4 = 29, 48
TARGETS = [("sharedassets5.assets", 214, "Nav_pole_arrow_grad")]


def sha(b):
    return hashlib.sha256(b).hexdigest()


def read_split(name):
    parts = sorted(SRC.glob(name + ".split*"), key=lambda p: int(p.name.rsplit("split", 1)[1]))
    return b"".join(p.read_bytes() for p in parts), len(parts)


def encode_astc4x4(tree):
    w, h = tree["m_Width"], tree["m_Height"]
    assert tree["m_TextureFormat"] == FMT_DXT5_CRUNCHED and tree["m_MipCount"] == 1 and tree["m_ImageCount"] == 1
    dxt5 = td.unpack_unity_crunch(bytes(tree["image data"]))
    bgra = np.frombuffer(td.decode_bc3(dxt5, w, h), np.uint8).reshape(h, w, 4)
    rgba = np.ascontiguousarray(bgra[..., [2, 1, 0, 3]])  # Unity row order kept (no flip)
    ctx = ae.ASTCContext(ae.ASTCConfig(ae.ASTCProfile.LDR, 4, 4, 1, 100.0))
    astc = ctx.compress(ae.ASTCImage(ae.ASTCType.U8, w, h, 1, rgba.tobytes()), ae.ASTCSwizzle.from_str("RGBA"))
    assert len(astc) == math.ceil(w / 4) * math.ceil(h / 4) * 16
    back = np.frombuffer(td.decode_astc(astc, w, h, 4, 4), np.uint8).reshape(h, w, 4)[..., [2, 1, 0, 3]]
    err = np.abs(back.astype(int) - rgba.astype(int))
    return astc, {"max_abs_error_rgb": int(err[..., :3].max()), "max_abs_error_alpha": int(err[..., 3].max()),
                  "alpha_range": [int(rgba[..., 3].min()), int(rgba[..., 3].max())],
                  "rgb_range": [int(rgba[..., :3].min()), int(rgba[..., :3].max())]}


def patch(name, path_id, tex_name):
    src, nsplit = read_split(name)
    env = UnityPy.load(src)
    sf = env.file
    h = sf.header
    assert h.version == 21 and not sf.big_id_enabled
    objs = {o.path_id: o for o in env.objects}
    obj = objs[path_id]
    tree = obj.read_typetree()
    assert tree["m_Name"] == tex_name, tree["m_Name"]
    old_obj = bytes(obj.get_raw_data())
    assert bytes(obj.save_typetree(obj.read_typetree())) == old_obj, "type tree writer round trip"
    old_image = len(tree["image data"])
    astc, quality = encode_astc4x4(tree)
    tree["m_TextureFormat"] = FMT_ASTC_4X4
    tree["m_CompleteImageSize"] = len(astc)
    tree["image data"] = astc
    new_obj = bytes(obj.save_typetree(tree))

    start = obj.byte_start
    later = sorted(o.byte_start for o in env.objects if o.byte_start > start)
    old_slot = (later[0] if later else len(src)) - start
    assert old_slot >= len(old_obj) and old_slot % 8 == 0
    new_slot = (len(new_obj) + 7) & ~7
    delta = new_slot - old_slot

    meta = bytearray(src[:h.data_offset])
    moved = 0
    for o in env.objects:
        rel = o.byte_start - h.data_offset
        entry = struct.pack("<qII", o.path_id, rel, o.byte_size)
        at = meta.find(entry)
        assert at > 0 and meta.find(entry, at + 1) < 0, ("object table entry", o.path_id)
        if o.path_id == path_id:
            struct.pack_into("<I", meta, at + 12, len(new_obj))
        elif o.byte_start > start:
            struct.pack_into("<I", meta, at + 8, rel + delta)
            moved += 1
    struct.pack_into(">I", meta, 4, h.file_size + delta)
    assert h.file_size == len(src)
    out = bytes(meta) + src[h.data_offset:start] + new_obj + b"\0" * (new_slot - len(new_obj)) + src[start + old_slot:]
    assert len(out) == len(src) + delta

    # the edited file must parse and every other object must be byte-identical
    env2 = UnityPy.load(out)
    objs2 = {o.path_id: o for o in env2.objects}
    for pid, o in objs.items():
        if pid != path_id:
            assert bytes(objs2[pid].get_raw_data()) == bytes(o.get_raw_data()), ("object changed", pid)
    t2 = objs2[path_id].read_typetree()
    assert t2["m_TextureFormat"] == FMT_ASTC_4X4 and bytes(t2["image data"]) == astc

    parts = [out[i:i + SPLIT] for i in range(0, len(out), SPLIT)]
    info = {"file": name, "path_id": path_id, "texture": tex_name, "src_sha256": sha(src), "out_sha256": sha(out),
            "size": [len(src), len(out)], "splits": [nsplit, len(parts)], "objects_moved": moved, "delta": delta,
            "format": ["DXT5Crunched(29)", "ASTC_4x4(48)"], "image_bytes": [old_image, len(astc)], "object_bytes": [len(old_obj), len(new_obj)],
            "quality_vs_decoded_source": quality,
            "split_sha256": [sha(p) for p in parts]}
    return info, parts


def main():
    check = "--check" in sys.argv
    results = []
    for name, pid, tex in TARGETS:
        info, parts = patch(name, pid, tex)
        results.append(info)
        if check:
            ok = all((DST / f"{name}.split{i}").exists() and sha((DST / f"{name}.split{i}").read_bytes()) == h
                     for i, h in enumerate(info["split_sha256"]))
            ok = ok and not (DST / f"{name}.split{len(parts)}").exists()
            print(("ok   " if ok else "DIFF ") + name, tex)
            if not ok:
                raise SystemExit("patched texture file differs")
            continue
        for i, p in enumerate(parts):
            (DST / f"{name}.split{i}").write_bytes(p)
        i = len(parts)
        while (DST / f"{name}.split{i}").exists():  # stale parts of a longer previous layout
            (DST / f"{name}.split{i}").unlink()
            i += 1
        print(name, tex, "->", info["format"][1], "delta", info["delta"], "moved", info["objects_moved"], info["quality_vs_decoded_source"])
    if not check:
        MANIFEST.write_text(json.dumps(results, indent=1) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
