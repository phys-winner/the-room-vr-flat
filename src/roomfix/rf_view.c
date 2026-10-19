// Flat camera: field of view for a wide phone screen; where the eyes are inside the location - at its centre after a
// teleport, moved by touch (pinch / two-finger pan) and the gyroscope only, never by turning the view; the position
// gauge (HudView.java) and the game's own play-area marker.
//
// FOV: without an XR display the centre camera renders with its own Camera.fieldOfView, authored as a VR
// value (90 vertical). On a 19.5:9 screen that is ~130 degrees across. The option value g_fov_h is the
// horizontal FOV of a 16:9 reference screen; the vertical FOV derived from it is kept on every screen, so
// wider phones see more to the sides and taller ones lose nothing at the top and bottom (hor+). Any FOV the
// game sets itself is scaled by the same factor, so authored zoom effects keep their relative strength.
//
// Eyes (round 5, operator 2026-10-17: "teleporting should put the player at the centre of the location, and turning
// the camera must not change the player's position" - this replaces round 4's step back, which moved the eyes away from
// where the camera looked): the eyes' offset from the location's eye point is kept in tracking space, so turning the
// view leaves them where they are; when the game turns the tracking space itself (its snap turn, about the location's
// eye point while seated) the offset turns back so the eyes keep their place. A location puts the seated player's
// eyes at its centre (TrackingSpace.MovePlayerToPosition: the location point at eye level, no head offset kept while
// sitting); every arrival - a teleport, a level's first location, a continued save - puts them back there. The pinch
// moves them along the view
// direction, the two-finger pan sideways (along the view's horizontal right) and up or down. Every move stays inside
// the location as the game draws it around a VR player, so the game never has a reason to fade the view out and ask to
// "return to the play area": within its fade-out radius (MaxPlayerDistanceBeforeFadeOut around the location point,
// the check of Player.UpdateOutOfRange) minus EDGE, within V_RANGE of the eye level, CLEAR away from walls, solid
// objects and the game's head-collision volumes along the way (a swept sphere; the head's own collider is a 5 cm
// sphere), and never with one of the game's layer-2 volumes between the head and the location's eye level (the ray of
// HeadInsideGeometryHUD.UpdateHeadCollisionInTheWay).
//
// Gauge (operator: "with enough deviation draw an interface that shows where the player is inside the location
// relative to its centre, for each of the three axes - the one he moves along - and let it be turned off"): one track
// per axis of the view - right, up, forward (horizontal) - drawn by the HUD overlay. A track appears while touch moves
// the eyes along its axis once they are GAUGE_SHOW from the centre on it, and fades out GAUGE_HOLD after the last such
// move. It shows the location's whole span on that axis (dim), the part reachable from here (bright: the chord of the
// play-area circle, the height band), the centre and the eyes; a stop flashes on the side where a move was held back.
// Options -> POSITION INDICATOR off hides it (g_gauge_on, gauge= in port_settings.cfg).
//
// The game's play-area display (operator: "if the game already shows the bounds of the camera space, show them too"):
// the game draws no outline of the play area (OVRBoundary is used only by the SDK's mixed-reality composition). Past
// its edge, or with the head inside geometry, HeadInsideGeometryHUD fades the scene to black and shows "Please return
// to the play area" with its return marker - a ring with an arrow over the location point, its glow and embers on the
// floor (ReturnToLocationMarker). The eyes stop at that edge here instead, so the marker is shown over the scene, at
// the location point as the game places it, while moves press against the edge of the play area or one of its
// head-collision volumes, and MARK_HOLD longer; the position-indicator option turns it off as well.
//
// Pause (round 4: "the pause menu sometimes shows turned"): the pause panel is placed in front of the eyes where they
// are when it opens, facing them; the menu framing then turns the camera to it (rf_menu.c). While a menu is open over
// the game the eyes stay exactly where they were.
//
// Menus (round 2): the vertical FOV is at least what the open menu screen needs to show all of its items
// (g_menu_vfov, rf_menu.c), so a narrow FOV option never hides a menu row. The gyroscope's eye moves (rf_gyro.c) take
// the same way as the touch moves; its forward push is a passing offset along the view.
#include <stdio.h>
#include <time.h>

