// Touch input for the flat port.
// Java (UnityPlayerActivity.dispatchTouchEvent -> RoomFix.onTouch) posts normalised touches from the
// UI thread; the Unity main thread consumes them once per frame (rf_touch_tick) and turns them into
// synthetic HMD/controller state:
//   * one-finger drag on the scene  -> head yaw/pitch (look around);
//   * short tap                     -> the right controller is aimed through the tapped pixel and its
//                                      trigger is pulsed (menus are operated by the game's own laser).
#include <jni.h>
#include <pthread.h>
#include <string.h>

#include "rf.h"

// ---------------------------------------------------------------- event queue (UI thread -> main thread)
typedef struct {
    int action;  // 0 down, 1 up, 2 move, 3 cancel
    int id;
    float x, y;  // 0..1 of the game view, y down
    int64_t t;   // ms (MotionEvent time base)
} touch_ev;

#define QN 256
static touch_ev g_q[QN];
static unsigned g_qh, g_qt;
static pthread_mutex_t g_qmu = PTHREAD_MUTEX_INITIALIZER;

void rf_touch_post(int action, int id, float x, float y, int64_t t) {
    pthread_mutex_lock(&g_qmu);
    if (g_qt - g_qh < QN) g_q[g_qt++ % QN] = (touch_ev){action, id, x, y, t};
    pthread_mutex_unlock(&g_qmu);
}

static int pop(touch_ev* out, int max) {
    pthread_mutex_lock(&g_qmu);
    int n = 0;
    while (g_qh != g_qt && n < max) out[n++] = g_q[g_qh++ % QN];
    pthread_mutex_unlock(&g_qmu);
    return n;
}

// ---------------------------------------------------------------- Unity helpers
typedef struct { v3 o, d; } ray_t;
typedef void* (*fn_obj0)(const void*);
typedef void* (*fn_obj1)(void*, const void*);
typedef v3 (*fn_v3_1)(void*, const void*);
typedef quat (*fn_q_1)(void*, const void*);
typedef float (*fn_f_1)(void*, const void*);
typedef int32_t (*fn_i0)(const void*);
typedef ray_t (*fn_ray)(void*, v3, const void*);

static bool alive(void* obj) { return obj && *(void**)((char*)obj + 0x10) != NULL; }  // UnityEngine.Object.m_CachedPtr

