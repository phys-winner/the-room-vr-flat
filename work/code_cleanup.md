# CODE_CLEANUP — The Room VR: A Dark Matter (PICO 1.02) → flat

## Inputs and coverage
- original APK sha256 d6608b54…e65e: AndroidManifest.xml, smali (single dex: com.unity3d 123 files, com.google 180
  (androidgamesdk/Swappy, gson), com.pvr 123, com.psmart 21, com.pxr 3, com.pico 3, com.picovr 1, com.unity 2,
  com.FireproofStudios 6, org.fmod 2, bitter.jnibridge 2, com.example 1), lib/arm64-v8a (libil2cpp 64.5 MB,
  libunity 23.3 MB, libmain, libpxr_api 0.37 MB, libPxrPlatform 0.29 MB, libpxrplatformloader 0.70 MB),
  assets/bin/Data/UnitySubsystems, boot.config, IL2CPP metadata (dump + script.json, xrefs over every bl).
- Runtime: /proc/<pid>/maps of the running port (all seven libraries are mapped), logcat of R002–R016.

## Manifest (reachability roots)
- Original: one activity (UnityPlayerActivity), no services/receivers/providers; VR features and PICO/Oculus/
  Samsung VR meta-data. Port (R011): VR feature/meta-data removed, launcher activity exported, port Application
  (DataRedirect), no permissions; nothing else declared, so the component graph has a single root.

## Candidates and decisions
| Candidate | Live roots / consumers | Decision | Reason / check |
|---|---|---|---|
| Unity XR subsystem manifests (UnitySubsystems/PxrPlatform, OculusXRPlugin) | XR Management loader at start | **removed** from app (work/tools/prune_app.py → work/removed) | flat rendering must not start an XR display; R002 onward |
| boot.config `xrsdk-pre-init-library=PxrPlatform` | Unity pre-initialises the PICO XR SDK before the graphics device | **removed** | VR_RUNTIME_DETACHED (R002) |
| boot.config `android-force-sdcard-permission=1` | Unity start-up permission request | **set to 0** | no permissions (permission_audit.md) |
| PICO / Oculus managed code (PXR_Manager, PicoPorting, OVRManager, Platform.Core, entitlement, MRC, foveation) | scene components on the camera rig; called every frame | keep, **neutralised by hooks**: XR loader start and splash init skipped, PXR verify-service binding skipped, store entitlement answered, AntiAliasingLevelRecommended answered with the game setting (rf_gfx.c), posture wizard answered | code is inside libil2cpp (single binary): removal would need patching every call site |
| PICO Java SDK (com.pvr, com.psmart, com.pxr, com.pico, com.picovr; 151 smali) and native libpxr_api / libPxrPlatform / libpxrplatformloader (1.37 MB, loaded) | P/Invoke from PXR managed code (e.g. UPxr_GetConfigInt is called in PXR_Manager.Awake/Start, PVRManager.Awake), JNI from the native libraries | **keep as inert dependency** | removing them turns live P/Invoke/JNI paths into DllNotFound/ClassNotFound exceptions during scene start; 1.4 MB of a 2.06 GB APK does not justify stubbing dozens of entry points |
| com.google.androidgamesdk (Swappy) | Unity frame pacing (PlayerSettings androidUseSwappy=false) | keep | Unity loads it reflectively; harmless |
| gson | PICO SDK | keep (with the SDK) | — |
| Unity Play Asset Delivery wrapper (refs to com.google.android.play.core) | Unity reflection only; Play Core not bundled | keep | inactive (no asset packs) |
| org.fmod Java helpers | Unity audio output | keep | live |
| NUnit / Unity Test Framework managed code | no player entry point | keep | inside libil2cpp |
| DebugMenuManager / dev saves (SaveManager.LoadDevSave, no caller) / demo front end | compiled-out or caller-less | keep | inside libil2cpp; see hidden_content.md |
| apktool `unknown/META-INF/com/android/otacert` | none (a re-signing tool's certificate, not part of the game) | **removed** from app/ | build metadata (content-cleanup 4.6) |

## ABI / bitness checks (4.3)
- No CPU_ABI / SUPPORTED_ABIS / os.arch / armeabi branches in smali; the input is arm64-v8a only and so is every
  build of the port (`native-code: 'arm64-v8a'`). libroomfix is built for aarch64 with -z max-page-size=16384 and
  uses the runtime page size for code patches (hook.c write_code, sysconf(_SC_PAGESIZE)).

## Result
- CODE_CLEANUP research PASSED: removals applied where a component is dead or VR-only and removable without new
  failure paths; the PICO/Oculus SDKs stay as inert dependencies with every live entry point neutralised and
  verified on device (R002, R011–R016).
