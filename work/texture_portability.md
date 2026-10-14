# TEXTURE_PORTABILITY — The Room VR: A Dark Matter (PICO 1.02)

## Inputs and coverage
- decoded_original/assets/bin/Data (original APK sha256 d6608b54…e65e): every serialized file (GUID-named files,
  level*/sharedassets* incl. .splitN parts, globalgamemanagers*, unity default resources) loaded with UnityPy;
  all Texture2D / Cubemap / Texture2DArray / Texture3D objects listed with format, size, mips, colour space and
  usage mode. No AssetBundles, OBB or .resS streams exist; every payload is inline in the serialized files.
- Runtime context: Unity GLES3 on Mali-G76 MC4, OpenGL ES 3.2 (GL_KHR_texture_compression_astc_ldr/hdr,
  ETC2/EAC core; no GL_EXT_texture_compression_s3tc).

## Inventory (1 473 textures + 253 sprites, 1 render texture)
| Unity format | Name | Count | MB | Objects |
|---|---|---|---|---|
| 50 | ASTC 6x6 | 1 356 | 1 967.4 | Texture2D, Cubemap (albedo sRGB, normal maps NormalmapPlain, lightmaps DoubleLDR) |
| 48 | ASTC 4x4 | 29 | 78.4 | Texture2D, Cubemap |
| 1 | Alpha8 | 23 | 51.9 | TextMeshPro SDF font atlases |
| 4 | RGBA32 | 19 | 22.1 | UI sprites, loading logos, controller screens |
| 8 (GraphicsFormat) | R8G8B8A8_UNorm | 6 | 6.3 | Texture3D noise volumes |
| 3 | RGB24 | 2 | 5.2 | blue noise, emissive mask |
| 47 | ETC2 RGBA8 | 1 | 2.8 | Unity splash |
| 13 | RGBA4444 | 34 | 0.1 | Unity built-in GUI skin / watermarks |
| 5 | ARGB32 | 1 | 0.0 | small reflection cubemap |
| 34 | ETC RGB4 | 1 | 0.0 | Unity built-in |
| **29** | **DXT5 Crunched** | **1** | 0.004 | **Nav_pole_arrow_grad** (sharedassets5.assets, path_id 214) |

1 332 of the textures carry full mip chains; colour space is per texture (1 156 sRGB ASTC 6x6, 200 linear —
normal maps, lightmaps, masks). All formats except DXT5 Crunched are decodable by the GLES 3.2 context of the
test GPU (ASTC LDR is core in ES 3.2, ETC2/EAC core since 3.0, the rest are uncompressed).

## Non-universal texture: Nav_pole_arrow_grad
- 256x256, 1 mip, m_ForcedFallbackFormat RGBA32: on GPUs without S3TC Unity unpacks the crunch data to DXT5 and
  decompresses it on the CPU at load. Content: RGB constant white, alpha gradient 0–255 (decoded by
  texture2ddecoder: unpack_unity_crunch → decode_bc3, Unity row order).
- Users: only material HUD_Cone (_MainTex, UI/Unlit/Transparent) in sharedassets5; five HUD_Cone renderers exist in
  the tutorial, all disabled/inactive in every state seen (normal view, teleport mode, inventory) — VR-only helper
  geometry.
- Candidate encodings from the decoded DXT5 source (work tool: astc-encoder 100 % quality, etcpak):
  | Encoding | Bytes | PSNR RGB | PSNR alpha | max abs alpha error |
  |---|---|---|---|---|
  | ASTC 4x4 | 65 536 | ∞ | ∞ (0–1 LSB) | 1 |
  | ASTC 5x5 | 43 264 | ∞ | 76.5 dB | 1 |
  | ASTC 6x6 | 29 584 | ∞ | 70.5 dB | 3 |
  | ASTC 8x8 | 16 384 | ∞ | 65.7 dB | 5 |
  | ETC2 RGBA8 | 65 536 | ∞ | 63.0 dB | 2 |
- Chosen: **ASTC 4x4** (format 48 — the same format as 29 other textures of the build, so the loader path is the
  proven one): smallest error, alpha preserved, size irrelevant at 64 KiB. Resolution and mip count unchanged.

## Conversion (work/tools/patch_textures.py, manifest work/manifests/texture_patches.json)
- Reads the original split file, re-encodes, writes the Texture2D object through the type tree (round trip of the
  unmodified object checked byte-exact first), and edits the serialized file in place: the object grows from 3 860
  to 65 652 bytes (8-aligned slot +61 792), 1 714 later objects are shifted with their object-table byte_start
  values, the header file size updated; the result is split again into 169 × 1 MiB parts for
  app/assets/bin/Data. Self-checks: the edited file parses, every other object is byte-identical, the texture reads
  back as ASTC 4x4 with the encoded payload; `--check` verifies the app copies.
- File: sharedassets5.assets 176 743 056 → 176 804 848 bytes (src sha256 72e2e54b…, out 461a42ea…).

## Runtime (R015)
- out/room_p061.apk (sha256 6225b002…) on redmi-note8pro: cold start → main menu → Continue → tutorial (the scene
  that loads sharedassets5) without new Unity errors (only the pre-existing missing Hx script warnings), all 169
  parts in the APK match the manifest. The HUD_Cone renderers stay inactive in play, so the texture itself is not on
  screen in normal play; force-enabling one renderer by dev command changed no pixel (its GameObject hierarchy is
  inactive).

## Status
- TEXTURE_PORTABILITY research PASSED; TEXTURES_RUNTIME_VERIFIED on Mali-G76: inventory processed, the only
  non-universal payload converted and loaded with its scene; ASTC 6x6/4x4 payloads of the rest render throughout
  R002–R014. Other GPUs (Adreno is ASTC-capable as the original target; PowerVR) device-limited.
