// Unity / game helpers shared by the touch and interaction modules (main thread only).
#include <stdio.h>
#include <time.h>

#include "rf.h"

typedef void* (*fn_obj0)(const void*);
typedef void* (*fn_obj1)(void*, const void*);
typedef v3 (*fn_v3_1)(void*, const void*);
typedef v3 (*fn_v3_v3)(void*, v3, const void*);
typedef quat (*fn_q_1)(void*, const void*);
typedef float (*fn_f_1)(void*, const void*);
typedef int32_t (*fn_i0)(const void*);
typedef struct { v3 o, d; } ray_t;
typedef ray_t (*fn_ray)(void*, v3, const void*);

v3 rf_tr_pos(void* t) { return CALLP(C_TR_POSITION, fn_v3_1)(t, NULL); }
quat rf_tr_rot(void* t) { return CALLP(C_TR_ROTATION, fn_q_1)(t, NULL); }
v3 rf_tr_localpos(void* t) { return CALLP(C_TR_LOCALPOS, fn_v3_1)(t, NULL); }
void* rf_tr_parent(void* t) { return CALLP(C_TR_PARENT, fn_obj1)(t, NULL); }
void* rf_transform(void* component) { return CALLP(C_COMP_TRANSFORM, fn_obj1)(component, NULL); }
void* rf_gameobject(void* component) { return CALLP(C_COMP_GO, fn_obj1)(component, NULL); }
v3 rf_tr_dir(void* t, v3 local_dir) { return CALLP(C_TR_TRANSDIR, fn_v3_v3)(t, local_dir, NULL); }

void rf_obj_name(void* obj, char* out, size_t n) {
    out[0] = 0;
    if (!rf_alive(obj)) { snprintf(out, n, "(dead)"); return; }
    rf_str_utf8((Il2CppString*)CALLP(C_OBJ_NAME, fn_obj1)(obj, NULL), out, n);
}

void* rf_main_camera(void) {
    void* cam = CALLP(C_CAM_MAIN, fn_obj0)(NULL);
    return rf_alive(cam) ? cam : NULL;
}

bool rf_screen_ray(float nx, float ny, v3* o, v3* d) {
    void* cam = rf_main_camera();
    if (!cam) return false;
    float sw = (float)CALLP(C_SCREEN_W, fn_i0)(NULL), sh = (float)CALLP(C_SCREEN_H, fn_i0)(NULL);
    ray_t r = CALLP(C_CAM_RAY, fn_ray)(cam, (v3){nx * sw, (1.0f - ny) * sh, 0}, NULL);
    *o = r.o;
    *d = v3_norm(r.d);
    return true;
}

// world point -> normalised screen (x right, y down); returns false when behind the camera
bool rf_world_to_screen(v3 p, float* nx, float* ny) {
    void* cam = rf_main_camera();
    if (!cam) return false;
    v3 s = CALLP(C_CAM_W2S, fn_v3_v3)(cam, p, NULL);
    float sw = (float)CALLP(C_SCREEN_W, fn_i0)(NULL), sh = (float)CALLP(C_SCREEN_H, fn_i0)(NULL);
    *nx = s.x / sw;
    *ny = 1.0f - s.y / sh;
    return s.z > 0.01f;
}

