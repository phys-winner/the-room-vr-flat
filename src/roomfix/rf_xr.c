// Synthetic XR devices for flat mode (head + two hand controllers).
//
// Every VR pose and button the game reads ends in the Unity XR input API:
//  * camera: the game adds a TrackedPoseDriver(Center) to the centre camera; it reads
//    SpatialTracking.PoseDataSource.GetNodePoseData(XRNode) (managed, hooked directly);
//  * PICO porting rig/input (PicoPorting.PVRCameraRig, PVRInput, PVRControllerBase) and the game's
//    eye helpers: InputDevices / InputTracking icalls. IL2CPP inlines icall calls into every caller,
//    so they are intercepted when the runtime resolves them by name (codegen resolve_icall) and the
//    native engine implementation stays the fallback for every non-fake device.
// The fake devices' state comes from the flat controls (rf_head_t, rf_hand_t); the native camera
// and input pipelines of the game stay untouched.
#include <stdio.h>
#include <string.h>

#include "rf.h"

rf_head_t g_head = {.yaw = 0, .pitch = 0, .pos = {0, 0, 0}, .ipd = 0.064f};
rf_hand_t g_hand[2] = {  // rest pose (rf_unity.c REST_OFF); resting hands follow the head yaw every frame
    {.pos = {-0.2f, -0.5f, -0.3f}, .rot = {0, 0, 0, 1}},
    {.pos = {0.2f, -0.5f, -0.3f}, .rot = {0, 0, 0, 1}},
};

#define FAKE_BASE 0x524F4F4D00000000ULL  // "ROOM"
enum { XRN_LEFT_EYE = 0, XRN_RIGHT_EYE = 1, XRN_CENTER_EYE = 2, XRN_HEAD = 3, XRN_LEFT_HAND = 4, XRN_RIGHT_HAND = 5 };

static bool is_fake(uint64_t id) { return (id & 0xFFFFFFFF00000000ULL) == FAKE_BASE; }
static int fake_node(uint64_t id) { return (int)(id & 0xFF); }
static int hand_of(int node) { return node == XRN_LEFT_HAND ? 0 : node == XRN_RIGHT_HAND ? 1 : -1; }

// ---- dev: record each (node, usage) pair the game asks for, once ----
static char g_seen[128][48];
static int g_nseen;
static void note_usage(int node, const char* type, const Il2CppString* usage) {
    if (!g_dev) return;
    char u[32];
    rf_str_utf8(usage, u, sizeof u);
    char key[48];
    snprintf(key, sizeof key, "%d:%s:%s", node, type, u);
    for (int i = 0; i < g_nseen; i++)
        if (!strcmp(g_seen[i], key)) return;
    if (g_nseen < 128) {
        strcpy(g_seen[g_nseen++], key);
        rf_log("xr usage node=%d %s '%s'", node, type, u);
    }
}

static v3 eye_pos(int node) {
    v3 p = g_head.pos;
    if (node == XRN_LEFT_EYE || node == XRN_RIGHT_EYE) {
        v3 right = q_rot(rf_head_rot(), (v3){1, 0, 0});
        p = v3_add(p, v3_scale(right, (node == XRN_LEFT_EYE ? -0.5f : 0.5f) * g_head.ipd));
    }
    return p;
}

static bool node_pose(int node, v3* pos, quat* rot) {
    if (node >= XRN_LEFT_EYE && node <= XRN_HEAD) {
        *pos = eye_pos(node == XRN_HEAD ? XRN_CENTER_EYE : node);
        *rot = rf_head_rot();
        return true;
    }
    int h = hand_of(node);
    if (h >= 0) {
        *pos = g_hand[h].pos;
        *rot = g_hand[h].rot;
        return true;
    }
    return false;
}

// ---- legacy node states (TrackedPoseDriver on the camera), managed hook ----
static int32_t h_node_pose(int32_t node, v3* pose /* Pose: position, rotation */, const void* mi) {
    v3 p;
    quat q;
    if (node_pose(node, &p, &q)) {
        pose[0] = p;
        *(quat*)((char*)pose + 12) = q;
        // the camera takes this pose now (TrackedPoseDriver in Update and before rendering): flat texts move with it
        if (node == XRN_CENTER_EYE) {
            rf_flatui_pin_all(false);
            if (g_dev) {
                static int last, calls;
                calls++;
                if (g_frame - last >= 600) {
                    rf_log("xr: centre-eye pose read %.2f times per frame", last ? calls / (float)(g_frame - last) : 0.0f);
                    last = g_frame;
                    calls = 0;
                }
            }
        }
        return 3;  // PoseDataFlags.Position | Rotation
    }
    return ORIG(H_XR_NODEPOSE, int32_t (*)(int32_t, v3*, const void*))(node, pose, mi);
}

