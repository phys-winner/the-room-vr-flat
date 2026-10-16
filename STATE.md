# Game port state — The Room VR: A Dark Matter (PICO build) → flat touch phone

## Current
- state_revision: 9
- updated_at: 2026-10-16T09:10+03:00
- objective: round 2 after ADAPTATION_COMPLETE r6 (user 2026-10-16): R2-1 no camera turning in menus/options; R2-2 every menu item visible at a small FOV; R2-3 no laser on menu taps; R2-4 gyro control, two rows (eye position only / camera tilt+turn only) + sensitivity for each; R2-5 head-following lagging texts ("comments", e.g. the first tutorial boxes) and every similar UI element made flat; R2-6 test on realme-rmx3371; R2-7 on every location check that three objects can be interacted with (no full play-through); R2-4b (user 2026-10-16, later): with EYE POSITION on, moving the phone forward / back moves the camera
- workflow_status: COMPLETE — round 2 (R2-1..R2-7) IMPLEMENTED and verified; accepted final out/room_p094.apk e1b1e398c30604fd7e813833fa0882114d208ea58e83b22d2e0cff9dbbee3729 (realme-rmx3371; previous final room_p084 kept as rollback); ADAPTATION_COMPLETE (device-limited as before: 16 KB pages, reverse landscape, late story places)
- current_step: R025 baseline on realme-rmx3371; R026 round-2 features; R027 crash fixes (weak refs, rf_player, end-of-game screen); R028 three objects per location by touch (Epilogue has two); R029 player mode; R030 5.8 matrix (16:9, 2:1, native 20:9, 4:3), lifecycle, soak (10 min with the gyroscope on, same pid, PSS +1.8 %)
- next_action: none. Optional operator inputs: feel of the gyroscope options with the phone in the hand (shown here with synthetic sensor input only); a 16 KB-page device for the runtime page proof
- blocker: none
- bridge_mode: NOT_APPLICABLE — input is native arm64-v8a only (lib/arm64-v8a: libil2cpp, libunity, libmain, libpxr_api, libPxrPlatform, libpxrplatformloader); no armeabi-v7a code exists
- bridge_input_snapshot: NOT_APPLICABLE (no bridge); ARM32_READY is interpreted as "restoration snapshot verified" for a native arm64 game (workflow.md "Native arm64 runtime")
- content_review_status: IMPLEMENTED for R021 (C1, C3, C4, QA row; C2, C5 rejected); round-2 operator requests R2-1..R2-7 IMPLEMENTED (R026-R030)
- earlier: stage 8 R022, final release room_p084 R023/R024 on redmi-note8pro (device-limited 16 KB pages; reverse landscape; late story places)
- tooling (realme-rmx3371, no root): work/tools/mtinject_ap (app_process MotionEvent injector, dex 1d564df3...), touch_ap.py, loc_check.py (menu, chapter, sweep, interact, tp), loc_final.py (R2-7 recipes), menu_cycle.py (round trips), tap_shot.py; screenrecord cannot write or stream on this Realme; roomfix.dev present on the device for dev runs

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
| Device | All hardware work | User 2026-10-16 | Round 2: test device realme-rmx3371 (all test roles); pixel-10 install only (room_p084, 2026-10-16); redmi-note8pro not used in round 2; never touch pixel-10a (Pixel 10a) or other serials | Only redmi-note8pro (user /goal 2026-10-14) |
| Round-2 scope | Port additions | User 2026-10-16 | R2-1..R2-7 (Current/objective); R2-7 = three interactable objects per location, no full play-through | — |
| Test devices | realme-rmx3371 (round 2), redmi-note8pro (rounds 1); package com.FireproofStudios.TheRoomVRPlus | Skill policy | Root probe/use where present (realme-rmx3371: no su); uninstall/reinstall/clear without backup or repeated approval | — |
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
| round-2 test device | realme-rmx3371 | Realme RMX3371 (RE54E4L1, kona/Snapdragon, Adreno), Android 15 / SDK 35 (build UKQ1.230924.001), abilist arm64-v8a,armeabi-v7a,armeabi; 1080x2400 @480 dpi; touchpanel /dev/input/event2 (8639x19199); no su | 4096 (getconf PAGESIZE) | online |
| install-only (user 2026-10-16) | pixel-10 | Pixel 10 (frankel), Android 17 DP11 / SDK 37, arm64-v8a only, PowerVR DXT-48-1536 | 4096 (getconf PAGESIZE) | room_p084 installed, not launched |
| round-1 test device | redmi-note8pro | Redmi Note 8 Pro (begonia, MT6785, Mali-G76 MC4), Android 11/SDK 30, MIUI 12.5, abilist arm64-v8a,armeabi-v7a,armeabi | 4096 (getconf PAGESIZE) | online; root via Magisk `su -c` |