#include "rf.h"

typedef float (*fn_f_1)(void*, const void*);
typedef void (*fn_set_f)(void*, float, const void*);

float g_fov_h = 90.0f;
int g_gauge_on = 1;
volatile float g_gauge[RF_GAUGE_N];

#define VR_FOV 90.0f
#define REF_ASPECT (16.0f / 9.0f)

static struct {
    void* cam;
    float written;  // value we wrote last (-1: none)
    float game;     // last value the game set
} g_fov = {NULL, -1.0f, VR_FOV};

static void tick_fov(void) {
    void* cam = rf_main_camera();
    if (!cam) return;
    if (cam != g_fov.cam) {
        g_fov.cam = cam;
        g_fov.written = -1.0f;
    }
    float cur = CALLP(C_CAM_GET_FOV, fn_f_1)(cam, NULL);
    if (fabsf(cur - g_fov.written) > 0.01f) {
        if (g_dev && fabsf(cur - g_fov.game) > 0.01f) rf_log("view: camera fov set by the game: %.2f", cur);
        g_fov.game = cur;
    }
    float v = 2.0f * atanf(tanf(g_fov_h * 0.5f * DEG2RAD) / REF_ASPECT);  // radians, kept vertical FOV
    if (rf_menu_context() && g_menu_vfov * DEG2RAD > v) v = g_menu_vfov * DEG2RAD;  // every menu item in view
    float k = tanf(v * 0.5f) / tanf(VR_FOV * 0.5f * DEG2RAD);
    float want = 2.0f * atanf(tanf(g_fov.game * 0.5f * DEG2RAD) * k) / DEG2RAD;
    if (fabsf(want - cur) > 0.01f) {
        CALLP(C_CAM_SET_FOV, fn_set_f)(cam, want, NULL);
        g_fov.written = want;
    }
}

static struct {
    void* location;
    char scene[64];
    int face_frames;  // frames left to keep the camera on the new location's forward direction
    bool level_start; // the location is the level's first one (the camera faces the level's title)
} g_arrive;

typedef v3 (*fn_v3_1)(void*, const void*);

// After a teleport the camera looks along the arrival location's authored forward direction, at the point a seated
// VR player at the location point sees in the middle (FOCUS_M ahead, 20 degrees down), the way they face the puzzle
// the location was placed for. Kept for a few frames because the game re-aligns the tracking space during the
// teleport fade.
#define FOCUS_M 0.6f
static void face_location(void* loc) {
    void* cam = rf_main_camera();
    if (!cam || !rf_alive(loc)) return;
    v3 f = CALLP(C_TR_FORWARD, fn_v3_1)(rf_transform(loc), NULL);
    void* rig = rf_tr_parent(rf_transform(cam));
    if (!rf_alive(rig) || fabsf(f.x) + fabsf(f.z) < 1e-3f) return;
    v3 fh = v3_norm((v3){f.x, 0, f.z});
    rf_face_point(v3_add(rf_tr_pos(rig), v3_add(v3_scale(fh, FOCUS_M), (v3){0, -FOCUS_M * 0.364f, 0})));  // tan 20
}

