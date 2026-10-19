// Spatial puzzle contacts position real synthetic hands; the original consumers still process them.
#include <assert.h>

#include "rf.h"

typedef struct { v3 c, e; } ball_bounds;
static rf_ref g_ball;
static int g_ball_frame = -100;
static struct { bool down; int id; float x, y; } g_ball_finger[2];
static uint32_t g_other_fingers;
static rf_ref g_keyboard;
enum { KEY_IDLE, KEY_ACQUIRE, KEY_PRESS, KEY_RELEASE };
static struct {
    bool down;
    int id, stage, frames;
    void* key;
    void* pending_key;
    void* pending_pivot;
    int pending_id;
    v3 rest, pivot, axis, radial;
    float height, turn;
} g_key_finger[2];
static uint32_t g_key_ignored;

bool rf_puzzle_hand_active(int h) {
    return h >= 0 && h < 2 && (g_ball_finger[h].down || g_key_finger[h].stage != KEY_IDLE);
}

static void clear_key_hand(int h) {
    if (g_key_finger[h].stage == KEY_IDLE) return;
    g_key_finger[h].down = false;
    g_key_finger[h].pending_key = g_key_finger[h].pending_pivot = NULL;
    g_key_finger[h].stage = KEY_IDLE;
    g_hand[h].grip = g_hand[h].trigger = 0;
    rf_hand_rest(h);
}

static void cancel_keys(void) {
    clear_key_hand(0);
    clear_key_hand(1);
    g_key_ignored = 0;
}

static void key_up(int h) {
#if !RF_RELEASE
    if (g_dev && rf_alive(g_key_finger[h].key)) {
        char name[64];
        rf_obj_name(g_key_finger[h].key, name, sizeof name);
        rf_log("keyboard: up '%s' angle=%.3f stage=%d", name, *(float*)((char*)g_key_finger[h].key + 0x118),
               g_key_finger[h].stage);
    }
#endif
    g_key_finger[h].down = false;
    g_key_finger[h].stage = KEY_RELEASE;
    g_key_finger[h].frames = 0;
}

void h_keyboard_keys(void* self, int32_t state, const void* mi) {
    if (self != rf_ref_get(&g_keyboard)) {
        cancel_keys();
        rf_ref_set(&g_keyboard, self);
    }
    ORIG(H_KEYBOARD_KEYS, void (*)(void*, int32_t, const void*))(self, state, mi);
}

static bool actuator_pending(float value, float angle, float threshold) {
    return isfinite(value) && value >= 0 && value <= 1 && isfinite(angle) && isfinite(threshold) &&
           (angle >= threshold ? value < 1 : value > 0);
}

bool h_key_modifier_update(void* self, void* key, const void* mi) {
    bool update = ORIG(H_KEY_MODIFIER_UPDATE, bool (*)(void*, void*, const void*))(self, key, mi);
    if (update || !*((char*)self + 0x38) || !rf_class_is(self, "RotationActivatedAutoMoveModifier")) return update;
    char* keyboard = rf_ref_get(&g_keyboard);
    if (!rf_alive(keyboard) || !rf_alive(key)) return false;
    char* keys = *(char**)(keyboard + 0x40);
    int count = keys ? (int)*(uint64_t*)(keys + 0x18) : 0;
    for (int i = 0; i < count && i < 64; i++) {
        if (((void**)(keys + 0x20))[i] != key) continue;
        // A4's speed gate stops its column mid-return after release, including focus cancellation.
        return actuator_pending(*(float*)((char*)self + 0x84), *(float*)((char*)key + 0x118),
                                *(float*)((char*)self + 0x44));
    }
    return false;
}

static void release_hand(int h) {
    if (!g_ball_finger[h].down) return;
    g_ball_finger[h].down = false;
    g_hand[h].grip = g_hand[h].trigger = 0;
    rf_hand_rest(h);
}

static void cancel_hands(void) {
    release_hand(0);
    release_hand(1);
}

