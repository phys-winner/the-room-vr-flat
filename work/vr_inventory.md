# VR inventory — The Room VR: A Dark Matter (PICO 1.02, Unity 2019.4.40f1 IL2CPP)

Inputs: original/original.apk sha256 d6608b54…e65e; libil2cpp.so build-id
689e6add5baa2220aab9a52be8eaf486284038ff; Il2CppDumper output work/il2cppdumper/ (metadata v24.5).
Addresses below are RVAs in that libil2cpp.so. Tools: work/tools/il2dis.py (annotated disassembly),
work/tools/xrefs.py (direct call sites), work/tools/dumpcls.py (class layouts).

## Runtime layers present

| Layer | Where | Role on the headset | Flat decision |
|---|---|---|---|
| Unity XR SDK pre-init | assets/bin/Data/boot.config `xrsdk-pre-init-library=PxrPlatform` | Unity calls libPxrPlatform.so XRSDKPreInit before the graphics device | remove the line (app/ boot.config) |
| XR Management | UnityEngine.XR.Management.XRGeneralSettings.AttemptInitializeXRSDKOnLoad 0x2b6925c, AttemptStartXRSDKOnBeforeSplashScreen 0x2b69644 → XRManagerSettings.InitializeLoaderSync → Unity.XR.PXR.PXR_Loader | creates PICO display/input subsystems | hook both entry points to return (no loader): Unity renders mono to the window |
| PICO native runtime client | lib/arm64-v8a/libpxr_api.so (Pxr_* C API), libPxrPlatform.so (Unity XR provider) | tracking, compositor, layers | not loaded when XR is not initialised; any remaining managed caller is stubbed when observed |
| PICO platform services | libpxrplatformloader.so (ppf_*), PicoPorting.Platform.Core / Entitlements; StorePlatform_Oculus.Initialise 0x151c224 | entitlement, achievements; on a non-PICO device shows "This SDK can not run on this device" (checks ro.pvr.product.name and the com.bytedance.pico.matrix package) | Initialise hooked: EntitlementCheckResult field +0x10 = 1 (Success), Core.AsyncInitialize not called |
| PICO porting input (OVRInput clone) | PicoPorting.PVRInput (Get 0x1617134 / 0x16195b8 / 0x1619aa4, Update 0x1616db4, controller pose getters 0x1617384..0x16179ac) | controller buttons/axes/poses | game reads buttons only through SantaCruzController.OnInputSourceUpdate 0x14b7144 (xrefs) |
| Head pose | PicoPorting.PVRCameraRig.UpdateAnchors 0x170d6b8: InputDevices.GetDeviceAtXRNode(2/0/1) + InputDevice.TryGetFeatureValue(centerEye/leftEye/rightEye Position 0x2b1dee4, Rotation 0x2b1dfec) → anchor localPosition/localRotation | HMD pose | feed a synthetic head device through these XR InputDevice calls (runtime-API seam; the camera pipeline stays native) |
| Hand poses | VrPlatform_Oculus.GetTrackedNodes 0x13943b8 → GetControllerState(PVRInput.Controller) 0x1394250 (struct VrPlatform.TrackedNodeState, 0x60 bytes: uniqueID@0, nodeType@8, tracked@0xC, position@0x10, rotation@0x1C, velocity@0x2C, angVel@0x38, acc@0x44, angAcc@0x50) → VrPlatformManager Left/RightHandNode (+0x60/+0xC0) → TrackedControllerInterface.LateUpdate 0x1527858 sets controller localPosition/localRotation (parent = tracking space) | Touch controller poses | hook GetControllerState: synthetic poses relative to the tracking space |
| Buttons / axes | SantaCruzController.OnInputSourceUpdate 0x14b7144 → Rewired.CustomController.SetAxisValueById 0x1c79e1c / SetButtonValueById 0x1c7a320 (Oculus_Touch_Left/Right: axis 0 stick X, 1 stick Y, 2 trigger, 3 grip; buttons 10 X/A, 11 Y/B, 12 menu) → Rewired actions (RwAction: 0/1 grab, 2/3 grip, 11/13 teleport, 12/21 inventory, 22 pause, 28 goggles, 25/26 menu select, 27 unpause, 15–20/29/30 debug menu) | controller buttons | replace OnInputSourceUpdate: write touch-synthesised element values; Rewired computes press/release edges natively |
| Focus | PVRManager.get_hasInputFocus 0x161fbc4 returns constant true in the PICO port; VrPlatform_Oculus.Update → GameStateManager.TryPauseGameForOsReasons | VR focus pause | emulate from window focus later (pause menu on focus loss) |
| HMD presence | PVRManager.get_isHmdPresent 0x161df94 (XR head device isValid + userPresence) | proximity sensor | answered by the synthetic head device |

## VR-specific features found so far (to map in work/touch_controls.md)
- Locomotion: teleport between authored PlayerLocation points (Player.TeleportTo 0x13ae658, TeleportPointer on each Controller), snap turn (RwAction 6/7).
- Hands: Controller (Grabber, GrabProbe/PokeProbe collision probes, Teleporter), Grabbable family (Rotation/Slider/Rope/Combination/InventoryItem/Button), poke buttons via fingertip.
- Inventory arc on a hand (InventoryManager/InventoryArc, RwAction inventory), InventoryReceiver targets.
- Goggles (eyepiece) toggle (RwAction 28), Superzoom (miniature worlds).
- Menus: world-space canvases driven by VrInputModule/VrRaycaster with a controller laser (FrontEndPointer, ControllerLineRenderer).
- Comfort/setup: PostureSelectScreen, StandingHeightScreen, ResetViewScreen, SittingStandingToggle, Recenter, snap turn.
- Haptics (ControllerHaptics, AudioEventHaptics) — no phone equivalent needed beyond optional vibration.

## Open questions
- Which camera transform the game renders from in flat mode (PVRCameraRig centre-eye anchor vs. a TrackedPoseDriver added in VrPlatform_Oculus.Start).
- Whether PXR managed calls outside the loader (PVRManager Awake/Update, PVRDisplay) reach libpxr_api without an initialised runtime.