// A level opens with its title in the scene - place and time ("Bloomsbury Police Station, 6.18pm ...", a world canvas
// named Scene_Setting) - set up beside the start location's forward for the VR player to look around at. Round 4
// (operator 2026-10-16: "at the start of a new game the camera looks somewhere to the side, not at the level's opening
// message"): at a level's first location the camera faces that title while it is there.
static bool level_title(v3* centre) {
    static void* t_tmp;
    if (!t_tmp) t_tmp = rf_class_type(rf_find_class("Unity.TextMeshPro.dll", "TMPro", "TMP_Text"));
    if (!t_tmp || !g_il2.string_new) return false;
    void* go = CALLP(C_GO_FIND, void* (*)(void*, const void*))(g_il2.string_new("Scene_Setting"), NULL);
    if (!rf_alive(go)) return false;
    // only while it is shown: its timeline (fade in, hold, fade out) plays or its canvas group is visible - a level
    // continued from a save does not play it again
    static void* t_cg, *t_pd;
    if (!t_cg) t_cg = rf_class_type(rf_find_class("UnityEngine.UIModule.dll", "UnityEngine", "CanvasGroup"));
    if (!t_pd) t_pd = rf_class_type(rf_find_class("UnityEngine.DirectorModule.dll", "UnityEngine.Playables", "PlayableDirector"));
    typedef void* (*fn_getcomp)(void*, void*, const void*);
    void* cg = t_cg ? CALLP(C_GO_GETCOMP, fn_getcomp)(go, t_cg, NULL) : NULL;
    void* pd = t_pd ? CALLP(C_GO_GETCOMP, fn_getcomp)(go, t_pd, NULL) : NULL;
    bool playing = rf_alive(pd) && CALLP(C_PD_STATE, int32_t (*)(void*, const void*))(pd, NULL) == 1;  // PlayState.Playing
    bool visible = rf_alive(cg) && CALLP(C_CG_ALPHA, float (*)(void*, const void*))(cg, NULL) > 0.02f;
    if (!playing && !visible) return false;
    char* arr = CALLP(C_GO_GETCOMPSCHILD, char* (*)(void*, void*, bool, const void*))(go, t_tmp, false, NULL);
    int n = arr ? (int)*(uint64_t*)(arr + 0x18) : 0, used = 0;
    v3 sum = {0, 0, 0};
    for (int i = 0; i < n; i++) {
        void* t = ((void**)(arr + 0x20))[i];
        if (!rf_alive(t)) continue;
        sum = v3_add(sum, rf_tr_pos(rf_transform(t)));
        used++;
    }
    if (!used) return false;
    *centre = v3_scale(sum, 1.0f / (float)used);
    return true;
}

// ---- the eyes inside the location ----
#define EDGE 0.12f        // metres: the eyes stay this far inside the game's fade-out radius
#define V_RANGE 0.4f      // metres (of the player's scale) above and below the location's eye level
#define CLEAR 0.1f        // metres from walls, solid objects and head-collision volumes (camera near plane 0.01)
#define SKIN 0.01f        // a swept move stops this short of the contact
#define GAUGE_SHOW 0.08f  // metres from the centre along an axis before its track appears
#define GAUGE_HOLD 0.9f   // s: a track stays this long after the last touch move along its axis
#define GAUGE_IN 0.15f    // s: fade in
#define GAUGE_OUT 0.35f   // s: fade out
#define STOP_HOLD 0.45f   // s: a stop flash
#define MARK_HOLD 1.5f    // s: the game's return marker stays this long after the last push against the edge

static struct {
    v3 eye;        // eyes' offset from the location's eye point (tracking-space origin), tracking space
    v3 req;        // moves asked for this frame, tracking space
    v3 req_touch;  // the touch part of them (the gauge follows touch moves only)
    float off_m;   // eyes' distance from the location's eye point, world metres (last frame)
    float now;     // s, monotonic
    quat rq_prev;  // the tracking space's rotation last frame
    bool have_rq;
} g_eyes;

static struct {
    float alpha, hold, stop;
    int stop_side;
} g_ax[3];  // gauge tracks: 0 right, 1 up, 2 forward

static struct {
    float until;  // the game's return marker is wanted until then
    bool shown;   // we activated it
    rf_ref go;    // ReturnToLocationMarker we activated
} g_mark;

void rf_view_move(v3 d, bool touch) {
    g_eyes.req = v3_add(g_eyes.req, d);
    if (touch) g_eyes.req_touch = v3_add(g_eyes.req_touch, d);
}

// the current location with the camera rig (the tracking space) around the eyes
typedef struct {
    void* pl;
    void* loc;
    void* rig;
    v3 rig_pos;
    quat rq;
    float scale;   // tracking space -> world
    v3 lp;         // location point (PlayerLocation transform)
    float radius;  // PlayerLocation.MaxPlayerDistanceBeforeFadeOut
} place_t;