void h_crystal_update(void* self, const void* mi) {
    if (self != rf_ref_get(&g_ball)) {
        cancel_hands();
        rf_ref_set(&g_ball, self);
    }
    g_ball_frame = g_frame;
    ORIG(H_CRYSTAL_UPDATE, void (*)(void*, const void*))(self, mi);
}

static bool ball_geometry(ball_bounds* b) {
    void* ball = rf_ref_get(&g_ball);
    if (rf_menu_context() || !rf_player() || g_frame - g_ball_frame > 2 || !rf_alive(ball) ||
        *((char*)ball + 0xe8) || !g_call_ok[C_BEH_ACTIVEENABLED] || !g_call_ok[C_COMP_GETCOMP] ||
        !g_call_ok[C_MF_SHAREDMESH] || !g_call_ok[C_MESH_BOUNDS] || !g_call_ok[C_TR_LOSSYSCALE] ||
        !g_call_ok[C_TR_POINT] || !g_call_ok[C_REND_GETENABLED] || !g_call_ok[C_GO_ACTIVEH] ||
        !CALLP(C_BEH_ACTIVEENABLED, bool (*)(void*, const void*))(ball, NULL)) return false;
    void* ready = *(void**)((char*)ball + 0xc8);  // ReadyHandPoseVolume.Active: the authored puzzle has started
    void* renderer = *(void**)((char*)ball + 0x48);
    if (!rf_alive(ready) || !*((char*)ready + 0x38) || !rf_alive(renderer) ||
        !CALLP(C_REND_GETENABLED, bool (*)(void*, const void*))(renderer, NULL) ||
        !CALLP(C_GO_ACTIVEH, bool (*)(void*, const void*))(rf_gameobject(renderer), NULL)) return false;
    static void* meshfilter_type;
    if (!meshfilter_type) meshfilter_type = rf_class_type(rf_find_class("UnityEngine.CoreModule.dll", "UnityEngine", "MeshFilter"));
    void* filter = meshfilter_type ? CALLP(C_COMP_GETCOMP, void* (*)(void*, void*, const void*))(renderer, meshfilter_type, NULL) : NULL;
    void* mesh = rf_alive(filter) ? CALLP(C_MF_SHAREDMESH, void* (*)(void*, const void*))(filter, NULL) : NULL;
    if (!rf_alive(mesh)) return false;
    // Rotating the mesh's bounding cube inflates Renderer.bounds, even though the glass is a sphere.
    ball_bounds local = CALLP(C_MESH_BOUNDS, ball_bounds (*)(void*, const void*))(mesh, NULL);
    void* t = rf_transform(filter);
    v3 s = CALLP(C_TR_LOSSYSCALE, v3 (*)(void*, const void*))(t, NULL);
    float r = fmaxf(fabsf(local.e.x * s.x), fmaxf(fabsf(local.e.y * s.y), fabsf(local.e.z * s.z)));
    if (!isfinite(r) || r <= 0.001f || r > 1) return false;
    b->c = CALLP(C_TR_POINT, v3 (*)(void*, v3, const void*))(t, local.c, NULL);
    b->e = (v3){r, r, r};  // the authored Glass_Top mesh is a sphere
    return true;
}

static bool contact_in_reach(v3 point, v3 origin) {
    v3 base;
    float reach = rf_view_base(&base) ? fminf(v3_len(v3_sub(point, base)), v3_len(v3_sub(point, origin)))
                                     : v3_len(v3_sub(point, origin));
    return reach <= RF_REACH_M;
}

static void* keyboard_ready(void) {
    char* keyboard = rf_ref_get(&g_keyboard);
    void* player = rf_player();
    if (rf_menu_context() || !player || !rf_alive(keyboard) || !keyboard[0x68] || !keyboard[0x69] ||
        *(void**)((char*)player + 0x70) != *(void**)(keyboard + 0x70) ||
        !CALLP(C_BEH_ACTIVEENABLED, bool (*)(void*, const void*))(keyboard, NULL)) return NULL;
    void* input = *(void**)((char*)player + 0x118);
    return input && CALLP(C_INPUTCTL_ALLOWED, bool (*)(void*, const void*))(input, NULL) ? keyboard : NULL;
}