- display 1080x2340 (landscape 2340x1080), 440 dpi, single mode 60 Hz, cutout top 76 px; the left bar was Unity's own safe-area letterbox (boot.config androidRenderOutsideSafeArea=0) — set to 1 in p060: full 2340x1080 render, HUD inside the safe insets
- final_acceptance (arm64-only + 16 KB runtime): no allowed device has 16 KB pages → open, device-limited (not faked)

## Artifacts
| Role | Source revision/patch | Build inputs | APK/set hash manifest | Evidence |
|---|---|---|---|---|
| accepted final round 2 (device-limited) | git commit of STATE r9 | app/ (libroomfix 4a16e345f16dfefb2a4de9d2b7ceb834268f8dc51c831adc8626bca6231e60fe: menu framing, flat texts + text camera, no menu beam, gyroscope, Options gyroscope page, weak refs, rf_player, end-screen menu context; Java RoomFix sensors, HelpCard gyroscope line) | out/room_p094.apk e1b1e398c30604fd7e813833fa0882114d208ea58e83b22d2e0cff9dbbee3729 (cert a40da80a...f5dc, arm64-v8a only, zipalign -P 16, versionCode 5011, min 24 / target 37) | R026-R030 |
| accepted final round 1 (rollback) | 73a7234 + docs commit of STATE r6 | app/ (libroomfix 2d3354b6c4eb82044a17eaf9202e1b938c2083a9b276cf3faf6de8aa6ef6acd3: stage-8 hooks, gesture-card requests; Java HelpCard; globalgamemanagers hasPublishingRights (C4); 31 patched shaders; sharedassets5 ASTC; boot.config full screen) | out/room_p084.apk 590eb40e4245b2ac6ad08c4e3f206ce7f63188c2a8ba5fbca733e1502798f94e (cert a40da80a...f5dc, ABIs arm64-v8a only, zipalign -P 16, versionCode 5011, min 24 / target 37) | R022, R023, R024 |
| accepted before stage 8 (rollback) | fbb436d | app/ at fbb436d (libroomfix 79b06a7f...) | out/room_p080.apk 1179459217fb8a74e8fcd3802ac3a4da7e48b14ec4fc4eec38e7cb44a4cc7719 | R020 |
| previous candidates (deleted 2026-10-14) | b404304 / d0e54fb / 44a61aa / 73a7234 | app/ | room_p061 6225b002..., room_p055 e34a7231..., room_p081 (C4 only), room_p083 30d7ddc6... (dev build of the p084 sources) — superseded by p084 | R011-R022 |
| installed per serial | redmi-note8pro: room_p084.apk (clean install R023; profile 2 new game in the tutorial from R024); pixel-10: room_p084.apk (2026-10-16 streamed install, on-device base.apk sha256 590eb40e..., not launched); realme-rmx3371: room_p094.apk (on-device hash e1b1e398..., settings back to defaults FOV 90 / gyroscope off, QA level select on, roomfix.dev removed after R030) | 73a7234 | 590eb40e... | R023, R024 |
| ARM32_LASTGOOD and snapshot | room_p080 (native arm64 restoration snapshot) | fbb436d | 1179459... | R020 |
| ARM64_ACCEPTED before additions | room_p080 (rollback point for stage 8) | fbb436d | 1179459... | R020 |
| round-2 storage cleanup | candidates p085-p093 deleted after R028 (18.55 GB logical) | — | work/manifests/cleanup_round2.json (paths, sha256, sizes, reasons) | R026-R028 |

