// Flat texts (round 2, operator request 2026-10-16: "comments move in the air with a lag after the camera turns —
// find every such UI element and make it flat").
//
// The game's message boxes - the hero's remarks (TUTORIAL_INTRO "It's getting dark..." right at the start,
// TUTORIAL_FOUND_KEY, EGYPT_INTRO ...), the tutorial instruction boxes and the loading-scene notes (MessageBox,
// TutorialMessageBox) - and the head-inside-geometry warning (HeadInsideGeometryHUD) are world-space canvases in front
// of the VR player. Head-attached ones follow the view lazily: StayInView keeps the box where it is until the view
// turns more than StayInViewFOV (20 degrees) away, then glides after it (StayInViewLerp 0.15 per frame); unattached
// ones hang where they spawned. On a flat screen both read as text drifting through the scene after every swipe.
//
// Every such element is pinned flat to the screen instead:
//  - StayInView.Update - the lazy follow, the class has no other job - is replaced; every element is placed parallel
//    to the image plane in front of the eye each time the camera's pose is read (rf_xr.c: TrackedPoseDriver ->
//    GetNodePoseData, in Update and before rendering), so text and view move in the same frame;
//  - it stays at the authored distance (0.55 m: TextMesh Pro's SDF shaders lose their edge maths in FP16 far closer
//    to the eye - the glyph quads turn into solid blocks) but is drawn by its own camera after the scene (child of
//    the main camera, same pose and FOV, depth cleared, only layer 30 - unnamed in the game), so scenery between the
//    eye and the text never cuts it; the main camera skips that layer;
//  - its size on screen stays what it was at the default field of view (90 across 16:9) whatever FOV is chosen or a
//    menu needs; tutorial boxes take the top of the screen, remarks the bottom (two boxes at once never overlap, the
//    puzzle in the middle stays free), the geometry warning the centre;
//  - while the pause menu is open the texts are hidden (they would cover its panel).
#include <string.h>

#include "rf.h"

typedef struct { float x, y, w, h; } rf_rect;
typedef struct { float r, g, b, a; } rf_color;

#define NFLAT 16
#define FLAT_LAYER 30
#define REF_TAN_HALF_V 0.56261f  // tan(58.72 / 2): vertical half FOV of the default option (90 across 16:9)
#define REF_DIST 0.55f           // the authored distance of the boxes in front of the VR player
#define SIZE_K 1.15f             // a little larger than in the headset: phone screens are small
#define MARGIN 0.06f             // gap to the screen edge, fraction of the half height
#define LAYER_UI 5
#define LAYER_DEFAULT 0

static struct flat {
    rf_ref tr;     // pinned transform (the element's root; weak: elements die with their scene)
    bool used;
    v3 scale0;     // authored local scale
    float cx, cy;  // centre of the visible content in the element's local units
    float hx, hy;  // half size of the visible content
    int slot;
    int measured;  // frame of the last measurement (0: never)
} g_flat[NFLAT];

static void* g_t_graphic;    // System.Type of UnityEngine.UI.Graphic
static void* g_t_transform;  // System.Type of UnityEngine.Transform
static rf_ref g_hudcam;       // camera drawing the flat texts
static rf_ref g_hudcam_main;  // main camera it belongs to

static void* core_type(const char* name) {
    return rf_class_type(rf_find_class("UnityEngine.CoreModule.dll", "UnityEngine", name));
}

// every GameObject of the element on the flat layer (the game's UI / Default layer only: a box the game hides on
// another layer stays hidden)
static void flat_layer(void* tr) {
    if (!g_t_transform) g_t_transform = core_type("Transform");
    void* go = rf_gameobject(tr);
    if (!g_t_transform || !rf_alive(go)) return;
    char* arr = CALLP(C_GO_GETCOMPSCHILD, char* (*)(void*, void*, bool, const void*))(go, g_t_transform, true, NULL);
    int n = arr ? (int)*(uint64_t*)(arr + 0x18) : 0;
    for (int i = 0; i < n; i++) {
        void* t = ((void**)(arr + 0x20))[i];
        void* g = rf_alive(t) ? rf_gameobject(t) : NULL;
        if (!rf_alive(g)) continue;
        int l = CALLP(C_GO_GETLAYER, int32_t (*)(void*, const void*))(g, NULL);
        if (l == LAYER_UI || l == LAYER_DEFAULT) CALLP(C_GO_SETLAYER, void (*)(void*, int32_t, const void*))(g, FLAT_LAYER, NULL);
    }
}

