// Front-end and pause menus on a flat screen.
//
// Menu screens are world-space panels placed around the VR player's body; in flat mode the camera turns to
// each new screen (MenuController.CurrentScreen) so it is in view, like a VR player turning to the menu.
//
// Framing (round 2, operator request 2026-10-16): in menus the player does not turn the camera (rf_touch.c), so every
// item of the current screen has to be on screen whatever field of view is chosen. The screen's visible content -
// every enabled UI Graphic (texts, buttons, option rows, title art) and mesh (profile badges) under CurrentScreen - is
// measured from the eye; the camera turns to the middle of it and, when the chosen FOV is too narrow for all of it,
// the menu gets the wider vertical FOV it needs (g_menu_vfov, used by rf_view.c only while a menu is open). Turning
// and widening glide over a few frames, so a screen change reads as the camera moving to the next panel. Closing the
// pause menu gives the view back where the player left it.
//
// VR setup wizards in the front end are answered with the flat defaults the way a player would answer them.
//
// PostureSelectScreen ("Select Posture: Sitting / Standing") appears before every level load. Flat play is
// seated (camera at the authored seated eye height), so once the screen has faded in the port presses its
// own Sitting button handler (PostureSelectScreen.SittingPressed: SlotManager sitting mode + continue).
#include <string.h>

#include "rf.h"

typedef float (*fn_f_1)(void*, const void*);

static struct {
    void* screen;
    int frame;
} g_posture;

void* h_posture_setup(void* self, const void* mi) {
    void* r = ORIG(H_POSTURE_SETUP, void* (*)(void*, const void*))(self, mi);
    g_posture.screen = self;
    g_posture.frame = g_frame;
    rf_log("menu: posture wizard shown");
    return r;
}

static struct {
    void* ctrl;     // MenuController updated this frame (FrontendMenuController / PauseMenuController)
    int frame;
    void* screen;   // CurrentScreen we turned to
    int since;      // frames since the screen appeared
} g_menu;

int g_menu_frame = -100;

// screens visited in the active menu, oldest first (Android Back walks it backwards)
#define STACK 8
static rf_ref g_stack[STACK];  // weak: screens die with their scene
static int g_depth;

void h_menu_update(void* self, const void* mi) {
    ORIG(H_MENU_UPDATE, void (*)(void*, const void*))(self, mi);
    if (self != g_menu.ctrl || g_frame - g_menu.frame > 5) {  // (re)activated
        g_menu.screen = NULL;
        while (g_depth) rf_ref_clear(&g_stack[--g_depth]);
    }
    g_menu.ctrl = self;
    g_menu.frame = g_frame;
    // any menu showing a screen, also outside the front end (the end-of-game screen in the epilogue)
    if (rf_alive(*(void**)((char*)self + 0xd8))) g_menu_frame = g_frame;  // MenuController.CurrentScreen
}

static void stack_visit(void* scr) {
    if (g_depth >= 2 && rf_ref_get(&g_stack[g_depth - 2]) == scr) {
        rf_ref_clear(&g_stack[--g_depth]);  // went back
    } else {
        if (g_depth == STACK) {
            rf_ref_clear(&g_stack[0]);
            memmove(g_stack, g_stack + 1, sizeof g_stack[0] * (STACK - 1));
            g_stack[STACK - 1].h = 0;
            g_depth--;
        }
        rf_ref_set(&g_stack[g_depth++], scr);
    }
}

// Back inside a menu: previous screen of this menu, never into the start-up chain. False when at the root.
bool rf_menu_back(void) {
    if (rf_options_back()) return true;  // gyroscope page of Options -> its options
    void* mc = rf_menu_controller();
    if (!mc || g_depth < 2) return false;
    if (*((char*)mc + 0x120)) return true;  // MenuController.ScreenTransitionInProgress: ignore the press
    void* target = rf_ref_get(&g_stack[g_depth - 2]);
    if (!rf_alive(target) || rf_class_is(target, "StartupSplashScreen") || rf_class_is(target, "SelectProfileScreen") ||
        rf_class_is(target, "PostureSelectScreen"))
        return false;
    rf_log("menu: back to '%s'", rf_class_name(target));
    CALLP(C_MENU_TOSCREEN, void (*)(void*, void*, const void*))(mc, target, NULL);
    return true;
}