static bool place_get(place_t* p) {
    p->pl = rf_player();
    p->loc = p->pl ? *(void**)((char*)p->pl + 0x70) : NULL;  // Player.CurrentLocation
    void* cam = rf_main_camera();
    p->rig = cam ? rf_tr_parent(rf_transform(cam)) : NULL;
    if (!rf_alive(p->loc) || !rf_alive(p->rig)) return false;
    p->rig_pos = rf_tr_pos(p->rig);
    p->rq = rf_tr_rot(p->rig);
    p->scale = CALLP(C_TR_LOSSYSCALE, fn_v3_1)(p->rig, NULL).y;
    if (p->scale < 1e-3f) p->scale = 1.0f;
    p->lp = rf_tr_pos(rf_transform(p->loc));
    p->radius = *(float*)((char*)p->loc + 0x70);
    return true;
}

static v3 ts_to_world(const place_t* p, v3 ts) { return v3_add(p->rig_pos, q_rot(p->rq, v3_scale(ts, p->scale))); }
static v3 world_to_ts(const place_t* p, v3 w) {
    return v3_scale(q_rot(q_conj(p->rq), v3_sub(w, p->rig_pos)), 1.0f / p->scale);
}
static float r_area(const place_t* p) { return fmaxf(p->radius - EDGE * p->scale, 0.0f); }

// The play area: within r_area of the location point horizontally - the game's own check - and within V_RANGE of the
// eye level. A point already outside (a location whose eye point lies off its centre) may move, but not farther out.
static v3 clamp_area(const place_t* p, v3 w, v3 from, bool* edge, bool* height) {
    float rmax = r_area(p);
    float hx = w.x - p->lp.x, hz = w.z - p->lp.z, h = sqrtf(hx * hx + hz * hz);
    float fx = from.x - p->lp.x, fz = from.z - p->lp.z;
    float lim = fmaxf(rmax, sqrtf(fx * fx + fz * fz));
    if (h > lim + 1e-5f) {
        w.x = p->lp.x + hx * lim / h;
        w.z = p->lp.z + hz * lim / h;
        *edge = true;
    }
    float vlim = fmaxf(V_RANGE * p->scale, fabsf(from.y - p->rig_pos.y)), dy = w.y - p->rig_pos.y;
    if (dy > vlim + 1e-5f || dy < -vlim - 1e-5f) {
        w.y = p->rig_pos.y + (dy > 0 ? vlim : -vlim);
        *height = true;
    }
    return w;
}

typedef struct { v3 point, normal; uint32_t face; float dist; v2 uv; int32_t col; } hit_t;  // RaycastHit, 44 bytes

static bool child_of(void* t, void* root) {
    for (int i = 0; i < 16 && rf_alive(t); i++, t = rf_tr_parent(t))
        if (t == root) return true;
    return false;
}

// the player's own colliders (head, hands, the rig), what the hand holds and the open inventory's grid
static bool own_collider(const place_t* p, void* col) {
    void* t = rf_transform(col);
    if (child_of(t, rf_transform(p->pl)) || child_of(t, p->rig)) return true;
    void* rc = rf_controller(1);
    void* held = rc ? CALLP(C_CTRL_GRABBED, void* (*)(void*, const void*))(rc, NULL) : NULL;
    if (rf_alive(held) && child_of(t, rf_transform(held))) return true;
    void* inv = rf_inventory_root();
    return inv && child_of(t, inv);
}

static bool is_head_volume(void* col) {
    static void* t_hcv;
    if (!t_hcv) t_hcv = rf_class_type(rf_find_class("TheRoomVR.dll", "", "HeadCollisionVolume"));
    void* h = t_hcv ? CALLP(C_COMP_GETCOMP, void* (*)(void*, void*, const void*))(col, t_hcv, NULL) : NULL;
    return rf_alive(h);
}