void rf_flatui_add(void* comp, int slot) {
    if (!rf_alive(comp)) return;
    void* t = rf_transform(comp);
    if (!rf_alive(t)) return;
    struct flat* free_slot = NULL;
    for (int i = 0; i < NFLAT; i++) {
        struct flat* f = &g_flat[i];
        void* ft = f->used ? rf_ref_get(&f->tr) : NULL;
        if (ft == t && rf_alive(ft)) {
            if (slot != RF_FLAT_KEEP && slot != f->slot) f->slot = slot;
            f->measured = 0;  // new text: measure again
            return;
        }
        if (!free_slot && !rf_alive(ft)) free_slot = f;
    }
    if (!free_slot) return;
    rf_ref_clear(&free_slot->tr);
    memset(free_slot, 0, sizeof *free_slot);
    rf_ref_set(&free_slot->tr, t);
    free_slot->used = true;
    free_slot->scale0 = CALLP(C_TR_LOCALSCALE, v3 (*)(void*, const void*))(t, NULL);
    free_slot->slot = slot == RF_FLAT_KEEP ? RF_FLAT_CENTRE : slot;
    flat_layer(t);
    char nm[64];
    rf_obj_name(t, nm, sizeof nm);
    rf_log("flatui: '%s' (%s) pinned to the screen, %s, scale %.4f", nm, rf_class_name(comp),
           free_slot->slot == RF_FLAT_TOP ? "top" : free_slot->slot == RF_FLAT_BOTTOM ? "bottom" : "centre",
           free_slot->scale0.x);
}

// Visible content of the element (transform t) in its own local units: every active Graphic's rect.
static void measure(struct flat* f, void* t) {
    f->measured = g_frame;
    flat_layer(t);  // children the game or TextMesh Pro added since
    if (!g_t_graphic) g_t_graphic = rf_class_type(rf_find_class("UnityEngine.UI.dll", "UnityEngine.UI", "Graphic"));
    void* go = rf_gameobject(t);
    if (!g_t_graphic || !rf_alive(go)) return;
    char* arr = CALLP(C_GO_GETCOMPSCHILD, char* (*)(void*, void*, bool, const void*))(go, g_t_graphic, false, NULL);
    int n = arr ? (int)*(uint64_t*)(arr + 0x18) : 0;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        void* g = ((void**)(arr + 0x20))[i];
        if (!rf_alive(g) || !CALLP(C_BEH_ACTIVEENABLED, bool (*)(void*, const void*))(g, NULL)) continue;
        rf_color c = CALLP(C_GRAPHIC_COLOR, rf_color (*)(void*, const void*))(g, NULL);
        if (c.a < 0.01f) continue;
        void* gt = rf_transform(g);
        rf_rect r = CALLP(C_RT_GETRECT, rf_rect (*)(void*, const void*))(gt, NULL);
        for (int k = 0; k < 4; k++) {
            v3 lp = {r.x + ((k & 1) ? r.w : 0), r.y + ((k & 2) ? r.h : 0), 0};
            v3 w = CALLP(C_TR_POINT, v3 (*)(void*, v3, const void*))(gt, lp, NULL);
            v3 e = CALLP(C_TR_INVPOINT, v3 (*)(void*, v3, const void*))(t, w, NULL);
            if (e.x < x0) x0 = e.x;
            if (e.x > x1) x1 = e.x;
            if (e.y < y0) y0 = e.y;
            if (e.y > y1) y1 = e.y;
        }
    }
    if (x1 < x0) return;  // nothing visible yet
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
    if (g_dev && (fabsf(cx - f->cx) > 1 || fabsf(cy - f->cy) > 1 || fabsf(hx - f->hx) > 1 || fabsf(hy - f->hy) > 1))
        rf_log("flatui: content centre %.0f %.0f half %.0f x %.0f (local units)", cx, cy, hx, hy);
    f->cx = cx;
    f->cy = cy;
    f->hx = hx;
    f->hy = hy;
}

// The camera pose this frame will be rendered with: the rig (camera parent, tracking space) and the synthetic head,
// exactly what the TrackedPoseDriver writes into the camera's local transform.
static bool camera_pose(v3* p, quat* q, float* tv, float* aspect) {
    void* cam = rf_main_camera();
    if (!cam) return false;
    void* ct = rf_transform(cam);
    void* rig = rf_tr_parent(ct);
    if (rf_alive(rig)) {
        *q = q_mul(rf_tr_rot(rig), rf_head_rot());
        *p = CALLP(C_TR_POINT, v3 (*)(void*, v3, const void*))(rig, g_head.pos, NULL);
    } else {
        *q = rf_tr_rot(ct);
        *p = rf_tr_pos(ct);
    }
    *tv = tanf(CALLP(C_CAM_GET_FOV, float (*)(void*, const void*))(cam, NULL) * 0.5f * DEG2RAD);
    *aspect = CALLP(C_CAM_ASPECT, float (*)(void*, const void*))(cam, NULL);
    return *tv > 0.01f && *aspect > 0.1f;
}

