// Shared declarations for libroomfix.
#pragma once
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gen_symbols.h"

typedef struct { float x, y; } v2;
typedef struct { float x, y, z; } v3;
typedef struct { float x, y, z, w; } quat;

typedef struct Il2CppString {
    void* klass;
    void* monitor;
    int32_t length;
    uint16_t chars[];
} Il2CppString;

extern uintptr_t g_il2cpp_base;
extern char g_ext_dir[512];
extern bool g_dev;

void rf_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#define RVA(x) ((void*)(g_il2cpp_base + (x)))
#define CALLP(id, type) ((type)RVA(g_call_desc[id].rva))
// Original code of an installed hook (NULL when the hook is a full replacement or failed).
extern void* g_orig[H_COUNT];
extern bool g_hook_ok[H_COUNT];
extern bool g_call_ok[C_COUNT];
#define ORIG(id, type) ((type)g_orig[id])

bool rf_str_eq(const Il2CppString* s, const char* ascii);
void rf_str_utf8(const Il2CppString* s, char* out, size_t n);

// ---- math (Unity conventions: left-handed, +Y up, +Z forward) ----
static inline v3 v3_add(v3 a, v3 b) { return (v3){a.x + b.x, a.y + b.y, a.z + b.z}; }
static inline v3 v3_sub(v3 a, v3 b) { return (v3){a.x - b.x, a.y - b.y, a.z - b.z}; }
static inline v3 v3_scale(v3 a, float s) { return (v3){a.x * s, a.y * s, a.z * s}; }
static inline float v3_dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline float v3_len(v3 a) { return sqrtf(v3_dot(a, a)); }
static inline v3 v3_norm(v3 a) { float l = v3_len(a); return l > 1e-6f ? v3_scale(a, 1.0f / l) : (v3){0, 0, 1}; }
static inline v3 v3_cross(v3 a, v3 b) { return (v3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

static inline quat q_mul(quat a, quat b) {
    return (quat){a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                  a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                  a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                  a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
static inline quat q_axis(v3 axis, float rad) {
    float s = sinf(rad * 0.5f);
    return (quat){axis.x * s, axis.y * s, axis.z * s, cosf(rad * 0.5f)};
}
static inline quat q_conj(quat q) { return (quat){-q.x, -q.y, -q.z, q.w}; }
static inline v3 q_rot(quat q, v3 v) {
    v3 u = {q.x, q.y, q.z};
    v3 t = v3_scale(v3_cross(u, v), 2.0f);
    return v3_add(v3_add(v, v3_scale(t, q.w)), v3_cross(u, t));
}
#define DEG2RAD 0.017453292519943295f
// yaw: degrees clockwise seen from above (+Y), pitch: degrees, positive looks up.
static inline quat q_yaw_pitch(float yaw, float pitch) {
    return q_mul(q_axis((v3){0, 1, 0}, yaw * DEG2RAD), q_axis((v3){1, 0, 0}, -pitch * DEG2RAD));
}

// ---- head (synthetic HMD), tracking-space coordinates ----
typedef struct {
    float yaw, pitch;  // degrees
    v3 pos;            // centre eye, metres, tracking space
    float ipd;         // metres
} rf_head_t;
extern rf_head_t g_head;

// ---- hands (synthetic controllers), tracking-space coordinates; [0] = left, [1] = right ----
typedef struct {
    v3 pos;
    quat rot;
    v3 vel;
    float trigger, grip;  // 0..1
    v2 stick;
    bool btn_primary, btn_secondary, btn_menu, stick_click;
} rf_hand_t;
extern rf_hand_t g_hand[2];
static inline quat rf_head_rot(void) { return q_yaw_pitch(g_head.yaw, g_head.pitch); }

// ---- hook tables contributed by modules ----
struct rf_hook_entry {
    int id;
    void* repl;
    bool want_orig;
    bool dev_only;
};
extern const struct rf_hook_entry g_xr_hooks[];
extern const size_t g_xr_hook_count;

// ---- il2cpp exported API (resolved with dlsym from libil2cpp.so) ----
typedef struct {
    void* (*object_get_class)(void* obj);
    const char* (*class_get_name)(void* klass);
    const char* (*class_get_namespace)(void* klass);
} rf_il2cpp_api_t;
extern rf_il2cpp_api_t g_il2;
const char* rf_class_name(void* obj);  // "?" when unknown

// ---- live game objects captured by hooks ----
extern void* g_vrpm;  // VrPlatformManager singleton (captured in its Update)
extern void* g_vrim;  // VrInputModule (captured in Process)

// ---- module entry points ----
void rf_xr_init(void);      // XR device emulation (head)
void rf_dev_tick(void);     // command file polling (dev builds)
void rf_frame_tick(void);   // called once per frame on the Unity main thread
void rf_touch_post(int action, int id, float x, float y, int64_t t);  // any thread
void rf_touch_tick(void);   // main thread
void rf_touch_dev_aim(bool on, float x, float y);