// How far the eyes can go from o along the unit direction d (at most maxd) before a wall, a solid object or one of the
// game's head-collision volumes comes within CLEAR of them: a sphere swept along the move. Other triggers (location
// capsules, puzzle volumes) do not stop the eyes; a collider the sphere already overlaps where it starts is passed
// over (a door swung into it must not lock the eyes in place).
static float sweep(const place_t* p, v3 o, v3 d, float maxd, bool* volume, char* what, size_t n) {
    void* arr = CALLP(C_PHYS_SPHERECASTALL, void* (*)(v3, float, v3, float, int32_t, int32_t, const void*))(
        o, CLEAR * p->scale, d, maxd, -1, 2 /* QueryTriggerInteraction.Collide */, NULL);
    int32_t cnt = arr ? (int32_t) * (uint64_t*)((char*)arr + 0x18) : 0;
    hit_t* hits = arr ? (hit_t*)((char*)arr + 0x20) : NULL;
    float best = maxd;
    for (int i = 0; i < cnt && i < 64; i++) {
        if (hits[i].dist <= 0 || hits[i].dist >= best) continue;
        void* col = CALLP(C_HIT_COLLIDER, void* (*)(hit_t*, const void*))(&hits[i], NULL);
        if (!rf_alive(col) || own_collider(p, col)) continue;
        bool hv = false;
        if (CALLP(C_COL_ISTRIGGER, bool (*)(void*, const void*))(col, NULL)) {
            if (!is_head_volume(col)) continue;
            hv = true;
        }
        best = hits[i].dist;
        *volume = hv;
        if (g_dev && what) rf_obj_name(rf_gameobject(col), what, n);
    }
    return best;
}

// HeadInsideGeometryHUD.UpdateHeadCollisionInTheWay for eyes at e: one of the game's layer-2 colliders (its
// head-collision volumes) between them and the location's eye level fades the view out
static bool in_the_way(const place_t* p, v3 e) {
    void* el = *(void**)((char*)p->loc + 0x60);  // PlayerLocation.EyeLevel
    if (!rf_alive(el)) return false;
    v3 d = v3_sub(rf_tr_pos(el), e);
    float len = v3_len(d);
    if (len < 1e-3f) return false;
    void* arr = ((void* (*)(v3, v3, float, int32_t, int32_t, const void*))RVA(g_call_desc[C_PHYS_RAYCASTALL].rva))(
        e, v3_scale(d, 1.0f / len), len, 1 << 2, 2 /* QueryTriggerInteraction.Collide */, NULL);
    int32_t cnt = arr ? (int32_t) * (uint64_t*)((char*)arr + 0x18) : 0;
    hit_t* hits = arr ? (hit_t*)((char*)arr + 0x20) : NULL;
    for (int i = 0; i < cnt && i < 64; i++) {
        void* col = CALLP(C_HIT_COLLIDER, void* (*)(hit_t*, const void*))(&hits[i], NULL);
        if (rf_alive(col) && !own_collider(p, col)) return true;
    }
    return false;
}

// the gauge's axes in the world: the view's horizontal right and forward, up
static void gauge_axes(const place_t* p, v3 ax[3]) {
    float yaw = g_head.yaw * DEG2RAD;
    ax[0] = q_rot(p->rq, (v3){cosf(yaw), 0, -sinf(yaw)});
    ax[1] = (v3){0, 1, 0};
    ax[2] = q_rot(p->rq, (v3){sinf(yaw), 0, cosf(yaw)});
}

// this frame's moves, kept inside the play area and clear of walls; the gauge learns which axes touch moved along and
// where a move was held back
static void apply_moves(const place_t* p, v3 ax[3], float req_ax[3], float got_ax[3]) {
    v3 req = g_eyes.req, req_t = g_eyes.req_touch;
    g_eyes.req = g_eyes.req_touch = (v3){0, 0, 0};
    v3 req_tw = q_rot(p->rq, v3_scale(req_t, p->scale));
    for (int k = 0; k < 3; k++) req_ax[k] = v3_dot(req_tw, ax[k]), got_ax[k] = 0;
    if (v3_len(req) < 1e-6f) return;
    v3 from = ts_to_world(p, g_eyes.eye);
    bool edge = false, height = false, volume = false, way = false;
    v3 to = clamp_area(p, ts_to_world(p, v3_add(g_eyes.eye, req)), from, &edge, &height);
    v3 d = v3_sub(to, from);
    float len = v3_len(d);
    char what[64] = "";
    if (len > 1e-5f) {
        v3 u = v3_scale(d, 1.0f / len);
        float skin = SKIN * p->scale, free_d = sweep(p, from, u, len + skin, &volume, what, sizeof what);
        if (free_d < len + skin) to = v3_add(from, v3_scale(u, fmaxf(free_d - skin, 0.0f)));
        if (in_the_way(p, to) && !in_the_way(p, from)) {
            to = from;
            way = true;
        }
    }
    v3 got = v3_sub(to, from);
    for (int k = 0; k < 3; k++) got_ax[k] = v3_dot(got, ax[k]);
    g_eyes.eye = world_to_ts(p, to);
    if (edge || volume || way) g_mark.until = g_eyes.now + MARK_HOLD;
    if (g_dev && (edge || height || what[0] || way)) {
        static int last;
        if (g_frame - last > 20) {
            last = g_frame;
            rf_log("view: move held back:%s%s%s%s%s", edge ? " play-area edge" : "", height ? " height" : "",
                   what[0] ? (volume ? " head volume " : " solid ") : "", what, way ? " volume in the way" : "");
        }
    }
}

