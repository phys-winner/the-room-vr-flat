# Game port state — The Room VR: A Dark Matter (PICO build) → flat touch phone

## Current
- state_revision: 3
- updated_at: 2026-10-14T10:45+03:00
- objective: ADAPTATION_COMPLETE for a VR→flat conversion (user /goal 2026-10-14: «/adapt-arm32-game адаптируй vr игру под плоские сенсорные экраны. работай только с adb устройством redmi-note8pro»)
- workflow_status: IN_PROGRESS
- current_step: phase 4–6 research reports; DEVICE_EFFECTS closed on Mali (R012); phase 5 touch/HUD/options/localisation/manifest modernisation done on device (R007–R011)
- next_action: GRAPHICS_UPGRADE + HIDDEN_CONTENT (quality level Low by VrPlatform_Oculus, hidden GraphicsScreen, MSAA, skip-level/debug menu), then CODE/RESOURCE/PERMISSION/OBB/TEXTURE reports; FPS_LIMIT_VERIFIED measurements; FOV policy → Hor+ against a 16:9 reference (default 90); lifecycle + data-root restart/update checks; release build without dev paths
- blocker: none
- bridge_mode: NOT_APPLICABLE — input is native arm64-v8a only (lib/arm64-v8a: libil2cpp, libunity, libmain, libpxr_api, libPxrPlatform, libpxrplatformloader); no armeabi-v7a code exists
- bridge_input_snapshot: NOT_APPLICABLE (no bridge); ARM32_READY is interpreted as "restoration snapshot verified" for a native arm64 game (workflow.md "Native arm64 runtime")
- content_review_status: NOT_READY

