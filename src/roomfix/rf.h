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

// Release builds (build_port.py --release, -DRF_RELEASE=1) write no log at all: rf_log compiles to nothing, there is
// no roomfix.log and no developer console, every g_dev branch is dead code, and the process is muted in logcat.
#ifndef RF_RELEASE
#define RF_RELEASE 0
#endif
#if RF_RELEASE
#define g_dev false
static inline __attribute__((format(printf, 1, 2))) void rf_log(const char* fmt, ...) { (void)fmt; }
#else
extern bool g_dev;
void rf_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#endif

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
    void* (*string_new)(const char* utf8);       // System.String from UTF-8
    void* (*class_get_type)(void* klass);        // Il2CppType*
    void* (*type_get_object)(void* type);        // System.Type object
    void* (*domain_get)(void);
    void** (*domain_get_assemblies)(void* domain, size_t* count);
    void* (*assembly_get_image)(void* assembly);
    const char* (*image_get_name)(void* image);
    void* (*class_from_name)(void* image, const char* ns, const char* name);
    void* (*resolve_icall)(const char* name);  // engine internal call by "Namespace.Class::method"
    void* (*object_new)(void* klass);          // managed object of a class (constructor not run)
    uint32_t (*gchandle_new_weakref)(void* obj, bool track_resurrection);
    void* (*gchandle_get_target)(uint32_t handle);
    void (*gchandle_free)(uint32_t handle);
} rf_il2cpp_api_t;
void* rf_find_class(const char* image, const char* ns, const char* name);  // e.g. "UnityEngine.CoreModule.dll"
void* rf_class_type(void* klass);                                          // System.Type of a class
extern rf_il2cpp_api_t g_il2;
const char* rf_class_name(void* obj);  // "?" when unknown

// ---- live game objects captured by hooks ----
extern void* g_vrpm;  // VrPlatformManager singleton (captured in its Update)
extern void* g_vrim;  // VrInputModule (captured in Process)
extern void* g_player;  // gameplay Player (captured in Update)
extern int g_frame, g_player_frame, g_vrim_frame;  // frame counter of our tick / last capture
extern char g_scene[64];  // active scene name (updated every 30 frames)
extern int g_pause_frame;  // last frame the pause menu controller updated
// The Player updated in the last frames, else NULL. The frame check comes first: after a scene unload the pointer is
// stale and its memory may already hold another object (R027 crash on realme-rmx3371: Component.get_transform on garbage).
static inline void* rf_player(void) {
    return g_player && g_frame - g_player_frame <= 2 && *(void**)((char*)g_player + 0x10) ? g_player : NULL;
}
extern int g_menu_frame;  // last frame a MenuController updated (front end, pause, end-of-game screen)
static inline bool rf_menu_context(void) {
    return g_scene[0] == 0 || !strcmp(g_scene, "MainMenu") || !strcmp(g_scene, "Startup") || g_frame - g_pause_frame <= 2 ||
           g_frame - g_menu_frame <= 2;
}

// ---- weak references to managed objects kept across frames ----
// A scene change destroys objects; the GC may then free their wrappers and give the same address to new objects (seen
// on realme-rmx3371: a new MainMenuScreen at the old screen's address), so a raw pointer kept from an earlier frame can be
// neither dereferenced nor compared. A weak GC handle yields the object while it lives and NULL once it is collected.
typedef struct { uint32_t h; } rf_ref;
void rf_ref_set(rf_ref* r, void* obj);  // obj may be NULL
void* rf_ref_get(const rf_ref* r);      // NULL when unset or collected (a destroyed Unity object: check rf_alive)
void rf_ref_clear(rf_ref* r);

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
void rf_act_list_volumes_all(void);     // dev: every registered volume with usability and distance
void rf_gc_add(void* gc);
void rf_gc_remove(void* gc);
void rf_act_grab_drag(float nx, float ny);
void rf_act_grab_end(void);
bool rf_act_grabbing(void);
bool rf_act_grab_can_roll(void);  // a carried object can be turned in the hand
void rf_act_grab_turn(float x0, float y0, float x1, float y1);  // second finger moved (ball radii from the first)
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
void* rf_inventory_root(void);  // the open inventory grid (HudManager.LayoutParent), else NULL
bool rf_goggles_on(void);
void h_inv_show_xform(void* self, const void* mi);
void h_inv_show(void* self, const void* mi);

