#!/usr/bin/env python3
"""Removes payload paths from the canonical app/ tree that are ignored by Git (assets/bin/Data/*).

Each entry is moved to work/removed/ (restorable) after checking its SHA-256 against the decoded original.
Decisions and reasons: work/code_cleanup.md.
"""
import hashlib
import shutil
import sys
from pathlib import Path

GAME = Path(__file__).resolve().parents[2]
REMOVE = {
    # Unity XR subsystem manifests: make Unity load and register the PICO / Oculus XR plugins at start-up.
    "assets/bin/Data/UnitySubsystems/PxrPlatform/UnitySubsystemsManifest.json":
        None,
    "assets/bin/Data/UnitySubsystems/OculusXRPlugin/UnitySubsystemsManifest.json":
        None,
}


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    for rel in REMOVE:
        src = GAME / "app" / rel
        ref = GAME / "decoded_original" / rel
        dst = GAME / "work" / "removed" / rel
        if not src.exists():
            print(f"already removed: {rel}")
            continue
        if ref.exists() and sha(src) != sha(ref):
            raise SystemExit(f"preimage mismatch: {rel}")
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(src), str(dst))
        print(f"removed: {rel}")
    for d in sorted({(GAME / "app" / r).parent for r in REMOVE}, reverse=True):
        while d != GAME / "app" and d.exists() and not any(d.iterdir()):
            d.rmdir()
            d = d.parent


if __name__ == "__main__":
    sys.exit(main())
