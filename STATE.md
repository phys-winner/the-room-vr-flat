# Game port state — The Room VR: A Dark Matter (PICO build) → flat touch phone

## Current
- state_revision: 4
- updated_at: 2026-10-14T13:40+03:00
- objective: ADAPTATION_COMPLETE for a VR→flat conversion (user /goal 2026-10-14: «/adapt-arm32-game адаптируй vr игру под плоские сенсорные экраны. работай только с adb устройством redmi-note8pro»)
- workflow_status: IN_PROGRESS
- current_step: eight research reports PASSED (R012–R016 device evidence); FPS limit verified; full-screen render; touch route beyond the tutorial pending (chapter mechanics: Egypt ropes/spinners, Chapel superzoom/ropes/organ keyboard/lockpick, Witch painting/symbol drawing/cauldron/pouring, Desert painting)
- next_action: chapter mechanics by real touch via the authored pause-menu skip secret (work/manifests/scene_mechanics.txt), fix touch gaps, close VR_ACTIONS_COVERED / TOUCH_GAMEPLAY_VERIFIED; then release build (no RF_DEV), lifecycle matrix rows, PORT_FEATURES.md, final checks
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
| FOV | Options screen | Agent 2026-10-14 | option row 70–110 = horizontal FOV of a 16:9 reference, vertical kept (hor+), default 90 | hor at actual aspect, default 100 |

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

- display 1080x2340 (landscape 2340x1080), 440 dpi, single mode 60 Hz, cutout top 76 px; the left bar was Unity's own safe-area letterbox (boot.config androidRenderOutsideSafeArea=0) — set to 1 in p060: full 2340x1080 render, HUD inside the safe insets
- final_acceptance (arm64-only + 16 KB runtime): no allowed device has 16 KB pages → open, device-limited (not faked)

## Artifacts
| Role | Source revision/patch | Build inputs | APK/set hash manifest | Evidence |
|---|---|---|---|---|
| accepted | none yet | | | |
| candidate (dev) | HEAD b404304 | app/ (dev libroomfix effe1fa8… pushed in place; APK carries a2e6650e…; 31 patched shaders; sharedassets5 ASTC; boot.config full screen) | out/room_p061.apk 6225b0026ee8df7dec1ce472682399db7ee8d88485ab5d4fb9dd4d9c97acdb04 | R013–R016 |
| previous candidates | d0e54fb / a01750b | app/ | out/room_p055.apk e34a7231… (R011 clean install), out/room_p057.apk aae8d362… (R012) | R011, R012 |
| installed per serial | redmi-note8pro: room_p061.apk (install -r chain since p055) + dev libroomfix effe1fa8… pushed | b404304 | — | R015, R016 |
| ARM32_LASTGOOD and snapshot | none | | | |
| ARM64_ACCEPTED before additions | none | | | |

- storage_cleanup: 2026-10-14 removed out/room_p007.apk; keep p055 (clean-install baseline), p057, p061 until the release candidate; leaked adb logcat captures killed and route.start now stops previous captures

## Gates
| Phase/subgate | Status | Evidence | Dependency |
|---|---|---|---|
| PHASE0_1_SETUP | PASSED | inputs hashed, decode, app copy, git; portable signer builds | — |
| ORIGINAL_BEHAVIOUR_RECORDED | PASSED | work/evidence/R001_original_baseline/RESULT.md | — |
| VR_RUNTIME_DETACHED | IN_PROGRESS | R002 no PICO dialog/crash; R011 XR subsystem manifests pruned, PXR verify service skipped, entitlement forced, VR meta-data removed | release build (no dev paths) + soak |
| FLAT_FRAME_VERIFIED | PASSED | R002 validated frames; R012 FastGlow disc fixed, Mali link errors 30 → 0; p060 full-screen render (no static bar), HUD in safe insets | — |
| FLAT_CAMERA_VERIFIED | PASSED | R003 look, R008 FOV row, R009 arrival facing/resting hands, R010 pinch lean; hor+ policy (16:9 reference, default 90 → 101° across at 2340x1080) | — |
| VR_ACTIONS_COVERED | IN_PROGRESS | R003–R010: menus, snap/teleport, grab/levers/sliders/knobs/cranks, inventory/receivers, eyepiece, hints, pinch lean, hold+look, Back | superzoom/poke/ropes reached on device or exact debts |
| TOUCH_GAMEPLAY_VERIFIED | IN_PROGRESS | R009 new game → tutorial safe by real touch; R010 later-mechanics samples; R014 skip secret by touch; work/touch_controls.md | chapter mechanics (scene_mechanics.txt) by touch |
| DEVICE_EFFECTS_VERIFIED | PASSED (Mali-G76) | R012; work/device_effects.md | other GPUs device-limited |
| RESTORATION_RESEARCH_COMPLETE | NOT_STARTED | Nine reviewed reports | All research rows PASSED |
| DATA_EXTERNAL_ROOT (5.1.1) | IN_PROGRESS | R011 clean install redirect; R012–R016 update installs (install -r) kept saves, PlayerPrefs (graphics QUALITY/MSAA) and port_settings.cfg | final release run |
| FPS_LIMIT_VERIFIED | PASSED | R016: SF 30.0/45.2/60.0/60.0 fps, game time = wall time, persistence across cold restart/scene change/Home-resume; default 60 | Hz-change rows device-limited (single-mode panel) |
| ARM32_READY (restoration snapshot) | NOT_STARTED | | phases 0–6 |
| BRIDGE (phase 7 guest gates) | NOT_APPLICABLE | native arm64 input | — |
| ARM64_ACCEPTED | NOT_STARTED | | final matrix; 16 KB runtime proof device-limited |
| PORT_FEATURES_READY | NOT_STARTED | PORT_FEATURES.md matched to final APK | ARM64_ACCEPTED and phase 8 decision |