quat rf_q_look(v3 f, v3 up) {
    f = v3_norm(f);
    v3 r = v3_cross(up, f);
    if (v3_len(r) < 1e-4f) r = v3_cross((v3){0, 0, 1}, f);
    r = v3_norm(r);
    v3 u = v3_cross(f, r);
    float m00 = r.x, m01 = u.x, m02 = f.x, m10 = r.y, m11 = u.y, m12 = f.y, m20 = r.z, m21 = u.z, m22 = f.z;
    float tr = m00 + m11 + m22;
    quat q;
    if (tr > 0) {
        float s = sqrtf(tr + 1.0f) * 2;
        q = (quat){(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        float s = sqrtf(1.0f + m00 - m11 - m22) * 2;
        q = (quat){0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        float s = sqrtf(1.0f + m11 - m00 - m22) * 2;
        q = (quat){(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        float s = sqrtf(1.0f + m22 - m00 - m11) * 2;
        q = (quat){(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    return q;
}

void* rf_controller(int h) {
    if (!rf_player()) return NULL;
    void* c = *(void**)((char*)g_player + (h ? 0x40 : 0x38));  // Player.RightHand / LeftHand (Controller)
    return rf_alive(c) ? c : NULL;
}

// The transform whose local pose we drive for hand h is the ancestor of `t` whose localPosition equals
// the synthetic hand position (current or previous frame).
v3 g_hand_prev_pos[2];
static void* find_anchor(void* t, int h) {
    void* p = t;
    for (int i = 0; i < 16 && rf_alive(p); i++) {  // the animated fingertip is nine parents below its tracker
        v3 lp = rf_tr_localpos(p);
        if (v3_len(v3_sub(lp, g_hand[h].pos)) < 0.005f || v3_len(v3_sub(lp, g_hand_prev_pos[h])) < 0.005f) return p;
        p = rf_tr_parent(p);
    }
    return NULL;
}

// Sets the synthetic pose of hand h so that transform T (rigidly attached to the hand: menu laser,
// teleporter, grabber, fingertip) gets world pose (pos, rot).
bool rf_place_transform(int h, void* T, v3 pos, quat rot) {
    if (!rf_alive(T)) return false;
    void* anchor = find_anchor(T, h);
    if (!anchor) return false;
    void* parent = rf_tr_parent(anchor);
    if (!rf_alive(parent)) return false;
    v3 ap = rf_tr_pos(anchor), rp = rf_tr_pos(T);
    quat aq = rf_tr_rot(anchor), rq = rf_tr_rot(T), pq = rf_tr_rot(parent);
    quat lq = q_mul(q_conj(aq), rq);               // T relative to the anchor (rigid)
    v3 lp = q_rot(q_conj(aq), v3_sub(rp, ap));
    quat want_aq = q_mul(rot, q_conj(lq));
    v3 want_ap = v3_sub(pos, q_rot(want_aq, lp));
    g_hand[h].rot = q_mul(q_conj(pq), want_aq);
    // Superzoom scales the tracking space; XR positions must stay in that parent's local coordinates.
    g_hand[h].pos = CALLP(C_TR_INVPOINT, fn_v3_v3)(parent, want_ap, NULL);
    g_hand_resting[h] = false;
    return true;
}

bool rf_aim_transform(int h, void* T, v3 origin, v3 dir) { return rf_place_transform(h, T, origin, rf_q_look(dir, (v3){0, 1, 0})); }

// Rest pose: hands lowered beside and slightly behind the seated body, outside the view at any pitch
// (|pitch| <= 80), turning with the head yaw like a body that faces where the player looks.
static const v3 REST_OFF[2] = {{-0.2f, -0.5f, -0.3f}, {0.2f, -0.5f, -0.3f}};
bool g_hand_resting[2] = {true, true};
static bool g_hand_velocity_active[2];

static void rest_pose(int h) {
    quat yaw = q_axis((v3){0, 1, 0}, g_head.yaw * DEG2RAD);
    g_hand[h].pos = v3_add(g_head.pos, q_rot(yaw, REST_OFF[h]));
    // in menus the resting controllers point at the floor: the menu laser only appears when a controller
    // points towards the menu (MenuController.DotToleranceToStartInputLine)
    g_hand[h].rot = rf_menu_context() ? q_mul(yaw, q_axis((v3){1, 0, 0}, 80.0f * DEG2RAD)) : yaw;
    g_hand[h].vel = (v3){0, 0, 0};
}

void rf_face_point(v3 p) {
    void* cam = rf_main_camera();
    if (!cam) return;
    void* ct = rf_transform(cam);
    void* rig = rf_tr_parent(ct);  // the head pose is local to the camera's parent
    quat r = rf_alive(rig) ? rf_tr_rot(rig) : (quat){0, 0, 0, 1};
    v3 l = q_rot(q_conj(r), v3_sub(p, rf_tr_pos(ct)));
    if (v3_len(l) < 1e-3f) return;
    g_head.yaw = atan2f(l.x, l.z) / DEG2RAD;
    g_head.pitch = atan2f(l.y, sqrtf(l.x * l.x + l.z * l.z)) / DEG2RAD;
    if (g_head.pitch > 80) g_head.pitch = 80;
    if (g_head.pitch < -80) g_head.pitch = -80;
}

void rf_hand_velocity_reset(int h) {
    g_hand_velocity_active[h] = false;
    g_hand[h].vel = (v3){0, 0, 0};
}

void rf_hand_rest(int h) {
    g_hand_resting[h] = true;
    rf_hand_velocity_reset(h);
    rest_pose(h);
}

void rf_hand_rest_apply(void) {
    for (int h = 0; h < 2; h++)
        if (g_hand_resting[h]) rest_pose(h);
}

static v3 hand_velocity_step(v3 previous, v3 delta, float dt) {
    float alpha = 1 - expf(-dt / .05f);
    return v3_add(previous, v3_scale(v3_sub(v3_scale(delta, 1 / dt), previous), alpha));
}

// Touch samples leave gaps between frames; raw zero/spike velocities defeat native acceleration limits.
void rf_hand_velocity_tick(void) {
    static int64_t last;
    static void* player;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t now = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    float dt = (float)(now - last) * 1e-9f;
    void* current = rf_player();
    bool valid = last && dt >= 0.001f && dt <= 0.1f && current && current == player && !rf_menu_context();
    for (int h = 0; h < 2; h++) {
        bool moving = current && !g_hand_resting[h] && !rf_menu_context();
        g_hand[h].vel = valid && moving && g_hand_velocity_active[h]
                           ? hand_velocity_step(g_hand[h].vel, v3_sub(g_hand[h].pos, g_hand_prev_pos[h]), dt)
                           : (v3){0, 0, 0};
        g_hand_velocity_active[h] = moving;
    }
    last = now;
    player = current;
}
