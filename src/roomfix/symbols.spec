# libil2cpp.so symbols used by libroomfix.so (resolved by work/tools/gen_hooks.py)
# ---- VR runtime detachment (VR_RUNTIME_DETACHED) ----
HOOK XR_INIT_ONLOAD   UnityEngine.XR.Management.XRGeneralSettings$$AttemptInitializeXRSDKOnLoad
HOOK XR_START_SPLASH  UnityEngine.XR.Management.XRGeneralSettings$$AttemptStartXRSDKOnBeforeSplashScreen
HOOK STORE_INIT       StorePlatform_Oculus$$Initialise
FIELD OFF_STORE_RESULT 0x10
# ---- synthetic XR head device (FLAT_CAMERA_VERIFIED) ----
HOOK XR_NODEPOSE     UnityEngine.SpatialTracking.PoseDataSource$$GetNodePoseData
# IL2CPP inlines icall invocations into callers (cached pointer per call site), so XR input is
# intercepted where icalls are resolved by name: il2cpp codegen resolve_icall (runtime code,
# 0x1106ea4, 0x4c bytes, prologue str/stp/add/mov - verified with work/tools/il2dis.py)
HOOK ICALL_RESOLVE    0x1106ea4|0x4c
# ---- per-frame tick on the Unity main thread ----
HOOK VRPM_UPDATE      VrPlatformManager$$Update
# ---- menu pointer (VrInputModule.rayTransform +0x58) ----
HOOK VRIM_PROCESS     UnityEngine.EventSystems.VrInputModule$$Process
# ---- diagnostics: let the shipped logger reach logcat (dev builds only) ----
HOOK LOG_ALLOWED      UnityEngine.Logger$$IsLogTypeAllowed
# ---- Unity API used from native code ----
CALL CAM_MAIN         UnityEngine.Camera$$get_main
CALL CAM_GET_FOV      UnityEngine.Camera$$get_fieldOfView
CALL CAM_SET_FOV      UnityEngine.Camera$$set_fieldOfView
CALL CAM_ASPECT       UnityEngine.Camera$$get_aspect
CALL COMP_TRANSFORM   UnityEngine.Component$$get_transform
CALL TR_POSITION      UnityEngine.Transform$$get_position
CALL TR_ROTATION      UnityEngine.Transform$$get_rotation
CALL TR_LOCALPOS      UnityEngine.Transform$$get_localPosition
CALL TR_PARENT        UnityEngine.Transform$$get_parent
CALL OBJ_NAME         UnityEngine.Object$$get_name
CALL TIME_FRAME       UnityEngine.Time$$get_frameCount
CALL SCENE_ACTIVE     UnityEngine.SceneManagement.SceneManager$$GetActiveScene
CALL SCENE_NAME       UnityEngine.SceneManagement.Scene$$get_name
CALL SCREEN_W         UnityEngine.Screen$$get_width
CALL SCREEN_H         UnityEngine.Screen$$get_height
CALL VRPM_STARTEDUP   VrPlatformManager$$IsStartedUp
CALL CAM_RAY         UnityEngine.Camera$$ScreenPointToRay|UnityEngine_Vector3_o pos, const MethodInfo