// ---- rf_menu.c: VR setup wizards answered with flat defaults ----
void* h_posture_setup(void* self, const void* mi);
void h_menu_update(void* self, const void* mi);
void* rf_menu_controller(void);  // MenuController active this frame, or NULL
bool rf_menu_back(void);         // Back inside the active menu; false at its root
void rf_menu_tick(void);

// ---- rf_settings.c: port settings (FPS limit, field of view), <ext>/port_settings.cfg ----
enum { RF_FPS_30 = 0, RF_FPS_45, RF_FPS_60, RF_FPS_DISPLAY, RF_FPS_COUNT };
#define RF_FOV_COUNT 5
extern int g_fps_idx, g_fov_idx;
extern volatile float g_display_hz;    // current display refresh rate (from Java)
extern volatile float g_display_max_hz;  // highest refresh rate of the display at this resolution
int rf_fps_value(int idx);              // frames per second for an FPS-limit option
int rf_fov_value(int idx);              // horizontal degrees for a field-of-view option
void rf_settings_load(void);
void rf_settings_changed(void);         // apply + save
void rf_settings_save(void);
void rf_settings_tick(void);            // main thread: keep Application.targetFrameRate on the chosen limit
void* rf_type_of(void* obj);            // System.Type of an object's class

// ---- rf_options.c / rf_loc.c ----
void* h_options_setup(void* self, const void* mi);
// stage 8 (rf_chapters.c): chapter select, replay without saving, QA unlocks
#define QA_CHAPTERS 1  // every chapter in the level select
#define QA_SKIP 2      // pause menu SKIP LEVEL without the secret
#define QA_COUNT 4     // off, level select, skip level, all
extern int g_qa;
void* h_mainmenu_setup(void* self, const void* mi);
void h_button_press(void* self, const void* mi);
void h_gsm_level_complete(void* self, const void* mi);
void h_gsm_skip_level(void* self, const void* mi);
void h_gsm_story_load(void* self, void* level, const void* mi);
void* h_pause_setup(void* self, const void* mi);
void* h_loadscene_name(void* level, const void* mi);
void rf_chapters_scene(const char* scene);  // scene change: a replay ends in the front end
// stage 8 (rf_help.c): gesture card requests for the Java overlay (HelpCard.java)
extern int g_help_seen;          // help= in port_settings.cfg: the card was shown once
int rf_help_take(void);          // pending request (language index + 1) or 0; clears it (any thread)
void rf_help_first_menu(void);   // main menu: the card once per installation
bool rf_help_press(void* btn);   // Options "view controls": the card instead of the VR controller map
void h_uiopt_left(void* self, const void* mi);
void h_uiopt_right(void* self, const void* mi);
void h_uiopt_update(void* self, const void* mi);
void* h_loc_getstring(void* key, void* ctx, const void* mi);
int rf_lang(void);                      // index into RF_LANG_NAMES of the game's current language
extern char g_app_locale[64];
void rf_loc_app_locale(const char* tags);  // from Java: LocaleManager.getApplicationLocales() (API 33+)
void h_loc_load_initial(const void* mi);

// ---- rf_view.c: flat camera (FOV, the eyes inside the location) ----
extern float g_fov_h;     // horizontal field of view on screen, degrees
void rf_view_tick(void);

// ---- round 2 (operator 2026-10-16) ----
// rf_menu.c: every item of the open menu screen in view
extern float g_menu_vfov;  // vertical FOV (degrees) the open menu needs; 0 outside menus
// rf_flatui.c: head-following texts pinned flat to the screen; menu laser hidden
enum { RF_FLAT_BOTTOM = 0, RF_FLAT_TOP = 1, RF_FLAT_CENTRE = 2, RF_FLAT_KEEP = -1 };
void rf_flatui_add(void* comp, int slot);
void rf_dev_flats(void);  // dev: pinned flat elements  // element = comp's transform; RF_FLAT_KEEP keeps a known slot
void rf_flatui_pin_all(bool tick);         // tick: main tick (may re-measure); false: camera pose callback
void h_siv_update(void* self, const void* mi);
void h_fep_setlength(void* self, float len, const void* mi);
void rf_act_dev_beam(bool on, float x, float y);  // dev: hold the menu pointer from the lower right at a screen point
void rf_dev_beam_watch(void);   // dev: count frames with an enabled menu beam renderer (every frame tick)
void rf_dev_beam_report(void);  // dev: "beam?" answer
// rf_gyro.c: gyroscope options (eye position / tilt and turn, each with a sensitivity)
#define RF_GYRO_SENS_COUNT 7
extern int g_gyro_pos, g_gyro_rot, g_gyro_pos_sens, g_gyro_rot_sens;
extern float g_gyro_pz;  // passing eye offset from pushing the phone, metres along the view
int rf_gyro_sens_pct(int idx);
void rf_gyro_post(float dx, float dy, float dz, float ux, float uy, float uz);  // any thread (Java sensor thread)
void rf_accel_post(float az, float dt);  // linear acceleration along the screen normal (towards the player), any thread
int rf_gyro_wanted(void);                // bit 0: gyroscope + gravity, bit 1: linear acceleration
void rf_gyro_reset_pos(void);
void rf_gyro_tick(void);
// rf_options.c: the Options page with the gyroscope rows
bool rf_options_press(void* btn);  // a port button of Options was pressed (handled)
bool rf_options_back(void);        // Back on the gyroscope page: back to the options (handled)

