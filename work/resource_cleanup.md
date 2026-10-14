# RESOURCE_CLEANUP — The Room VR: A Dark Matter (PICO 1.02) → flat

## Inputs and coverage
- original APK sha256 d6608b54…e65e: res/ (317 KB: app_icon mipmaps ldpi…xxxhdpi, values, values-v28, values-v30),
  assets/res.json, assets/Videos (19 mp4, ~190 MB), assets/bin/Data (2.56 GB: 3 345 entries), work/manifests.
- Texture inventory (texture_portability.md) and shader export (device_effects.md) were used to size VR-only data;
  scene list from BuildSettings; references checked with UnityPy (material → texture PPtr scans across all files).

## Android resources
| Resource | Owner | Decision |
|---|---|---|
| mipmap-*/app_icon | launcher icon | keep (all densities) |
| values: app_name, styles | manifest, theme | keep; the port adds RoomFlatTheme (values, values-v28, values-v29) and xml/locales_config |
| values-v28 / values-v30 (Unity theme variants) | Unity activity theme fallbacks | keep (tiny, referenced by the theme chain) |
Resource IDs are unchanged by the port except the new style/xml entries appended by aapt2.

## Assets
| Asset | Owner / consumer | Size | Decision |
|---|---|---|---|
| assets/Videos/*.mp4 (notes and void videos) | in-game video notes (VideoPlayer) | ~190 MB | keep — game content |
| assets/bin/Data level*/sharedassets*/GUID files | scenes and their assets | 2.56 GB | keep — every scene is in the progression (hidden_content.md) |
| VR controller help screens (ControllerScreen_* atlases), controller models, PICO/Oculus/SteamVR shaders | VR "view controls" screens and controller art (hidden in the flat port) | ~22 MB textures (+ meshes, shaders) | keep: they live inside shared serialized files with references from scenes/prefabs; removing objects means rewriting those files and their PPtrs for < 1 % of the size |
| Glove textures/models (player hands) | hand rendering (hands are shown when objects are held/inspected) | ~11 MB | keep — live |
| Hidden/HBAO, Hidden/Hx* shaders and the missing-script data of Hx volumetric lights | stripped scripts (no driver) | small | keep (shaders are referenced by the always-included list; data is inside scenes) |
| assets/res.json (PICO "phoenix" boundary/tracking dialog texts) | com.psmart.aosoperation.VRResUtils (kept PICO SDK) | 2 KB | keep with its SDK (removing it could only create a missing-asset path) |
| assets/bin/Data/UnitySubsystems (PxrPlatform, OculusXRPlugin manifests) | XR loader | — | **removed** (code_cleanup.md) |
| unknown/META-INF/com/android/otacert | re-signing tool artefact | 2 KB | **removed** |

Changed asset payloads (not removals): 31 shader files (patch_shaders.py, device_effects.md), sharedassets5.assets
re-split with one ASTC texture (patch_textures.py, texture_portability.md), boot.config.

## Size before / after
- Original APK 2 061 253 795 B; port p061 2 061 396 202 B (+142 KB: libroomfix, port dex/smali, shader and
  texture edits; the removed items are tiny). The APK size is dominated by level data and videos the game needs.

## Result
- RESOURCE_CLEANUP research PASSED: no dead Android resources; VR-only Unity data identified and sized, kept with
  reasons (embedded in shared serialized files, < 1 %); dead XR subsystem manifests and build metadata removed.
