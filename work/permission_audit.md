# PERMISSION_CLEANUP — The Room VR: A Dark Matter (PICO 1.02)

## Inputs
- original APK sha256 d6608b54…e65e (`aapt2 dump permissions`), decoded smali (decoded_original/smali*), IL2CPP
  dump (managed callers), native strings of the PICO libraries, boot.config.
- Final candidate out/room_p061.apk sha256 6225b002… (`aapt2 dump permissions`: package only, no
  uses-permission; badging: minSdk 24, targetSdk 37, features touchscreen + multitouch required, landscape).

## Original permissions → consumers → decision
| Permission | Consumers found | Needed for the flat port? | Decision | Behaviour without it |
|---|---|---|---|---|
| INTERNET | no java.net / HttpURLConnection in smali; no UnityConnectSettings (no Unity Analytics / Cloud Diagnostics) in globalgamemanagers; UnityWebRequest only in SaveManager dev-save loading (jar:file:// local read, no caller) and a PICO SDK sample (DownloadImage); PICO platform services (entitlement, achievements) are online but neutralised (entitlement answered locally, PXR verify-service binding skipped) | no — the game is offline | **removed** | nothing reaches the network; no exception in the routes (R011–R016) |
| ACCESS_NETWORK_STATE | Unity NetworkConnectivity.getActiveNetworkInfo (Application.internetReachability); only caller in managed code: PicoPorting.Platform.Core.Initialize | no | **removed** | Core.Initialize is part of the neutralised PICO platform path; no SecurityException in logcat of any run |
| CHANGE_WIFI_MULTICAST_STATE | no createMulticastLock in smali; PICO SDK leftover (casting/mDNS) | no | **removed** | — |
| WRITE_EXTERNAL_STORAGE | Unity start-up request (boot.config android-force-sdcard-permission=1, Unity permission fragment com.unity3d.player.k), com.psmart.aosoperation.SysActivity (PICO), legacy /sdcard paths | no — saves, PlayerPrefs and caches live in getExternalFilesDir (5.1.1, DataRedirect), which needs no permission since API 19 | **removed**; boot.config android-force-sdcard-permission=0, manifest unityplayer.SkipPermissionsDialog=true, requestLegacyExternalStorage dropped | no start-up permission dialog (R011 clean install); saves written and reloaded (R009–R016) |
| WRITE_SETTINGS | com.psmart.aosoperation.SysActivity (Settings.System.putInt: PICO brightness/system settings) | no | **removed** | PICO system UI only |

## Runtime requests and metadata
- Runtime requests: Unity's permission fragment (requests what boot.config/manifest ask for: nothing now) and the
  PICO SysActivity (never started in flat). No other requestPermissions call sites.
- SDK metadata removed with the VR components: com.oculus.supportedDevices, com.oculus.vr.focusaware,
  com.samsung.android.vr.application.mode, pvr.app.type, enable_entitlementcheck, PICO rendering/display/handtracking
  keys, android.hardware.vr.headtracking feature (R011, work/manifests).
- Cache/data readers: no OBB and no external cache (see cache_embedding.md); the APK's own assets are read through
  AssetManager — no storage permission needed.

## Checks performed
- Clean install (adb uninstall → install) of the permission-free APK: no permission dialog, first start to the
  profile screen, data redirected to Android/data/<pkg>/files, full real-touch tutorial route (R011).
- Later runs on the same build line: cold starts, scene changes, Home/resume, settings persistence (R012–R016) —
  no SecurityException or permission-related error in logcat.

## Result
- Final permission set: **none**. PERMISSION_CLEANUP research PASSED; implementation verified (R011, R016).