static void pin(struct flat* f, void* t, v3 cp, quat cq, float tv) {
    float d = REF_DIST;
    float k = (tv / REF_TAN_HALF_V) * SIZE_K;
    v3 sc = v3_scale(f->scale0, k);
    void* parent = rf_tr_parent(t);
    v3 ps = rf_alive(parent) ? CALLP(C_TR_LOSSYSCALE, v3 (*)(void*, const void*))(parent, NULL) : (v3){1, 1, 1};
    float wx = sc.x * ps.x, wy = sc.y * ps.y;  // metres per local unit
    float half_h = d * tv;                     // half height of the view at distance d
    float nh = f->hy * wy / half_h;            // content half height, fraction of the half height
    float y = 0;
    if (nh < 1.0f - MARGIN) {
        if (f->slot == RF_FLAT_TOP) y = 1.0f - MARGIN - nh;
        else if (f->slot == RF_FLAT_BOTTOM) y = -(1.0f - MARGIN - nh);
    }
    v3 centre = v3_add(cp, q_rot(cq, (v3){0, y * half_h, d}));
    v3 pos = v3_sub(centre, q_rot(cq, (v3){f->cx * wx, f->cy * wy, 0}));
    CALLP(C_TR_SETLOCALSCALE, void (*)(void*, v3, const void*))(t, sc, NULL);
    CALLP(C_TR_SETROT, void (*)(void*, quat, const void*))(t, cq, NULL);
    CALLP(C_TR_SETPOS, void (*)(void*, v3, const void*))(t, pos, NULL);
}

