#!/usr/bin/env python3
"""Print class bodies from Il2CppDumper dump.cs by exact class name (nested: Outer.Inner)."""
import re, sys
from pathlib import Path

DUMP = Path(__file__).resolve().parents[1] / "il2cppdumper" / "dump.cs"

def main(argv):
    src = DUMP.read_text(encoding="utf-8")
    maxlines = 100000
    names = []
    for a in argv:
        if a.startswith("--max="):
            maxlines = int(a[6:])
        else:
            names.append(a)
    for name in names:
        pat = re.compile(r"\n([^\n]*(?:class|struct|enum|interface) " + re.escape(name)
                         + r"(?:<[^>\n]*>)?(?:\s*:[^\n]*)?\s*// TypeDefIndex: \d+\n\{)")
        found = False
        for m in pat.finditer(src):
            found = True
            start = m.start(1)
            end = src.find("\n}\n", start)
            body = src[start:end + 2].split("\n")
            # drop attribute-only lines to keep it compact
            body = [l for l in body if not re.match(r"^\s*\[(CompilerGenerated|DebuggerHidden|DebuggerBrowsable|SerializeField|Tooltip|Header|Space|HideInInspector|Range|FormerlySerializedAs|TextArea|Multiline|Min|ContextMenu)", l)]
            print("\n".join(body[:maxlines]))
            print("-----")
        if not found:
            print("NOT FOUND:", name)

if __name__ == "__main__":
    main(sys.argv[1:])
