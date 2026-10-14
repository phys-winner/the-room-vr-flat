# DEVICE_EFFECTS — The Room VR: A Dark Matter (PICO 1.02) → flat phones

## Inputs and coverage
- original/original.apk sha256 d6608b54…e65e; Unity 2019.4.40f1 IL2CPP arm64
  (libil2cpp build-id 689e6add…); decoded_original/assets/bin/Data (immutable source of every shader asset).
- Exclusivity: PICO Neo3 / PICO 4 headsets only (Snapdragon XR2, Adreno 650). No other GPU family ever ran this
  build, so every shader was only validated by Adreno's compiler and linker.
- Renderer path: OpenGL ES 3 only for the game (all 217 `TheRoomVR/*` shaders carry only GLES3Plus programs,
  `platforms [9]`; Vulkan/GLES2 programs exist only in Unity built-in resources). Flat target: Mali-G76 MC4
  (Android 11), the only device allowed for this task.
- Coverage: all 288 Shader objects exported with UnityPy (17 596 gles3 subprograms + tier variants) and checked by
  script for (a) fragment inputs not written by the vertex stage, (b) mediump overflow/NaN sources (constants
  ≥ 1000 stored into `u_xlat16_*` temporaries), (c) GL extensions; Unity log of full runs on Mali (p055_r1,
  p057_r1/r2); scene inspection on device (dev console `comps`, `rlist`, `orphans`, `rhide` in rf_scene.c);
  IL2CPP code of every quality/depth/post path (QualitySettingsManager, VrPlatform_Oculus, VrCameraRig).

## Device and renderer gates (producer → consumer)
| Gate | Code / data | PICO value | Flat result |
|---|---|---|---|
| Graphics settings allowed | VrPlatform_Oculus.ShouldAllowGraphicsSettings (slot 17) | false → VrPlatformManager.Start calls SetLevel(default) instead of prefs | same (GraphicsScreen stays hidden; GRAPHICS_UPGRADE decides) |
| Quality level | VrPlatform_Oculus.GetDefaultQualityLevel = 1 (Low) → QualitySettingsManager.SetLevel: shader keyword FP_QUALITY_LEVEL_LOW, Unity quality "Low", imposters on (Level < 3) | Low | same |
| MSAA | GetDefaultMSAA = 4 → QualitySettings.antiAliasing; all three quality levels antiAliasing 4 | 4x | same (GRAPHICS_UPGRADE measures) |
| Render scale | GetDefaultRenderScale = PicoPorting PPTRuntimeSetting.GetDefaultEyeTextureScale | eye texture scale | eye buffers not used in flat |
| Realtime shadows | QualitySettings: shadows 0 (Disable) in Low/Medium/High, pixelLightCount 0 | none | none — SHADOWCASTER passes never draw shadows |
| Camera depth texture | QualitySettingsManager.GetDepthTextureMode: Depth only if AO or Volumetrics or Level == 3 | None (AO/Volumetrics false, Level 1) | None; no game shader samples `_CameraDepthTexture` |
| Post effects | QualitySettingsManager.UpdatePostFX only sets Camera.forceIntoRenderTexture = false | no post FX | same |
| Volumetrics / AO | HxVolumetricLight, HxDummyLight, HxVolumetricCamera, HBAO_Integrated scripts absent from IL2CPP metadata (Unity: "referenced script … is missing"); their shaders (Hidden/HBAO, Hidden/Hx*) are in the build but have no driver | off (code stripped for the VR tier) | off — see finding 3 |
| Stereo | STEREO_MULTIVIEW_ON variants (`GL_OVR_multiview2`) | multiview | non-stereo variants exist for every pass and are the ones linked in flat |
| Foveation | QualitySettingsManager.UpdateFoveationLevel / ShouldManuallyControlFoveation (PICO FFR) | on | inert without the PICO runtime (VR_RUNTIME_DETACHED) |

## Findings

### 1. FastGlow renders as a hard, uniformly lit disc on Mali — defect, fixed
- Assets: shaders TheRoomVR/FastGlow (6be81ece…, 4 gles3 subprograms) and TheRoomVR/FastGlow_HideOutsidePortal
  (3e1f40d7…, 8 subprograms); 14 / 4 serialized files name them as external dependency (main menu MainMenu_Glow
  blue/orange on sphere meshes r 0.5, scale 4.19 and 2.76×4.95×2.76; tutorial TUT_Glow ×3 and
  TUT_ProjectorGlow_Yellow; later levels).
  Material values: _FresnelPow 12.35, _AlphaScale 0.12 / 0.061, _Intensity 2.35, _Dither 0.
- Producer → consumer: fragment stage, alpha-blended Transparent+1, ZWrite Off. Intended alpha =
  |N·V|^FresnelPow · AlphaScale · Color.a · Intensity (bright centre, soft fall-off). The view vector gets a dither
  offset `hash · _Dither`, where `hash = fract(dot(vec2(171119, 2312319), clipXY + time·0.0123) · (1/103, 1/71)) − 0.5`
  is stored in the mediump temporary `u_xlat16_1`.
- Cause on Mali: mediump is fp16; the dot product (~10^6–10^7) overflows to +inf, fract(inf) = NaN,
  NaN · _Dither(0) = NaN, so N·V and the fresnel term are NaN and `min(NaN, 1.0)` returns 1.0 → the alpha becomes
  AlphaScale · Intensity over the whole sphere silhouette (blue disc / orange wedge of the main menu). Adreno did not
  show it (the authored look is a fresnel halo); the GLSL result for NaN is undefined, Mali's is the visible one.