static bool keyboard_touch(int action, int id, float x, float y) {
    uint32_t bit = 1u << id;
    if (g_key_ignored & bit) {
        if (action == 1 || action == 3) g_key_ignored &= ~bit;
        return true;
    }
    for (int h = 0; h < 2; h++) {
        if (g_key_finger[h].pending_key && g_key_finger[h].pending_id == id) {
            if (action == 1 || action == 3) g_key_finger[h].pending_key = g_key_finger[h].pending_pivot = NULL;
            return true;
        }
        if (!g_key_finger[h].down || g_key_finger[h].id != id) continue;
        if (action == 1 || action == 3) key_up(h);
        return true;
    }
    if (action != 0 || g_other_fingers || rf_act_grabbing() || rf_act_teleport_mode_on()) return false;
    char* keyboard = keyboard_ready();
    if (!keyboard) return false;
    int h = -1;
    for (int i = 1; i >= 0; i--)
        if (g_key_finger[i].stage == KEY_IDLE) { h = i; break; }
    if (h < 0)
        for (int i = 1; i >= 0; i--)
            if (g_key_finger[i].stage == KEY_RELEASE && !g_key_finger[i].pending_key) { h = i; break; }
    if (h < 0) {
        g_key_ignored |= bit;
        return true;
    }
    v3 point, o, d;
    void* key = NULL;
    void* gc = rf_act_pick_poke(x, y, &point, &key);
    if (!gc || !rf_screen_ray(x, y, &o, &d) || !contact_in_reach(point, o)) return false;
    char* keys = *(char**)(keyboard + 0x40);
    int count = keys ? (int)*(uint64_t*)(keys + 0x18) : 0;
    bool found = false;
    for (int i = 0; i < count && i < 64; i++)
        if (((void**)(keys + 0x20))[i] == key) { found = true; break; }
    if (!found || !rf_class_is(key, "RotationGrabbable")) return false;
    char* anchors = *(char**)((char*)gc + 0x38);
    void* contact_pivot = NULL;
    int nanchors = anchors ? (int)*(uint64_t*)(anchors + 0x18) : 0;
    for (int i = 0; i < nanchors && i < 64; i++) {
        void* anchor = ((void**)(anchors + 0x20))[i];
        if (!rf_alive(anchor) || !rf_class_is(anchor, "GrabbableHandAnchorKey")) continue;
        void* pivot = *(void**)((char*)anchor + 0x28);
        if (!rf_alive(pivot)) continue;
        contact_pivot = pivot;
        // Key anchors sit just above the surface; contact there avoids an adjacent key at a ray-entry edge.
        point = v3_sub(rf_tr_pos(pivot), v3_scale(rf_tr_dir(pivot, (v3){0, 1, 0}), 0.006f));
        break;
    }
    if (!contact_in_reach(point, o)) return false;
    if (g_key_finger[h].stage == KEY_RELEASE) {
        if (!contact_pivot) { g_key_ignored |= bit; return true; }
        // Finish the previous native release before acquiring a quickly repeated note.
        g_key_finger[h].pending_key = key;
        g_key_finger[h].pending_pivot = contact_pivot;
        g_key_finger[h].pending_id = id;
        return true;
    }
    g_key_finger[h].down = true;
    g_key_finger[h].id = id;
    g_key_finger[h].key = key;
    g_key_finger[h].rest = point;
    g_key_finger[h].stage = KEY_ACQUIRE;
    g_key_finger[h].frames = 0;
    return true;
}

static v3 key_press_point(int h, float degrees) {
    return v3_add(g_key_finger[h].pivot,
                  v3_add(v3_scale(g_key_finger[h].axis, g_key_finger[h].height),
                         q_rot(q_axis(g_key_finger[h].axis, degrees * DEG2RAD), g_key_finger[h].radial)));
}

