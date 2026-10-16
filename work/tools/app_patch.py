#!/usr/bin/env python3
"""The port's edits of the decoded game as one patch: patches/app.patch (unified diff, decoded_original -> app).

The repository carries no decompiled game files. app/ is rebuilt from a decode of the player's own APK and this
patch (prepare_app.py). Generated payloads are not part of the patch: libroomfix.so and smali/port/ come from
build_port.py, the binary asset edits from patch_shaders.py / patch_textures.py / patch_buildsettings.py, the
removed files are moved out by prune_app.py.

  app_patch.py make     write patches/app.patch from decoded_original/ and app/
  app_patch.py apply    apply patches/app.patch to app/ (a fresh copy of decoded_original/)
  app_patch.py check    every patched file of app/ equals what apply produces from decoded_original/

Line endings: apktool writes CRLF on Windows and LF elsewhere; a patched file is always written with LF (the
canonical app/ keeps its edited files that way). Hunks are matched line by line without the ending, so any decode
of the same APK applies and gives byte-identical files (the recorded sha256 after the arrow is checked).
"""
import difflib
import hashlib
import re
import sys
from pathlib import Path

GAME = Path(__file__).resolve().parents[2]
SRC = GAME / "decoded_original"
DST = GAME / "app"
PATCH = GAME / "patches" / "app.patch"

# generated or binary-patched paths (relative to app/), never part of the text patch
GENERATED = re.compile(r"^(build/|dist/|lib/arm64-v8a/libroomfix\.so$|smali/port/|assets/bin/Data/(?!boot\.config$))")
REMOVED_BY_PRUNE = {  # deleted from app/ by prune_app.py (kept restorable in work/removed/)
    "assets/bin/Data/UnitySubsystems/OculusXRPlugin/UnitySubsystemsManifest.json",
    "assets/bin/Data/UnitySubsystems/PxrPlatform/UnitySubsystemsManifest.json",
    "unknown/META-INF/com/android/otacert",
}
NO_EOL = "\\ No newline at end of file"


def sha(b):
    return hashlib.sha256(b).hexdigest()


def split(data):
    """(lines without endings, ends with a newline)."""
    text = data.decode("utf-8")
    lines = re.split(r"\r?\n", text)
    if lines and lines[-1] == "":
        return lines[:-1], True
    return lines, False


def join(lines, final_nl):
    return ("\n".join(lines) + ("\n" if final_nl and lines else "")).encode("utf-8")


def walk(root):
    return {p.relative_to(root).as_posix(): p for p in root.rglob("*") if p.is_file()}


def make():
    a, b = walk(SRC), walk(DST)
    changed = []
    for rel in sorted(set(a) | set(b)):
        if GENERATED.match(rel):
            continue
        if rel in a and rel not in b:
            if rel not in REMOVED_BY_PRUNE:
                raise SystemExit(f"{rel}: removed in app/ but not by prune_app.py")
            continue
        da = a[rel].read_bytes() if rel in a else None
        db = b[rel].read_bytes()
        if da == db:
            continue
        try:
            la, nla = split(da) if da is not None else ([], True)
            lb, nlb = split(db)
        except UnicodeDecodeError:
            raise SystemExit(f"{rel}: binary change outside the patch scripts")
        if b"\r" in db or not nlb:
            raise SystemExit(f"{rel}: the canonical app/ copy of a patched file must use LF and end with a newline")
        if la == lb and nla == nlb:
            raise SystemExit(f"{rel}: differs from the decode only in line endings - restore the decoded copy")
        changed.append((rel, da, db, la, nla, lb, nlb))
    out = ["# The port's edits of the decoded game (decoded_original -> app), written by work/tools/app_patch.py.",
           "# Apply with work/tools/prepare_app.py; per file: sha256 of the decoded file -> sha256 of the result."]
    for rel, da, db, la, nla, lb, nlb in changed:
        out.append(f"# sha256 {sha(da) if da is not None else '-'} -> {sha(db)}")
        out.append(f"--- {'a/' + rel if da is not None else '/dev/null'}")
        out.append(f"+++ b/{rel}")
        sm = difflib.SequenceMatcher(None, la, lb, autojunk=False)
        for group in sm.get_grouped_opcodes(3):
            i1, i2, j1, j2 = group[0][1], group[-1][2], group[0][3], group[-1][4]
            out.append(f"@@ -{i1 + 1 if i2 > i1 else i1},{i2 - i1} +{j1 + 1 if j2 > j1 else j1},{j2 - j1} @@")
            for tag, a1, a2, b1, b2 in group:
                if tag == "equal":
                    for i in range(a1, a2):
                        out.append(" " + la[i])
                        if i == len(la) - 1 and not nla:
                            out.append(NO_EOL)
                    continue
                for i in range(a1, a2):
                    out.append("-" + la[i])
                    if i == len(la) - 1 and not nla:
                        out.append(NO_EOL)
                for i in range(b1, b2):
                    out.append("+" + lb[i])
                    if i == len(lb) - 1 and not nlb:
                        out.append(NO_EOL)
    PATCH.parent.mkdir(parents=True, exist_ok=True)
    PATCH.write_bytes(("\n".join(out) + "\n").encode("utf-8"))
    print(f"wrote {PATCH.relative_to(GAME).as_posix()}: {len(changed)} files")
    for rel, da, *_ in changed:
        print(f"  {'new ' if da is None else ''}{rel}")


