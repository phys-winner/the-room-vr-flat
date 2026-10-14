# Game port state — The Room VR: A Dark Matter (PICO build) → flat touch phone

## Current
- state_revision: 5
- updated_at: 2026-10-14T16:10+03:00
- objective: ADAPTATION_COMPLETE for a VR→flat conversion (user /goal 2026-10-14: «/adapt-arm32-game адаптируй vr игру под плоские сенсорные экраны. работай только с adb устройством redmi-note8pro»)
- workflow_status: IN_PROGRESS (stage 8: implementing C4, C1+QA, C3)
- current_step: research rows all PASSED; touch gates closed with story-placed debts (R017-R019); release candidate out/room_p080.apk verified on redmi-note8pro (R020: update/clean install, three clean cold starts, lifecycle, 5.8 matrix, audio, locale); ARM64_ACCEPTED device-limited (no allowed 16 KB device); stage 8 catalog R021 shown
- next_action: wait for the operator's stage-8 choice (work/evidence/R021_phase8_catalog.md); then implement and verify the chosen IDs (or NONE), finalise PORT_FEATURES.md for the final APK, ADAPTATION_COMPLETE
- blocker: none
- bridge_mode: NOT_APPLICABLE — input is native arm64-v8a only (lib/arm64-v8a: libil2cpp, libunity, libmain, libpxr_api, libPxrPlatform, libpxrplatformloader); no armeabi-v7a code exists
- bridge_input_snapshot: NOT_APPLICABLE (no bridge); ARM32_READY is interpreted as "restoration snapshot verified" for a native arm64 game (workflow.md "Native arm64 runtime")
- content_review_status: SELECTED (operator 2026-10-14 on catalog R021 / room_p080): C1 chapter select, C3 gesture help card, C4 hide the Unity trial mark, plus a QA unlock for chapters/other content with an activation button (operator: for testing; no inactive authored button exists, so a new entry is added); rejected: C2 gamepad, C5 local achievements

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
| accepted (device-limited) | fbb436d | app/ (libroomfix 79b06a7f... player build: diagnostics only with the developer flag file; bounded log; 31 patched shaders; sharedassets5 ASTC; boot.config full screen) | out/room_p080.apk 1179459217fb8a74e8fcd3802ac3a4da7e48b14ec4fc4eec38e7cb44a4cc7719 (cert a40da80a...f5dc, ABIs arm64-v8a only, zipalign -P 16) | R020 |
| previous candidates | b404304 / d0e54fb | app/ | out/room_p061.apk 6225b002... (dev candidate, the old version in the R020 update test), out/room_p055.apk e34a7231... (R011 clean install) | R011-R019 |
| installed per serial | redmi-note8pro: room_p080.apk (clean install R020, profile 2 save in the tutorial) | fbb436d | - | R020 |
| ARM32_LASTGOOD and snapshot | room_p080 (native arm64 restoration snapshot) | fbb436d | 1179459... | R020 |
| ARM64_ACCEPTED before additions | room_p080 (rollback point for stage 8) | fbb436d | 1179459... | R020 |

- storage_cleanup: 2026-10-14 removed out/room_p007.apk, room_p057.apk, room_p060.apk; keep p080 (accepted), p061 (update-test old version) and p055 (R011 baseline) until ADAPTATION_COMPLETE