static void keyboard_tick(void) {
    if (!g_key_finger[0].stage && !g_key_finger[1].stage) return;
    if (!keyboard_ready()) { cancel_keys(); return; }
    for (int h = 0; h < 2; h++) {
        if (!g_key_finger[h].stage) continue;
        void* key = g_key_finger[h].key;
        void* ctrl = rf_controller(h);
        void* probe = ctrl ? *(void**)((char*)ctrl + 0x60) : NULL;
        if (!rf_alive(key) || !rf_alive(probe)) { clear_key_hand(h); continue; }
        bool owned = *(void**)((char*)key + 0x80) == ctrl && *((char*)key + 0x90) == 1;
        v3 point = g_key_finger[h].rest;
        g_key_finger[h].frames++;
        if (g_key_finger[h].stage == KEY_ACQUIRE && owned) {
            void* tr = rf_transform(key);
            g_key_finger[h].pivot = rf_tr_pos(tr);
            g_key_finger[h].axis = v3_norm(rf_tr_dir(tr, *(v3*)((char*)key + 0x94)));
            v3 r = v3_sub(point, g_key_finger[h].pivot);
            float height = v3_dot(r, g_key_finger[h].axis);
            float radius = v3_len(v3_sub(r, v3_scale(g_key_finger[h].axis, height)));
            g_key_finger[h].height = height;
            g_key_finger[h].radial = v3_scale(v3_norm(rf_tr_dir(tr, *(v3*)((char*)key + 0x12c))), radius);
            g_key_finger[h].turn = 15 - *(float*)((char*)key + 0x118);
            g_key_finger[h].stage = KEY_PRESS;
            g_key_finger[h].frames = 0;
        }
        if (g_key_finger[h].stage == KEY_PRESS) {
            point = key_press_point(h, g_key_finger[h].turn);
        } else if (g_key_finger[h].stage == KEY_RELEASE) {
            if (!owned) {
                void* next = g_key_finger[h].pending_key;
                void* pivot = g_key_finger[h].pending_pivot;
                int id = g_key_finger[h].pending_id;
                clear_key_hand(h);
                if (!rf_alive(next) || !rf_alive(pivot)) continue;
                point = v3_sub(rf_tr_pos(pivot), v3_scale(rf_tr_dir(pivot, (v3){0, 1, 0}), .006f));
                void* cam = rf_main_camera();
                if (!cam || !contact_in_reach(point, rf_tr_pos(rf_transform(cam)))) { g_key_ignored |= 1u << id; continue; }
                g_key_finger[h].down = true;
                g_key_finger[h].id = id;
                g_key_finger[h].key = key = next;
                g_key_finger[h].rest = point;
                g_key_finger[h].stage = KEY_ACQUIRE;
                g_key_finger[h].frames = 0;
            } else {
                // Leaving the whole volume immediately can leave this note held while the other hand plays.
                point.y += 0.03f;
            }
        } else if (g_key_finger[h].frames >= 30) {
            g_key_ignored |= 1u << g_key_finger[h].id;
            clear_key_hand(h);
            continue;
        }
        g_hand[h].grip = g_hand[h].trigger = 0;
        if (!rf_place_transform(h, rf_transform(probe), point, rf_tr_rot(rf_transform(key)))) {
            if (g_key_finger[h].down) g_key_ignored |= 1u << g_key_finger[h].id;
            clear_key_hand(h);
        }
    }
}

// Outside the silhouette a held finger stays on the rim; both authored hotspots face the table's eye point.
static bool sphere_point(v3 o, v3 d, v3 centre, float r, bool clamp, v3* point) {
    v3 w = v3_sub(centre, o);
    float t = v3_dot(w, d);
    if (t <= r) return false;
    v3 radial = v3_sub(v3_scale(d, t), w);
    float d2 = v3_dot(radial, radial), disc = r * r - d2;
    if (disc < 0) {
        if (!clamp || d2 < 1e-10f) return false;
        *point = v3_add(centre, v3_scale(radial, r / sqrtf(d2)));
    } else {
        *point = v3_add(o, v3_scale(d, t - sqrtf(disc)));
    }
    return true;
}

