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
CALL INV_ISIN        InventoryManager$$IsItemInInventory
META INVMGR_TYPEINFO InventoryManager_TypeInfo
META INVHUD_TYPEINFO InventoryHud_TypeInfo
CALL RECV_CENTRE     InventoryReceiver$$GetCentre
META INVRECV_TYPEINFO InventoryReceiver_TypeInfo
CALL TR_INVPOINT     UnityEngine.Transform$$InverseTransformPoint
CALL TR_POINT        UnityEngine.Transform$$TransformPoint
# ---- HUD state (rf_hud.c) ----
META HUDMGR_TYPEINFO HudManager_TypeInfo
META GOGGLES_TYPEINFO Goggles_TypeInfo
META GOGGLESHUD_TYPEINFO GogglesHudManager_TypeInfo
# ---- VR setup wizards skipped with flat defaults (rf_menu.c) ----
HOOK POSTURE_SETUP    PostureSelectScreen$$OnSetupScreen
CALL POSTURE_SITTING  PostureSelectScreen$$SittingPressed
CALL CG_ALPHA         UnityEngine.CanvasGroup$$get_alpha
# ---- hierarchy inspection (dev) ----
CALL TR_CHILDCOUNT    UnityEngine.Transform$$get_childCount
CALL TR_GETCHILD      UnityEngine.Transform$$GetChild
CALL GO_ACTIVESELF    UnityEngine.GameObject$$get_activeSelf
CALL GO_SETACTIVE     UnityEngine.GameObject$$SetActive
CALL TR_LOSSYSCALE    UnityEngine.Transform$$get_lossyScale
HOOK MENU_UPDATE      MenuController$$Update
# ---- port settings in the game's Options screen (rf_options.c), localisation (rf_loc.c), FPS limit ----
HOOK OPTIONS_SETUP    OptionsScreen$$OnSetupScreen
HOOK UIOPT_LEFT       UiOptionSelector$$OnLeftPressed
HOOK UIOPT_RIGHT      UiOptionSelector$$OnRightPressed
HOOK UIOPT_UPDATE     UiOptionSelector$$UpdateText
HOOK LOC_GETSTRING    Localisation$$GetString|LocaliseContext_o* context
CALL LOC_GETLANG      Localisation$$GetLanguage
CALL LOCTEXT_SETKEY   LocaliseText$$set_Key
CALL LOCTEXT_REFRESH  LocaliseText$$OnLocalise
CALL OBJ_INSTANTIATE  UnityEngine.Object$$Instantiate|UnityEngine_Transform_o* parent, bool instantiateInWorldSpace
CALL OBJ_SETNAME      UnityEngine.Object$$set_name
CALL TR_SETSIBLING    UnityEngine.Transform$$SetSiblingIndex
CALL TR_GETSIBLING    UnityEngine.Transform$$GetSiblingIndex
CALL TR_FIND          UnityEngine.Transform$$Find
CALL COMP_GETCOMP     UnityEngine.Component$$GetComponent|System_Type_o* type
CALL GO_GETCHILDCOMP  UnityEngine.GameObject$$GetComponentInChildren
CALL APP_SETFPS       UnityEngine.Application$$set_targetFrameRate
CALL APP_GETFPS       UnityEngine.Application$$get_targetFrameRate
CALL MENU_TOSCREEN    MenuController$$TransitionToScreen
CALL GOGGLESHUD_RELEASE GogglesHudManager$$ReleaseGrab
CALL GOGGLESHUD_ON    GogglesHudManager$$ActivateWithFade
CALL GOGGLESHUD_OFF   GogglesHudManager$$DeactivateWithFade
CALL TR_SETPOS        UnityEngine.Transform$$set_position
CALL TR_SETROT        UnityEngine.Transform$$set_rotation
HOOK INV_SHOWXFORM    HudManager$$CalculateShowTransform
CALL RES_FINDALL      UnityEngine.Resources$$FindObjectsOfTypeAll|System_Type_o* type
CALL PAUSE_SKIPLEVEL  PauseMenuController$$SkipLevelPressed
# ---- per-app language (API 33+ picker) through the game's own first-run path ----
HOOK LOC_LOADINITIAL  Localisation$$LoadInitialDictionary
CALL PREFS_SETSTRING  UnityEngine.PlayerPrefs$$SetString
META LOCALISATION_TYPEINFO Localisation_TypeInfo
# ---- PICO platform services neutralised (CODE_CLEANUP) ----
HOOK PXR_BINDVERIFY   Unity.XR.PXR.PXR_Plugin.PlatformSetting$$UPxr_BindVerifyService
# ---- scene inspection (dev) and DEVICE_EFFECTS fixes ----
CALL GO_FIND          UnityEngine.GameObject$$Find
CALL GO_GETCOMPS      UnityEngine.GameObject$$GetComponents|UnityEngine_Component_array* UnityEngine_GameObject__GetComponents
CALL OBJ_FINDALLTYPE  UnityEngine.Object$$FindObjectsOfType|System_Type_o* type, const MethodInfo
CALL REND_MATERIALS   UnityEngine.Renderer$$get_sharedMaterials
CALL MAT_SHADER       UnityEngine.Material$$get_shader
CALL REND_SETENABLED  UnityEngine.Renderer$$set_enabled
CALL REND_GETENABLED  UnityEngine.Renderer$$get_enabled

# graphics diagnostics / upgrade (rf_gfx.c)
CALL CAM_SET_FORCERT  UnityEngine.Camera$$set_forceIntoRenderTexture
CALL CAM_SET_MSAA     UnityEngine.Camera$$set_allowMSAA
CALL CAM_DEPTHMODE    UnityEngine.Camera$$get_depthTextureMode
CALL CAM_TARGETTEX    UnityEngine.Camera$$get_targetTexture
CALL QS_GET_AA        UnityEngine.QualitySettings$$get_antiAliasing
CALL QS_SET_AA        UnityEngine.QualitySettings$$set_antiAliasing
CALL QSM_SETLEVEL     QualitySettingsManager$$SetLevel
CALL QSM_SETMSAA      QualitySettingsManager$$SetMSAA
HOOK QSM_POSTFX       QualitySettingsManager$$UpdatePostFX
HOOK PXR_CONFIGINT    Unity.XR.PXR.PXR_Plugin.System$$UPxr_GetConfigInt|int32_t type
CALL OCULUS_ALLOWGFX  VrPlatform_Oculus$$ShouldAllowGraphicsSettings
HOOK GFX_SETUP        GraphicsScreen$$OnSetupScreen
CALL CAM_SET_DEPTHMODE UnityEngine.Camera$$set_depthTextureMode
HOOK QSM_DEPTHMODE    QualitySettingsManager$$GetDepthTextureMode