## Gates
| Phase/subgate | Status | Evidence | Dependency |
|---|---|---|---|
| PHASE0_1_SETUP | PASSED | inputs hashed, decode, app copy, git; portable signer builds | — |
| ORIGINAL_BEHAVIOUR_RECORDED | PASSED | work/evidence/R001_original_baseline/RESULT.md | — |
| VR_RUNTIME_DETACHED | PASSED | R002/R011 no PICO dialog/crash, XR loaders skipped, entitlement local; R020 player build: three clean cold starts, crash buffer empty, hooks 26/26 + calls 89/89 verified each start; long dev sessions R017-R019 (hours) without crash | - |
| FLAT_FRAME_VERIFIED | PASSED | R002 validated frames; R012 FastGlow disc fixed, Mali link errors 30 -> 0; p060 full-screen render (no static bar), HUD in safe insets; R020 aspect rows | - |
| FLAT_CAMERA_VERIFIED | PASSED | R003 look, R008 FOV row, R009 arrival facing/resting hands, R010 pinch lean, p063 head sway; hor+ policy (16:9 reference, default 90 -> 101 deg across at 2340x1080); R020 16:9/2:1/19.5:9/4:3 same vertical span | - |
| VR_ACTIONS_COVERED | PASSED | work/touch_controls.md action map: every row has runtime evidence (R003-R010; R017 Egypt: lift-and-hook bolts, twist-and-pull bar, lever, rope, 14-turn winch, note hole through the eyepiece, box, lens, eyepiece tab, poke panel; R018 hub artefact + letter -> Chapel, turning a held object; R019 superzoom enter/exit) or a story-placed debt: Chapel organ keys ("Organ Holo Press Black Key"), lockpick ("Solve Lockpick Organ/Clock"); Witch pouring ("MixFirstSpell", turn gesture R018 in place), mirror alignment ("RevealTreeWithMirror"), bellows, candles | - |
| TOUCH_GAMEPLAY_VERIFIED | PASSED | cold start -> new game -> tutorial by real multi-touch (R009) -> inventory/hints/eyepiece/menus (R007-R010) -> save -> continue/restart (R009, R020 update + cold starts); chapter mechanics R017-R019; multitouch + cancellation rules in work/touch_controls.md; debts above are story places the route did not reach | - |
| DEVICE_EFFECTS_VERIFIED | PASSED (Mali-G76) | R012; work/device_effects.md | other GPUs device-limited |
| RESTORATION_RESEARCH_COMPLETE | PASSED | nine reviewed reports, all rows PASSED | - |
| DATA_EXTERNAL_ROOT (5.1.1) | PASSED | R011 clean install redirect; R020: update p061 -> p080 kept 0.save; clean install writes 1.save, roomfix.log, port_settings.cfg under getExternalFilesDir | - |
| FPS_LIMIT_VERIFIED | PASSED | R016: SF 30.0/45.2/60.0/60.0 fps, game time = wall time, persistence across cold restart/scene change/Home-resume; default 60 | Hz-change rows device-limited (single-mode panel) |
| ARM32_READY (restoration snapshot) | PASSED | native arm64 game: snapshot = room_p080 (R020 restore = clean install + update + cold starts) | phases 0-6 |
| BRIDGE (phase 7 guest gates) | NOT_APPLICABLE | native arm64 input | - |
| ARM64_ACCEPTED | PASSED_DEVICE_LIMITED | arm64-only APK (lib/arm64-v8a only) running on the allowed device (R020); new ELF libroomfix PT_LOAD align 0x4000; runtime 16384-byte pages cannot be shown: the only allowed device has 4096 (getconf PAGESIZE) - not faked | a 16 KB-page device if the operator allows one |
| PORT_FEATURES_READY | IN_PROGRESS | PORT_FEATURES.md drafted for room_p080 | stage 8 decision, final APK |

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
| TOUCH_CONTROLS | PASSED | work/touch_controls.md (action map with VR/native columns, multi-touch rules, story-placed debts) | applied: grab/slider/rotation/twist/crank, combination parts (cylinder + roll-over), poke probe, turn-in-hand ball, hold+look, pinch lean, head sway, teleport tap (+ forced TeleportTo), superzoom tap, HUD; TOUCH_GAMEPLAY_VERIFIED R009-R020 |

## Current blocker and experiments
- blocker_id: none; consecutive_no_progress 0/3; candidate_id 80 (p080, accepted device-limited); diagnostic_runs R001-R021

## Content and cache
- hidden_content_status: AUTO_PROCESSED (automatic_unlocks H1; deferred H4 H5 H6 H11 H15 H17; no purchase-gated content; no QA dialog needed)
- stage8_catalog: work/evidence/R021_phase8_catalog.md; decision SELECTED C1 C3 C4 + QA unlock (chapters, other content) with an activation button; NONE for C2 C5
- obb_unpack_policy: KEEP_CONTAINER; obb_unpack_instruction: NONE; cache_layout: base-assets as shipped by the original (no external cache exists)