// one gauge track per axis: data for HudView (any thread reads g_gauge)
static void tick_gauge(const place_t* p, const v3 ax[3], const float req_ax[3], const float got_ax[3], float dt) {
    float vals[3] = {0, 0, 0}, lo[3] = {-1, -1, -1}, hi[3] = {1, 1, 1};
    bool live = p && g_gauge_on && !rf_menu_context();
    if (p) {
        v3 e = ts_to_world(p, g_eyes.eye);
        v3 c = {e.x - p->lp.x, e.y - p->rig_pos.y, e.z - p->lp.z};  // from the centre: location point, eye level
        v3 ch = {c.x, 0, c.z};
        float rmax = r_area(p), span = fmaxf(rmax, 0.05f), hh = v3_dot(ch, ch);
        for (int k = 0; k < 3; k += 2) {  // horizontal axes: the chord of the play-area circle through the eyes
            float b = v3_dot(ch, ax[k]), half = sqrtf(fmaxf(rmax * rmax - (hh - b * b), 0.0f));
            vals[k] = b / span;
            lo[k] = -half / span;
            hi[k] = half / span;
        }
        vals[1] = c.y / (V_RANGE * p->scale);
    }
    int flags = 0;
    float out[RF_GAUGE_N];
    for (int k = 0; k < 3; k++) {
        float noise = 0.0005f * (p ? p->scale : 1.0f);
        bool moving = live && fabsf(req_ax[k]) > noise;
        g_ax[k].hold = moving ? GAUGE_HOLD : fmaxf(g_ax[k].hold - dt, 0.0f);
        float span_m = k == 1 ? V_RANGE * (p ? p->scale : 1.0f) : (p ? fmaxf(r_area(p), 0.05f) : 1.0f);
        bool far = fabsf(vals[k]) * span_m >= GAUGE_SHOW * (p ? p->scale : 1.0f);
        bool want = live && g_ax[k].hold > 0 && (far || g_ax[k].alpha > 0.5f);
        g_ax[k].alpha += want ? dt / GAUGE_IN : -dt / GAUGE_OUT;
        if (!live) g_ax[k].alpha = 0;
        g_ax[k].alpha = fminf(fmaxf(g_ax[k].alpha, 0.0f), 1.0f);
        // a stop: the move along this axis came out less than half of what the fingers asked for
        if (moving && fabsf(req_ax[k]) > 4 * noise && got_ax[k] * (req_ax[k] > 0 ? 1 : -1) < 0.5f * fabsf(req_ax[k])) {
            g_ax[k].stop = STOP_HOLD;
            g_ax[k].stop_side = req_ax[k] > 0 ? 1 : -1;
        } else {
            g_ax[k].stop = fmaxf(g_ax[k].stop - dt, 0.0f);
        }
        if (g_ax[k].alpha > 0.01f) flags |= 1 << k;
        float* o = out + 1 + k * RF_GAUGE_STRIDE;
        o[0] = g_ax[k].alpha;
        o[1] = vals[k];
        o[2] = lo[k];
        o[3] = hi[k];
        o[4] = g_ax[k].stop > 0 ? (float)g_ax[k].stop_side : 0.0f;
        o[5] = g_ax[k].stop / STOP_HOLD;
    }
    out[0] = (float)flags;
    for (int i = 0; i < RF_GAUGE_N; i++) g_gauge[i] = out[i];
}

