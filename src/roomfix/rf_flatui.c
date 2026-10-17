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
//  - tutorial boxes take the top of the screen, remarks the bottom (two boxes at once never overlap, the puzzle in the
//    middle stays free), the geometry warning the centre;
//  - while the pause menu is open the texts are hidden (they would cover its panel).
//
// Size (round 4, operator 2026-10-16: "the hero's remarks can look very small; on the tablet a message runs off the
// screen"): boxes of one kind are authored at very different sizes (remark boxes at local scale 0.0004 - 0.001,
// tutorial boxes 0.9 - 2.2), so a box with text is sized by its own main text: the letters' em is a set physical size
// on the screen (remarks REMARK_EM_MM, tutorial instructions TUTOR_EM_MM, from the display's density), kept between a
// minimum and maximum share of the view height for very dense or very large screens. Whatever the aspect ratio and FOV,
// a box then fits the screen width (WIDTH_MAX) and its half of the height (HEIGHT_MAX); a box without text keeps its
// authored size at the default field of view (90 across 16:9), within the same limits.
//
// Several tutorial boxes at once (round 4): the game shows some steps with two boxes that a VR player sees at different
// places in the room - "turn to the desk" with "teleport to the desk", "turn to face the doorway" with "teleport to
// location" - and on the flat screen both took the top and were drawn over each other. The boxes shown at the same
// time are stacked down from the top in the order they showed their messages (reading order): each one's text below
// the text of the one above (their soft backgrounds may overlap), smaller when it would reach past STACK_FLOOR; when one
// leaves, the others glide up into its place.
#include <string.h>

#include "rf.h"

typedef struct { float x, y, w, h; } rf_rect;
typedef struct { float r, g, b, a; } rf_color;

#define NFLAT 16
#define FLAT_LAYER 30
#define REF_TAN_HALF_V 0.56261f  // tan(58.72 / 2): vertical half FOV of the default option (90 across 16:9)
#define REF_DIST 0.55f           // the authored distance of the boxes in front of the VR player
#define SIZE_K 1.15f             // boxes without text: a little larger than in the headset
#define MARGIN 0.06f             // gap to the screen edge, fraction of the half height
#define REMARK_EM_MM 3.4f        // em of a remark's letters on the screen, millimetres
#define TUTOR_EM_MM 2.8f         // em of a tutorial instruction
#define EM_MIN 0.036f            // ... at least this share of the view height (very dense screens)
#define EM_MAX 0.058f            // ... at most this share (large tablets keep the authored proportions)
#define WIDTH_MAX 0.92f          // a box never runs wider than this share of the screen
#define HEIGHT_MAX 0.42f         // and never higher than this share (top and bottom boxes stay apart)
#define STACK_GAP 0.08f          // between the texts of stacked top boxes, fraction of the half height
#define STACK_FLOOR -0.25f       // stacked top boxes end above this height (fraction of the half height from the middle)
#define LAYER_UI 5
#define LAYER_DEFAULT 0

static struct flat {
    rf_ref tr;     // pinned transform (the element's root; weak: elements die with their scene)
    rf_ref text;   // main text (TMP_Text) the size follows, when the element is a message box
    bool used;
    v3 scale0;     // authored local scale
    float cx, cy;  // centre of the visible content in the element's local units
    float hx, hy;  // half size of the visible content
    float ty0, ty1;  // its text from the bottom to the top (local units; the soft background reaches well beyond)
    float em;      // main text: em in world metres per unit of the root's local scale (0: no text)
    float em_mm;   // wanted em on the screen, millimetres
    int slot;
    int measured;  // frame of the last measurement (0: never)
    rf_ref box;    // the MessageBox (its state tells whether it is on the screen), when the element is one
    int seq;       // order in which the boxes last showed a message (stacking)
    float y;       // top slot: centre height in fractions of the half height, gliding to its place in the stack
    bool placed;   // y holds a place
} g_flat[NFLAT];
static int g_flat_seq;

