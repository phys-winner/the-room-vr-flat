// Flat camera: field of view for a wide phone screen and leaning in (pinch).
//
// FOV: without an XR display the centre camera renders with its own Camera.fieldOfView, authored as a VR
// value (90 vertical). On a 19.5:9 screen that is ~130 degrees across. The port shows g_fov_h degrees
// horizontally at the real aspect (hor+) and scales any FOV the game sets itself by the same factor, so
// authored zoom effects keep their relative strength.
//
// Lean: VR players lean in to look closely; the pinch gesture moves the synthetic head along the view
// direction (g_lean metres). Teleports and scene changes reset it.
#include "rf.h"

typedef float (*fn_f_1)(void*, const void*);
typedef void (*fn_set_f)(void*, float, const void*);

float g_fov_h = 100.0f;
float g_lean;

#define VR_FOV 90.0f

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
    float aspect = CALLP(C_CAM_ASPECT, fn_f_1)(cam, NULL);
    if (aspect < 0.2f) return;
    float v = 2.0f * atanf(tanf(g_fov_h * 0.5f * DEG2RAD) / aspect);  // radians, flat vertical FOV
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
} g_lean_ctx;

static void tick_lean(void) {
    void* loc = rf_alive(g_player) ? *(void**)((char*)g_player + 0x70) : NULL;  // Player.CurrentLocation
    if (loc != g_lean_ctx.location || strcmp(g_scene, g_lean_ctx.scene) != 0) {
        g_lean_ctx.location = loc;
        memcpy(g_lean_ctx.scene, g_scene, sizeof g_lean_ctx.scene);
        g_lean = 0;
    }
    if (g_lean < 0) g_lean = 0;
    if (g_lean > 0.35f) g_lean = 0.35f;
    g_head.pos = v3_scale(q_rot(rf_head_rot(), (v3){0, 0, 1}), g_lean);
}

void rf_view_tick(void) {
    tick_lean();
    tick_fov();
}
