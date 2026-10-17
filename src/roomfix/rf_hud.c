// Touch HUD support: the Java overlay (port.roomfix.HudView) draws the buttons and polls g_hud_state from the
// UI thread; button presses come back through the touch queue and run here on the Unity main thread.
// Button visibility follows the game's own input gates (Player.TeleportInput / InventoryInput / GogglesInput),
// so a button is only shown when the VR player could use the same control at that moment.
#include "rf.h"

volatile int g_hud_state;

#define LEFT 0
#define RIGHT 1

typedef bool (*fn_b1)(void*, const void*);

static void* static_field0(uintptr_t meta) {
    uintptr_t k = *(uintptr_t*)(g_il2cpp_base + meta);
    if (!k || (k & 1) || k < 0x10000) return NULL;  // metadata slot not initialised yet
    void* statics = *(void**)(k + 0xb8);
    return statics ? *(void**)statics : NULL;
}

// Player.InputControl.IsAllowed for the control stored at Player+off
static bool input_allowed(int off) {
    void* ic = *(void**)((char*)g_player + off);
    return ic && CALLP(C_INPUTCTL_ALLOWED, fn_b1)(ic, NULL);
}

static bool gameplay(void) { return !rf_menu_context() && rf_player(); }

bool rf_inventory_open(void) {
    void* hm = static_field0(META_HUDMGR_TYPEINFO);  // HudManager.Instance
    return rf_alive(hm) && *((char*)hm + 0x88);      // HudManager.Showing
}

void* rf_inventory_root(void) {
    void* hm = static_field0(META_HUDMGR_TYPEINFO);
    void* lp = rf_alive(hm) && *((char*)hm + 0x88) ? *(void**)((char*)hm + 0x18) : NULL;  // HudManager.LayoutParent
    return rf_alive(lp) ? lp : NULL;
}

// The eyepiece control exists once the story has given it: GogglesHudManager.Instance with its HUD enabled
// (GogglesHudManager.EnableGogglesHud sets Enabled +0x7C).
static bool goggles_owned(void) {
    void* gh = static_field0(META_GOGGLESHUD_TYPEINFO);
    return rf_alive(gh) && *((char*)gh + 0x7c);
}

bool rf_goggles_on(void) {
    void* g = static_field0(META_GOGGLES_TYPEINFO);  // Goggles.Instance
    return rf_alive(g) && *((char*)g + 0xf8);        // Goggles.GogglesActive
}

void rf_hud_tick(void) {
    int s = 0;
    if (rf_menu_context()) s |= HUD_MENU;
    if (gameplay()) {
        s |= HUD_VISIBLE;
        if (input_allowed(0xf8) || rf_act_teleport_mode_on()) s |= HUD_TELEPORT;     // TeleportInput
        if (rf_act_teleport_mode_on()) s |= HUD_TELEPORT_ON;
        if (input_allowed(0x100)) s |= HUD_INVENTORY;                                  // InventoryInput
        if (rf_inventory_open()) s |= HUD_INV_OPEN;
        if (input_allowed(0x108) && goggles_owned()) s |= HUD_GOGGLES;                 // GogglesInput
        if (rf_goggles_on()) s |= HUD_GOGGLES_ON;
        // snap turn is covered by swipe-look; its buttons appear while the tutorial asks for it
        if (g_tut_action == 6 && input_allowed(0x110)) s |= HUD_SNAP;                // SnapTurnInput
    }
    if (s != g_hud_state) {
        if (g_dev) rf_log("hud: state %#x -> %#x", g_hud_state, s);
        g_hud_state = s;
    }
}

