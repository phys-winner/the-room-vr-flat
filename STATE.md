# Game port state — The Room VR: A Dark Matter (PICO build) → flat touch phone

## Current
- state_revision: 2
- updated_at: 2026-10-14T07:40+03:00
- objective: ADAPTATION_COMPLETE for a VR→flat conversion (user /goal 2026-10-14: «/adapt-arm32-game адаптируй vr игру под плоские сенсорные экраны. работай только с adb устройством redmi-note8pro»)
- workflow_status: IN_PROGRESS
- current_step: phase 5 VR→flat core: XR detached, flat frames, synthetic head/hands, touch actions verified through the tutorial safe (R002–R006) with dev-command routes
- next_action: Java HUD overlay (teleport mode, inventory, eyepiece, pause, snap turn) + real-touch route through the tutorial; then FOV/hands-out-of-view/tutorial wording, then release build (no dev hooks) and the nine research reports
- blocker: none
- bridge_mode: NOT_APPLICABLE — input is native arm64-v8a only (lib/arm64-v8a: libil2cpp, libunity, libmain, libpxr_api, libPxrPlatform, libpxrplatformloader); no armeabi-v7a code exists
- bridge_input_snapshot: NOT_APPLICABLE (no bridge); ARM32_READY is interpreted as "restoration snapshot verified" for a native arm64 game (workflow.md "Native arm64 runtime")
- content_review_status: NOT_READY