// The text camera: created as a child of each scene's main camera, depth-only clear after it, layer 30 only.
static void* make_hud_camera(void* main_cam) {
    void* go_k = rf_find_class("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
    void* cam_t = core_type("Camera");
    if (!go_k || !cam_t || !g_il2.object_new || !g_il2.string_new) return NULL;
    void* go = g_il2.object_new(go_k);
    if (!go) return NULL;
    CALLP(C_GO_CTOR_NAME, void (*)(void*, void*, const void*))(go, g_il2.string_new("RoomFix Flat Text Camera"), NULL);
    void* cam = CALLP(C_GO_ADDCOMP, void* (*)(void*, void*, const void*))(go, cam_t, NULL);
    if (!rf_alive(cam)) return NULL;
    void* t = rf_transform(cam);
    CALLP(C_TR_SETPARENT2, void (*)(void*, void*, bool, const void*))(t, rf_transform(main_cam), false, NULL);
    CALLP(C_TR_SETLOCALPOS, void (*)(void*, v3, const void*))(t, (v3){0, 0, 0}, NULL);
    CALLP(C_TR_SETLOCALROT, void (*)(void*, quat, const void*))(t, (quat){0, 0, 0, 1}, NULL);
    CALLP(C_CAM_SET_CLEARFLAGS, void (*)(void*, int32_t, const void*))(cam, 3, NULL);  // CameraClearFlags.Depth
    CALLP(C_CAM_SET_CULLING, void (*)(void*, int32_t, const void*))(cam, 1 << FLAT_LAYER, NULL);
    float depth = CALLP(C_CAM_GET_DEPTH, float (*)(void*, const void*))(main_cam, NULL);
    CALLP(C_CAM_SET_DEPTH, void (*)(void*, float, const void*))(cam, depth + 1.0f, NULL);
    CALLP(C_CAM_SET_NEAR, void (*)(void*, float, const void*))(cam, 0.05f, NULL);
    CALLP(C_CAM_SET_FAR, void (*)(void*, float, const void*))(cam, 5.0f, NULL);
    CALLP(C_CAM_SET_MSAA, void (*)(void*, bool, const void*))(cam, true, NULL);
    rf_log("flatui: text camera made under the main camera (depth %.1f, layer %d)", depth + 1.0f, FLAT_LAYER);
    return cam;
}

static void tick_hud_camera(bool any) {
    void* main_cam = rf_main_camera();
    if (!main_cam) return;
    int mask = CALLP(C_CAM_GET_CULLING, int32_t (*)(void*, const void*))(main_cam, NULL);
    if (mask & (1 << FLAT_LAYER))  // the scene camera never draws the flat layer (the game resets masks at times)
        CALLP(C_CAM_SET_CULLING, void (*)(void*, int32_t, const void*))(main_cam, mask & ~(1 << FLAT_LAYER), NULL);
    if (!any) return;
    void* hc = rf_ref_get(&g_hudcam);
    if (!rf_alive(hc) || rf_ref_get(&g_hudcam_main) != main_cam) {
        hc = make_hud_camera(main_cam);
        rf_ref_set(&g_hudcam, hc);
        rf_ref_set(&g_hudcam_main, main_cam);
    }
    if (!rf_alive(hc)) return;
    bool paused = g_frame - g_pause_frame <= 2;
    CALLP(C_BEH_SETENABLED, void (*)(void*, bool, const void*))(hc, !paused, NULL);
    CALLP(C_CAM_SET_FOV, void (*)(void*, float, const void*))(
        hc, CALLP(C_CAM_GET_FOV, float (*)(void*, const void*))(main_cam, NULL), NULL);
}

void rf_flatui_pin_all(bool tick) {
    v3 cp;
    quat cq;
    float tv, aspect;
    bool have = false, any = false;
    for (int i = 0; i < NFLAT; i++) {
        struct flat* f = &g_flat[i];
        if (!f->used) continue;
        void* t = rf_ref_get(&f->tr);
        if (!rf_alive(t)) {
            rf_ref_clear(&f->tr);
            f->used = false;
            continue;
        }
        any = true;
        void* go = rf_gameobject(t);
        if (!rf_alive(go) || !CALLP(C_GO_ACTIVEH, bool (*)(void*, const void*))(go, NULL)) continue;
        if (tick && (!f->measured || g_frame - f->measured > 30)) measure(f, t);
        if (!have && !(have = camera_pose(&cp, &cq, &tv, &aspect))) break;
        pin(f, t, cp, cq, tv);
    }
    if (tick) {
        tick_hud_camera(any);
    } else {  // pose callback: the FOV the scene renders with now
        void* hc = rf_ref_get(&g_hudcam), *mc = rf_ref_get(&g_hudcam_main);
        if (rf_alive(hc) && rf_alive(mc))
            CALLP(C_CAM_SET_FOV, void (*)(void*, float, const void*))(
                hc, CALLP(C_CAM_GET_FOV, float (*)(void*, const void*))(mc, NULL), NULL);
    }
}

// StayInView.Update replaced: no lazy follow; the element is pinned flat instead (see the top of this file).
void h_siv_update(void* self, const void* mi) {
    (void)mi;
    rf_flatui_add(self, RF_FLAT_KEEP);
}

// Menu laser: FrontEndPointer.SetLength sizes the two line renderers of the pointer beam and is the only place that
// turns them on (CreateLineRenderer makes them disabled, OnDisable turns them off). Touch clicks the menu directly
// under the finger (rf_interact.c aims the pointer ray through it), so the beam is never drawn, in any build: the
// authored body is not kept.
static struct { int calls, found_on, frames_on; } g_beam;  // dev check of the hidden beam (R031)

void h_fep_setlength(void* self, float len, const void* mi) {
    (void)len; (void)mi;
    g_beam.calls++;
    for (int off = 0x58; off <= 0x60; off += 8) {  // LineRend_Opaque, LineRend_Transparent
        void* lr = *(void**)((char*)self + off);
        if (rf_alive(lr) && CALLP(C_REND_GETENABLED, bool (*)(void*, const void*))(lr, NULL)) {
            g_beam.found_on++;
            CALLP(C_REND_SETENABLED, void (*)(void*, bool, const void*))(lr, false, NULL);
        }
    }
}

// dev: frames in which a beam renderer of the open menu's pointers (MenuController.RightHand / LeftHand) is enabled
// at the frame tick; with found_on it shows whether anything besides SetLength ever turns the beam on
void rf_dev_beam_watch(void) {
    if (!g_dev) return;
    void* mc = rf_menu_controller();
    if (!mc) return;
    for (int h = 0; h < 2; h++) {
        void* fep = *(void**)((char*)mc + (h ? 0x28 : 0x20));
        if (!rf_alive(fep)) continue;
        for (int off = 0x58; off <= 0x60; off += 8) {
            void* lr = *(void**)((char*)fep + off);
            if (rf_alive(lr) && CALLP(C_REND_GETENABLED, bool (*)(void*, const void*))(lr, NULL)) {
                g_beam.frames_on++;
                return;
            }
        }
    }
}

void rf_dev_beam_report(void) {
    rf_log("beam: SetLength calls %d, renderers found enabled there %d, frames with an enabled beam renderer %d",
           g_beam.calls, g_beam.found_on, g_beam.frames_on);
}