static quat q_look(v3 f, v3 up) {
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

// Screen-normalised point (x right, y down) -> world ray through the main camera.
static bool screen_ray(float nx, float ny, ray_t* out, void** cam_out) {
    void* cam = CALLP(C_CAM_MAIN, fn_obj0)(NULL);
    if (!alive(cam)) return false;
    float sw = (float)CALLP(C_SCREEN_W, fn_i0)(NULL), sh = (float)CALLP(C_SCREEN_H, fn_i0)(NULL);
    *out = CALLP(C_CAM_RAY, fn_ray)(cam, (v3){nx * sw, (1.0f - ny) * sh, 0}, NULL);
    out->d = v3_norm(out->d);
    if (cam_out) *cam_out = cam;
    return true;
}

// ---------------------------------------------------------------- hand aiming
// Finds the transform whose local pose we drive for hand h (an ancestor of `t`): it is the ancestor
// whose localPosition equals the synthetic hand position (current or previous frame).
static v3 g_hand_prev_pos[2];
static void* find_anchor(void* t, int h) {
    void* p = t;
    for (int i = 0; i < 6 && alive(p); i++) {
        v3 lp = CALLP(C_TR_LOCALPOS, fn_v3_1)(p, NULL);
        if (v3_len(v3_sub(lp, g_hand[h].pos)) < 0.005f || v3_len(v3_sub(lp, g_hand_prev_pos[h])) < 0.005f) return p;
        p = CALLP(C_TR_PARENT, fn_obj1)(p, NULL);
    }
    return NULL;
}

// Sets the synthetic pose of hand h so that `rayT` (a transform rigidly attached to the hand, e.g. the
// menu laser origin) lies at world `origin` looking along world `dir`.
static bool aim_transform(int h, void* rayT, v3 origin, v3 dir) {
    if (!alive(rayT)) return false;
    void* anchor = find_anchor(rayT, h);
    if (!anchor) return false;
    void* parent = CALLP(C_TR_PARENT, fn_obj1)(anchor, NULL);
    if (!alive(parent)) return false;
    v3 ap = CALLP(C_TR_POSITION, fn_v3_1)(anchor, NULL);
    quat aq = CALLP(C_TR_ROTATION, fn_q_1)(anchor, NULL);
    v3 rp = CALLP(C_TR_POSITION, fn_v3_1)(rayT, NULL);
    quat rq = CALLP(C_TR_ROTATION, fn_q_1)(rayT, NULL);
    v3 pp = CALLP(C_TR_POSITION, fn_v3_1)(parent, NULL);
    quat pq = CALLP(C_TR_ROTATION, fn_q_1)(parent, NULL);
    // ray offset in anchor space (rigid)
    quat lq = q_mul(q_conj(aq), rq);
    v3 lp = q_rot(q_conj(aq), v3_sub(rp, ap));
    // desired anchor world pose
    quat want_rq = q_look(dir, (v3){0, 1, 0});
    quat want_aq = q_mul(want_rq, q_conj(lq));
    v3 want_ap = v3_sub(origin, q_rot(want_aq, lp));
    // to parent (tracking space) local
    g_hand[h].rot = q_mul(q_conj(pq), want_aq);
    g_hand[h].pos = q_rot(q_conj(pq), v3_sub(want_ap, pp));
    return true;
}

// ---------------------------------------------------------------- gestures
static const v3 REST_POS[2] = {{-0.2f, -0.55f, 0.25f}, {0.2f, -0.55f, 0.25f}};

typedef struct {
    bool active;
    int id;
    float x0, y0, x, y;
    int64_t t0;
    bool look;
} finger_t;
static finger_t g_f;  // primary finger

// pending click: aim through (cx, cy) for a few frames, pulse trigger, then park the hand
static struct {
    int frames;  // >0 while active
    float cx, cy;
} g_click;

static float g_look_deg_per_unit_x = 120.0f, g_look_deg_per_unit_y = 60.0f;

static void update_look_scale(void) {
    void* cam = CALLP(C_CAM_MAIN, fn_obj0)(NULL);
    if (!alive(cam)) return;
    float vfov = CALLP(C_CAM_GET_FOV, fn_f_1)(cam, NULL);
    float aspect = CALLP(C_CAM_ASPECT, fn_f_1)(cam, NULL);
    float hfov = 2.0f * atanf(tanf(vfov * 0.5f * DEG2RAD) * aspect) / DEG2RAD;
    g_look_deg_per_unit_x = hfov;
    g_look_deg_per_unit_y = vfov;
}

static void begin_click(float x, float y) {
    g_click.frames = 1;
    g_click.cx = x;
    g_click.cy = y;
    rf_log("touch: tap %.3f %.3f -> laser click", x, y);
}

// dev: persistent aim through a screen point (rf_dev "aim x y" / "rest")
static bool g_dev_aim;
static float g_dev_ax, g_dev_ay;
void rf_touch_dev_aim(bool on, float x, float y) {
    g_dev_aim = on;
    g_dev_ax = x;
    g_dev_ay = y;
    if (!on) {
        g_hand[1].pos = (v3){0.2f, -0.55f, 0.25f};
        g_hand[1].rot = (quat){0, 0, 0, 1};
    }
}

static void log_ray_state(const char* tag, v3 want_o, v3 want_d) {
    void* rayT = g_vrim && alive(g_vrim) ? *(void**)((char*)g_vrim + 0x58) : NULL;
    if (!alive(rayT)) { rf_log("%s: no rayTransform", tag); return; }
    v3 rp = CALLP(C_TR_POSITION, fn_v3_1)(rayT, NULL);
    quat rq = CALLP(C_TR_ROTATION, fn_q_1)(rayT, NULL);
    v3 rf = q_rot(rq, (v3){0, 0, 1});
    rf_log("%s: ray now pos(%.3f %.3f %.3f) fwd(%.3f %.3f %.3f) want pos(%.3f %.3f %.3f) dir(%.3f %.3f %.3f) hand(%.3f %.3f %.3f)",
           tag, rp.x, rp.y, rp.z, rf.x, rf.y, rf.z, want_o.x, want_o.y, want_o.z, want_d.x, want_d.y, want_d.z,
           g_hand[1].pos.x, g_hand[1].pos.y, g_hand[1].pos.z);
}

static void tick_dev_aim(void) {
    if (!g_dev_aim) return;
    static unsigned n;
    ray_t r;
    void* rayT = g_vrim && alive(g_vrim) ? *(void**)((char*)g_vrim + 0x58) : NULL;
    if (rayT && screen_ray(g_dev_ax, g_dev_ay, &r, NULL)) {
        v3 o = v3_sub(r.o, v3_scale(r.d, 0.30f));
        bool ok = aim_transform(1, rayT, o, r.d);
        if ((n++ % 60) == 0) {
            rf_log("aim ok=%d", ok);
            log_ray_state("aim", o, r.d);
        }
    }
}

static void tick_click(void) {
    tick_dev_aim();
    if (!g_click.frames) return;
    int f = g_click.frames++;
    ray_t r;
    void* rayT = g_vrim && alive(g_vrim) ? *(void**)((char*)g_vrim + 0x58) : NULL;
    if (f <= 14 && rayT && screen_ray(g_click.cx, g_click.cy, &r, NULL)) {
        // laser origin 30 cm behind the eye on the same ray: the beam passes exactly under the finger
        v3 o = v3_sub(r.o, v3_scale(r.d, 0.30f));
        bool ok = aim_transform(1, rayT, o, r.d);
        if (g_dev && (f == 1 || f == 4 || f == 9)) {
            rf_log("click f=%d aim=%d", f, ok);
            log_ray_state("click", o, r.d);
        }
    } else if (g_dev && f == 1) {
        rf_log("click: no rayTransform (vrim=%p)", g_vrim);
    }
    g_hand[1].trigger = (f >= 4 && f < 10) ? 1.0f : 0.0f;
    if (f > 16) {
        g_click.frames = 0;
        g_hand[1].pos = REST_POS[1];
        g_hand[1].rot = (quat){0, 0, 0, 1};
        g_hand[1].trigger = 0;
    }
}

static void on_event(const touch_ev* e) {
    if (e->action == 0) {
        if (g_f.active) return;  // single-finger controls for now
        g_f = (finger_t){.active = true, .id = e->id, .x0 = e->x, .y0 = e->y, .x = e->x, .y = e->y, .t0 = e->t};
        return;
    }
    if (!g_f.active || e->id != g_f.id) return;
    if (e->action == 2) {
        float dx = e->x - g_f.x, dy = e->y - g_f.y;
        if (!g_f.look && (fabsf(e->x - g_f.x0) > 0.012f || fabsf(e->y - g_f.y0) > 0.02f)) g_f.look = true;
        if (g_f.look) {
            g_head.yaw += dx * g_look_deg_per_unit_x;
            g_head.pitch -= dy * g_look_deg_per_unit_y;
            if (g_head.pitch > 80) g_head.pitch = 80;
            if (g_head.pitch < -80) g_head.pitch = -80;
        }
        g_f.x = e->x;
        g_f.y = e->y;
    } else if (e->action == 1 || e->action == 3) {
        if (e->action == 1 && !g_f.look && e->t - g_f.t0 < 400) begin_click(e->x, e->y);
        g_f.active = false;
    }
}

void rf_touch_tick(void) {
    g_hand_prev_pos[0] = g_hand[0].pos;
    g_hand_prev_pos[1] = g_hand[1].pos;
    update_look_scale();
    touch_ev ev[64];
    int n = pop(ev, 64);
    for (int i = 0; i < n; i++) on_event(&ev[i]);
    tick_click();
}