void* rf_menu_controller(void) {
    return g_menu.ctrl && g_frame - g_menu.frame <= 2 && rf_alive(g_menu.ctrl) ? g_menu.ctrl : NULL;
}

// ---- framing: the whole screen in view ----
typedef struct { float x, y, w, h; } rf_rect;
typedef struct { float r, g, b, a; } rf_color;
typedef struct { v3 c, e; } rf_bounds;

float g_menu_vfov;  // vertical FOV (degrees) the open menu needs; 0 outside menus

#define NPTS 768
#define FIT_MARGIN 1.08f  // 8 % air around the content
#define FIT_MAX_VFOV 100.0f
static v3 g_pts[NPTS];
static void* g_pt_owner[NPTS];  // component the point belongs to (dev: which element sets an edge)
static int g_npts;
static void* g_t_graphic;
static void* g_t_rend;

static void add_pt(v3 p, void* owner) {
    if (g_npts < NPTS) {
        g_pt_owner[g_npts] = owner;
        g_pts[g_npts++] = p;
    }
}

// renderers that are part of a screen's look: meshes and sprites (the 3D logo, profile badges); particles, lines and
// trails are effects around it (fog, glints, the pointer beam)
static bool panel_renderer(void* r) {
    const char* c = rf_class_name(r);
    return strcmp(c, "ParticleSystemRenderer") && strcmp(c, "LineRenderer") && strcmp(c, "TrailRenderer");
}

static void add_renderers(void* go) {
    typedef char* (*fn_comps)(void*, void*, bool, const void*);
    char* arr = g_t_rend && rf_alive(go) ? CALLP(C_GO_GETCOMPSCHILD, fn_comps)(go, g_t_rend, false, NULL) : NULL;
    int n = arr ? (int)*(uint64_t*)(arr + 0x18) : 0;
    for (int i = 0; i < n; i++) {
        void* m = ((void**)(arr + 0x20))[i];
        if (!rf_alive(m) || !CALLP(C_REND_GETENABLED, bool (*)(void*, const void*))(m, NULL) || !panel_renderer(m)) continue;
        rf_bounds b = CALLP(C_REND_BOUNDS, rf_bounds (*)(void*, const void*))(m, NULL);
        if (b.e.x > 2.0f || b.e.y > 2.0f || b.e.z > 2.0f) continue;  // scenery, not part of a panel
        for (int k = 0; k < 8; k++)
            add_pt((v3){b.c.x + ((k & 1) ? b.e.x : -b.e.x), b.c.y + ((k & 2) ? b.e.y : -b.e.y),
                        b.c.z + ((k & 4) ? b.e.z : -b.e.z)},
                   m);
    }
}

