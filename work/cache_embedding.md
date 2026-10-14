# OBB_EMBEDDING — The Room VR: A Dark Matter (PICO 1.02)

## Inputs and findings
- Input: one base APK original/original.apk (sha256 d6608b54…e65e, 2 061 253 795 B).
  No main/patch OBB, no split APKs, no asset packs were supplied, and none are expected by the code:
  - PlayerSettings: no "Split Application Binary"; the data lives in assets/bin/Data (2.56 GB: serialized files,
    .splitN parts, 69 .resource streams) and assets/Videos (19 mp4) inside the base APK.
  - Unity's Play Asset Delivery wrapper (com.unity3d.player.a, references to com.google.android.play.core.assetpacks)
    is present but the Play Core library is not bundled and no asset pack exists; it stays unused.
  - No game-specific OBB reader: smali has no getObbDir / mountObb / .obb reference; libunity.so carries Unity's
    standard probe (getObbDir/getObbDirs, `%s/main.%d.%s.obb`, `%s/patch.%d.%s.obb`), which only looks for files
    and, by calling getObbDir(), creates an empty Android/obb/<pkg> directory at first start (seen on the device:
    empty, created at the R011 clean install). The other libraries' "obb" strings are unrelated words.
- Readers: Unity reads everything through the APK (AssetManager / zip entries; the .splitN parts are concatenated by
  Unity's file system). Nothing is extracted to disk by the game; IL2CPP metadata is read from the APK.

## Decision
- obb_unpack_policy: KEEP_CONTAINER (no operator instruction; nothing to unpack). Cache layout stays base-assets as
  shipped. CACHE_PREPARED / CACHE_RUNTIME_VERIFIED: NOT_APPLICABLE — proven absence of an external cache (single
  complete base APK; every run R002–R016 starts and loads the levels from the installed APK alone; the
  Android/obb/<pkg> directory Unity's probe creates stays empty).
- Size note: the APK is ~2.06 GB because of the level data; a base APK of this size installs over adb on the test
  device (R011, R012–R016). A Play-Store-style split into asset packs is a distribution choice, not needed for
  sideloading, and is left to phase 8.

## Result
- OBB_EMBEDDING research PASSED with a documented not-applicable implementation (no OBB exists).
