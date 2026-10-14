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
# ---- gameplay player (Player.LeftHand +0x38 / RightHand +0x40: Controller; Controller.Grabber +0x18, Teleporter +0x50) ----
HOOK PLAYER_UPDATE    Player$$Update
HOOK PAUSE_UPDATE     PauseMenuController$$Update
# ---- tutorial / monologue messages (logged; flat wording substituted later) ----
HOOK TUT_MSG          TutorialMessageBox$$DoTutorialMessageBoxWithAlternate
HOOK MSG_BOX          MessageBox$$DoMessageBox|int32_t attachPosition
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
CALL COMP_GO          UnityEngine.Component$$get_gameObject
CALL TR_FORWARD       UnityEngine.Transform$$get_forward
CALL CAM_W2S         UnityEngine.Camera$$WorldToScreenPoint|UnityEngine_Vector3_o position, const MethodInfo
CALL FP_GETBUTTON     FPInput$$GetButton|int32_t id, int32_t playerId
CALL FP_GETAXIS       FPInput$$GetAxis|int32_t id, int32_t playerId
CALL CTRL_ALLOWTELE   Controller$$AllowTeleporting
CALL INPUTCTL_ALLOWED Player.InputControl$$IsAllowed
# ---- interaction (rf_interact.c) ----
HOOK GC_AWAKE         GrabbableCollider$$Awake
HOOK GC_DESTROY       GrabbableCollider$$OnDestroy
CALL PHYS_RAYCASTALL  UnityEngine.Physics$$RaycastAll|UnityEngine_Vector3_o direction, float maxDistance, int32_t layerMask, int32_t queryTriggerInteraction
CALL HIT_COLLIDER     UnityEngine.RaycastHit$$get_collider
CALL COL_ISTRIGGER    UnityEngine.Collider$$get_isTrigger
CALL GC_FOROBJECT     GrabbableCollider$$GetForObject
CALL GC_GRABBABLE     GrabbableCollider$$GetGrabbable
CALL GRAB_CANGRAB     Grabbable$$CanBeGrabbed
CALL CTRL_GRABBED     Controller$$GetGrabbedGrabbable
CALL TR_TRANSDIR      UnityEngine.Transform$$TransformDirection
CALL GO_ACTIVEH       UnityEngine.GameObject$$get_activeInHierarchy