## Project and inputs
- root: <game> (PROJECT_ROOT <workspace>: nearest parent with tools/android/toolchain.sh; machine_config project_root <other-machine>/... is stale on this machine — tools resolved from E:)
- input: original/original.apk (NTFS hardlink of the user's file in the game root, unmodified), sha256 d6608b54f62f3b2166292655945e8a32e717b18e07b1de010d410a68acfbe65e, 2061253795 B; manifest work/manifests/original.json
- package com.FireproofStudios.TheRoomVRPlus, versionCode 5011, versionName 1.02_PICO_1.0, minSdk 26, targetSdk 29, single base APK, no OBB/splits
- engine: Unity 2019.4.40f1, IL2CPP (metadata v24.5), arm64-v8a only; XR: Unity.XR.PICO (PXR) + PICO.Porting adapters over Oculus.VR; Rewired input; Bolt visual scripting; FMOD Java present (org/fmod) — audio stack to confirm
- data: assets/bin/Data 2.58 GB (3345 entries; 1798 GUID-named serialized files, levels level0..N with .splitN parts, 69 .resource), assets/Videos 19 mp4 (~190 MB); texture compression declared GL_KHR_texture_compression_astc_ldr
- decoded_original/: one apktool 3.0.3 decode (-p work/tool-cache/apktool-framework), manifest work/manifests/decoded_original.json (tree digest e9c5b914775efa19c9dd77ac2cb086463b08abd052f38d0c212ed62dcb30a396)
- app/: independent copy via artifacts.py copy-decoded, compare equal (same digest)
- IL2CPP dump: work/il2cppdumper/ (Il2CppDumper run with DOTNET_ROLL_FORWARD=Major because only .NET 9/10 are installed; not tracked by Git)
- Git: <game>/.git (new, bootstrap marker set, no ancestor repo). Large payloads (original APK, assets/bin, assets/Videos, lib/) are ignored and tracked by SHA-256 manifests

## Decisions and scope
| Decision | Scope | Source/date | Current value | Supersedes |
|---|---|---|---|---|
| Device | All hardware work | User /goal 2026-10-14 | Only adb serial redmi-note8pro; never touch pixel-10a (Pixel 10a) or any other serial | — |
| Test devices | redmi-note8pro, package com.FireproofStudios.TheRoomVRPlus | Skill policy; user 2026-09-15 | Root probe/use; uninstall/reinstall/clear without backup or repeated approval | — |
| Scope | Full adaptation | User 2026-10-14 | VR→flat with touch controls, all nine research rows, phase 8 choice later | — |
| ARM64 transition | — | Input is native arm64 | bridge NOT_APPLICABLE; final 16 KB runtime check cannot run on the only allowed device (4 KB pages) → final gate recorded as device-limited, see Devices | — |
| obb_unpack_policy | Cache | Skill default | KEEP_CONTAINER (no OBB exists; data already inside the base APK) | — |
| Git payload policy | Repo | Agent 2026-10-14 | Binary payloads ignored, hashed in work/manifests; modified binaries are produced by tracked scripts with preimage checks | — |

## Toolchain
- machine_config.json: skill copy (<workspace>/.claude/skills/adapt-arm32-game → C:/disk_d/.../.agents/skills/adapt-arm32-game); java <jdk-24.0.1>/bin/java.exe
- APK tools: <workspace>/tools/android (apktool 3.0.3, smali/baksmali 3.0.9-dev, build-tools 37.0.0 aapt2/zipalign); hashes in tools/android/TOOLCHAIN.md
- NDK 27.2.12479018 (%LOCALAPPDATA%/Android/Sdk/ndk), clang 18.0.3 (r522817c), target aarch64-linux-android24
- signer: <signer-dir>/{apksigner.jar,testkey.pk8,testkey.x509.pem}, expected cert sha256 a40da80a59d170caa950cf15c18c454d47a39b26989d8b640ecd745ba71bf5dc
- adb pinned: %LOCALAPPDATA%/Android/Sdk/platform-tools/adb.exe (37.0.1-15733141)
- local Python 3.13.3 packages: UnityPy 1.25.3, capstone 5.0.9, pyelftools 0.33, texture2ddecoder, astc-encoder-py, etcpak, Pillow, numpy (no downloads needed so far)

## Devices
| Role | Serial | ABI evidence | Runtime page size/source | Status |
|---|---|---|---|---|
| test device (all roles allowed by the user) | redmi-note8pro | Redmi Note 8 Pro (begonia, MT6785, Mali-G76 MC4), Android 11/SDK 30, abilist arm64-v8a,armeabi-v7a,armeabi | 4096 (getconf PAGESIZE) | online; root via Magisk `su -c` (uid 0, context u:r:magisk:s0) |

- display 1080x2340 (landscape 2340x1080), 440 dpi, single mode 60 Hz, cutout top 76 px (portrait coordinates)
- final_acceptance (arm64-only + 16 KB runtime): no allowed device; redmi-note8pro has 4 KB pages and 32-bit ABIs. The user restricted work to this serial, so the 16 KB runtime proof stays an open, device-limited gate (not faked)

## Artifacts
| Role | Source revision/patch | Build inputs | APK/set hash manifest | Evidence |
|---|---|---|---|---|
| accepted | none yet | | | |
| candidate (dev) | HEAD 2ac38fa | app/ + src/roomfix | out/room_p007.apk d6faa837… + libroomfix.so 052754f4… (pushed in place, dev shortcut) | R003–R006 |
| installed per serial | redmi-note8pro: room_p007.apk + lib 052754f4… in /data/app/.../lib/arm64 | HEAD 2ac38fa | — | R006 |
| ARM32_LASTGOOD and snapshot | none | | | |
| ARM64_ACCEPTED before additions | none | | | |

- storage_cleanup: 2026-10-14 start — nothing to remove yet

## Gates
| Phase/subgate | Status | Evidence | Dependency |
|---|---|---|---|
| PHASE0_1_SETUP | PASSED | inputs hashed, decode, app copy, git; build+sign out/room_p001.apk, out/room_p007.apk (portable signer) | — |
| ORIGINAL_BEHAVIOUR_RECORDED | PASSED | work/evidence/R001_original_baseline/RESULT.md | — |
| VR_RUNTIME_DETACHED | IN_PROGRESS | R002: no PICO dialog/crash, Startup→MainMenu→Tut run; XR loader start skipped, store entitlement Success; soak + PICO SDK removal pending | release build |
| FLAT_FRAME_VERIFIED | IN_PROGRESS | R002 validated frames (pid, resumed activity, landscape); open: blue disc/orange wedge, cutout bar, Mali Dissolve shader link errors | VR_RUNTIME_DETACHED |
| FLAT_CAMERA_VERIFIED | IN_PROGRESS | R002/R003 head pose via game TrackedPoseDriver (PoseDataSource), swipe look; open: FOV 90 vertical → hor+ ~65–70 vertical, hands in view | FLAT_FRAME_VERIFIED |
| VR_ACTIONS_COVERED | IN_PROGRESS | R003 menu laser, R004 snap/teleport, R005 lever, R006 slider/inventory/receiver/twist; open: eyepiece, hints tab, superzoom, later levels | FLAT_CAMERA_VERIFIED |
| TOUCH_GAMEPLAY_VERIFIED | NOT_STARTED | | VR_ACTIONS_COVERED |
| RESTORATION_RESEARCH_COMPLETE | NOT_STARTED | Nine reviewed reports | All research rows PASSED |
| DATA_EXTERNAL_ROOT (5.1.1) | NOT_STARTED | | |
| FPS_LIMIT_VERIFIED | NOT_STARTED | Selector, default 60, four measured modes, persistence/lifecycle/simulation | Phase 5 |
| ARM32_READY (restoration snapshot) | NOT_STARTED | | phases 0–6 |
| BRIDGE (phase 7 guest gates) | NOT_APPLICABLE | native arm64 input | — |
| ARM64_ACCEPTED | NOT_STARTED | | final matrix; 16 KB runtime proof device-limited |
| PORT_FEATURES_READY | NOT_STARTED | PORT_FEATURES.md matched to final APK | ARM64_ACCEPTED and phase 8 decision |

## Required research
| Direction | Research | Report and input revision/hash | Implementation gate, evidence, dependency |
|---|---|---|---|
| CODE_CLEANUP | NOT_STARTED | work/code_cleanup.md (input d6608b54…) | NOT_STARTED; phases 4.1–4.3 |
| RESOURCE_CLEANUP | NOT_STARTED | work/resource_cleanup.md | NOT_STARTED; phases 4.4–4.6 |
| PERMISSION_CLEANUP | NOT_STARTED | work/permission_audit.md | NOT_STARTED; phase 5.6 |
| OBB_EMBEDDING | NOT_STARTED | work/cache_embedding.md | NOT_STARTED; CACHE_PREPARED / CACHE_RUNTIME_VERIFIED |
| HIDDEN_CONTENT | NOT_STARTED | work/hidden_content.md | NOT_STARTED; phase 4.0 auto-processing, QA shop/dialog if applicable, phase 8 choice |
| GRAPHICS_UPGRADE | NOT_STARTED | work/graphics_upgrade.md | NOT_STARTED; phase 5.3 |
| TEXTURE_PORTABILITY | NOT_STARTED | work/texture_portability.md | NOT_STARTED; TEXTURES_RUNTIME_VERIFIED |
| DEVICE_EFFECTS | NOT_STARTED | work/device_effects.md | NOT_STARTED; DEVICE_EFFECTS_VERIFIED |
| TOUCH_CONTROLS | IN_PROGRESS | work/touch_controls.md (to write; action map draft in work/vr_inventory.md) | IN_PROGRESS; R003–R006, HUD pending; TOUCH_GAMEPLAY_VERIFIED |

## Current blocker and experiments
- blocker_id: none; consecutive_no_progress 0/3; candidate_id 32 (p032); diagnostic_runs 6 (R001–R006)

## Content and cache
- hidden_content_status: NOT_STARTED
- obb_unpack_policy: KEEP_CONTAINER; obb_unpack_instruction: NONE; cache_layout: base-assets as shipped by the original (no external cache exists)

## Acceptance evidence
- R001: original shows storage permission → "This SDK can not run on this device" (PICO platform loader) → black; no crash
- R002 first flat frame; R003 touch look + menu tap; R004 snap turn + teleport tap; R005 lever; R006 inventory key → safe

## Port documentation
- PORT_FEATURES.md: not started

## History pointers
- work/evidence/R001_original_baseline/RESULT.md … R006_inventory_key/RESULT.md
- libroomfix design: src/roomfix/*.c (hooks from symbols.spec → gen_symbols.h, preimage + build-id checked); dev console rf_dev.c (roomfix.cmd), routes work/tools/route.py