bool rf_puzzle_touch(int action, int id, float x, float y) {
    if (action == 10 || action == 11 || action == 12) {
        cancel_hands();
        cancel_keys();
        g_other_fingers = 0;
        return false;  // HUD, global cancel and the hold-distance slider still reach their normal handlers
    }
    if (id < 0 || id > 31) return false;
    if (keyboard_touch(action, id, x, y)) return true;
    uint32_t bit = 1u << id;
    for (int h = 0; h < 2; h++) {
        if (!g_ball_finger[h].down || g_ball_finger[h].id != id) continue;
        if (action == 2) {
            g_ball_finger[h].x = x;
            g_ball_finger[h].y = y;
        } else if (action == 1 || action == 3) {
            release_hand(h);
        }
        return true;  // owns this ID through UP/CANCEL, even when the other finger already lifted
    }
    if (action == 1 || action == 3) g_other_fingers &= ~bit;
    if (action != 0) return false;
    ball_bounds b;
    v3 o, d, p;
    if (!g_other_fingers && !rf_act_grabbing() && ball_geometry(&b) && rf_screen_ray(x, y, &o, &d) &&
        sphere_point(o, d, b.c, b.e.x, false, &p)) {
        // The unsolved puzzle stays active after leaving its table; distant touches keep normal ownership.
        if (contact_in_reach(p, o)) {
            float sx, sy;
            if (!rf_world_to_screen(b.c, &sx, &sy)) return false;
            int h = x < sx ? 0 : 1;
            if (g_ball_finger[h].down) h = 1 - h;
            if (!g_ball_finger[h].down) {
                g_ball_finger[h].down = true;
                g_ball_finger[h].id = id;
                g_ball_finger[h].x = x;
                g_ball_finger[h].y = y;
                return true;
            }
        }
    }
    cancel_hands();  // an ordinary grab/look must own the controllers before its DOWN is routed
    cancel_keys();
    g_other_fingers |= bit;  // a gesture that starts elsewhere keeps its normal pan/pinch/interaction path
    return false;
}

void rf_puzzle_tick(void) {
    keyboard_tick();
#if !RF_RELEASE
    static bool checked;
    if (!checked) {
        checked = true;
        v3 p;
        assert(sphere_point((v3){0, 0, -3}, (v3){0, 0, 1}, (v3){0, 0, 0}, 1, false, &p));
        assert(v3_len(v3_sub(p, (v3){0, 0, -1})) < 1e-5f);
        assert(!sphere_point((v3){2, 0, -3}, (v3){0, 0, 1}, (v3){0, 0, 0}, 1, false, &p));
        assert(sphere_point((v3){2, 0, -3}, (v3){0, 0, 1}, (v3){0, 0, 0}, 1, true, &p) &&
               fabsf(v3_len(p) - 1) < 1e-5f);
    }
#endif
    if (!g_ball_finger[0].down && !g_ball_finger[1].down) return;
    ball_bounds b;
    if (!ball_geometry(&b)) {
        cancel_hands();
        return;
    }
    for (int h = 0; h < 2; h++) {
        if (!g_ball_finger[h].down) continue;
        void* ctrl = rf_controller(h);
        void* hand = ctrl ? *(void**)((char*)ctrl + 0x38) : NULL;
        v3 o, d, p;
        if (!rf_alive(hand) || !rf_screen_ray(g_ball_finger[h].x, g_ball_finger[h].y, &o, &d) ||
            !sphere_point(o, d, b.c, b.e.x, true, &p) ||
            !contact_in_reach(p, o) ||
            !rf_place_transform(h, rf_transform(hand), p, rf_q_look(v3_norm(v3_sub(b.c, p)), (v3){0, 1, 0}))) {
            release_hand(h);
            continue;
        }
        g_hand[h].grip = g_hand[h].trigger = 0;
    }
}
