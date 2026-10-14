// Unity / game helpers shared by the touch and interaction modules (main thread only).
#include <stdio.h>

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
    if (!rf_alive(g_player) || g_frame - g_player_frame > 2) return NULL;
    void* c = *(void**)((char*)g_player + (h ? 0x40 : 0x38));  // Player.RightHand / LeftHand (Controller)
    return rf_alive(c) ? c : NULL;
}

// The transform whose local pose we drive for hand h is the ancestor of `t` whose localPosition equals
// the synthetic hand position (current or previous frame).
v3 g_hand_prev_pos[2];
static void* find_anchor(void* t, int h) {
    void* p = t;
    for (int i = 0; i < 8 && rf_alive(p); i++) {
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
    v3 ap = rf_tr_pos(anchor), rp = rf_tr_pos(T), pp = rf_tr_pos(parent);
    quat aq = rf_tr_rot(anchor), rq = rf_tr_rot(T), pq = rf_tr_rot(parent);
    quat lq = q_mul(q_conj(aq), rq);               // T relative to the anchor (rigid)
    v3 lp = q_rot(q_conj(aq), v3_sub(rp, ap));
    quat want_aq = q_mul(rot, q_conj(lq));
    v3 want_ap = v3_sub(pos, q_rot(want_aq, lp));
    g_hand[h].rot = q_mul(q_conj(pq), want_aq);
    g_hand[h].pos = q_rot(q_conj(pq), v3_sub(want_ap, pp));
    return true;
}

bool rf_aim_transform(int h, void* T, v3 origin, v3 dir) { return rf_place_transform(h, T, origin, rf_q_look(dir, (v3){0, 1, 0})); }

const v3 RF_REST_POS[2] = {{-0.2f, -0.55f, 0.25f}, {0.2f, -0.55f, 0.25f}};

void rf_hand_rest(int h) {
    g_hand[h].pos = RF_REST_POS[h];
    g_hand[h].rot = (quat){0, 0, 0, 1};
    g_hand[h].vel = (v3){0, 0, 0};
}