// ---- icall replacements (exact native signatures, no MethodInfo) ----
static uint64_t (*o_get_dev_id)(int32_t);
static bool (*o_dev_valid)(uint64_t);
static uint32_t (*o_dev_char)(uint64_t);
static bool (*o_tgf_bool)(uint64_t, Il2CppString*, bool*);
static bool (*o_tgf_f)(uint64_t, Il2CppString*, float*);
static bool (*o_tgf_u32)(uint64_t, Il2CppString*, uint32_t*);
static bool (*o_tgf_v2)(uint64_t, Il2CppString*, v2*);
static bool (*o_tgf_v3)(uint64_t, Il2CppString*, v3*);
static bool (*o_tgf_q)(uint64_t, Il2CppString*, quat*);
static void (*o_local_pos)(int32_t, v3*);
static void (*o_local_rot)(int32_t, quat*);

static uint64_t i_get_dev_id(int32_t node) {
    if (node >= XRN_LEFT_EYE && node <= XRN_RIGHT_HAND) return FAKE_BASE | (uint64_t)node;
    return o_get_dev_id ? o_get_dev_id(node) : 0;
}

static bool i_dev_valid(uint64_t id) { return is_fake(id) || (o_dev_valid && o_dev_valid(id)); }

// InputDeviceCharacteristics
enum { HEAD_MOUNTED = 1, HELD_IN_HAND = 4, TRACKED = 32, CONTROLLER = 64, LEFT = 256, RIGHT = 512 };

static uint32_t i_dev_char(uint64_t id) {
    if (!is_fake(id)) return o_dev_char ? o_dev_char(id) : 0;
    int n = fake_node(id);
    if (n <= XRN_HEAD) return HEAD_MOUNTED | TRACKED;
    return HELD_IN_HAND | TRACKED | CONTROLLER | (n == XRN_LEFT_HAND ? LEFT : RIGHT);
}

static bool i_tgf_v3(uint64_t id, Il2CppString* usage, v3* out) {
    if (!is_fake(id)) return o_tgf_v3 && o_tgf_v3(id, usage, out);
    int n = fake_node(id), h = hand_of(n);
    note_usage(n, "v3", usage);
    if (h >= 0) {
        if (rf_str_eq(usage, "DevicePosition") || rf_str_eq(usage, "PointerPosition")) { *out = g_hand[h].pos; return true; }
        if (rf_str_eq(usage, "DeviceVelocity")) { *out = g_hand[h].vel; return true; }
        *out = (v3){0, 0, 0};
        return rf_str_eq(usage, "DeviceAngularVelocity") || rf_str_eq(usage, "DeviceAcceleration") ||
               rf_str_eq(usage, "DeviceAngularAcceleration");
    }
    if (rf_str_eq(usage, "CenterEyePosition") || rf_str_eq(usage, "DevicePosition")) {
        *out = eye_pos(n == XRN_HEAD ? XRN_CENTER_EYE : n);
        return true;
    }
    if (rf_str_eq(usage, "LeftEyePosition")) { *out = eye_pos(XRN_LEFT_EYE); return true; }
    if (rf_str_eq(usage, "RightEyePosition")) { *out = eye_pos(XRN_RIGHT_EYE); return true; }
    *out = (v3){0, 0, 0};
    return rf_str_eq(usage, "DeviceVelocity") || rf_str_eq(usage, "DeviceAngularVelocity") ||
           rf_str_eq(usage, "DeviceAcceleration") || rf_str_eq(usage, "DeviceAngularAcceleration");
}

static bool i_tgf_q(uint64_t id, Il2CppString* usage, quat* out) {
    if (!is_fake(id)) return o_tgf_q && o_tgf_q(id, usage, out);
    int n = fake_node(id), h = hand_of(n);
    note_usage(n, "q", usage);
    if (h >= 0) {
        if (rf_str_eq(usage, "DeviceRotation") || rf_str_eq(usage, "PointerRotation")) { *out = g_hand[h].rot; return true; }
        return false;
    }
    if (rf_str_eq(usage, "CenterEyeRotation") || rf_str_eq(usage, "DeviceRotation") ||
        rf_str_eq(usage, "LeftEyeRotation") || rf_str_eq(usage, "RightEyeRotation")) {
        *out = rf_head_rot();
        return true;
    }
    return false;
}

static bool i_tgf_bool(uint64_t id, Il2CppString* usage, bool* out) {
    if (!is_fake(id)) return o_tgf_bool && o_tgf_bool(id, usage, out);
    int n = fake_node(id), h = hand_of(n);
    note_usage(n, "bool", usage);
    // ControllerStatus: PICO usage "controller connected"; PVRInput reads no buttons while it is false
    if (rf_str_eq(usage, "IsTracked") || rf_str_eq(usage, "UserPresence") || rf_str_eq(usage, "ControllerStatus")) {
        *out = true;
        return true;
    }
    if (h < 0) return false;
    const rf_hand_t* s = &g_hand[h];
    if (rf_str_eq(usage, "TriggerButton")) { *out = s->trigger > 0.5f; return true; }
    if (rf_str_eq(usage, "GripButton")) { *out = s->grip > 0.5f; return true; }
    if (rf_str_eq(usage, "PrimaryButton")) { *out = s->btn_primary; return true; }
    if (rf_str_eq(usage, "SecondaryButton")) { *out = s->btn_secondary; return true; }
    if (rf_str_eq(usage, "MenuButton")) { *out = s->btn_menu; return true; }
    if (rf_str_eq(usage, "Primary2DAxisClick")) { *out = s->stick_click; return true; }
    *out = false;
    return true;  // every other button exists and is released
}