## Project and inputs
- root: <game> (PROJECT_ROOT <workspace>: nearest parent with tools/android/toolchain.sh; machine_config project_root <other-machine>/... is stale on this machine — tools resolved from E:)
- input: original/original.apk (NTFS hardlink of the user's file in the game root, unmodified), sha256 d6608b54f62f3b2166292655945e8a32e717b18e07b1de010d410a68acfbe65e, 2061253795 B; manifest work/manifests/original.json
- package com.FireproofStudios.TheRoomVRPlus, versionCode 5011, versionName 1.02_PICO_1.0, original minSdk 26 / targetSdk 29, single base APK, no OBB/splits
- engine: Unity 2019.4.40f1, IL2CPP (metadata v24.5), arm64-v8a only, GLES3 only for game shaders; XR: Unity.XR.PICO (PXR) + PICO.Porting adapters over Oculus.VR (VrPlatform_Oculus); Rewired input; Bolt visual scripting; FMOD
- data: assets/bin/Data 2.58 GB (3345 entries; GUID-named serialized files, level0..16 with .splitN parts, .resource), assets/Videos 19 mp4 (~190 MB); texture compression declared GL_KHR_texture_compression_astc_ldr
- decoded_original/: one apktool 3.0.3 decode, manifest work/manifests/decoded_original.json (tree digest e9c5b914…)
- app/: canonical sources = decoded copy + tracked edits (manifest, smali hooks, res/xml, port smali) + generated payloads: lib/arm64-v8a/libroomfix.so (build_port.py native), 31 shader files in assets/bin/Data (work/tools/patch_shaders.py, manifest work/manifests/shader_patches.json), XR subsystem manifests moved to work/removed (prune_app.py)
- IL2CPP dump: work/il2cppdumper/ (not tracked); helper tools work/tools/{il2dis,xrefs,dumpcls}.py
- Git: <game>/.git; large payloads ignored and hashed in work/manifests

## Decisions and scope
| Decision | Scope | Source/date | Current value | Supersedes |
|---|---|---|---|---|
| Device | All hardware work | User /goal 2026-10-14 | Only adb serial redmi-note8pro; never touch pixel-10a (Pixel 10a) or any other serial | — |
| Test devices | redmi-note8pro, package com.FireproofStudios.TheRoomVRPlus | Skill policy | Root probe/use; uninstall/reinstall/clear without backup or repeated approval | — |
| Scope | Full adaptation | User 2026-10-14 | VR→flat with touch controls, all nine research rows, phase 8 choice later | — |
| ARM64 transition | — | Input is native arm64 | bridge NOT_APPLICABLE; final 16 KB runtime check cannot run on the only allowed device (4 KB pages) → device-limited gate | — |
| obb_unpack_policy | Cache | Skill default | KEEP_CONTAINER (no OBB exists; data already inside the base APK) | — |
| Git payload policy | Repo | Agent 2026-10-14 | Binary payloads ignored, hashed in work/manifests; modified binaries produced by tracked scripts with preimage checks | — |
| FPS limit | Options screen | Skill contract | rows 30 / 45 / 60 / display rate, default 60, Application.targetFrameRate; stored in port_settings.cfg | — |
| FOV | Options screen | Agent 2026-10-14 | option row 70–110 (default 100, hor at actual aspect) — to change: Hor+ against 16:9 reference, default 90 | — |

## Toolchain
- machine_config.json: skill copy; java <jdk-24.0.1>/bin/java.exe
- APK tools: <workspace>/tools/android (apktool 3.0.3, smali/baksmali 3.0.9-dev, build-tools 37.0.0 aapt2/zipalign); build: work/tools/build_port.py [native] [java] [apk] [--dev] → out/base.apk, renamed out/room_pNNN.apk
- NDK 27.2.12479018 (%LOCALAPPDATA%/Android/Sdk/ndk), clang 18, target aarch64-linux-android24, -z max-page-size=16384
- signer: <signer-dir>/{apksigner.jar,testkey.pk8,testkey.x509.pem}, cert sha256 a40da80a…f5dc
- adb pinned: %LOCALAPPDATA%/Android/Sdk/platform-tools/adb.exe; device helpers work/tools/{devrun,route,touchroute}.py, mtinject (evdev multi-touch injector, root)
- local Python 3.13.3: UnityPy 1.25.3, lz4 4.4.5, capstone, pyelftools, texture2ddecoder, astc-encoder-py, etcpak, Pillow, numpy

## Devices
| Role | Serial | ABI evidence | Runtime page size/source | Status |
|---|---|---|---|---|
| test device (all roles allowed by the user) | redmi-note8pro | Redmi Note 8 Pro (begonia, MT6785, Mali-G76 MC4), Android 11/SDK 30, MIUI 12.5, abilist arm64-v8a,armeabi-v7a,armeabi | 4096 (getconf PAGESIZE) | online; root via Magisk `su -c` |

- display 1080x2340 (landscape 2340x1080), 440 dpi, single mode 60 Hz, cutout top 76 px; MIUI letterboxes the cutout side for this app (window [76,0][2340,1080]) despite shortEdges + MIUI notch flags — open item
- final_acceptance (arm64-only + 16 KB runtime): no allowed device has 16 KB pages → open, device-limited (not faked)

## Artifacts
| Role | Source revision/patch | Build inputs | APK/set hash manifest | Evidence |
|---|---|---|---|---|
| accepted | none yet | | | |
| candidate (dev) | HEAD a01750b | app/ (dev libroomfix 36fe29b9…, 31 patched shaders) | out/room_p057.apk aae8d362da1f467ff358c0a83bb69303139fb55de94869b32f58ecccdccc2762 | R012 |
| previous candidate | d0e54fb | app/ | out/room_p055.apk e34a72319d54…16a3 (clean-install baseline for R011) | R011 |
| installed per serial | redmi-note8pro: room_p057.apk (install -r over p055, data kept) | a01750b | — | R012 |
| ARM32_LASTGOOD and snapshot | none | | | |
| ARM64_ACCEPTED before additions | none | | | |

- storage_cleanup: out/room_p007.apk and old build-cache attempts are obsolete (superseded by p055/p057) — remove at the next cleanup pass

## Gates
| Phase/subgate | Status | Evidence | Dependency |
|---|---|---|---|
| PHASE0_1_SETUP | PASSED | inputs hashed, decode, app copy, git; portable signer builds | — |
| ORIGINAL_BEHAVIOUR_RECORDED | PASSED | work/evidence/R001_original_baseline/RESULT.md | — |
| VR_RUNTIME_DETACHED | IN_PROGRESS | R002 no PICO dialog/crash; R011 XR subsystem manifests pruned, PXR verify service skipped, entitlement forced, VR meta-data removed | release build (no dev paths) + soak |
| FLAT_FRAME_VERIFIED | IN_PROGRESS | R002 validated frames; R012 FastGlow disc fixed, Mali link errors 30 → 0 | open: MIUI cutout letterbox re-check |
| FLAT_CAMERA_VERIFIED | IN_PROGRESS | R003 look, R008 FOV row, R009 arrival facing/resting hands, R010 pinch lean | FOV Hor+ (16:9 ref, default 90) change + check |
| VR_ACTIONS_COVERED | IN_PROGRESS | R003–R010: menus, snap/teleport, grab/levers/sliders/knobs/cranks, inventory/receivers, eyepiece, hints, pinch lean, hold+look, Back | superzoom/poke/ropes reached on device or exact debts |
| TOUCH_GAMEPLAY_VERIFIED | IN_PROGRESS | R009 new game → tutorial safe by real touch; R010 later-mechanics samples; work/touch_controls.md | full route beyond the tutorial puzzles |
| DEVICE_EFFECTS_VERIFIED | PASSED (Mali-G76) | R012; work/device_effects.md | other GPUs device-limited |
| RESTORATION_RESEARCH_COMPLETE | NOT_STARTED | Nine reviewed reports | All research rows PASSED |
| DATA_EXTERNAL_ROOT (5.1.1) | IN_PROGRESS | R011 clean install: files/shared_prefs/databases redirected to getExternalFilesDir, saves written | restart/resume/update-install checks |
| FPS_LIMIT_VERIFIED | IN_PROGRESS | R008 selector (30/45/60/display, default 60) → targetFrameRate | SurfaceFlinger measurements of 4 modes, persistence, lifecycle, simulation speed |
| ARM32_READY (restoration snapshot) | NOT_STARTED | | phases 0–6 |
| BRIDGE (phase 7 guest gates) | NOT_APPLICABLE | native arm64 input | — |
| ARM64_ACCEPTED | NOT_STARTED | | final matrix; 16 KB runtime proof device-limited |
| PORT_FEATURES_READY | NOT_STARTED | PORT_FEATURES.md matched to final APK | ARM64_ACCEPTED and phase 8 decision |

## Required research
| Direction | Research | Report and input revision/hash | Implementation gate, evidence, dependency |
|---|---|---|---|
| CODE_CLEANUP | NOT_STARTED | work/code_cleanup.md (input d6608b54…) | partial: XR subsystem manifests pruned, PXR/entitlement neutralised (R011); report + dead-SDK decisions pending |
| RESOURCE_CLEANUP | NOT_STARTED | work/resource_cleanup.md | NOT_STARTED; VR-only shaders (Oculus/*, PXR/*, SteamVR/*) and controller models are candidates after HIDDEN_CONTENT |
| PERMISSION_CLEANUP | NOT_STARTED | work/permission_audit.md | implemented: all original permissions removed, runtime R011; consumer analysis report pending |
| OBB_EMBEDDING | NOT_STARTED | work/cache_embedding.md | no OBB in input (KEEP_CONTAINER); report pending |
| HIDDEN_CONTENT | NOT_STARTED | work/hidden_content.md | candidates seen: DebugMenuManager, skip-level secret (PauseMenuController.SkipLevelPressed), GraphicsScreen, dev saves |
| GRAPHICS_UPGRADE | IN_PROGRESS | work/graphics_upgrade.md (to write) | findings: default quality Low (VrPlatform_Oculus.GetDefaultQualityLevel=1), MSAA 4, shadows off in all levels, post FX/AO/volumetrics code stripped |
| TEXTURE_PORTABILITY | NOT_STARTED | work/texture_portability.md | ASTC declared; UnityPy format inventory pending |
| DEVICE_EFFECTS | PASSED | work/device_effects.md (input d6608b54…, shader manifest work/manifests/shader_patches.json) | DEVICE_EFFECTS_VERIFIED on Mali-G76 (R012); Adreno/PowerVR device-limited |
| TOUCH_CONTROLS | IN_PROGRESS | work/touch_controls.md (action map, multi-touch rules, debts) | TOUCH_GAMEPLAY_VERIFIED pending full route |

## Current blocker and experiments
- blocker_id: none; consecutive_no_progress 0/3; candidate_id 57 (p057); diagnostic_runs R001–R012

## Content and cache
- hidden_content_status: NOT_STARTED
- obb_unpack_policy: KEEP_CONTAINER; obb_unpack_instruction: NONE; cache_layout: base-assets as shipped by the original (no external cache exists)

## Acceptance evidence
- R001 original: storage permission → "This SDK can not run on this device" (PICO platform loader) → black
- R002 first flat frame; R003 touch look + menu tap; R004 snap/teleport; R005 lever; R006 inventory key → safe
- R007 HUD + real touch; R008 options rows (FPS limit, FOV) + localisation + Back; R009 tutorial to the open safe by real multi-touch; R010 later mechanics by touch; R011 modern manifest (min 24 / target 37, no permissions, data redirect); R012 device effects on Mali

## Port documentation
- PORT_FEATURES.md: not started

## History pointers
- work/evidence/R001_original_baseline … R012_device_effects (RESULT.md each)
- libroomfix: src/roomfix/*.c (hooks from symbols.spec → gen_symbols.h, preimage + build-id checked); dev console rf_dev.c (roomfix.cmd, only with roomfix.dev present); Java HUD/routing src/java/port/roomfix