- storage_cleanup: 2026-10-14 removed out/room_p007.apk, room_p057.apk, room_p060.apk; at ADAPTATION_COMPLETE removed room_p055, room_p061, room_p081, room_p083 and old build-attempt logs; kept room_p080 (rollback) and room_p084 (final)

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
| STAGE8_ADDITIONS | IMPLEMENTED | R022: C4 (no trial mark), C1 (chapter list, replay with saving off, profile unchanged, return to menu), QA row (all chapters, SKIP LEVEL without the secret, off by default, persisted), C3 (card on the first main menu and from Options view controls, 10 languages, tap/Back close) | operator choice R021 |
| FINAL_RELEASE_VERIFIED | PASSED | room_p084: R023 update over p083 (settings and save kept), clean install (first start 80.5 s, later 25.3 s, measured without su load), restart; R024 matrix 16:9/2:1/19.5:9/4:3 + 3-button navigation + density/font, lifecycle, 10.3-min soak; crash buffer empty | - |
| PORT_FEATURES_READY | PASSED | PORT_FEATURES.md for room_p084 checked against R022-R024 and work/touch_controls.md (SDK/ABI from the final APK; no OBB; honest limits: 16 KB pages untested, late story places) | - |
| ROUND2_MENU_CAMERA (R2-1) | PASSED | R026: a drag in menus never turns the camera (yaw/pitch unchanged), gyroscope inactive in menus; R030 look swipes in gameplay at every aspect | - |
| ROUND2_MENU_FRAMING (R2-2) | PASSED | R026/R027/R030: every screen's items (and the 3D logo) framed; Options at FOV 70 needs 54.4 deg vertical and gets it; pause view restored; 16:9 / 2:1 / 20:9 / 4:3 | - |
| ROUND2_NO_MENU_LASER (R2-3) | PASSED | R026 A/B: authored beam vs none (FrontEndPointer.SetLength) | - |
| ROUND2_GYROSCOPE (R2-4) | PASSED_DEVICE_LIMITED | R026: page/rows/save, sensors registered only while on and focused (R030 Home: 2 -> 0 -> 2), synthetic turn/tilt/push through the native path; physical motion of a held phone not recorded (device on a desk) | operator feel test |
| ROUND2_FLAT_TEXTS (R2-5) | PASSED | R026/R029/R030: remarks bottom, tutorial boxes top, fixed through swipes, drawn over near geometry (text camera, layer 30), crisp TMP at 0.55 m | - |
| ROUND2_LOCATIONS (R2-7) | PASSED | R028 on p094: three objects held by touch in Tutorial, Egypt, Chapel, Witch, Desert; Epilogue has two grabbable objects, both held | - |
| FINAL_RELEASE_VERIFIED (round 2) | PASSED | room_p094: R029 player mode, R030 matrix + lifecycle + soak on realme-rmx3371; crash buffer empty | - |
| ADAPTATION_COMPLETE | PASSED_DEVICE_LIMITED | stage 8 IMPLEMENTED, final set room_p084 accepted on the only allowed device, PORT_FEATURES_READY; open only what the device restriction prevents (runtime 16 KB pages, reverse landscape) | a 16 KB-page device if allowed |

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
- blocker_id: none; consecutive_no_progress 0/3; candidate_id 94 (p094, accepted final round 2); diagnostic_runs R001-R030
- round-2 candidates: p085 c4abb11f (REJECTED: gyroscope button crash), p086 b2a968bb (REJECTED: TMP blocks at 5 cm), p087 74b70601, p088 666a71b3 (never installed), p089 b988944f, p090 af4d5632 (crash after a replay, R027), p091 6dc3bacf (never installed), p092 9b76de5e (crash during Loading, R027), p093 ec902d62 (end screen stuck, R027), p094 e1b1e398 (final candidate)