// ---- round 4 (operator 2026-10-16) ----
bool rf_view_base(v3* base);  // rf_view.c: the location's eye point (tracking-space origin), world
// rf_flatui.c: texts sized by the display's density (Java DisplayMetrics) and the game view's height in pixels
extern volatile float g_disp_ydpi;
extern volatile int g_disp_h;
void rf_dev_texts(const char* filter);  // dev: TextMeshPro texts with where they are
void rf_dev_slotfaces(void);            // dev: inventory items' display pose against the eyes

// ---- round 5 (operator 2026-10-17): the eyes stay put when the view turns, teleports go to the centre ----
// rf_view.c: touch and the gyroscope move the eyes inside the location (its play area); the position gauge
void rf_view_move(v3 d, bool touch);  // move the eyes by d (tracking space) this frame; touch moves show the gauge
float rf_view_offset(void);           // eyes' distance from the location's eye point, world metres
extern int g_gauge_on;                // Options: position indicator (gauge= in port_settings.cfg)
#define RF_GAUGE_STRIDE 6             // per axis (right, up, forward): alpha, eyes, reach low, reach high, stop side, stop
#define RF_GAUGE_N (1 + 3 * RF_GAUGE_STRIDE)  // [0]: bit per drawn axis
extern volatile float g_gauge[RF_GAUGE_N];
void rf_view_dev_state(void);         // dev: view?
void rf_view_dev_set(v3 eye);         // dev: place the eyes (tracking space)

// ---- round 6 (operator 2026-10-17): how far a carried item is held ----
// rf_interact.c: while an item is carried, a slider under the pause button (HudView) sets its distance from the eyes -
// the VR player's reach (the painting puzzles: the Sun is carried through stars standing at different depths)
#define RF_HOLD_N 4                    // [0] slider shown, [1] distance (m), [2] near end, [3] far end of the slider
extern volatile float g_hold[RF_HOLD_N];
void rf_act_hold_depth(float metres);  // the slider moved (main thread: touch queue action 12, x = metres)

// ---- rf_scene.c: scene inspection (dev), volumetric leftovers ----
void rf_dev_comps(const char* name);
void rf_dev_orphans(void);
void rf_dev_rlist(const char* filter);
void rf_dev_objs(const char* cls);
void rf_dev_tpto(const char* name);
void rf_dev_peek(const char* cls, const char* spec, const char* filter);
void rf_dev_szstate(const char* name, int state);
void rf_dev_levels(void);
void* rf_live_instance(const char* cls);  // active instance of a TheRoomVR.dll class
void rf_dev_hint(void);
void rf_dev_rhide(const char* name, bool hide);

// ---- rf_gfx.c: graphics state (dev) ----
void rf_dev_gfx(void);
void rf_dev_aa(bool force_rt, int msaa);
void rf_dev_quality(int level, int msaa);
void rf_dev_depthtex(int mode);
void h_qsm_postfx(void* self, const void* mi);
int32_t h_pxr_config_int(int32_t type, const void* mi);
void rf_gfx_init(void);
int32_t h_qsm_depthmode(void* self, const void* mi);
void* h_gfx_setup(void* self, const void* mi);

// ---- module entry points ----
void rf_xr_init(void);      // XR device emulation (head)
void rf_dev_tick(void);     // command file polling (dev builds)
void rf_frame_tick(void);   // called once per frame on the Unity main thread
void rf_touch_post(int action, int id, float x, float y, int64_t t);  // any thread
void rf_touch_tick(void);   // main thread