void rf_hud_button(int id) {
    rf_log("hud: button %d (state %#x)", id, g_hud_state);
    switch (id) {
    case HB_TELEPORT:
        if (gameplay()) rf_act_teleport_mode(!rf_act_teleport_mode_on());
        break;
    case HB_INVENTORY:  // A on the right controller (RwAction 21) -> HudManager.ToggleOnOff
        if (gameplay()) {
            if (rf_act_teleport_mode_on()) rf_act_teleport_mode(false);
            rf_act_button(RIGHT, 0, 6);
        }
        break;
    case HB_GOGGLES:
        // the eyepiece button path of Player.UpdateGogglesInput (RwAction 28, gated by GogglesInput):
        // GogglesHudManager.ReleaseGrab, then Activate/DeactivateWithFade by Goggles.GogglesActive
        if (gameplay() && input_allowed(0x108)) {
            void* gh = static_field0(META_GOGGLESHUD_TYPEINFO);
            if (rf_alive(gh)) {
                CALLP(C_GOGGLESHUD_RELEASE, void (*)(void*, const void*))(gh, NULL);
                if (rf_goggles_on()) CALLP(C_GOGGLESHUD_OFF, void (*)(void*, const void*))(gh, NULL);
                else CALLP(C_GOGGLESHUD_ON, void (*)(void*, const void*))(gh, NULL);
            }
        }
        break;
    case HB_BACK:
        // menus: previous screen; pause menu root: resume; gameplay: pause
        if (rf_menu_context() && rf_menu_back()) break;
        if (rf_menu_context() && g_frame - g_pause_frame > 2) break;  // front-end root: nothing to go back to
        // fall through
    case HB_PAUSE:  // left menu button: RwAction 22 (GameStateManager.Pause), RwAction 27 in the pause menu (UnPause)
        if (gameplay() || g_frame - g_pause_frame <= 2) {
            if (rf_act_teleport_mode_on()) rf_act_teleport_mode(false);
            rf_act_button(LEFT, 2, 6);
        }
        break;
    case HB_TURN_L:
        if (gameplay()) rf_act_snap_turn(-1);
        break;
    case HB_TURN_R:
        if (gameplay()) rf_act_snap_turn(1);
        break;
    default:
        break;
    }
}

// The inventory grid opens in front of the VR player's *level* gaze (HudManager.CalculateShowTransform uses the
// flattened head forward), i.e. at the top of the screen whenever the flat camera looks down at a puzzle. Rotate
// the placement about the eye by the camera pitch so the grid opens where the player is looking; it stays
// world-fixed afterwards exactly as in VR (CalculateShowTransform runs once per HudManager.Show).
//
// Whole inventory in view (round 4, operator 2026-10-16: "make the inventory always visible when you open it"; "the
// hint lever in the inventory cannot be pulled"): the authored offset puts the grid beside the view axis and, at a
// puzzle the location stands close to (the tutorial safe), partly inside the puzzle - the grid lines draw over it, but
// the hint tab under the grid and the items are hidden in the safe door. On opening, the inventory's visible content
// (its renderers and UI graphics: grid, items, hint tab, badge) is measured from the eye: it is turned about the eye to the middle of
// the view, made smaller (about its own centre) when it would cover more than INV_FIT of the view, and when a wall or
// a puzzle lies between the eye and any part of it, moved towards the eye and scaled by the same factor - same size on
// the screen, nothing in front of it.
#define INV_FIT 0.8f     // the inventory covers at most this share of the view (half-angle tangents)
#define INV_GAP 0.04f    // metres kept free in front of whatever stood in the way
#define INV_MIN_S 0.25f  // smallest scale towards the eye

typedef void (*fn_set_v3)(void*, v3, const void*);
typedef struct { v3 c, e; } inv_bounds;
static void* g_t_rend;

static bool child_of_t(void* t, void* root) {
    for (int i = 0; i < 16 && rf_alive(t); i++, t = rf_tr_parent(t))
        if (t == root) return true;
    return false;
}

typedef struct { float x, y, w, h; } inv_rect;
static void* g_t_graphic;

