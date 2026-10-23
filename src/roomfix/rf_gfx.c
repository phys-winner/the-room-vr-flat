// Graphics state of the flat renderer (GRAPHICS_UPGRADE, work/graphics_upgrade.md).
//
// Dev console: `gfx?` prints what the engine really renders with (screen and camera size, render target, MSAA,
// depth texture, quality level, the game's QualitySettingsManager fields); `aa RT MSAA` switches the main camera
// between the window surface and an intermediate target with QualitySettings.antiAliasing = MSAA for A/B tests.
#include <stdio.h>
#include <string.h>

#include "hook.h"
#include "rf.h"

typedef bool (*fn_b_obj)(void*);
typedef int32_t (*fn_i_obj)(void*);
typedef int32_t (*fn_i_void)(void);

static void* icall(const char* name) {
    void* f = g_il2.resolve_icall ? g_il2.resolve_icall(name) : NULL;
    if (!f) rf_log("gfx: icall %s not found", name);
    return f;
}

// QualitySettingsManager is a ScriptableObject singleton (SingletonBase : ScriptableObject): not a scene object, so
// Object.FindObjectsOfType does not list it; Resources.FindObjectsOfTypeAll does.
static void* quality_manager(void) {
    static void* type;
    if (!type) type = rf_class_type(rf_find_class("TheRoomVR.dll", "", "QualitySettingsManager"));
    if (!type) return NULL;
    void* arr = CALLP(C_RES_FINDALL, void* (*)(void*, const void*))(type, NULL);
    if (!arr || *(uint64_t*)((char*)arr + 0x18) == 0) return NULL;
    return ((void**)((char*)arr + 0x20))[0];
}

void rf_dev_gfx(void) {
    void* cam = rf_main_camera();
    int sw = CALLP(C_SCREEN_W, int32_t (*)(const void*))(NULL);
    int sh = CALLP(C_SCREEN_H, int32_t (*)(const void*))(NULL);
    int aa = CALLP(C_QS_GET_AA, int32_t (*)(const void*))(NULL);
    fn_i_void level = (fn_i_void)icall("UnityEngine.QualitySettings::GetQualityLevel");
    rf_log("gfx: screen %dx%d quality level %d antiAliasing %d", sw, sh, level ? level() : -1, aa);
    if (rf_alive(cam)) {
        fn_i_obj pw = (fn_i_obj)icall("UnityEngine.Camera::get_pixelWidth");
        fn_i_obj ph = (fn_i_obj)icall("UnityEngine.Camera::get_pixelHeight");
        fn_b_obj msaa = (fn_b_obj)icall("UnityEngine.Camera::get_allowMSAA");
        fn_b_obj hdr = (fn_b_obj)icall("UnityEngine.Camera::get_allowHDR");
        fn_b_obj frt = (fn_b_obj)icall("UnityEngine.Camera::get_forceIntoRenderTexture");
        void* tt = CALLP(C_CAM_TARGETTEX, void* (*)(void*, const void*))(cam, NULL);
        rf_log("gfx: camera %dx%d targetTexture %p allowMSAA %d allowHDR %d forceIntoRT %d depthTextureMode %d",
               pw ? pw(cam) : -1, ph ? ph(cam) : -1, tt, msaa ? msaa(cam) : -1, hdr ? hdr(cam) : -1,
               frt ? frt(cam) : -1, CALLP(C_CAM_DEPTHMODE, int32_t (*)(void*, const void*))(cam, NULL));
    }
    char* q = quality_manager();
    if (!q) rf_log("gfx: QualitySettingsManager not loaded");
    if (q)
        rf_log("gfx: QualitySettingsManager Level %d MSAA %d RenderScale %.2f x%.2f AO %d Bloom %d Volumetrics %d",
               *(int32_t*)(q + 0x38), *(int32_t*)(q + 0x3C), *(float*)(q + 0x40), *(float*)(q + 0x44), q[0x49],
               q[0x4A], q[0x4B]);
}

// the game's own setters (QualitySettingsManager): level 1 Low / 2 Medium / 3 High, MSAA 0/2/4/8
void rf_dev_quality(int level, int msaa) {
    void* q = quality_manager();
    if (!q) return;
    if (level > 0) CALLP(C_QSM_SETLEVEL, void (*)(void*, int32_t, const void*))(q, level, NULL);
    if (msaa >= 0) CALLP(C_QSM_SETMSAA, void (*)(void*, int32_t, const void*))(q, msaa, NULL);
    rf_log("gfx: quality level %d msaa %d requested", level, msaa);
}

