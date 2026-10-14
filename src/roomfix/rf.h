// Shared declarations for libroomfix.
#pragma once
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
    void* (*class_get_parent)(void* klass);
} rf_il2cpp_api_t;
extern rf_il2cpp_api_t g_il2;
const char* rf_class_name(void* obj);  // "?" when unknown

// ---- live game objects captured by hooks ----
extern void* g_vrpm;  // VrPlatformManager singleton (captured in its Update)
extern void* g_vrim;  // VrInputModule (captured in Process)
extern void* g_player;  // gameplay Player (captured in Update)
extern int g_frame, g_player_frame, g_vrim_frame;  // frame counter of our tick / last capture
extern char g_scene[64];  // active scene name (updated every 30 frames)
extern int g_pause_frame;  // last frame the pause menu controller updated
static inline bool rf_menu_context(void) {
    return g_scene[0] == 0 || !strcmp(g_scene, "MainMenu") || !strcmp(g_scene, "Startup") || g_frame - g_pause_frame <= 2;
}

// ---- IL2CPP containers ----
static inline int32_t rf_list_size(void* list) { return list ? *(int32_t*)((char*)list + 0x18) : 0; }
static inline void* rf_list_get(void* list, int32_t i) {  // List<T> of reference types
    void* items = *(void**)((char*)list + 0x10);
    return items ? ((void**)((char*)items + 0x20))[i] : NULL;
}
static inline bool rf_alive(void* obj) { return obj && *(void**)((char*)obj + 0x10) != NULL; }  // UnityEngine.Object.m_CachedPtr

// ---- rf_unity.c: Unity helpers (main thread) ----
v3 rf_tr_pos(void* t);
quat rf_tr_rot(void* t);
v3 rf_tr_localpos(void* t);
void* rf_tr_parent(void* t);
void* rf_transform(void* component);
void* rf_gameobject(void* component);
v3 rf_tr_dir(void* t, v3 local_dir);
void rf_obj_name(void* obj, char* out, size_t n);
void* rf_main_camera(void);
bool rf_screen_ray(float nx, float ny, v3* o, v3* d);
bool rf_world_to_screen(v3 p, float* nx, float* ny);
quat rf_q_look(v3 f, v3 up);
void* rf_controller(int h);  // Player.LeftHand (0) / RightHand (1)
bool rf_place_transform(int h, void* T, v3 pos, quat rot);
bool rf_aim_transform(int h, void* T, v3 origin, v3 dir);
void rf_face_point(v3 world);   // turn the synthetic head to look at a world point
void rf_hand_rest(int h);        // hand h goes back to rest and follows the head yaw from now on
void rf_hand_rest_apply(void);   // per frame: resting hands follow the head yaw
extern bool g_hand_resting[2];
bool rf_class_is(void* obj, const char* name);  // obj's class or any base class has this name
extern v3 g_hand_prev_pos[2];

// ---- rf_interact.c: game actions driven by touch ----
enum { RF_GRAB_NONE = 0, RF_GRAB_OK = 1, RF_GRAB_FAR = 2, RF_GRAB_NOPLAYER = 3 };
void rf_act_laser_click(float nx, float ny);
int rf_act_grab_begin(float nx, float ny);
void rf_act_probe(float nx, float ny);  // dev diagnostics
void rf_act_list_volumes(void);         // dev diagnostics
void rf_gc_add(void* gc);
void rf_gc_remove(void* gc);
void rf_act_grab_drag(float nx, float ny);
void rf_act_grab_end(void);
bool rf_act_grabbing(void);
void rf_act_grab_depth(float depth);
void rf_act_teleport_mode(bool on);
bool rf_act_teleport_mode_on(void);
bool rf_act_teleport_tap(float nx, float ny);
void rf_act_snap_turn(int dir);  // -1 left, +1 right
void rf_act_button(int h, int which, int frames);  // pulse a controller button: 0 primary, 1 secondary, 2 menu
void rf_interact_tick(void);
void rf_act_dev_aim_tip(bool on, float x, float y, float depth);

// ---- rf_hud.c: state for the Java HUD overlay, HUD button actions ----
enum {
    HUD_VISIBLE = 1 << 0,      // gameplay: overlay shown
    HUD_TELEPORT = 1 << 1,     // Player.TeleportInput allowed
    HUD_TELEPORT_ON = 1 << 2,  // teleport mode active (tap a location marker)
    HUD_INVENTORY = 1 << 3,    // Player.InventoryInput allowed
    HUD_INV_OPEN = 1 << 4,     // HudManager.Showing
    HUD_GOGGLES = 1 << 5,      // Player.GogglesInput allowed
    HUD_GOGGLES_ON = 1 << 6,   // Goggles.GogglesActive
    HUD_SNAP = 1 << 7,         // tutorial asks for a snap turn
    HUD_MENU = 1 << 8,         // main menu / pause menu (taps click the menu)
};
enum { HB_TELEPORT = 1, HB_INVENTORY, HB_GOGGLES, HB_PAUSE, HB_TURN_L, HB_TURN_R, HB_BACK };
extern volatile int g_hud_state;
extern int g_tut_action;  // RwAction the current tutorial box asks for (-1 none)
void rf_hud_tick(void);
void rf_hud_button(int id);
bool rf_inventory_open(void);
bool rf_goggles_on(void);

// ---- rf_menu.c: VR setup wizards answered with flat defaults ----
void* h_posture_setup(void* self, const void* mi);
void h_menu_update(void* self, const void* mi);
void* rf_menu_controller(void);  // MenuController active this frame, or NULL
void rf_menu_tick(void);

// ---- rf_view.c: flat camera (FOV, lean) ----
extern float g_lean;      // metres the head leans along the view direction (pinch gesture)
extern float g_fov_h;     // horizontal field of view on screen, degrees
void rf_view_tick(void);

// ---- module entry points ----
void rf_xr_init(void);      // XR device emulation (head)
void rf_dev_tick(void);     // command file polling (dev builds)
void rf_frame_tick(void);   // called once per frame on the Unity main thread
void rf_touch_post(int action, int id, float x, float y, int64_t t);  // any thread
void rf_touch_tick(void);   // main thread
