# GRAPHICS_UPGRADE — The Room VR: A Dark Matter (PICO 1.02) → flat phones

## Inputs and coverage
- original APK sha256 d6608b54…e65e, Unity 2019.4.40f1 IL2CPP (libil2cpp build-id 689e6add…), GLES3 renderer.
- Code paths read in the IL2CPP dump/disassembly: QualitySettingsManager (SetLevel, SetMSAA, Set*FromPrefs,
  GetDepthTextureMode, ImpostersEnabled, UpdatePostFX, Update), VrPlatformManager.Start / ShouldAllowGraphicsSettings,
  VrPlatform_Oculus (graphics defaults), GraphicsScreen (all handlers), OptionsScreen.GraphicsSettingsButton,
  PXR_Manager.Awake / PVRManager.Awake, OnQualitySettingsChanged listeners; data: QualitySettings asset in
  globalgamemanagers, all 288 shaders (UnityPy export), localisation keys FE_GFX_* in all game dictionaries.
- Runtime state read on device with the dev console `gfx?` (rf_gfx.c): screen/camera size, render target, MSAA,
  depth texture, Unity quality index, QualitySettingsManager fields. Evidence: work/evidence/R013_graphics_upgrade.

## Baseline (flat port before this work)
- Internal resolution: the window's 2264x1080 view rendered 1:1 (camera pixelWidth/Height = Screen, no target
  texture). Render scale is 1.0 of the physical view; no supersampling.
- Quality: VrPlatform_Oculus.ShouldAllowGraphicsSettings = false → VrPlatformManager.Start sets the PICO defaults
  without prefs: Level 1 "Low" (GetDefaultQualityLevel), MSAA 4 (GetDefaultMSAA), render scale from PICO
  PPTRuntimeSetting, AO/bloom/volumetrics off. The Options screen hides its GRAPHICS SETTINGS button.
- MSAA actually **0**: PXR_Manager.Awake and PVRManager.Awake overwrite QualitySettings.antiAliasing with the PICO
  runtime's AntiAliasingLevelRecommended (UPxr_GetConfigInt(25)), which is 0 without the runtime — a VR-runtime
  leak into the flat frame (aliased edges, R013 crops).
- QualitySettings asset: Low/Medium/High, shadows disabled in all, pixelLightCount 0, antiAliasing 4, skinWeights
  2/4/4, anisotropic Enable/Enable/ForceEnable, textureQuality 0 (full resolution) everywhere, soft particles off.
- GPU: Mali-G76 MC4, GLES 3.2, ASTC LDR/HDR, GL_EXT_multisampled_render_to_texture, anisotropic filtering 16x.

## Where each switch reaches the engine
| Switch | Mechanism | Effect in this build |
|---|---|---|
| Environment detail (Level 1/2/3) | QualitySettingsManager.SetLevel → shader keywords FP_QUALITY_LEVEL_LOW/MEDIUM/HIGH, QualitySettings.SetQualityLevel("Low/Medium/High"), PlayerPrefs QUALITY, OnQualitySettingsChanged | LOW variants exist in 5 shaders (teleport pointer, ooze, sand dune, cauldron liquid); Imposter.UpdateImposter uses imposters below High; listeners EnableComponentFromQualitySetting, QualityLevelVisibility, MaterialManager, Sandstorm, WitchSoul, TeleportPointer switch authored content (main-menu ground fog at High, R013); skin weights 2→4; anisotropic forced at High |
| Anti aliasing 0/2/4/8 | SetMSAA → QualitySettings.antiAliasing, PlayerPrefs MSAA | backbuffer MSAA at runtime (Mali tile resolve) |
| Camera depth texture | GetDepthTextureMode: AO or volumetrics or Level 3 → VrCameraRig sets Camera.depthTextureMode | no consumer: no shader samples it, HBAO/Hx/soft particles absent |
| Render scale | QualitySettingsManager.Update → XRSettings.eyeTextureResolutionScale | XR eye buffers only; nothing in flat |
| Texture quality | GraphicsScreen.OnTextureQualityChanged is an empty method | none |
| AO / bloom / volumetrics | flags only feed GetDepthTextureMode; the HBAO, bloom and Hx volumetric drivers are not in the build | none (see device_effects.md finding 3) |
| Post FX | UpdatePostFX only resets Camera.forceIntoRenderTexture | none |