static bool i_tgf_f(uint64_t id, Il2CppString* usage, float* out) {
    if (!is_fake(id)) return o_tgf_f && o_tgf_f(id, usage, out);
    int n = fake_node(id), h = hand_of(n);
    note_usage(n, "float", usage);
    if (h < 0) return false;
    if (rf_str_eq(usage, "Trigger")) { *out = g_hand[h].trigger; return true; }
    if (rf_str_eq(usage, "Grip")) { *out = g_hand[h].grip; return true; }
    *out = rf_str_eq(usage, "BatteryLevel") ? 1.0f : 0.0f;
    return true;
}

static bool i_tgf_v2(uint64_t id, Il2CppString* usage, v2* out) {
    if (!is_fake(id)) return o_tgf_v2 && o_tgf_v2(id, usage, out);
    int n = fake_node(id), h = hand_of(n);
    note_usage(n, "v2", usage);
    if (h < 0) return false;
    *out = rf_str_eq(usage, "Primary2DAxis") ? g_hand[h].stick : (v2){0, 0};
    return true;
}

static bool i_tgf_u32(uint64_t id, Il2CppString* usage, uint32_t* out) {
    if (!is_fake(id)) return o_tgf_u32 && o_tgf_u32(id, usage, out);
    note_usage(fake_node(id), "u32", usage);
    // InputTrackingState.All: PICO PVRInput treats only Position(1) or All(0x3F) as "position tracked"
    *out = rf_str_eq(usage, "TrackingState") ? 0x3F : 0;
    return true;
}

static void i_local_pos(int32_t node, v3* out) {
    v3 p;
    quat q;
    if (node_pose(node, &p, &q)) { *out = p; return; }
    if (o_local_pos) o_local_pos(node, out);
}

static void i_local_rot(int32_t node, quat* out) {
    v3 p;
    quat q;
    if (node_pose(node, &p, &q)) { *out = q; return; }
    if (o_local_rot) o_local_rot(node, out);
}

static const struct { const char* name; void* repl; void** orig; } g_icalls[] = {
    {"UnityEngine.XR.InputTracking::GetDeviceIdAtXRNode", (void*)i_get_dev_id, (void**)&o_get_dev_id},
    {"UnityEngine.XR.InputTracking::GetLocalPosition_Injected", (void*)i_local_pos, (void**)&o_local_pos},
    {"UnityEngine.XR.InputTracking::GetLocalRotation_Injected", (void*)i_local_rot, (void**)&o_local_rot},
    {"UnityEngine.XR.InputDevices::IsDeviceValid", (void*)i_dev_valid, (void**)&o_dev_valid},
    {"UnityEngine.XR.InputDevices::GetDeviceCharacteristics", (void*)i_dev_char, (void**)&o_dev_char},
    {"UnityEngine.XR.InputDevices::TryGetFeatureValue_bool", (void*)i_tgf_bool, (void**)&o_tgf_bool},
    {"UnityEngine.XR.InputDevices::TryGetFeatureValue_float", (void*)i_tgf_f, (void**)&o_tgf_f},
    {"UnityEngine.XR.InputDevices::TryGetFeatureValue_UInt32", (void*)i_tgf_u32, (void**)&o_tgf_u32},
    {"UnityEngine.XR.InputDevices::TryGetFeatureValue_Vector2f", (void*)i_tgf_v2, (void**)&o_tgf_v2},
    {"UnityEngine.XR.InputDevices::TryGetFeatureValue_Vector3f", (void*)i_tgf_v3, (void**)&o_tgf_v3},
    {"UnityEngine.XR.InputDevices::TryGetFeatureValue_Quaternionf", (void*)i_tgf_q, (void**)&o_tgf_q},
};

// il2cpp codegen icall resolution: void* resolve_icall(const char* fullNameWithSignature)
static void* h_icall_resolve(const char* name) {
    void* r = ORIG(H_ICALL_RESOLVE, void* (*)(const char*))(name);
    if (!name || name[0] != 'U' || strncmp(name, "UnityEngine.XR.Input", 20) != 0) return r;
    for (size_t i = 0; i < sizeof g_icalls / sizeof g_icalls[0]; i++) {
        size_t n = strlen(g_icalls[i].name);
        if (!strncmp(name, g_icalls[i].name, n) && (name[n] == '(' || name[n] == 0)) {
            if (!*g_icalls[i].orig) *g_icalls[i].orig = r;
            rf_log("xr: icall %s -> synthetic devices (engine fallback %p)", g_icalls[i].name, r);
            return g_icalls[i].repl;
        }
    }
    return r;
}

const struct rf_hook_entry g_xr_hooks[] = {
    {H_XR_NODEPOSE, (void*)h_node_pose, true, false},
    {H_ICALL_RESOLVE, (void*)h_icall_resolve, true, false},
};
const size_t g_xr_hook_count = sizeof g_xr_hooks / sizeof g_xr_hooks[0];