volatile float g_disp_ydpi;  // display density (Java DisplayMetrics) and the game view's height in pixels
volatile int g_disp_h;

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

// The main text of a message box (TMP_Text): a tutorial box's instruction, otherwise MessageBox.MainText.
static void* box_text(void* comp, float* em_mm) {
    if (!rf_class_is(comp, "MessageBox")) return NULL;
    bool tutor = rf_class_is(comp, "TutorialMessageBox");
    void* lt = *(void**)((char*)comp + (tutor ? 0xe0 : 0x50));  // InstructionText / MainText (LocaliseText)
    void* tmp = rf_alive(lt) ? *(void**)((char*)lt + 0x50) : NULL;  // LocaliseText.TextMeshProText
    *em_mm = tutor ? TUTOR_EM_MM : REMARK_EM_MM;
    return rf_alive(tmp) ? tmp : NULL;
}

void rf_flatui_add(void* comp, int slot) {
    if (!rf_alive(comp)) return;
    void* t = rf_transform(comp);
    if (!rf_alive(t)) return;
    float em_mm = 0;
    void* text = box_text(comp, &em_mm);
    void* box = rf_class_is(comp, "MessageBox") ? comp : NULL;
    struct flat* free_slot = NULL;
    for (int i = 0; i < NFLAT; i++) {
        struct flat* f = &g_flat[i];
        void* ft = f->used ? rf_ref_get(&f->tr) : NULL;
        if (ft == t && rf_alive(ft)) {
            if (slot != RF_FLAT_KEEP) {  // the game shows a message on it
                f->slot = slot;
                f->seq = ++g_flat_seq;
            }
            if (box) rf_ref_set(&f->box, box);  // a StayInView of the box may have registered it first
            if (text) {
                rf_ref_set(&f->text, text);
                f->em_mm = em_mm;
            }
            f->measured = 0;  // new text: measure again
            return;
        }
        if (!free_slot && !rf_alive(ft)) free_slot = f;
    }
    if (!free_slot) return;
    rf_ref_clear(&free_slot->tr);
    rf_ref_clear(&free_slot->text);
    rf_ref_clear(&free_slot->box);
    memset(free_slot, 0, sizeof *free_slot);
    rf_ref_set(&free_slot->tr, t);
    if (text) rf_ref_set(&free_slot->text, text);
    if (box) rf_ref_set(&free_slot->box, box);
    free_slot->seq = ++g_flat_seq;
    free_slot->em_mm = em_mm;
    free_slot->used = true;
    free_slot->scale0 = CALLP(C_TR_LOCALSCALE, v3 (*)(void*, const void*))(t, NULL);
    free_slot->slot = slot == RF_FLAT_KEEP ? RF_FLAT_CENTRE : slot;
    flat_layer(t);
    char nm[64];
    rf_obj_name(t, nm, sizeof nm);
    rf_log("flatui: '%s' (%s) pinned to the screen, %s, scale %.4f%s", nm, rf_class_name(comp),
           free_slot->slot == RF_FLAT_TOP ? "top" : free_slot->slot == RF_FLAT_BOTTOM ? "bottom" : "centre",
           free_slot->scale0.x, text ? ", sized by its text" : "");
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
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, ty0 = 1e9f, ty1 = -1e9f;
    for (int i = 0; i < n; i++) {
        void* g = ((void**)(arr + 0x20))[i];
        if (!rf_alive(g) || !CALLP(C_BEH_ACTIVEENABLED, bool (*)(void*, const void*))(g, NULL)) continue;
        rf_color c = CALLP(C_GRAPHIC_COLOR, rf_color (*)(void*, const void*))(g, NULL);
        if (c.a < 0.01f) continue;
        void* gt = rf_transform(g);
        rf_rect r = CALLP(C_RT_GETRECT, rf_rect (*)(void*, const void*))(gt, NULL);
        bool text = rf_class_is(g, "TMP_Text");
        for (int k = 0; k < 4; k++) {
            v3 lp = {r.x + ((k & 1) ? r.w : 0), r.y + ((k & 2) ? r.h : 0), 0};
            v3 w = CALLP(C_TR_POINT, v3 (*)(void*, v3, const void*))(gt, lp, NULL);
            v3 e = CALLP(C_TR_INVPOINT, v3 (*)(void*, v3, const void*))(t, w, NULL);
            if (e.x < x0) x0 = e.x;
            if (e.x > x1) x1 = e.x;
            if (e.y < y0) y0 = e.y;
            if (e.y > y1) y1 = e.y;
            if (text && e.y < ty0) ty0 = e.y;
            if (text && e.y > ty1) ty1 = e.y;
        }
    }
    if (x1 < x0) return;  // nothing visible yet
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
    if (ty1 < ty0) ty0 = y0, ty1 = y1;  // no text: the whole content
    f->ty0 = ty0;
    f->ty1 = ty1;
    // main text: font size (its own units) times its scale per unit of the root's local scale
    float em = 0;
    void* tmp = rf_ref_get(&f->text);
    if (rf_alive(tmp)) {
        float fs = CALLP(C_TMP_FONTSIZE, float (*)(void*, const void*))(tmp, NULL);
        float ts = CALLP(C_TR_LOSSYSCALE, v3 (*)(void*, const void*))(rf_transform(tmp), NULL).y;
        float rs = CALLP(C_TR_LOCALSCALE, v3 (*)(void*, const void*))(t, NULL).y;
        if (fs > 0 && ts > 0 && rs > 0) em = fs * ts / rs;
    }
    if (g_dev && (fabsf(cx - f->cx) > 1 || fabsf(cy - f->cy) > 1 || fabsf(hx - f->hx) > 1 || fabsf(hy - f->hy) > 1 ||
                  fabsf(em - f->em) > 1e-4f * (em + f->em)))
        rf_log("flatui: content centre %.0f %.0f half %.0f x %.0f (local units), text em %.5g", cx, cy, hx, hy, em);
    f->cx = cx;
    f->cy = cy;
    f->hx = hx;
    f->hy = hy;
    f->em = em;
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

// share of the view height the em of a box's text should take on this display
static float em_share(const struct flat* f) {
    float share = f->em_mm >= TUTOR_EM_MM && f->em_mm < REMARK_EM_MM ? 0.04f : 0.048f;  // density unknown
    if (g_disp_ydpi > 50.0f && g_disp_h > 100) share = f->em_mm * g_disp_ydpi / 25.4f / (float)g_disp_h;
    return share < EM_MIN ? EM_MIN : share > EM_MAX ? EM_MAX : share;
}

// Size of the element: the factor k of its authored scale (text em at the wanted share of the view height, within the
// screen's width and its slot's share of the height), its parent's scale, and the content's half height as a fraction
// of the view's half height.
static float pin_size(struct flat* f, void* t, float tv, float aspect, v3* ps, float* nh) {
    float d = REF_DIST;
    void* parent = rf_tr_parent(t);
    *ps = rf_alive(parent) ? CALLP(C_TR_LOSSYSCALE, v3 (*)(void*, const void*))(parent, NULL) : (v3){1, 1, 1};
    float k = (tv / REF_TAN_HALF_V) * SIZE_K;  // no text: the authored size at the default FOV
    if (f->em > 0 && f->scale0.y > 0) k = em_share(f) * 2.0f * d * tv / (f->em * f->scale0.y);
    // the whole box on the screen: its width, and its half of the height (the centre slot may take most of it)
    float hmax = f->slot == RF_FLAT_CENTRE ? 0.9f : HEIGHT_MAX;
    if (f->hx > 0 && f->scale0.x * ps->x > 0) k = fminf(k, WIDTH_MAX * d * tv * aspect / (f->hx * f->scale0.x * ps->x));
    if (f->hy > 0 && f->scale0.y * ps->y > 0) k = fminf(k, hmax * d * tv / (f->hy * f->scale0.y * ps->y));
    *nh = f->hy * f->scale0.y * k * ps->y / (d * tv);
    return k;
}

// The element parallel to the image plane, its content centred across and at height y (fraction of the half height).
static void pin_place(struct flat* f, void* t, v3 cp, quat cq, float tv, float k, v3 ps, float y) {
    float d = REF_DIST;
    v3 sc = v3_scale(f->scale0, k);
    float wx = sc.x * ps.x, wy = sc.y * ps.y;  // metres per local unit
    v3 centre = v3_add(cp, q_rot(cq, (v3){0, y * d * tv, d}));
    v3 pos = v3_sub(centre, q_rot(cq, (v3){f->cx * wx, f->cy * wy, 0}));
    CALLP(C_TR_SETLOCALSCALE, void (*)(void*, v3, const void*))(t, sc, NULL);
    CALLP(C_TR_SETROT, void (*)(void*, quat, const void*))(t, cq, NULL);
    CALLP(C_TR_SETPOS, void (*)(void*, v3, const void*))(t, pos, NULL);
}

// a message box on the screen: fading in, shown, or fading out and still visible; other elements while active
static bool box_shown(struct flat* f) {
    void* mb = rf_ref_get(&f->box);
    if (!rf_alive(mb)) return true;
    int32_t state = *(int32_t*)((char*)mb + 0x88);  // MessageBox.State: Inactive, FadingIn, Active, FadingOut
    if (state == 1 || state == 2) return true;
    if (state != 3) return false;
    void* cg = *(void**)((char*)mb + 0x48);  // MessageBox.CanvasGroup
    return !rf_alive(cg) || CALLP(C_CG_ALPHA, float (*)(void*, const void*))(cg, NULL) > 0.02f;
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
    struct flat* list[NFLAT];
    int n = 0;
    bool any = false;
    for (int i = 0; i < NFLAT; i++) {
        struct flat* f = &g_flat[i];
        if (!f->used) continue;
        void* t = rf_ref_get(&f->tr);
        if (!rf_alive(t)) {
            rf_ref_clear(&f->tr);
            rf_ref_clear(&f->text);
            rf_ref_clear(&f->box);
            f->used = false;
            continue;
        }
        any = true;
        void* go = rf_gameobject(t);
        if (!rf_alive(go) || !CALLP(C_GO_ACTIVEH, bool (*)(void*, const void*))(go, NULL)) continue;
        if (tick && (!f->measured || g_frame - f->measured > 30)) measure(f, t);
        int j = n++;  // in the order the boxes showed their messages
        for (; j > 0 && list[j - 1]->seq > f->seq; j--) list[j] = list[j - 1];
        list[j] = f;
    }
    v3 cp;
    quat cq;
    float tv, aspect;
    if (n && camera_pose(&cp, &cq, &tv, &aspect)) {
        bool first = true;
        float text_bottom = 0;  // bottom of the text of the top box above, fraction of the half height
        for (int i = 0; i < n; i++) {
            struct flat* f = list[i];
            void* t = rf_ref_get(&f->tr);
            v3 ps;
            float nh, k = pin_size(f, t, tv, aspect, &ps, &nh), y = 0;
            if (f->slot == RF_FLAT_TOP && box_shown(f)) {
                float u = f->scale0.y * k * ps.y / (REF_DIST * tv);  // half heights per local unit
                float want;
                if (first) {  // the first one where a single box goes
                    want = nh < 1.0f - MARGIN ? 1.0f - MARGIN - nh : 0;
                } else {      // the next ones: their text below the text above
                    float room = text_bottom - STACK_GAP - STACK_FLOOR, need = (f->ty1 - f->ty0) * u;
                    if (need > room && room > 0.1f) {
                        k *= room / need;
                        u *= room / need;
                    }
                    want = text_bottom - STACK_GAP - (f->ty1 - f->cy) * u;
                }
                first = false;
                text_bottom = want + (f->ty0 - f->cy) * u;
                if (!f->placed) f->y = want;
                else if (tick) f->y += (want - f->y) * 0.3f;
                f->placed = true;
                y = f->y;
            } else if (f->slot == RF_FLAT_TOP) {
                f->placed = false;  // hidden: the next message takes its place at once
                y = nh < 1.0f - MARGIN ? 1.0f - MARGIN - nh : 0;
            } else if (f->slot == RF_FLAT_BOTTOM && nh < 1.0f - MARGIN) {
                y = -(1.0f - MARGIN - nh);
            }
            pin_place(f, t, cp, cq, tv, k, ps, y);
        }
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

// dev: the pinned elements, their measurements and where they are placed
void rf_dev_flats(void) {
    float tv = 0.5f, aspect = 2.0f;
    v3 cp;
    quat cq;
    camera_pose(&cp, &cq, &tv, &aspect);
    for (int i = 0; i < NFLAT; i++) {
        struct flat* f = &g_flat[i];
        void* t = f->used ? rf_ref_get(&f->tr) : NULL;
        if (!rf_alive(t)) continue;
        void* mb = rf_ref_get(&f->box);
        char nm[64];
        rf_obj_name(t, nm, sizeof nm);
        v3 ps;
        float nh, k = pin_size(f, t, tv, aspect, &ps, &nh);
        rf_log("flat '%s' slot %d seq %d state %d shown %d active %d: centre %.4g %.4g half %.4g x %.4g em %.4g scale0 %.4f k %.3f nh %.3f y %.3f",
               nm, f->slot, f->seq, rf_alive(mb) ? *(int32_t*)((char*)mb + 0x88) : -1, box_shown(f),
               CALLP(C_GO_ACTIVEH, bool (*)(void*, const void*))(rf_gameobject(t), NULL), f->cx, f->cy, f->hx, f->hy, f->em,
               f->scale0.y, k, nh, f->y);
        if (f->slot != RF_FLAT_TOP || !box_shown(f) || !g_t_graphic) continue;
        // its graphics, in the element's local units
        char* arr = CALLP(C_GO_GETCOMPSCHILD, char* (*)(void*, void*, bool, const void*))(rf_gameobject(t), g_t_graphic, false, NULL);
        int n = arr ? (int)*(uint64_t*)(arr + 0x18) : 0;
        for (int j = 0; j < n && j < 24; j++) {
            void* g = ((void**)(arr + 0x20))[j];
            if (!rf_alive(g) || !CALLP(C_BEH_ACTIVEENABLED, bool (*)(void*, const void*))(g, NULL)) continue;
            rf_color c = CALLP(C_GRAPHIC_COLOR, rf_color (*)(void*, const void*))(g, NULL);
            void* gt = rf_transform(g);
            rf_rect r = CALLP(C_RT_GETRECT, rf_rect (*)(void*, const void*))(gt, NULL);
            v3 a = CALLP(C_TR_INVPOINT, v3 (*)(void*, v3, const void*))(t, CALLP(C_TR_POINT, v3 (*)(void*, v3, const void*))(gt, (v3){r.x, r.y, 0}, NULL), NULL);
            v3 b = CALLP(C_TR_INVPOINT, v3 (*)(void*, v3, const void*))(t, CALLP(C_TR_POINT, v3 (*)(void*, v3, const void*))(gt, (v3){r.x + r.w, r.y + r.h, 0}, NULL), NULL);
            char gn[48];
            rf_obj_name(g, gn, sizeof gn);
            rf_log("  gfx '%s' %s alpha %.2f x %.4g..%.4g y %.4g..%.4g", gn, rf_class_name(g), c.a, a.x, b.x, a.y, b.y);
        }
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