// PICO runtime configuration query. Without the runtime every answer is 0, and PXR_Manager.Awake / PVRManager.Awake
// copy AntiAliasingLevelRecommended into QualitySettings.antiAliasing — that switched the flat frame's MSAA off.
// The flat answer is the game's own MSAA choice (QualitySettingsManager; VrPlatform_Oculus.GetDefaultMSAA = 4 before
// it has started).
#define PXR_CONFIG_AA_RECOMMENDED 25
int32_t h_pxr_config_int(int32_t type, const void* mi) {
    if (type != PXR_CONFIG_AA_RECOMMENDED) return ORIG(H_PXR_CONFIGINT, int32_t (*)(int32_t, const void*))(type, mi);
    char* q = quality_manager();
    int32_t msaa = q ? *(int32_t*)(q + 0x3C) : -1;
    if (msaa < 0) msaa = 4;
    rf_log("gfx: PICO recommended MSAA query -> %d (game setting)", msaa);
    return msaa;
}

// The game's graphics settings screen (environment detail Low/Medium/High, anti aliasing Off/2x/4x/8x, saved in
// PlayerPrefs QUALITY / MSAA) is hidden on PICO: VrPlatform_Oculus.ShouldAllowGraphicsSettings returns false, so the
// Options screen has no GRAPHICS button and VrPlatformManager.Start ignores the saved values. Flat phones get it:
// the one-instruction answer becomes true ("mov w0, wzr" -> "mov w0, #1").
#define INSN_MOV_W0_WZR 0x2A1F03E0u
#define INSN_MOV_W0_1 0x52800020u

void rf_gfx_init(void) {
    if (!g_call_ok[C_OCULUS_ALLOWGFX]) return;
    int r = rf_patch_insn(RVA(g_call_desc[C_OCULUS_ALLOWGFX].rva), INSN_MOV_W0_WZR, INSN_MOV_W0_1);
    rf_log("gfx: graphics settings screen %s", r == 0 ? "enabled" : "patch FAILED");
}

// Rows of the graphics screen whose code does not exist in this build are hidden: texture quality
// (OnTextureQualityChanged is empty), render scale (XR eye-texture scale only), ambient occlusion, bloom and
// volumetric lighting (their HBAO / bloom / Hx drivers were stripped from the PICO build).
static const int DEAD_ROWS[] = {0x60, 0xE8, 0xA0, 0xB8, 0xD0};

void* h_gfx_setup(void* self, const void* mi) {
    void* r = ORIG(H_GFX_SETUP, void* (*)(void*, const void*))(self, mi);
    for (size_t i = 0; i < sizeof DEAD_ROWS / sizeof DEAD_ROWS[0]; i++) {
        void* sel = *(void**)((char*)self + DEAD_ROWS[i]);
        if (!rf_alive(sel)) continue;
        void* row = rf_transform(sel);
        char nm[64];
        rf_obj_name(row, nm, sizeof nm);
        if (!strcmp(nm, "Option")) row = rf_tr_parent(row);
        if (rf_alive(row)) CALLP(C_GO_SETACTIVE, void (*)(void*, bool, const void*))(rf_gameobject(row), false, NULL);
    }
    rf_options_graphics(self);
    return r;
}

// QualitySettingsManager.GetDepthTextureMode asks VrCameraRig for a camera depth texture at High detail or with
// AO / volumetrics. Its consumers (HBAO, Hx volumetrics, soft particles) are not in this build and no shader samples
// the depth texture, so at High it only cost an extra depth pass of the opaque scene (~1.1 Mcycles/frame on
// Mali-G76, R013). The flat port never requests it.
int32_t h_qsm_depthmode(void* self, const void* mi) {
    (void)self;
    (void)mi;
    return 0;  // DepthTextureMode.None
}

static bool g_force_rt;  // dev A/B: main camera through an intermediate (MSAA) target

// QualitySettingsManager.UpdatePostFX runs every frame and turns Camera.forceIntoRenderTexture off (no post FX in
// the PICO build); the dev A/B keeps it on afterwards.
void h_qsm_postfx(void* self, const void* mi) {
    ORIG(H_QSM_POSTFX, void (*)(void*, const void*))(self, mi);
    if (!g_force_rt) return;
    void* cam = rf_main_camera();
    if (rf_alive(cam)) CALLP(C_CAM_SET_FORCERT, void (*)(void*, bool, const void*))(cam, true, NULL);
}

void rf_dev_depthtex(int mode) {
    void* cam = rf_main_camera();
    if (!rf_alive(cam)) return;
    CALLP(C_CAM_SET_DEPTHMODE, void (*)(void*, int32_t, const void*))(cam, mode, NULL);
    rf_log("gfx: depthTextureMode %d", mode);
}

void rf_dev_aa(bool force_rt, int msaa) {
    void* cam = rf_main_camera();
    if (!rf_alive(cam)) return;
    g_force_rt = force_rt;
    CALLP(C_QS_SET_AA, void (*)(int32_t, const void*))(msaa, NULL);
    CALLP(C_CAM_SET_MSAA, void (*)(void*, bool, const void*))(cam, msaa > 1, NULL);
    CALLP(C_CAM_SET_FORCERT, void (*)(void*, bool, const void*))(cam, force_rt, NULL);
    rf_log("gfx: aa forceIntoRT %d antiAliasing %d", force_rt, msaa);
}