// the game's return marker over the scene while moves press against the play area's edge (see the header)
static void tick_marker(const place_t* p) {
    char* hud = p ? *(char**)((char*)p->pl + 0xb0) : NULL;  // Player.HeadInsideHud
    void* go = rf_alive(hud) ? *(void**)(hud + 0x70) : NULL;  // HeadInsideGeometryHUD.ReturnToLocationMarker
    bool idle = rf_alive(hud) && *(int32_t*)(hud + 0x38) == 0;  // the game shows nothing of its own (FadeState)
    bool want = p && g_gauge_on && rf_alive(go) && idle && g_eyes.now < g_mark.until && !rf_menu_context();
    typedef void (*fn_setact)(void*, bool, const void*);
    if (want) {
        void* t = CALLP(C_GO_TRANSFORM, void* (*)(void*, const void*))(go, NULL);
        CALLP(C_TR_SETPOS, void (*)(void*, v3, const void*))(t, p->lp, NULL);  // where OnFadedOut puts it
        if (!g_mark.shown) {
            CALLP(C_GO_SETACTIVE, fn_setact)(go, true, NULL);
            rf_ref_set(&g_mark.go, go);
            g_mark.shown = true;
            rf_log("view: the game's play-area marker shown at the location point");
        }
    } else if (g_mark.shown) {
        void* mine = rf_ref_get(&g_mark.go);
        if (rf_alive(mine) && (!rf_alive(hud) || idle)) CALLP(C_GO_SETACTIVE, fn_setact)(mine, false, NULL);
        rf_ref_clear(&g_mark.go);
        g_mark.shown = false;
        rf_log("view: the game's play-area marker hidden");
    }
}

bool rf_view_base(v3* base) {
    void* cam = rf_main_camera();
    void* rig = cam ? rf_tr_parent(rf_transform(cam)) : NULL;
    if (!rf_alive(rig)) return false;
    *base = rf_tr_pos(rig);
    return true;
}

float rf_view_offset(void) { return g_eyes.off_m; }

int g_dev_view_trace;  // dev: frames left to log the view state every frame

void rf_view_dev_state(void) {
    rf_log("view? eyes (%.3f %.3f %.3f) from the location's eye point, %.3f m; push %.3f; gauge on %d flags %d "
           "x %.2f [%.2f %.2f] a %.2f, y %.2f a %.2f, z %.2f [%.2f %.2f] a %.2f; marker %d",
           g_eyes.eye.x, g_eyes.eye.y, g_eyes.eye.z, g_eyes.off_m, g_gyro_pz, g_gauge_on, (int)g_gauge[0], g_gauge[2],
           g_gauge[3], g_gauge[4], g_gauge[1], g_gauge[8], g_gauge[7], g_gauge[14], g_gauge[15], g_gauge[16], g_gauge[13],
           g_mark.shown);
}

void rf_view_dev_set(v3 eye) {  // dev: place the eyes (tracking space) for a check
    g_eyes.eye = eye;
}