## Required research
| Direction | Research | Report and input revision/hash | Implementation gate, evidence, dependency |
|---|---|---|---|
| CODE_CLEANUP | PASSED | work/code_cleanup.md (input d6608b54…) | applied: XR subsystem manifests, xrsdk pre-init, sdcard request, otacert removed; PICO/Oculus SDK kept inert with neutralised entry points |
| RESOURCE_CLEANUP | PASSED | work/resource_cleanup.md | no dead Android resources; VR-only Unity data (~43 MB textures) kept inside shared serialized files with reasons |
| PERMISSION_CLEANUP | PASSED | work/permission_audit.md | all five removed (consumers traced), runtime R011–R016 |
| OBB_EMBEDDING | PASSED | work/cache_embedding.md | NOT_APPLICABLE implementation: single base APK, no OBB (Unity probe only) |
| HIDDEN_CONTENT | PASSED | work/hidden_content.md | AUTO_PROCESSED: graphics screen exposed; skip secret verified by touch (R014); debug menu/level select/demo/dev saves are stubs |
| GRAPHICS_UPGRADE | PASSED | work/graphics_upgrade.md | MSAA restored (4x), graphics settings screen exposed, idle depth pass removed; default Low+4x by measurement (R013) |
| TEXTURE_PORTABILITY | PASSED | work/texture_portability.md | inventory: ASTC 6x6/4x4 + uncompressed; the one DXT5 Crunched texture → ASTC 4x4 (R015); TEXTURES_RUNTIME_VERIFIED on Mali |
| DEVICE_EFFECTS | PASSED | work/device_effects.md (input d6608b54…, shader manifest work/manifests/shader_patches.json) | DEVICE_EFFECTS_VERIFIED on Mali-G76 (R012); Adreno/PowerVR device-limited |
| TOUCH_CONTROLS | IN_PROGRESS | work/touch_controls.md (action map, multi-touch rules, debts) | TOUCH_GAMEPLAY_VERIFIED pending full route |

## Current blocker and experiments
- blocker_id: none; consecutive_no_progress 0/3; candidate_id 61 (p061); diagnostic_runs R001–R016

## Content and cache
- hidden_content_status: AUTO_PROCESSED (automatic_unlocks H1; deferred H4 H5 H6 H11 H15 H17; no purchase-gated content; no QA dialog needed)
- obb_unpack_policy: KEEP_CONTAINER; obb_unpack_instruction: NONE; cache_layout: base-assets as shipped by the original (no external cache exists)

## Acceptance evidence
- R001 original: storage permission → "This SDK can not run on this device" (PICO platform loader) → black
- R002 first flat frame; R003 touch look + menu tap; R004 snap/teleport; R005 lever; R006 inventory key → safe
- R007 HUD + real touch; R008 options rows (FPS limit, FOV) + localisation + Back; R009 tutorial to the open safe by real multi-touch; R010 later mechanics by touch; R011 modern manifest (min 24 / target 37, no permissions, data redirect); R012 device effects on Mali; R013 graphics upgrade; R014 hidden content; R015 texture portability; R016 FPS limit

## Port documentation
- PORT_FEATURES.md: not started

## History pointers
- work/evidence/R001_original_baseline … R012_device_effects (RESULT.md each)
- libroomfix: src/roomfix/*.c (hooks from symbols.spec → gen_symbols.h, preimage + build-id checked); dev console rf_dev.c (roomfix.cmd, only with roomfix.dev present); Java HUD/routing src/java/port/roomfix