- Proof of ownership: `rhide MainMenu_PoliceGlow 1` and `rhide MainMenu_PoliceGlow (1) 1` remove both shapes;
  `comps MainMenu_PoliceGlow` → MeshRenderer MainMenu_Glow[TheRoomVR/FastGlow] (R012 screenshots).
- Patch (glow_hash): the hash temporary is declared `highp` in the 12 fragment stages (6 vec2, 4 vec3, 2 vec4
  declarations). In fp32 the hash stays finite, × _Dither(0) = 0, so the fresnel term is exactly the authored one;
  materials with _Dither > 0 keep their dither. Nothing else in the program changes.

### 2. SHADOWCASTER programs fail to link on Mali — defect, fixed
- 110 subprograms in 29 shaders (Dissolve / DissolveReversed / GogglesSheen / MirrorReveal / FastRefraction
  dissolve families and the two `*_Cutout_VertColor` variants): the SHADOWCASTER fragment stage reads
  `vs_TEXCOORD0` (dissolve-noise UV) or `vs_COLOR0` (vertex alpha for cutout) that the vertex stage never writes
  (HLSLcc dropped the unassigned output). Adreno links such programs; Mali rejects them
  (`L0007 Fragment shader uses a varying … that has not been declared in the vertex shader`), 30 errors per run.
- Consumer analysis: shadows are disabled in every quality level and the camera depth texture is only requested
  with AO/volumetrics (stripped) or Level 3 (never set by this build); no game shader samples the depth texture.
  The programs are linked only by the shader warm-up (ShaderCompilationManager), so no frame ever used them, on
  PICO or here. The forward passes of the same shaders link and render (tutorial key TUT_Key_Placed).
- Patch (varying): the vertex stage declares the missing output and writes a neutral value — texcoord `vec2(0.0)`,
  vertex colour `vec4(1.0)` (the value Unity uses for meshes without colours, so a cutout depends on the texture
  alpha as in the forward pass). Real UVs would need new vertex-channel bindings and uniforms in the serialized
  program; not justified for a pass that never draws. 102 `vs_TEXCOORD0` + 8 `vs_COLOR0` insertions.

### 3. Volumetric lights, HBAO and post FX are compiled out of the PICO build — not restorable, no artefact
- level6 (tutorial) objects IntmoonLight_Vol_window/_projector, IntLight_Vol_01, ExtmoonLight_Vol, Alley_Vol1/2
  carry only a Transform and two MonoBehaviours whose script types (HxVolumetricLight 328 B, HxDummyLight 76 B of
  serialized settings) do not exist in the IL2CPP metadata; CenterEyeAnchor misses HxVolumetricCamera /
  HBAO_Integrated. No renderer is attached to any of them (UnityPy scene dump), so nothing is drawn; Unity only logs
  "referenced script is missing" at scene load. The Hx*/HBAO shaders remain in the data but nothing dispatches them.
- Restoring them means re-implementing the third-party Hx Volumetric Lighting / HBAO C# drivers (command buffers,
  camera hooks) — not present in any input; recorded as a rejected GRAPHICS_UPGRADE candidate, not as a defect.

### 4. Other GPU-specific code reviewed — no change needed
- Extensions: `GL_OVR_multiview2` only in STEREO_MULTIVIEW_ON variants; `GL_AMD_vertex_shader_layer`,
  geometry-shader extensions only in Unity built-in STEREO_INSTANCING_ON variants; `GL_EXT_shader_texture_lod`
  only in GLES2 programs. None is linked in flat rendering on GLES3.
- Precision sites with large constants: wind vertex noise (`43758.5469` into highp temporaries), hologram shake
  vertex value-noise (mediump, quantised but finite — no NaN path), reflection-probe box projection (`1000.0`
  fits fp16), Unity Skybox/Procedural (`8000.0` then clamp). Only FastGlow had an inf → NaN chain.
- `UNITY_ADRENO_ES3` blocks are Unity's own Adreno workarounds; Mali takes the standard `clamp` branch.

## Changes
- work/tools/patch_shaders.py: reads decoded_original/assets/bin/Data, edits only the GLES3 program text of the
  Shader object (LZ4 blob rebuilt with contiguous 4-aligned subprogram entries, recompressed), keeps the serialized
  file header, metadata and 4096-byte data offset (splice of the object bytes, object-table size updated; the type
  tree writer is round-trip checked on the unmodified object first), writes app/assets/bin/Data. `--check` verifies
  the app copies.
- work/manifests/shader_patches.json: 31 files with source/output sha256, blob sizes, per-subprogram fixes and code
  hashes. Re-export of the patched files: the only text differences are the 12 precision lines and the 110
  declaration+assignment pairs; the varying checker reports 0 mismatched subprograms (was 110).

## Runtime evidence (work/evidence/R012_device_effects/RESULT.md)
- p055 vs p057 on redmi-note8pro: main-menu disc/wedge gone, soft halo around the lamp (luminance profile);
  tutorial FastGlow renderers subtle as authored; dissolve key renders; Home → relaunch without artefacts;
  GLSL link errors 30 → 0; patched programs compiled and cached under new source hashes over an existing
  UnityShaderCache (update installs pick them up without clearing the cache).

## Status
- DEVICE_EFFECTS research: PASSED. Implementation: DEVICE_EFFECTS_VERIFIED on Mali-G76 (only allowed device).
- Device-limited: Adreno (the original GPU family) and PowerVR cannot be run here. The patched GLSL is plain
  GLSL ES 3.00 (highp temporaries, declared outputs) that Adreno already accepted in looser form; re-check on
  other GPUs when such a device is allowed.
- Not a substitute for an original-renderer reference: no PICO capture exists; the intended FastGlow look is
  derived from the shader math and material values, not from a pixel reference.