// corners of the visible content under LayoutParent, world: renderer bounds (items, the badge, the hint tab) and the
// rects of UI graphics (the grid is drawn on a world-space canvas)
static int inv_points(void* lp, v3* pts, int max) {
    if (!g_t_rend) g_t_rend = rf_class_type(rf_find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Renderer"));
    if (!g_t_graphic) g_t_graphic = rf_class_type(rf_find_class("UnityEngine.UI.dll", "UnityEngine.UI", "Graphic"));
    void* go = rf_gameobject(lp);
    typedef char* (*fn_comps)(void*, void*, bool, const void*);
    char* arr = g_t_rend && rf_alive(go) ? CALLP(C_GO_GETCOMPSCHILD, fn_comps)(go, g_t_rend, false, NULL) : NULL;
    int n = arr ? (int)*(uint64_t*)(arr + 0x18) : 0, np = 0, nr = 0, ng = 0;
    for (int i = 0; i < n && np + 8 <= max; i++) {
        void* r = ((void**)(arr + 0x20))[i];
        if (!rf_alive(r) || !CALLP(C_REND_GETENABLED, bool (*)(void*, const void*))(r, NULL)) continue;
        if (!strcmp(rf_class_name(r), "ParticleSystemRenderer")) continue;
        inv_bounds b = CALLP(C_REND_BOUNDS, inv_bounds (*)(void*, const void*))(r, NULL);
        if (b.e.x > 1.0f || b.e.y > 1.0f || b.e.z > 1.0f) continue;  // not part of the panel
        for (int k = 0; k < 8; k++)
            pts[np++] = (v3){b.c.x + ((k & 1) ? b.e.x : -b.e.x), b.c.y + ((k & 2) ? b.e.y : -b.e.y),
                             b.c.z + ((k & 4) ? b.e.z : -b.e.z)};
        nr++;
    }
    arr = g_t_graphic && rf_alive(go) ? CALLP(C_GO_GETCOMPSCHILD, fn_comps)(go, g_t_graphic, false, NULL) : NULL;
    n = arr ? (int)*(uint64_t*)(arr + 0x18) : 0;
    for (int i = 0; i < n && np + 4 <= max; i++) {
        void* g = ((void**)(arr + 0x20))[i];
        if (!rf_alive(g) || !CALLP(C_BEH_ACTIVEENABLED, bool (*)(void*, const void*))(g, NULL)) continue;
        void* gt = rf_transform(g);
        inv_rect rc = CALLP(C_RT_GETRECT, inv_rect (*)(void*, const void*))(gt, NULL);
        for (int k = 0; k < 4; k++)
            pts[np++] = CALLP(C_TR_POINT, v3 (*)(void*, v3, const void*))(
                gt, (v3){rc.x + ((k & 1) ? rc.w : 0), rc.y + ((k & 2) ? rc.h : 0), 0}, NULL);
        ng++;
    }
    if (g_dev) rf_log("inventory: %d renderers, %d graphics in view of the frame", nr, ng);
    return np;
}

typedef struct { v3 point, normal; uint32_t face; float dist; v2 uv; int32_t col; } inv_hit;

// distance to the first solid thing on the way from the eye to p (not the inventory, not the player), or the distance of p
static float inv_clear(v3 eye, v3 p, void* lp, void* player_t) {
    v3 d = v3_sub(p, eye);
    float len = v3_len(d);
    if (len < 1e-3f) return len;
    d = v3_scale(d, 1.0f / len);
    void* arr = ((void* (*)(v3, v3, float, int32_t, int32_t, const void*))RVA(g_call_desc[C_PHYS_RAYCASTALL].rva))(
        eye, d, len, -1, 1 /* QueryTriggerInteraction.Ignore */, NULL);
    int32_t n = arr ? (int32_t) * (uint64_t*)((char*)arr + 0x18) : 0;
    inv_hit* hits = arr ? (inv_hit*)((char*)arr + 0x20) : NULL;
    float best = len;
    for (int i = 0; i < n && i < 64; i++) {
        if (hits[i].dist >= best) continue;
        void* col = ((void* (*)(inv_hit*, const void*))RVA(g_call_desc[C_HIT_COLLIDER].rva))(&hits[i], NULL);
        if (!rf_alive(col)) continue;
        void* ct = rf_transform(col);
        if (child_of_t(ct, lp) || (player_t && child_of_t(ct, player_t))) continue;
        best = hits[i].dist;
    }
    return best;
}

static void inv_frame(void* lp) {
    void* cam = rf_main_camera();
    if (!cam) return;
    void* ct = rf_transform(cam);
    v3 eye = rf_tr_pos(ct);
    quat qc = rf_tr_rot(ct);
    static v3 pts[512];
    int np = inv_points(lp, pts, 512);
    if (!np) {
        rf_log("inventory: nothing visible to frame yet");
        return;
    }
    // angular box of the content in the camera frame (tangents)
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, z = 0;
    for (int i = 0; i < np; i++) {
        v3 l = q_rot(q_conj(qc), v3_sub(pts[i], eye));
        if (l.z < 0.02f) continue;
        x0 = fminf(x0, l.x / l.z), x1 = fmaxf(x1, l.x / l.z), y0 = fminf(y0, l.y / l.z), y1 = fmaxf(y1, l.y / l.z);
        z += l.z;
    }
    if (x1 < x0) return;
    z /= (float)np;
    // turn about the eye: the middle of the content onto the view axis
    v3 c_dir = v3_norm(q_rot(qc, (v3){(x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 1.0f}));
    v3 fwd = q_rot(qc, (v3){0, 0, 1});
    v3 axis = v3_cross(c_dir, fwd);
    float sn = v3_len(axis), cs = v3_dot(c_dir, fwd);
    quat turn = sn > 1e-5f ? q_axis(v3_scale(axis, 1.0f / sn), atan2f(sn, cs)) : (quat){0, 0, 0, 1};
    v3 p = rf_tr_pos(lp);
    p = v3_add(eye, q_rot(turn, v3_sub(p, eye)));
    quat r = q_mul(turn, rf_tr_rot(lp));
    v3 sc = CALLP(C_TR_LOCALSCALE, v3 (*)(void*, const void*))(lp, NULL);
    // fit: the content's half extents against the view's (tangents)
    float tv = tanf(CALLP(C_CAM_GET_FOV, float (*)(void*, const void*))(cam, NULL) * 0.5f * DEG2RAD);
    float aspect = CALLP(C_CAM_ASPECT, float (*)(void*, const void*))(cam, NULL);
    float fit = fminf(INV_FIT * tv / fmaxf((y1 - y0) * 0.5f, 1e-4f), INV_FIT * tv * aspect / fmaxf((x1 - x0) * 0.5f, 1e-4f));
    float s_fit = fit < 1.0f ? fit : 1.0f;
    v3 centre = v3_add(eye, v3_scale(fwd, z));  // middle of the content after the turn
    if (s_fit < 1.0f) {
        p = v3_add(centre, v3_scale(v3_sub(p, centre), s_fit));
        sc = v3_scale(sc, s_fit);
    }
    // as large as the VR player saw it: a position authored for the location's eye point (a puzzle's forced
    // position) is seen from eyes moved back from that point (rf_view.c) farther away - it comes towards the eyes to
    // the distance the VR player had, at its own size (so it looks as large as in the headset), unless that is more
    // than the view takes
    v3 base;
    float s_vr = 1.0f;
    if (rf_view_base(&base)) {
        float d_vr = v3_len(v3_sub(centre, base)), d_eye = v3_len(v3_sub(centre, eye));
        if (d_eye > 1e-3f && d_vr < d_eye) s_vr = fmaxf(d_vr / d_eye, INV_MIN_S);
        // the content's half angles grow by 1 / s_vr: keep them within INV_FIT of the view
        float grow = fmaxf((y1 - y0) * 0.5f * s_fit / (INV_FIT * tv), (x1 - x0) * 0.5f * s_fit / (INV_FIT * tv * aspect));
        if (grow > 1e-4f && s_vr < grow) s_vr = fminf(grow, 1.0f);
    }
    if (s_vr < 1.0f) {
        p = v3_add(eye, v3_scale(v3_sub(p, eye), s_vr));
        centre = v3_add(eye, v3_scale(v3_sub(centre, eye), s_vr));
        z *= s_vr;
        x0 /= s_vr, x1 /= s_vr, y0 /= s_vr, y1 /= s_vr;  // same size, nearer: larger angles
    }
    // nothing between the eye and the content: probe its middle and the corners of its (turned, fitted) box
    void* pl = rf_player();
    void* player_t = pl ? rf_transform(pl) : NULL;
    float hx = (x1 - x0) * 0.5f * s_fit * z, hy = (y1 - y0) * 0.5f * s_fit * z;
    v3 right = q_rot(qc, (v3){1, 0, 0}), up = q_rot(qc, (v3){0, 1, 0});
    float s_occ = 1.0f;
    for (int k = 0; k < 9; k++) {
        float ax = (float)(k % 3 - 1), ay = (float)(k / 3 - 1);
        v3 q = v3_add(centre, v3_add(v3_scale(right, ax * hx), v3_scale(up, ay * hy)));
        float len = v3_len(v3_sub(q, eye)), clear = inv_clear(eye, q, lp, player_t);
        if (clear < len - 0.005f) s_occ = fminf(s_occ, (clear - INV_GAP) / len);
    }
    if (s_occ < INV_MIN_S) s_occ = INV_MIN_S;
    if (s_occ < 1.0f) {
        p = v3_add(eye, v3_scale(v3_sub(p, eye), s_occ));
        sc = v3_scale(sc, s_occ);
    }
    CALLP(C_TR_SETPOS, fn_set_v3)(lp, p, NULL);
    CALLP(C_TR_SETROT, void (*)(void*, quat, const void*))(lp, r, NULL);
    CALLP(C_TR_SETLOCALSCALE, fn_set_v3)(lp, sc, NULL);
    rf_log("inventory: framed - turned %.1f deg, fit %.2f, as in VR %.2f, nearer %.2f (%d points, %.2f m)",
           atan2f(sn, cs) / DEG2RAD, s_fit, s_vr, s_occ, np, z * s_occ);
}

void h_inv_show_xform(void* self, const void* mi) {
    ORIG(H_INV_SHOWXFORM, void (*)(void*, const void*))(self, mi);
    void* lp = *(void**)((char*)self + 0x18);  // HudManager.LayoutParent
    void* cam = rf_main_camera();
    if (!rf_alive(lp) || !cam) return;
    void* ct = rf_transform(cam);
    quat qc = rf_tr_rot(ct);
    v3 f = q_rot(qc, (v3){0, 0, 1});
    v3 flat = {f.x, 0, f.z};
    if (v3_len(flat) < 1e-3f) return;
    quat delta = q_mul(qc, q_conj(rf_q_look(v3_norm(flat), (v3){0, 1, 0})));
    v3 h = rf_tr_pos(ct), p = rf_tr_pos(lp);
    CALLP(C_TR_SETPOS, void (*)(void*, v3, const void*))(lp, v3_add(h, q_rot(delta, v3_sub(p, h))), NULL);
    CALLP(C_TR_SETROT, void (*)(void*, quat, const void*))(lp, q_mul(delta, rf_tr_rot(lp)), NULL);
}

// HudManager.Show places the inventory either by CalculateShowTransform (above) or at a position a puzzle forces
// (HudManager.ForcedInventoryPosition: the tutorial safe puts it on the safe door, where the VR player stands); either
// way it is then framed in view.
void h_inv_show(void* self, const void* mi) {
    ORIG(H_INV_SHOW, void (*)(void*, const void*))(self, mi);
    void* lp = *(void**)((char*)self + 0x18);  // HudManager.LayoutParent
    if (*((char*)self + 0x88) && rf_alive(lp)) inv_frame(lp);  // HudManager.Showing
}
