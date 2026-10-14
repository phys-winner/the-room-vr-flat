// Flat camera: field of view for a wide phone screen and leaning in (pinch).
//
// FOV: without an XR display the centre camera renders with its own Camera.fieldOfView, authored as a VR
// value (90 vertical). On a 19.5:9 screen that is ~130 degrees across. The option value g_fov_h is the
// horizontal FOV of a 16:9 reference screen; the vertical FOV derived from it is kept on every screen, so
// wider phones see more to the sides and taller ones lose nothing at the top and bottom (hor+). Any FOV the
// game sets itself is scaled by the same factor, so authored zoom effects keep their relative strength.
//
// Lean: VR players lean in to look closely; the pinch gesture moves the synthetic head along the view
// direction (g_lean metres). Teleports and scene changes reset it.
#include "rf.h"

typedef float (*fn_f_1)(void*, const void*);
typedef void (*fn_set_f)(void*, float, const void*);

float g_fov_h = 90.0f;
float g_lean;
float g_sway_x, g_sway_y;  // head sway (two-finger pan), metres right / up

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
} g_lean_ctx;

typedef v3 (*fn_v3_1)(void*, const void*);

// After a teleport the camera looks along the arrival location's authored forward direction (a little down),
// the way a seated VR player faces the puzzle the location was placed for. Kept for a few frames because the
// game re-aligns the tracking space during the teleport fade.
static void face_location(void* loc) {
    void* cam = rf_main_camera();
    if (!cam || !rf_alive(loc)) return;
    v3 f = CALLP(C_TR_FORWARD, fn_v3_1)(rf_transform(loc), NULL);
    void* rig = rf_tr_parent(rf_transform(cam));
    quat r = rf_alive(rig) ? rf_tr_rot(rig) : (quat){0, 0, 0, 1};
    v3 l = q_rot(q_conj(r), f);
    if (fabsf(l.x) + fabsf(l.z) < 1e-3f) return;
    g_head.yaw = atan2f(l.x, l.z) / DEG2RAD;
    g_head.pitch = -20.0f;
}

static void tick_lean(void) {
    void* loc = rf_alive(g_player) ? *(void**)((char*)g_player + 0x70) : NULL;  // Player.CurrentLocation
    if (loc != g_lean_ctx.location || strcmp(g_scene, g_lean_ctx.scene) != 0) {
        if (loc && loc != g_lean_ctx.location) g_lean_ctx.face_frames = 45;
        g_lean_ctx.location = loc;
        memcpy(g_lean_ctx.scene, g_scene, sizeof g_lean_ctx.scene);
        g_lean = 0;
        g_sway_x = g_sway_y = 0;
    }
    if (g_lean_ctx.face_frames > 0) {
        g_lean_ctx.face_frames--;
        face_location(loc);
        if (!g_lean_ctx.face_frames) rf_log("view: arrived, facing location forward (yaw %.1f)", g_head.yaw);
    }
    if (g_lean < 0) g_lean = 0;
    if (g_lean > 0.35f) g_lean = 0.35f;
    if (g_sway_x < -0.3f) g_sway_x = -0.3f;
    if (g_sway_x > 0.3f) g_sway_x = 0.3f;
    if (g_sway_y < -0.2f) g_sway_y = -0.2f;
    if (g_sway_y > 0.2f) g_sway_y = 0.2f;
    float yaw = g_head.yaw * DEG2RAD;
    v3 right = {cosf(yaw), 0, -sinf(yaw)};  // horizontal, follows the head's yaw only
    g_head.pos = v3_add(v3_scale(q_rot(rf_head_rot(), (v3){0, 0, 1}), g_lean),
                        v3_add(v3_scale(right, g_sway_x), (v3){0, g_sway_y, 0}));
}

void rf_view_tick(void) {
    tick_lean();
    tick_fov();
}