## Acceptance evidence
- R001 original: storage permission → "This SDK can not run on this device" (PICO platform loader) → black
- R002 first flat frame; R003 touch look + menu tap; R004 snap/teleport; R005 lever; R006 inventory key → safe
- R007 HUD + real touch; R008 options rows (FPS limit, FOV) + localisation + Back; R009 tutorial to the open safe by real multi-touch; R010 later mechanics by touch; R011 modern manifest (min 24 / target 37, no permissions, data redirect); R012 device effects on Mali; R013 graphics upgrade; R014 hidden content; R015 texture portability; R016 FPS limit
- R017 Egypt mechanics by touch; R018 hub -> Chapel, turn-in-hand; R019 superzoom; R020 release candidate checks; R021 stage-8 catalog

## Port documentation
- PORT_FEATURES.md: drafted for room_p080 (Russian); finalised after the stage-8 decision

## 5.8 matrix and fields (R020, release candidate p080, redmi-note8pro)
- reference_aspect 16:9; target_aspect 19.5:9 (2340x1080); render_rect_px [0,0,2340,1080] (androidRenderOutsideSafeArea=1)
- projection_policy hor+: vertical FOV from a 16:9 reference at the chosen horizontal FOV (default 90 -> fov_y 58.7 deg), horizontal grows with width (101.3 deg at 2340x1080); fov_y_reference 58.7 / fov_y_target 58.7; fov_tolerance 1 %
- ui_reference_resolution: game UI is world-space Unity canvases (unchanged); ui_scaling_policy: Android HUD in dp; ui_aspect_tolerance: n/a (no stretched layers); ui_anchor_policy: HUD anchored to corners inside the display-cutout safe insets; background_policy: none (full-screen render)
- geometry_screenshot_hashes (R020 jpg sha256 prefix): 16x9 e18dba86, 2x1 f0bf0518, 19.5x9 1ea6bb27, 4x3 da8964a5
- input_transform_result PASS (look drags at every aspect/density/font/navigation variant; HUD taps; letterboxed logical displays map through the system input transform)
- lifecycle_result PASS (Home, screen off/on, other app: same pid, HUD/FOV/input/audio restored); a system navigation-mode switch while running restarts the game (activity recreated, Unity ends its process - engine behaviour)
- rows: reference 16:9 PASS; wide 2:1 PASS; native 19.5:9 PASS; narrower 4:3 PASS (hor+ crop of the sides); cutout PASS; 3-button navigation PASS; gesture navigation PASS; density 360/520 + font scale 1.3 PASS; reverse landscape DEVICE_LIMITED (sensorLandscape, device lies flat; a forced wm rotation gives the system compat letterbox); pause/resume + surface PASS; formats/GPU PASS on Mali (R012, R015); touch only PASS
- audio: FMOD -> OpenSL ES AudioPlayer on the FAST output (HAL 256 frames, 48 kHz), track 2048 frames, AudioFlinger track latency 52.4 ms
- three cold starts clean (menu 39/50/40 s, level 84/97/89 s); first start after install 146 s to the menu (one-time compile of ~780 shader programs into Unity's shader cache)
- locale_config_path app/res/xml/locales_config.xml; locale_config_locales en fr de es it ru ja ko zh-CN zh-TW; locale_config_manifest_attribute android:localeConfig="@xml/locales_config"; locale_picker_result DEVICE_LIMITED (Android 11 has no per-app picker); locale_runtime_result PASS (in-game LANGUAGE selector, R008); locale_fallback_result: system language without a dictionary -> English (game Localisation); locale_hot_swap_result: none while running (the game reads its language at start; a per-app locale change clears the saved language for the next start, rf_loc.c)
- fps_limit_ui_path Pause/Main -> OPTIONS -> FPS LIMIT; fps_limit_storage <getExternalFilesDir>/port_settings.cfg; fps_limit_default=60; fps_limit_selected 60; fps_limit_effective_target Application.targetFrameRate; fps_limit_mode_results 30.0/45.2/60.0/60.0 (R016); fps_limit_lifecycle_result PASS; fps_limit_simulation_result PASS (game time = wall time)

## History pointers
- work/evidence/R001_original_baseline … R012_device_effects (RESULT.md each)
- libroomfix: src/roomfix/*.c (hooks from symbols.spec → gen_symbols.h, preimage + build-id checked); dev console rf_dev.c (roomfix.cmd, only with roomfix.dev present); Java HUD/routing src/java/port/roomfix