## Content and cache
- hidden_content_status: AUTO_PROCESSED (automatic_unlocks H1; deferred H4 H5 H6 H11 H17; H15 done as stage-8 C4; no purchase-gated content; QA unlocks added at the operator's request as an Options row)
- stage8_catalog: work/evidence/R021_phase8_catalog.md; decision C1 C3 C4 + QA unlock (chapters, skip level) with an activation control — IMPLEMENTED (R022); C2 C5 rejected
- obb_unpack_policy: KEEP_CONTAINER; obb_unpack_instruction: NONE; cache_layout: base-assets as shipped by the original (no external cache exists)

## Acceptance evidence
- R001 original: storage permission → "This SDK can not run on this device" (PICO platform loader) → black
- R002 first flat frame; R003 touch look + menu tap; R004 snap/teleport; R005 lever; R006 inventory key → safe
- R007 HUD + real touch; R008 options rows (FPS limit, FOV) + localisation + Back; R009 tutorial to the open safe by real multi-touch; R010 later mechanics by touch; R011 modern manifest (min 24 / target 37, no permissions, data redirect); R012 device effects on Mali; R013 graphics upgrade; R014 hidden content; R015 texture portability; R016 FPS limit
- R017 Egypt mechanics by touch; R018 hub -> Chapel, turn-in-hand; R019 superzoom; R020 release candidate checks; R021 stage-8 catalog
- R022 stage-8 additions; R023 final release room_p084 install/update/cold starts; R024 final matrix, lifecycle, soak
- round 2 (realme-rmx3371): R025 baseline p084 on Adreno; R026 R2-1..R2-5; R027 crash fixes; R028 three objects per location; R029 player mode; R030 matrix/lifecycle/soak

## Port documentation
- PORT_FEATURES.md: final for room_p094 (Russian): round 2 — menus without camera turning and framed at any FOV, no menu beam, flat texts, gyroscope page, end screen by touch, checks on Adreno

## 5.8 matrix and fields (R020, release candidate p080, redmi-note8pro)
- reference_aspect 16:9; target_aspect 19.5:9 (2340x1080); render_rect_px [0,0,2340,1080] (androidRenderOutsideSafeArea=1)
- projection_policy hor+: vertical FOV from a 16:9 reference at the chosen horizontal FOV (default 90 -> fov_y 58.7 deg), horizontal grows with width (101.3 deg at 2340x1080); fov_y_reference 58.7 / fov_y_target 58.7; fov_tolerance 1 %
- ui_reference_resolution: game UI is world-space Unity canvases (unchanged); ui_scaling_policy: Android HUD in dp; ui_aspect_tolerance: n/a (no stretched layers); ui_anchor_policy: HUD anchored to corners inside the display-cutout safe insets; background_policy: none (full-screen render)
- geometry_screenshot_hashes (R020 jpg sha256 prefix): 16x9 e18dba86, 2x1 f0bf0518, 19.5x9 1ea6bb27, 4x3 da8964a5
- input_transform_result PASS (look drags at every aspect/density/font/navigation variant; HUD taps; letterboxed logical displays map through the system input transform)
- lifecycle_result PASS (Home, screen off/on, other app: same pid, HUD/FOV/input/audio restored); a system navigation-mode switch while running restarts the game (activity recreated, Unity ends its process - engine behaviour)
- rows: reference 16:9 PASS; wide 2:1 PASS; native 19.5:9 PASS; narrower 4:3 PASS (hor+ crop of the sides); cutout PASS; 3-button navigation PASS; gesture navigation PASS; density 360/520 + font scale 1.3 PASS; reverse landscape DEVICE_LIMITED (sensorLandscape, device lies flat; a forced wm rotation gives the system compat letterbox); pause/resume + surface PASS; formats/GPU PASS on Mali (R012, R015); touch only PASS
- audio: FMOD -> OpenSL ES AudioPlayer on the FAST output (HAL 256 frames, 48 kHz), track 2048 frames, AudioFlinger track latency 52.4 ms
- three cold starts clean (menu 39/50/40 s, level 84/97/89 s); first start after install 146 s to the menu (one-time compile of ~780 shader programs into Unity's shader cache) — R020 values include the su-polling load of the route helpers; room_p084 without it: first start 80.5 s, later 25.2-25.4 s to the menu (R023), menu 26 s / level 50 s in the R024 matrix
- room_p084 (R024): the matrix rows above repeated on the final set (cold start per size with the gesture card, 3-button navigation, density/font, lifecycle, 10.3-min soak) — all PASS
- locale_config_path app/res/xml/locales_config.xml; locale_config_locales en fr de es it ru ja ko zh-CN zh-TW; locale_config_manifest_attribute android:localeConfig="@xml/locales_config"; locale_picker_result DEVICE_LIMITED (Android 11 has no per-app picker); locale_runtime_result PASS (in-game LANGUAGE selector, R008); locale_fallback_result: system language without a dictionary -> English (game Localisation); locale_hot_swap_result: none while running (the game reads its language at start; a per-app locale change clears the saved language for the next start, rf_loc.c)
- fps_limit_ui_path Pause/Main -> OPTIONS -> FPS LIMIT; fps_limit_storage <getExternalFilesDir>/port_settings.cfg; fps_limit_default=60; fps_limit_selected 60; fps_limit_effective_target Application.targetFrameRate; fps_limit_mode_results 30.0/45.2/60.0/60.0 (R016); fps_limit_lifecycle_result PASS; fps_limit_simulation_result PASS (game time = wall time)

## History pointers
- work/evidence/R001_original_baseline … R012_device_effects (RESULT.md each)
- libroomfix: src/roomfix/*.c (hooks from symbols.spec → gen_symbols.h, preimage + build-id checked); dev console rf_dev.c (roomfix.cmd, only with roomfix.dev present); Java HUD/routing src/java/port/roomfix