static void collect(void* scr) {
    g_npts = 0;
    if (!g_t_graphic) g_t_graphic = rf_class_type(rf_find_class("UnityEngine.UI.dll", "UnityEngine.UI", "Graphic"));
    if (!g_t_rend) g_t_rend = rf_class_type(rf_find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Renderer"));
    void* go = rf_gameobject(scr);
    if (!rf_alive(go)) return;
    typedef char* (*fn_comps)(void*, void*, bool, const void*);
    char* arr = g_t_graphic ? CALLP(C_GO_GETCOMPSCHILD, fn_comps)(go, g_t_graphic, false, NULL) : NULL;
    int n = arr ? (int)*(uint64_t*)(arr + 0x18) : 0;
    for (int i = 0; i < n; i++) {
        void* g = ((void**)(arr + 0x20))[i];
        if (!rf_alive(g) || !CALLP(C_BEH_ACTIVEENABLED, bool (*)(void*, const void*))(g, NULL)) continue;
        if (CALLP(C_GRAPHIC_COLOR, rf_color (*)(void*, const void*))(g, NULL).a < 0.01f) continue;
        void* gt = rf_transform(g);
        rf_rect r = CALLP(C_RT_GETRECT, rf_rect (*)(void*, const void*))(gt, NULL);
        for (int k = 0; k < 4; k++)
            add_pt(CALLP(C_TR_POINT, v3 (*)(void*, v3, const void*))(
                       gt, (v3){r.x + ((k & 1) ? r.w : 0), r.y + ((k & 2) ? r.h : 0), 0}, NULL), g);
    }
    add_renderers(go);
    // the screen's own scene geometry lives outside its hierarchy (the main menu's 3D "The Room VR" logo): the screen
    // fades it in and out with itself through MenuScreen.GeometryFades / TransientGeometryFades, or the controller's
    // MenuGeometryFadeController does for the screens listed in each fader (FadeGeometry.ActiveScreens)
    for (int off = 0x20; off <= 0x28; off += 8) {
        void* fades = *(void**)((char*)scr + off);
        for (int i = 0; i < rf_list_size(fades); i++) {
            void* f = rf_list_get(fades, i);
            if (rf_alive(f)) add_renderers(rf_gameobject(f));
        }
    }
    void* fc = rf_alive(g_menu.ctrl) ? *(void**)((char*)g_menu.ctrl + 0x118) : NULL;  // MenuController.FadeController
    void* faders = rf_alive(fc) ? *(void**)((char*)fc + 0x18) : NULL;                  // Faders: List<FadeGeometry>
    for (int i = 0; i < rf_list_size(faders); i++) {
        char* fg = rf_list_get(faders, i);
        if (!fg || fg[0x28]) continue;  // AlwaysActive: scenery shown with every screen
        void* screens = *(void**)(fg + 0x20);  // ActiveScreens
        void* anim = *(void**)(fg + 0x10);     // MaterialAnimator
        for (int k = 0; k < rf_list_size(screens); k++)
            if (rf_list_get(screens, k) == scr && rf_alive(anim)) add_renderers(rf_gameobject(anim));
    }
}

static struct {
    bool valid;
    float yaw, pitch, vfov;  // target framing (head angles in the rig, vertical FOV)
} g_fit;

// Where to look and how wide the view must be so that every point of the screen is inside it.
static void measure_fit(void* scr) {
    void* cam = rf_main_camera();
    if (!cam) return;
    collect(scr);
    if (!g_npts) return;
    void* ct = rf_transform(cam);
    void* rig = rf_tr_parent(ct);
    quat rr = rf_alive(rig) ? rf_tr_rot(rig) : (quat){0, 0, 0, 1};
    v3 eye = rf_tr_pos(ct);
    float y0 = 1e9f, y1 = -1e9f, p0 = 1e9f, p1 = -1e9f;
    int used = 0, e[4] = {0, 0, 0, 0};  // points at the left, right, bottom and top edge
    for (int i = 0; i < g_npts; i++) {
        v3 l = q_rot(q_conj(rr), v3_sub(g_pts[i], eye));
        if (l.z < 0.05f) continue;  // behind or beside the eye: not part of a panel in front
        float yaw = atan2f(l.x, l.z) / DEG2RAD, pitch = atan2f(l.y, sqrtf(l.x * l.x + l.z * l.z)) / DEG2RAD;
        if (yaw < y0) y0 = yaw, e[0] = i;
        if (yaw > y1) y1 = yaw, e[1] = i;
        if (pitch < p0) p0 = pitch, e[2] = i;
        if (pitch > p1) p1 = pitch, e[3] = i;
        used++;
    }
    if (!used) return;
    float yaw_c = (y0 + y1) * 0.5f, pitch_c = (p0 + p1) * 0.5f;
    quat qc = q_yaw_pitch(yaw_c, pitch_c);
    float aspect = CALLP(C_CAM_ASPECT, float (*)(void*, const void*))(cam, NULL);
    if (aspect < 0.2f) aspect = 2.0f;
    float need = 0.05f;
    for (int i = 0; i < g_npts; i++) {
        v3 v = q_rot(q_conj(qc), q_rot(q_conj(rr), v3_sub(g_pts[i], eye)));
        if (v.z < 0.05f) continue;
        float ty = fabsf(v.y / v.z), tx = fabsf(v.x / v.z) / aspect;
        if (ty > need) need = ty;
        if (tx > need) need = tx;
    }
    float vfov = 2.0f * atanf(need * FIT_MARGIN) / DEG2RAD;
    if (vfov > FIT_MAX_VFOV) vfov = FIT_MAX_VFOV;
    bool first = !g_fit.valid;
    g_fit.valid = true;
    g_fit.yaw = yaw_c;
    g_fit.pitch = pitch_c;
    g_fit.vfov = vfov;
    static float last_v, last_y;
    if (first || (g_dev && (fabsf(vfov - last_v) > 0.5f || fabsf(yaw_c - last_y) > 0.5f))) {
        rf_log("menu: framing %d pts -> yaw %.1f pitch %.1f, needs vertical fov %.1f (aspect %.2f)", used, yaw_c,
               pitch_c, vfov, aspect);
        if (g_dev) {
            static const char* const EDGE[4] = {"left", "right", "bottom", "top"};
            for (int k = 0; k < 4; k++) {
                char nm[64];
                rf_obj_name(g_pt_owner[e[k]], nm, sizeof nm);
                rf_log("menu:   %s edge '%s' (%s)", EDGE[k], nm, rf_class_name(g_pt_owner[e[k]]));
            }
        }
    }
    last_v = vfov;
    last_y = yaw_c;
}

static float wrap180(float a) {
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

static void tick_face_menu(void) {
    if (!g_menu.ctrl || g_frame - g_menu.frame > 2 || !rf_alive(g_menu.ctrl)) return;
    void* scr = *(void**)((char*)g_menu.ctrl + 0xd8);  // MenuController.CurrentScreen
    if (!rf_alive(scr)) return;
    if (scr != g_menu.screen) {
        g_menu.screen = scr;
        g_menu.since = 0;
        g_fit.valid = false;
        stack_visit(scr);
        char nm[64];
        rf_obj_name(scr, nm, sizeof nm);
        rf_log("menu: screen '%s' (%s)", nm, rf_class_name(scr));
    }
    g_menu.since++;
    // measure right away, then a few times a second (rows appear, screens fade in, layouts settle)
    if (g_menu.since == 2 || g_menu.since % 6 == 0) measure_fit(scr);
    if (!g_fit.valid) return;
    float a = g_menu.since <= 3 ? 1.0f : 0.25f;  // a new screen: jump; afterwards glide
    g_head.yaw += wrap180(g_fit.yaw - g_head.yaw) * a;
    g_head.pitch += (g_fit.pitch - g_head.pitch) * a;
    if (g_head.pitch > 80) g_head.pitch = 80;
    if (g_head.pitch < -80) g_head.pitch = -80;
    g_menu_vfov = g_menu_vfov <= 0 ? g_fit.vfov : g_menu_vfov + (g_fit.vfov - g_menu_vfov) * a;
}

// The pause menu opens over the game: the view the player had comes back when it closes (same scene only).
static struct {
    bool on;
    char scene[64];
    float yaw, pitch;
} g_pausev;

static void tick_menu_edges(void) {
    bool menu = rf_menu_context();
    if (menu && !g_pausev.on) {
        g_pausev.on = true;
        memcpy(g_pausev.scene, g_scene, sizeof g_pausev.scene);
        g_pausev.yaw = g_head.yaw;
        g_pausev.pitch = g_head.pitch;
    } else if (!menu && g_pausev.on) {
        g_pausev.on = false;
        g_menu_vfov = 0;
        g_fit.valid = false;
        g_menu.screen = NULL;
        bool game = g_scene[0] && strcmp(g_scene, "MainMenu") && strcmp(g_scene, "Startup");
        if (game && !strcmp(g_pausev.scene, g_scene)) {
            g_head.yaw = g_pausev.yaw;
            g_head.pitch = g_pausev.pitch;
            rf_log("menu: closed, view back to yaw %.1f pitch %.1f", g_head.yaw, g_head.pitch);
        }
    }
}

void rf_menu_tick(void) {
    tick_menu_edges();
    tick_face_menu();
    if (!g_posture.screen) return;
    void* s = g_posture.screen;
    if (!rf_alive(s) || g_frame - g_posture.frame > 1800) {
        g_posture.screen = NULL;
        return;
    }
    void* cg = *(void**)((char*)s + 0x18);  // MenuScreen.CanvasGroup
    float a = rf_alive(cg) ? CALLP(C_CG_ALPHA, fn_f_1)(cg, NULL) : 1.0f;
    if (g_frame - g_posture.frame < 20 || a < 0.99f) return;  // still fading in: buttons not live yet
    g_posture.screen = NULL;
    rf_log("menu: posture wizard -> Sitting (flat default), alpha %.2f after %d frames", a, g_frame - g_posture.frame);
    CALLP(C_POSTURE_SITTING, void (*)(void*, const void*))(s, NULL);
}