def parse(data):
    """[[rel, src_sha, dst_sha, created, hunks]], hunks [(old_start, [(op, text)])]; a checkout that turned the
    patch into CRLF still parses (endings are dropped)."""
    files, cur, hunk, meta, created = [], None, None, None, False
    for ln in re.split(r"\r?\n", data.decode("utf-8")):
        if ln.startswith("# sha256 "):
            s, _, d = ln[9:].strip().partition(" -> ")
            meta = (None if s == "-" else s, d)
        elif ln.startswith("--- "):
            created, hunk = ln[4:].strip() == "/dev/null", None
        elif ln.startswith("+++ b/"):
            cur = [ln[6:].strip(), meta[0] if meta else None, meta[1] if meta else None, created, []]
            files.append(cur)
            meta = None
        elif ln.startswith("@@ "):
            m = re.match(r"@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@", ln)
            hunk = (int(m.group(1)), [])
            cur[4].append(hunk)
        elif ln == NO_EOL:
            op, text = hunk[1][-1]
            hunk[1][-1] = (op + "!", text)  # that line ends its file without a newline
        elif ln[:1] in (" ", "-", "+") and hunk is not None:
            hunk[1].append((ln[:1], ln[1:]))
        elif ln.startswith("#") or not ln.strip():
            continue
        else:
            raise SystemExit(f"app.patch: unexpected line {ln[:60]!r}")
    return files


def apply_file(lines, hunks, rel):
    """Hunks matched line by line without endings (offset search, no fuzz); the result is LF with a final newline."""
    out, pos = [], 0
    for old_start, body in hunks:
        old = [t for op, t in body if op[0] != "+"]
        want = max(old_start - 1, 0) if old else old_start
        at = None
        for off in sorted(range(-200, 201), key=abs):
            k = want + off
            if pos <= k <= len(lines) - len(old) and all(lines[k + i] == t for i, t in enumerate(old)):
                at = k
                break
        if at is None:
            raise SystemExit(f"{rel}: hunk at line {old_start} does not match the decode")
        out += lines[pos:at]
        k = at
        for op, t in body:
            if op[0] == " ":
                out.append(lines[k])
                k += 1
            elif op[0] == "-":
                k += 1
            else:
                out.append(t)
        pos = k
    out += lines[pos:]
    return join(out, True)


def apply(check=False):
    files = parse(PATCH.read_bytes())
    for rel, src_sha, dst_sha, created, hunks in files:
        target = DST / rel
        if created:
            base = b""
            if not check and target.exists():
                raise SystemExit(f"{rel}: exists, expected a new file")
        else:
            src = SRC / rel if check else target
            if not src.is_file():
                raise SystemExit(f"{rel}: missing in the decode")
            base = src.read_bytes()
        new = apply_file(split(base)[0] if base else [], hunks, rel)
        if dst_sha and sha(new) != dst_sha:
            raise SystemExit(f"{rel}: patched result {sha(new)[:12]} != recorded {dst_sha[:12]}")
        if check:
            if not target.is_file() or target.read_bytes() != new:
                raise SystemExit(f"check FAILED: {rel} differs from decode + patch")
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(new)
    print(f"{'checked' if check else 'applied'} {len(files)} files")


def main(a):
    cmd = a[0] if a else ""
    if cmd == "make":
        make()
    elif cmd == "apply":
        apply()
    elif cmd == "check":
        apply(check=True)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