static void tick_eyes(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    float t = (float)now.tv_sec + now.tv_nsec * 1e-9f;
    float dt = g_eyes.now > 0 ? t - g_eyes.now : 0;
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0) dt = 0;
    g_eyes.now = t;

    place_t pl_, *p = place_get(&pl_) ? &pl_ : NULL;
    void* loc = p ? p->loc : NULL;
    // a new location (teleport, a level's first one) or a new scene: the eyes go to its centre. The player missing for a
    // while (the pause menu stops it) changes nothing - after the pause the view the player had comes back (rf_menu.c)
    bool new_scene = strcmp(g_scene, g_arrive.scene) != 0, arrived = loc && loc != g_arrive.location;
    if (new_scene || arrived) {
        rf_hand_velocity_reset(0);
        rf_hand_velocity_reset(1);
        if (arrived) g_arrive.face_frames = 45;
        if (new_scene) g_arrive.level_start = true;  // a level's first location
        else if (g_arrive.location) g_arrive.level_start = false;  // a teleport
        g_arrive.location = loc;
        memcpy(g_arrive.scene, g_scene, sizeof g_arrive.scene);
        g_eyes.eye = (v3){0, 0, 0};
        if (p) {
            // the location point at eye level - the rig stands there for every location seen so far; for one standing
            // on another location's floor (PlayerLocationInverted: BaseLocation.FloorTransform) only when the rig's
            // origin lies inside its play area, else the eyes stay where the game put them
            v3 c = {p->lp.x, p->rig_pos.y, p->lp.z}, d = v3_sub(c, p->rig_pos);
            if (v3_len(d) <= r_area(p)) g_eyes.eye = world_to_ts(p, c);
            rf_log("view: at the centre of the new location (the rig's origin %.3f m from the location point)", v3_len(d));
        }
        g_eyes.req = g_eyes.req_touch = (v3){0, 0, 0};
        for (int k = 0; k < 3; k++) g_ax[k].alpha = g_ax[k].hold = g_ax[k].stop = 0;
        g_mark.until = 0;
        rf_gyro_reset_pos();
    } else if (p && g_eyes.have_rq) {
        // The game turning the tracking space - its snap turn (TrackingSpace.SnapTurnByAngle turns it about its origin
        // while the player sits) - turns the view, not the place: the eyes keep their offset from the location's eye
        // point in the scene. (The origin itself may move: a location carried along takes the eyes with it.)
        quat a = g_eyes.rq_prev, b = p->rq;
        g_eyes.eye = q_rot(q_conj(b), q_rot(a, g_eyes.eye));  // exact for any turn, slow ones included
        if (g_dev && fabsf(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) < 0.99999f && v3_len(g_eyes.eye) > 0.01f)
            rf_log("view: the tracking space turned; the eyes keep their place");
    }
    if (p) {
        g_eyes.rq_prev = p->rq;
        g_eyes.have_rq = true;
    }
    if (g_arrive.face_frames > 0) {
        g_arrive.face_frames--;
        v3 title;
        bool at_title = g_arrive.level_start && level_title(&title);
        if (at_title) rf_face_point(title);
        else face_location(loc);
        if (!g_arrive.face_frames)
            rf_log("view: arrived, facing %s (yaw %.1f pitch %.1f)", at_title ? "the level title" : "location forward",
                   g_head.yaw, g_head.pitch);
    }
    v3 ax[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    float req_ax[3] = {0, 0, 0}, got_ax[3] = {0, 0, 0};
    // a menu over the game (pause): the eyes stay where the menu opened; moves asked for meanwhile are dropped
    static bool menu_was;
    bool menu = rf_menu_context() && g_scene[0] && strcmp(g_scene, "MainMenu") && strcmp(g_scene, "Startup");
    if (g_dev && g_dev_view_trace > 0) {
        g_dev_view_trace--;
        rf_log("vt f%d menu=%d was=%d pl=%p loc=%p eye(%.3f %.3f %.3f) pos(%.3f %.3f %.3f) yaw %.1f pause=%d mf=%d", g_frame,
               menu, menu_was, p ? p->pl : NULL, loc, g_eyes.eye.x, g_eyes.eye.y, g_eyes.eye.z, g_head.pos.x, g_head.pos.y,
               g_head.pos.z, g_head.yaw, g_frame - g_pause_frame, g_frame - g_menu_frame);
    }
    bool frozen = menu_was && menu;
    menu_was = menu;
    if (menu || !p) {
        g_eyes.req = g_eyes.req_touch = (v3){0, 0, 0};
    } else {
        gauge_axes(p, ax);
        apply_moves(p, ax, req_ax, got_ax);
        g_eyes.off_m = v3_len(g_eyes.eye) * p->scale;
    }
    tick_gauge(p && !menu ? p : NULL, ax, req_ax, got_ax, dt);
    tick_marker(p);
    if (frozen || menu) return;  // the first menu frame keeps the position computed in the frame before
    g_head.pos = v3_add(g_eyes.eye, v3_scale(q_rot(rf_head_rot(), (v3){0, 0, 1}), g_gyro_pz));
}

void rf_view_tick(void) {
    tick_eyes();
    tick_fov();
}