## Candidates and decisions
| Candidate | Decision | Evidence / test |
|---|---|---|
| MSAA lost to the PICO runtime query | **enable** (fix): libroomfix answers AntiAliasingLevelRecommended with the game's own MSAA (QualitySettingsManager.MSAA; 4 before it starts) — h_pxr_config_int | static A/B: strong-edge pixels 22 162 → 16 438; runtime antiAliasing 4; user 8x survives scene loads and cold start |
| Hidden graphics settings screen | **enable**: VrPlatform_Oculus.ShouldAllowGraphicsSettings answers true (one instruction, preimage-checked); Options gets GRAPHICS SETTINGS, VrPlatformManager.Start reads the saved QUALITY/MSAA prefs | real-touch arrows LOW→MEDIUM→HIGH, 4X→8X applied immediately; cold start restores Level 3 / 8x |
| Dead rows on that screen (texture quality, render scale, AO, bloom, volumetric lighting) | **hide** (h_gfx_setup) — they change nothing in this build | screen shows only ENVIRONMENT DETAIL and ANTI ALIASING |
| Depth pre-pass at High | **remove**: GetDepthTextureMode answers None | identical frames with/without (R013); GPU 7.79 → 6.69 Mcycles/frame |
| Default environment detail | **keep Low** (PICO tier) | GPU Mcycles/frame Low 4.01, Medium 4.94, High 6.69 (no depth); game fps CPU-bound under MIUI thermal caps (26–56) even at Low on the test phone, so no higher level holds 60 there; stronger phones choose Medium/High in the screen |
| Default MSAA | **keep 4x** (authored) | Low no-AA 3.53 vs 4x 4.01 Mcycles (+14 %), no PSS change |
| Render scale > 1.0 (supersampling) | reject | GPU budget already the limit at High; CPU-bound device; render scale row is XR-only |
| Texture quality / higher-resolution textures | reject (nothing to unlock) | textureQuality 0 = full-resolution mips in every level; no HD texture set in the data |
| Realtime shadows | reject | content is lightmapped; shadows disabled in every quality level; no shadow-casting light setup to restore |
| HBAO / bloom / volumetric lighting | reject (not restorable) | C# drivers stripped from the PICO build (missing-script MonoBehaviours, orphaned Hidden/HBAO and Hidden/Hx* shaders) |
| Anisotropic filtering | keep as authored (forced at High) | part of the High profile |

## Measurements (R013)
- GPU cost per frame on the same tutorial view (MediaTek GED KPI gpu time × gpu clock): Low 4x 4.01, Medium 4x
  4.94, High 4x 7.79 → 6.69 without the idle depth pass, Low without AA 3.53 Mcycles.
- Memory: PSS 934–958 MB / RSS 1 054–1 079 MB, no level-specific change (dumpsys meminfo, 15 s per level).
- Frame rate: limited by the main thread on the throttled test phone (26–56 fps windows), GPU at its lowest
  operating point (270–314 MHz) with ~50 % load — quality changes cost GPU time, not frame rate, until the CPU limit
  is lifted; FPS_LIMIT_VERIFIED measurements are recorded separately.

## Result and open items
- Implemented in src/roomfix/rf_gfx.c (production hooks: PXR_CONFIGINT, GFX_SETUP, QSM_DEPTHMODE, one-instruction
  patch of VrPlatform_Oculus.ShouldAllowGraphicsSettings via hook.c rf_patch_insn; dev-only: `gfx?`, `quality`,
  `aa`, `depthtex`, UpdatePostFX A/B hook). Selected profile: Low + 4x MSAA by default, Medium/High and 0/2/4/8x
  selectable in the game's own graphics screen; High without the idle depth pass.
- Open: visual check of Medium/High content switches (QualityLevelVisibility, EnableComponentFromQualitySetting,
  Sandstorm, WitchSoul, MaterialManager) in the later chapters during the TOUCH_GAMEPLAY route; repeat GPU/fps on
  the final build (release libroomfix, no dev logging) with the device cooled.
