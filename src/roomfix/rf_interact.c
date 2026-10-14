// Game actions driven by touch. Every action is performed the way a VR player would do it: the synthetic
// right controller is moved to where the player's real hand would be and its buttons are pressed; the
// game's own probes, grab rules, teleport pointer and menu raycaster decide what happens.
#include <stdio.h>
#include <string.h>

#include "rf.h"

typedef void* (*fn_obj1)(void*, const void*);
typedef bool (*fn_b1)(void*, const void*);

#define RIGHT 1

// ------------------------------------------------------------------------------------------ laser click
// Menus draw the laser of one controller at a time; MenuController hands the pointer to a controller when its
// trigger is pressed. A click points the right controller at the tapped spot; if the menu's VrInputModule ray
// still belongs to the left hand, a short trigger press hands the pointer over first. Then the right ray is put
// exactly on the finger's camera ray and the trigger is pulsed.
static struct {
    int frames;  // frames since the tap (0 = idle)
    int armed;   // frame the right-hand ray took over (0 = not yet)
    float x, y;
} g_click;

void rf_act_laser_click(float nx, float ny) {
    g_click.frames = 1;
    g_click.armed = 0;
    g_click.x = nx;
    g_click.y = ny;
}

static void* menu_ray_transform(void) {
    void* rt = rf_alive(g_vrim) ? *(void**)((char*)g_vrim + 0x58) : NULL;  // VrInputModule.rayTransform
    return rf_alive(rt) ? rt : NULL;
}

// Point the right controller (kept at its rest position) towards a world point.
static void rough_aim_right(v3 target) {
    void* cam = rf_main_camera();
    if (!cam) return;
    void* rig = rf_tr_parent(rf_transform(cam));  // hand anchors share the camera rig's tracking space
    quat r = rf_alive(rig) ? rf_tr_rot(rig) : (quat){0, 0, 0, 1};
    v3 rp = rf_alive(rig) ? rf_tr_pos(rig) : (v3){0, 0, 0};
    v3 hp = v3_add(rp, q_rot(r, g_hand[RIGHT].pos));
    g_hand[RIGHT].rot = rf_q_look(q_rot(q_conj(r), v3_norm(v3_sub(target, hp))), (v3){0, 1, 0});
    g_hand_resting[RIGHT] = false;
}

#define CLICK_WAIT_MAX 30  // frames to wait for the menu to switch to the right-hand ray

static void tick_click(void) {
    if (!g_click.frames) return;
    int f = g_click.frames++;
    v3 o, d;
    void* rt = menu_ray_transform();
    if (rf_screen_ray(g_click.x, g_click.y, &o, &d)) {
        // laser origin 30 cm behind the eye on the same ray: the beam passes exactly under the finger
        if (rt && rf_aim_transform(RIGHT, rt, v3_sub(o, v3_scale(d, 0.30f)), d)) {
            if (!g_click.armed) g_click.armed = f;
        } else {
            rough_aim_right(v3_add(o, v3_scale(d, 2.0f)));
        }
    }
    int start = g_click.armed ? g_click.armed + 3 : CLICK_WAIT_MAX;
    bool handover = !g_click.armed && f >= 2 && f < 5;  // press that makes the right controller the pointer
    g_hand[RIGHT].trigger = (handover || (f >= start && f < start + 6)) ? 1.0f : 0.0f;
    if (g_dev && (f == 1 || f == start || f == start + 6)) {
        void* mc = rf_menu_controller();
        rf_log("click f=%d armed=%d ray=%p pointerHand=%p inputEnabled=%d", f, g_click.armed, rt,
               mc ? *(void**)((char*)mc + 0x100) : NULL, mc ? *((char*)mc + 0x108) : -1);
    }
    if (f > start + 12) {
        g_click.frames = 0;
        g_hand[RIGHT].trigger = 0;
        rf_hand_rest(RIGHT);
    }
}

// ------------------------------------------------------------------------------------------ grabbing
enum { K_FREE = 0, K_SLIDER = 1, K_ROTATION = 2, K_TWIST = 3, K_PLANE = 4, K_CYLINDER = 5, K_ROLL = 6 };
enum { G_IDLE = 0, G_HOVER, G_HOLD, G_RELEASE, G_REGRAB };

static struct {
    int state, frames, kind;
    void* grabbable;
    v3 point0;   // grab point (world) at grab time
    v3 cam0;     // camera position at grab time
    float depth; // camera distance to the grab point
    v3 axis;     // slider direction / rotation axis (world, unit)
    v3 pivot;    // rotation pivot (world)
    float h0, r0;
    float hmin, hmax;  // K_CYLINDER: heights along the axis (from the pivot) the slide limits allow
    v3 radial0, roll_n;  // K_ROLL: grab direction from the axis, drag plane normal (towards the eyes)
    float roll_r;        // K_ROLL: radius the finger rolls the bar at
    quat rot0;   // grabber rotation at grab time
    float fx0, fy0, fx, fy;
    float twist0;  // finger angle around the projected pivot at grab time
    bool twist_armed;  // finger has left the dead zone around the pivot: twist0 is a valid baseline
    float twist_last, twist_acc;  // last finger angle and accumulated turn (multi-turn cranks)
    v3 target;
    quat target_rot;
    bool inv;   // item taken out of the inventory grid with the pointing finger ray
    v3 inv_dir;
} g_grab;

#define REACH_M 1.6f       // farthest grab point from the eyes (seated VR reach incl. leaning)
#define HOVER_FRAMES 3     // frames the hand rests on the object before the grip closes

typedef struct { v3 point, normal; uint32_t face; float dist; v2 uv; int32_t col; } raycast_hit;  // 44 bytes

static void* grabber_transform(void) {
    void* c = rf_controller(RIGHT);
    if (!c) return NULL;
    void* g = *(void**)((char*)c + 0x18);  // Controller.Grabber
    return rf_alive(g) ? g : NULL;
}

// ---- registry of the game's grab volumes (GrabbableCollider: math volumes with virtual queries) ----
#define MAXGC 4096
static void* g_gcs[MAXGC];
static int g_ngc;

void rf_gc_add(void* gc) {
    for (int i = 0; i < g_ngc; i++)
        if (g_gcs[i] == gc) return;
    if (g_ngc < MAXGC) g_gcs[g_ngc++] = gc;
}

void rf_gc_remove(void* gc) {
    for (int i = 0; i < g_ngc; i++)
        if (g_gcs[i] == gc) { g_gcs[i] = g_gcs[--g_ngc]; return; }
}

// vtable slot i of an IL2CPP object: klass + 0x130 + 16*i = {methodPtr, MethodInfo*}
static void* vslot(void* obj, int slot, void** mi) {
    char* k = *(char**)obj;
    if (mi) *mi = *(void**)(k + 0x130 + 16 * slot + 8);
    return *(void**)(k + 0x130 + 16 * slot);
}
static bool gc_contains(void* gc, v3 p) {
    void* mi;
    bool (*f)(void*, v3, void*) = (bool (*)(void*, v3, void*))vslot(gc, 5, &mi);
    return f(gc, p, mi);
}
static v3 gc_closest_in(void* gc, v3 p) {
    void* mi;
    v3 (*f)(void*, v3, void*) = (v3(*)(void*, v3, void*))vslot(gc, 6, &mi);
    return f(gc, p, mi);
}
static v3 gc_center(void* gc) {
    void* mi;
    v3 (*f)(void*, void*) = (v3(*)(void*, void*))vslot(gc, 8, &mi);
    return f(gc, mi);
}

static bool gc_usable(void* gc, void* ctrl, void** grabbable_out) {
    if (!rf_alive(gc)) return false;
    char* g = (char*)gc;
    if (!g[0x20] /* CanBeGrabbed */ && !g[0x21] /* CanBePoked */) return false;
    void* go = rf_gameobject(gc);
    if (!rf_alive(go) || !CALLP(C_GO_ACTIVEH, fn_b1)(go, NULL)) return false;
    void* gr = CALLP(C_GC_GRABBABLE, fn_obj1)(gc, NULL);
    if (rf_alive(gr) && ctrl && !((bool (*)(void*, void*, const void*))RVA(g_call_desc[C_GRAB_CANGRAB].rva))(gr, ctrl, NULL))
        return false;
    if (grabbable_out) *grabbable_out = rf_alive(gr) ? gr : NULL;
    return true;
}

// Finger ray against the grab volumes: exact hit by marching along the ray, otherwise the volume whose
// closest point lies within a fingertip radius on screen ("magnet" for small handles and knobs).
#define SNAP_R 0.035f
static void* pick_volume(float nx, float ny, v3* point, float* dist, void** grabbable) {
    v3 o, d;
    if (!rf_screen_ray(nx, ny, &o, &d)) return NULL;
    void* ctrl = rf_controller(RIGHT);
    void* best = NULL;
    float best_t = 1e9f, best_snap = SNAP_R;
    void* near = NULL;
    v3 near_p = {0, 0, 0};
    float near_t = 0;
    for (int i = 0; i < g_ngc; i++) {
        void* gc = g_gcs[i];
        void* gr = NULL;
        if (!gc_usable(gc, ctrl, &gr)) continue;
        v3 c = gc_center(gc);
        float t = v3_dot(v3_sub(c, o), d);
        if (t < 0.1f || t > 3.5f) continue;
        if (v3_len(v3_sub(c, v3_add(o, v3_scale(d, t)))) > 0.8f) continue;
        bool hit = false;
        float t0 = t - 0.8f < 0.1f ? 0.1f : t - 0.8f;
        for (float tt = t0; tt < t + 0.8f && tt < best_t; tt += 0.004f) {
            if (gc_contains(gc, v3_add(o, v3_scale(d, tt)))) {
                best = gc;
                best_t = tt;
                *grabbable = gr;
                hit = true;
                break;
            }
        }
        if (hit || best) continue;
        v3 q = gc_closest_in(gc, v3_add(o, v3_scale(d, t)));
        float sx, sy;
        if (!rf_world_to_screen(q, &sx, &sy)) continue;
        float dx = (sx - nx) * 2.1f, dy = sy - ny, sd = sqrtf(dx * dx + dy * dy);
        if (sd < best_snap) {
            best_snap = sd;
            near = gc;
            near_p = q;
            near_t = v3_len(v3_sub(q, o));
            *grabbable = gr;
        }
    }
    if (best) {
        *point = v3_add(o, v3_scale(d, best_t));
        *dist = best_t;
        return best;
    }
    if (near) {
        *point = near_p;
        *dist = near_t;
        return near;
    }
    return NULL;
}

void rf_act_list_volumes(void) {
    void* ctrl = rf_controller(RIGHT);
    int shown = 0;
    rf_log("volumes: %d registered", g_ngc);
    for (int i = 0; i < g_ngc && shown < 80; i++) {
        void* gc = g_gcs[i];
        void* gr = NULL;
        if (!gc_usable(gc, ctrl, &gr)) continue;
        v3 c = gc_center(gc);
        float sx, sy;
        if (!rf_world_to_screen(c, &sx, &sy) || sx < 0 || sx > 1 || sy < 0 || sy > 1) continue;
        char nm[64];
        rf_obj_name(gc, nm, sizeof nm);
        rf_log("  vol '%s' %s grab=%d poke=%d grabbable=%s screen(%.3f %.3f) center(%.2f %.2f %.2f)", nm,
               rf_class_name(gc), ((char*)gc)[0x20], ((char*)gc)[0x21], gr ? rf_class_name(gr) : "-", sx, sy, c.x, c.y, c.z);
        shown++;
    }
}

// Raycast through the finger. Returns the nearest grabbable the right hand may grab (NULL if a solid
// non-grabbable surface is in front of it) and the hit point.
static void* pick_grabbable(float nx, float ny, v3* point, float* dist) {
    void* gr = NULL;
    void* gc = pick_volume(nx, ny, point, dist, &gr);
    if (gc && gr) return gr;
    return NULL;
}

__attribute__((unused)) static void* pick_grabbable_physics(float nx, float ny, v3* point, float* dist) {
    v3 o, d;
    if (!rf_screen_ray(nx, ny, &o, &d)) return NULL;
    void* arr = ((void* (*)(v3, v3, float, int32_t, int32_t, const void*))RVA(g_call_desc[C_PHYS_RAYCASTALL].rva))(
        o, d, 4.0f, -1, 2 /* QueryTriggerInteraction.Collide */, NULL);
    if (!arr) return NULL;
    int32_t n = (int32_t) * (uint64_t*)((char*)arr + 0x18);
    raycast_hit* hits = (raycast_hit*)((char*)arr + 0x20);
    void* ctrl = rf_controller(RIGHT);
    float best = 1e9f, solid = 1e9f;
    void* best_g = NULL;
    v3 best_p = {0, 0, 0};
    for (int i = 0; i < n && i < 64; i++) {
        raycast_hit* h = &hits[i];
        if (h->dist < 0.12f) continue;  // our own hands/head volumes
        void* col = ((void* (*)(raycast_hit*, const void*))RVA(g_call_desc[C_HIT_COLLIDER].rva))(h, NULL);
        if (!rf_alive(col)) continue;
        void* go = rf_gameobject(col);
        void* gc = ((void* (*)(void*, const void*))RVA(g_call_desc[C_GC_FOROBJECT].rva))(go, NULL);
        void* gr = NULL;
        if (rf_alive(gc) && *((char*)gc + 0x20) /* CanBeGrabbed */) {
            gr = CALLP(C_GC_GRABBABLE, fn_obj1)(gc, NULL);
            if (rf_alive(gr) && ctrl &&
                !((bool (*)(void*, void*, const void*))RVA(g_call_desc[C_GRAB_CANGRAB].rva))(gr, ctrl, NULL))
                gr = NULL;
        }
        if (rf_alive(gr)) {
            if (h->dist < best) { best = h->dist; best_g = gr; best_p = h->point; }
        } else if (!CALLP(C_COL_ISTRIGGER, fn_b1)(col, NULL)) {
            if (h->dist < solid) solid = h->dist;
        }
    }
    if (!best_g || solid + 0.03f < best) return NULL;
    *point = best_p;
    *dist = best;
    return best_g;
}

// InventoryManager.Instance (static field 0) .IsItemInInventory(item)
static bool in_inventory(void* item) {
    uintptr_t k = *(uintptr_t*)(g_il2cpp_base + META_INVMGR_TYPEINFO);
    if (!k || (k & 1) || k < 0x10000) return false;           // metadata slot not initialised yet
    void* statics = *(void**)(k + 0xb8);
    void* mgr = statics ? *(void**)statics : NULL;
    if (!rf_alive(mgr)) return false;
    return ((bool (*)(void*, void*, const void*))RVA(g_call_desc[C_INV_ISIN].rva))(mgr, item, NULL);
}

static void* pointing_tip(void) {
    void* c = rf_controller(RIGHT);
    void* hc = c ? *(void**)((char*)c + 0x38) : NULL;  // Controller.HandController
    void* tip = rf_alive(hc) ? *(void**)((char*)hc + 0x20) : NULL;  // HandController.PointingFingerTip
    return rf_alive(tip) ? tip : NULL;
}

static float finger_angle(float nx, float ny) {
    float px, py;
    if (!rf_world_to_screen(g_grab.pivot, &px, &py)) return 0;
    return atan2f((ny - py), (nx - px) * 2.1f);  // aspect-corrected screen angle
}

// Twisting starts once the finger is clearly off the pivot (a finger put down on a knob's centre has no angle):
// the first position outside the dead zone becomes the baseline.
#define TWIST_DEADZONE 0.035f
static bool finger_off_pivot(float nx, float ny) {
    float px, py;
    if (!rf_world_to_screen(g_grab.pivot, &px, &py)) return true;
    float dx = (nx - px) * 2.1f, dy = ny - py;
    return sqrtf(dx * dx + dy * dy) > TWIST_DEADZONE;
}

// dev: log every hit along the finger ray
void rf_act_probe(float nx, float ny) {
    v3 o, d;
    if (!rf_screen_ray(nx, ny, &o, &d)) return;
    void* arr = ((void* (*)(v3, v3, float, int32_t, int32_t, const void*))RVA(g_call_desc[C_PHYS_RAYCASTALL].rva))(
        o, d, 6.0f, -1, 2, NULL);
    int32_t n = arr ? (int32_t) * (uint64_t*)((char*)arr + 0x18) : 0;
    raycast_hit* hits = arr ? (raycast_hit*)((char*)arr + 0x20) : NULL;
    void* ctrl = rf_controller(RIGHT);
    rf_log("probe %.3f %.3f: %d hits, dir(%.2f %.2f %.2f)", nx, ny, n, d.x, d.y, d.z);
    for (int i = 0; i < n && i < 32; i++) {
        raycast_hit* h = &hits[i];
        void* col = ((void* (*)(raycast_hit*, const void*))RVA(g_call_desc[C_HIT_COLLIDER].rva))(h, NULL);
        char cn[64] = "?";
        rf_obj_name(col, cn, sizeof cn);
        void* gc = rf_alive(col) ? ((void* (*)(void*, const void*))RVA(g_call_desc[C_GC_FOROBJECT].rva))(rf_gameobject(col), NULL) : NULL;
        void* gr = rf_alive(gc) ? CALLP(C_GC_GRABBABLE, fn_obj1)(gc, NULL) : NULL;
        int can = (rf_alive(gr) && ctrl) ? ((bool (*)(void*, void*, const void*))RVA(g_call_desc[C_GRAB_CANGRAB].rva))(gr, ctrl, NULL) : -1;
        rf_log("  hit %.3f m '%s' (%s) trigger=%d gc=%s canGrab=%d poke=%d grabbable=%s", h->dist, cn, rf_class_name(col),
               rf_alive(col) ? CALLP(C_COL_ISTRIGGER, fn_b1)(col, NULL) : -1, rf_alive(gc) ? rf_class_name(gc) : "-",
               rf_alive(gc) ? *((char*)gc + 0x20) : -1, rf_alive(gc) ? *((char*)gc + 0x21) : -1,
               rf_alive(gr) ? rf_class_name(gr) : "-");
        (void)can;
    }
}

// A part moved by several grabbables at once (CombinationGrabbable: the game passes the same hand position to
// each child). The hand is kept on the surface those children allow, so the part follows the finger:
//  - rotation + slide along the same axis, held by a lever (door bolts: lift and swing): the cylinder around
//    that axis through the grab point; the finger ray's hit gives the swing angle and the lift height
//  - the same, held on the bar itself (a grip on the axis: twist and pull): the finger rolls the bar like a
//    pencil under it — across the bar turns it, along the bar slides it
//  - two slides (sockets moving on a panel): their plane through the grab point
//  - rotation + slide across it: the plane of the slide and the swing direction at the grab point
//  - anything else: the upright plane through the grab point facing the player
static void setup_combination(void* gr, v3 p) {
    void* kids = *(void**)((char*)gr + 0x98);  // CombinationGrabbable.Grabbables
    v3 ra = {0, 0, 0}, rp = {0, 0, 0}, sa[2];
    char* sk = NULL;  // the (first) slider child
    int nr = 0, ns = 0;
    for (int i = 0; i < rf_list_size(kids) && i < 4; i++) {
        void* k = rf_list_get(kids, i);
        if (!rf_alive(k)) continue;
        void* tr = rf_transform(k);
        if (rf_class_is(k, "RotationGrabbable") && !nr) {
            ra = v3_norm(rf_tr_dir(tr, *(v3*)((char*)k + 0x94)));  // RotateAxis
            rp = rf_tr_pos(tr);
            nr++;
        } else if (rf_class_is(k, "SliderGrabbable") && ns < 2) {
            if (!sk) sk = k;
            sa[ns++] = v3_norm(rf_tr_dir(tr, *(v3*)((char*)k + 0x94)));  // LocalSlideAxis
        }
    }
    g_grab.kind = K_PLANE;
    g_grab.axis = (v3){0, 0, 0};  // plane normal; zero = upright plane facing the player
    if (nr && ns == 1 && fabsf(v3_dot(ra, sa[0])) > 0.9f) {
        v3 rel = v3_sub(p, rp);
        g_grab.h0 = v3_dot(rel, ra);
        g_grab.r0 = v3_len(v3_sub(rel, v3_scale(ra, g_grab.h0)));
        v3 n = v3_sub(g_grab.cam0, p);
        n = v3_sub(n, v3_scale(ra, v3_dot(n, ra)));
        if (g_grab.r0 < 0.05f && v3_len(n) > 1e-3f) {
            g_grab.kind = K_ROLL;
            g_grab.axis = ra;
            g_grab.pivot = rp;
            g_grab.roll_n = v3_norm(n);
            v3 radial = v3_sub(rel, v3_scale(ra, g_grab.h0));
            g_grab.radial0 = g_grab.r0 > 1e-3f ? v3_scale(radial, 1.0f / g_grab.r0) : g_grab.roll_n;
            g_grab.roll_r = 0.04f;
            g_grab.r0 = fmaxf(g_grab.r0, 0.03f);  // the hand stays off the axis, where its angle is defined
        } else if (g_grab.r0 > 0.02f) {
            g_grab.kind = K_CYLINDER;
            g_grab.axis = ra;
            g_grab.pivot = rp;
            // the slide limits (LimitSliderModifier Upper/Lower displacement) as heights of the grab point
            float lo = -1e6f, hi = 1e6f;
            void* mods = *(void**)(sk + 0xe0);  // SliderGrabbable.Modifiers
            for (int i = 0; i < rf_list_size(mods); i++) {
                char* m = rf_list_get(mods, i);
                if (!rf_alive(m) || !rf_class_is(m, "LimitSliderModifier")) continue;
                float lim = *(float*)(m + 0x48);  // Displacement
                if (*(int32_t*)(m + 0x44) == 0) hi = fminf(hi, lim);  // LimitType Upper
                else lo = fmaxf(lo, lim);
            }
            float disp = *(float*)(sk + 0xe8), dir = v3_dot(sa[0], ra) > 0 ? 1.0f : -1.0f;  // TotalDisplacement
            float e1 = g_grab.h0 + dir * (lo - disp), e2 = g_grab.h0 + dir * (hi - disp);
            g_grab.hmin = fminf(e1, e2);
            g_grab.hmax = fmaxf(e1, e2);
        }
    } else if (!nr && ns == 2 && v3_len(v3_cross(sa[0], sa[1])) > 0.3f) {
        g_grab.axis = v3_norm(v3_cross(sa[0], sa[1]));
    } else if (nr && ns == 1) {
        v3 rel = v3_sub(p, rp);
        v3 tangent = v3_cross(ra, v3_sub(rel, v3_scale(ra, v3_dot(rel, ra))));
        v3 n = v3_cross(sa[0], tangent);
        if (v3_len(n) > 1e-4f) g_grab.axis = v3_norm(n);
    }
}

// Constraint of the grabbed object: slider axis, rotation axis/pivot, or wrist twist.
static void setup_constraint(void* gr, v3 p, float nx, float ny) {
    void* tr = rf_transform(gr);
    g_grab.kind = K_FREE;
    if (rf_class_is(gr, "SliderGrabbable") || rf_class_is(gr, "RopeGrabbable")) {
        // ropes are pulled along their own LocalSlideAxis (same field offset as SliderGrabbable): each drag along
        // the rope is one hand-over-hand pull
        g_grab.kind = K_SLIDER;
        g_grab.axis = v3_norm(rf_tr_dir(tr, *(v3*)((char*)gr + 0x94)));  // LocalSlideAxis
    } else if (rf_class_is(gr, "CombinationGrabbable")) {
        setup_combination(gr, p);
    } else if (rf_class_is(gr, "RotationGrabbable")) {
        g_grab.kind = *((char*)gr + 0xa8) /* TwistControl */ ? K_TWIST : K_ROTATION;
        g_grab.axis = v3_norm(rf_tr_dir(tr, *(v3*)((char*)gr + 0x94)));  // RotateAxis
        g_grab.pivot = rf_tr_pos(tr);
        v3 rel = v3_sub(p, g_grab.pivot);
        g_grab.h0 = v3_dot(rel, g_grab.axis);
        v3 radial = v3_sub(rel, v3_scale(g_grab.axis, g_grab.h0));
        g_grab.r0 = v3_len(radial);
        if (g_grab.kind == K_ROTATION && g_grab.r0 < 0.015f) {
            // grabbed on the axis of a knob that turns with the hand's position (not the wrist): take hold 2 cm
            // off the axis, towards the top of the screen, so the turn has a lever arm like a VR hand on the knob
            void* cam = rf_main_camera();
            v3 up = cam ? q_rot(rf_tr_rot(rf_transform(cam)), (v3){0, 1, 0}) : (v3){0, 1, 0};
            v3 radial = v3_sub(up, v3_scale(g_grab.axis, v3_dot(up, g_grab.axis)));
            if (v3_len(radial) < 0.2f) {
                v3 right = cam ? q_rot(rf_tr_rot(rf_transform(cam)), (v3){1, 0, 0}) : (v3){1, 0, 0};
                radial = v3_sub(right, v3_scale(g_grab.axis, v3_dot(right, g_grab.axis)));
            }
            g_grab.r0 = 0.02f;
            g_grab.point0 = v3_add(g_grab.pivot, v3_add(v3_scale(g_grab.axis, g_grab.h0), v3_scale(v3_norm(radial), g_grab.r0)));
        }
        g_grab.twist0 = finger_angle(nx, ny);
        g_grab.twist_last = g_grab.twist0;
        g_grab.twist_acc = 0;
        g_grab.twist_armed = finger_off_pivot(nx, ny);
    }
}

int rf_act_grab_begin(float nx, float ny) {
    if (!grabber_transform()) return RF_GRAB_NOPLAYER;
    v3 p;
    float dist;
    void* gr = pick_grabbable(nx, ny, &p, &dist);
    if (!gr) return RF_GRAB_NONE;
    v3 o, d;
    rf_screen_ray(nx, ny, &o, &d);
    if (dist > REACH_M) {
        rf_log("grab: %s at %.2f m is out of reach", rf_class_name(gr), dist);
        return RF_GRAB_FAR;
    }
    memset(&g_grab, 0, sizeof g_grab);
    g_grab.grabbable = gr;
    g_grab.point0 = p;
    g_grab.cam0 = o;
    g_grab.depth = v3_len(v3_sub(p, o));
    g_grab.rot0 = rf_q_look(d, (v3){0, 1, 0});
    g_grab.fx0 = g_grab.fx = nx;
    g_grab.fy0 = g_grab.fy = ny;
    setup_constraint(gr, p, nx, ny);
    g_grab.inv = rf_class_is(gr, "InventoryItemGrabbable") && in_inventory(gr);
    g_grab.inv_dir = d;
    g_grab.target = g_grab.point0;
    g_grab.target_rot = g_grab.rot0;
    g_grab.state = G_HOVER;
    g_grab.frames = 0;
    rf_log("grab: %s kind=%d inv=%d dist=%.2f axis(%.2f %.2f %.2f) r0=%.3f h=%.3f..%.3f twistControl=%d", rf_class_name(gr),
           g_grab.kind, g_grab.inv, dist, g_grab.axis.x, g_grab.axis.y, g_grab.axis.z, g_grab.r0, g_grab.hmin, g_grab.hmax,
           rf_class_is(gr, "RotationGrabbable") ? *((char*)gr + 0xa8) : -1);
    return RF_GRAB_OK;
}

void rf_act_grab_drag(float nx, float ny) {
    g_grab.fx = nx;
    g_grab.fy = ny;
}

void rf_act_grab_end(void) {
    if (g_grab.state == G_HOVER || g_grab.state == G_HOLD) {
        g_grab.state = G_RELEASE;
        g_grab.frames = 0;
    }
}

bool rf_act_grabbing(void) { return g_grab.state != G_IDLE; }

void rf_act_grab_depth(float depth) {
    if (g_grab.state == G_HOLD || g_grab.state == G_HOVER) g_grab.depth = depth;
}

// ---- inventory receivers (keyholes, sockets): InventoryReceiver.AllReceivers (static field 0) ----
static void* static_field0(uintptr_t meta) {
    uintptr_t k = *(uintptr_t*)(g_il2cpp_base + meta);
    if (!k || (k & 1) || k < 0x10000) return NULL;
    void* statics = *(void**)(k + 0xb8);
    return statics ? *(void**)statics : NULL;
}

static bool list_contains(void* list, void* obj) {
    for (int i = 0; i < rf_list_size(list); i++)
        if (rf_list_get(list, i) == obj) return true;
    return false;
}

// When the finger is over a receiver that accepts the held item, returns the grabber position that puts
// the item's probe point (InventoryItemGrabbable.ProbeTransform) on the receiver centre.
static bool receiver_magnet(float nx, float ny, v3* grabber_target) {
    void* ctrl = rf_controller(RIGHT);
    void* held = ctrl ? CALLP(C_CTRL_GRABBED, fn_obj1)(ctrl, NULL) : NULL;
    if (!rf_alive(held) || !rf_class_is(held, "InventoryItemGrabbable")) return false;
    void* all = static_field0(META_INVRECV_TYPEINFO);
    void* gt = grabber_transform();
    if (!all || !gt) return false;
    // The game's own probe point (InventoryItemGrabbable.GetBestTarget):
    //   Controller.InventoryPoint.TransformPoint(GrabTransform.InverseTransformPoint(ProbeTransform.position))
    // It moves rigidly with the hand, so shifting the grabber by (centre - probe) lands the probe on the
    // centre; recomputed every frame (closed loop) because the held item lerps after the hand.
    void* inv_pt = *(void**)((char*)ctrl + 0x20);  // Controller.InventoryPoint
    if (!rf_alive(inv_pt)) return false;
    void* probe = *(void**)((char*)held + 0xd8);
    void* grab_t = *(void**)((char*)held + 0xb0);
    if (!rf_alive(grab_t)) grab_t = rf_transform(held);
    v3 probe_p = rf_tr_pos(rf_alive(probe) ? probe : rf_transform(held));
    v3 local = ((v3(*)(void*, v3, const void*))RVA(g_call_desc[C_TR_INVPOINT].rva))(grab_t, probe_p, NULL);
    v3 pgame = ((v3(*)(void*, v3, const void*))RVA(g_call_desc[C_TR_POINT].rva))(inv_pt, local, NULL);
    v3 off = v3_sub(pgame, rf_tr_pos(gt));
    float best = 0.075f;
    bool found = false;
    for (int i = 0; i < rf_list_size(all) && i < 256; i++) {
        void* r = rf_list_get(all, i);
        if (!rf_alive(r) || !list_contains(*(void**)((char*)r + 0x50) /* ValidItems */, held)) continue;
        v3 c = ((v3(*)(void*, const void*))RVA(g_call_desc[C_RECV_CENTRE].rva))(r, NULL);
        float sx, sy;
        if (!rf_world_to_screen(c, &sx, &sy)) continue;
        float dx = (sx - nx) * 2.1f, dy = sy - ny, dd = sqrtf(dx * dx + dy * dy);
        if (dd < best) {
            best = dd;
            *grabber_target = v3_sub(c, off);
            found = true;
            if (g_dev && (g_grab.frames % 30) == 0) {
                char nm[64];
                rf_obj_name(r, nm, sizeof nm);
                void* cur = *(void**)((char*)held + 0x168);
                rf_log("magnet: receiver '%s' d=%.3f centre(%.2f %.2f %.2f) off(%.3f %.3f %.3f) radius=%.3f sticky=%.3f target=%s snapped=%d state=%d",
                       nm, dd, c.x, c.y, c.z, off.x, off.y, off.z, *(float*)((char*)r + 0x78), *(float*)((char*)held + 0xe0),
                       cur == r ? "this" : (cur ? "other" : "-"), *((char*)held + 0x170), *(int32_t*)((char*)held + 0x190));
                rf_log("magnet: receiver state=%d currentItem=%p sincePickup=%.2f conditions=%d requireRelease=%d",
                       *(int32_t*)((char*)r + 0x40), *(void**)((char*)r + 0xa8), *(float*)((char*)r + 0xb0),
                       *(int32_t*)((char*)r + 0x3c), *((char*)r + 0x94));
            }
        }
    }
    return found;
}

static void* static_field0(uintptr_t meta);

static bool child_of(void* t, void* root) {
    for (int i = 0; i < 12 && rf_alive(t); i++, t = rf_tr_parent(t))
        if (t == root) return true;
    return false;
}

// Depth of the first solid surface under the finger (world colliders), ignoring the held object and the player's
// own hands/head; <= 0 when nothing is hit within reach.
static float surface_depth(v3 o, v3 d, void* held) {
    void* arr = ((void* (*)(v3, v3, float, int32_t, int32_t, const void*))RVA(g_call_desc[C_PHYS_RAYCASTALL].rva))(
        o, d, REACH_M + 0.3f, -1, 1 /* QueryTriggerInteraction.Ignore */, NULL);
    if (!arr) return 0;
    int32_t n = (int32_t) * (uint64_t*)((char*)arr + 0x18);
    raycast_hit* hits = (raycast_hit*)((char*)arr + 0x20);
    void* held_t = rf_alive(held) ? rf_transform(held) : NULL;
    void* hud = static_field0(META_HUDMGR_TYPEINFO);  // the open inventory grid has colliders of its own
    void* hud_t = rf_alive(hud) ? *(void**)((char*)hud + 0x18) : NULL;  // HudManager.LayoutParent
    float best = 0;
    for (int i = 0; i < n && i < 64; i++) {
        raycast_hit* h = &hits[i];
        if (h->dist < 0.15f || (best > 0 && h->dist >= best)) continue;
        void* col = ((void* (*)(raycast_hit*, const void*))RVA(g_call_desc[C_HIT_COLLIDER].rva))(h, NULL);
        if (!rf_alive(col) || (held_t && child_of(rf_transform(col), held_t))) continue;
        if (rf_alive(hud_t) && child_of(rf_transform(col), hud_t)) continue;
        best = h->dist;
    }
    return best;
}

static v3 drag_target(void) {
    v3 c, d;
    if (!rf_screen_ray(g_grab.fx, g_grab.fy, &c, &d)) return g_grab.target;
    switch (g_grab.kind) {
    case K_SLIDER: {
        // point of the slide line closest to the finger ray
        v3 a = g_grab.axis, w0 = v3_sub(g_grab.point0, c);
        float b = v3_dot(a, d), dd = v3_dot(a, w0), e = v3_dot(d, w0);
        float den = 1.0f - b * b;
        if (den < 0.02f) {
            // slide axis along the view ray: vertical finger motion pulls (down) / pushes (up)
            float t = (g_grab.fy - g_grab.fy0) * 0.8f;
            return v3_add(g_grab.point0, v3_scale(a, v3_dot(a, v3_sub(g_grab.cam0, g_grab.point0)) > 0 ? t : -t));
        }
        float t = (b * e - dd) / den;
        return v3_add(g_grab.point0, v3_scale(a, t));
    }
    case K_ROTATION: {
        v3 n = g_grab.axis;
        float dn = v3_dot(d, n);
        if (fabsf(dn) > 0.08f) {
            float s = v3_dot(v3_sub(g_grab.pivot, c), n) / dn;
            if (s > 0) {
                v3 hit = v3_add(c, v3_scale(d, s));
                v3 rel = v3_sub(hit, g_grab.pivot);
                v3 radial = v3_sub(rel, v3_scale(n, v3_dot(rel, n)));
                if (v3_len(radial) > 1e-4f)
                    return v3_add(g_grab.pivot, v3_add(v3_scale(n, g_grab.h0), v3_scale(v3_norm(radial), g_grab.r0)));
            }
        }
        // rotation plane edge-on: fall through to a camera-plane drag
        return v3_add(c, v3_scale(d, g_grab.depth));
    }
    case K_TWIST:
        return g_grab.point0;
    case K_PLANE: {
        v3 n = g_grab.axis;
        if (v3_len(n) < 0.5f || fabsf(v3_dot(d, n)) < 0.15f) {
            // no plane of its own, or seen edge-on: the upright plane through the grab point facing the player
            n = v3_sub(g_grab.point0, g_grab.cam0);
            n.y = 0;
            if (v3_len(n) < 1e-3f) return v3_add(c, v3_scale(d, g_grab.depth));
            n = v3_norm(n);
        }
        float dn = v3_dot(d, n);
        if (fabsf(dn) < 0.05f) return v3_add(c, v3_scale(d, g_grab.depth));
        float s = v3_dot(v3_sub(g_grab.point0, c), n) / dn;
        if (s <= 0) return g_grab.target;
        return v3_add(c, v3_scale(d, s));
    }
    case K_CYLINDER: {
        // The finger ray against the cylinder (axis through the pivot, radius r0): the grabbed point stays under
        // the finger. A ray crosses the cylinder twice (the side facing the eyes and the far side) — seen from
        // above, a handle at rest can sit on the far side and has to come round the outline to swing out. Of the
        // two, the one the slide limits allow wins (at the bottom a drag towards the player swings the handle out
        // instead of pushing it below its lowest position; at the top a drag past the outline swings it back
        // over), otherwise the one nearer the current hand position.
        v3 a = g_grab.axis, w = v3_sub(c, g_grab.pivot);
        v3 dp = v3_sub(d, v3_scale(a, v3_dot(d, a))), wp = v3_sub(w, v3_scale(a, v3_dot(w, a)));
        float A = v3_dot(dp, dp), B = 2.0f * v3_dot(dp, wp), C = v3_dot(wp, wp) - g_grab.r0 * g_grab.r0;
        if (A < 1e-4f) return g_grab.target;  // looking along the axis
        float disc = B * B - 4.0f * A * C, sq = disc > 0 ? sqrtf(disc) : 0;
        if (disc < 0) {
            // Off the outline the part rolls on over it: from the outline point (the ray's closest approach) it
            // turns on towards the far side by the arc the finger has gone past the outline. Coming back inside,
            // the far side is then the nearer of the two hits, so a twist can go all the way round.
            float s = -B / (2.0f * A);
            if (s <= 0) return g_grab.target;
            v3 rel = v3_sub(v3_add(c, v3_scale(d, s)), g_grab.pivot);
            float h = v3_dot(rel, a);
            v3 radial = v3_sub(rel, v3_scale(a, h));
            float dist = v3_len(radial);
            if (dist < 1e-4f) return g_grab.target;
            float beta = fminf((dist - g_grab.r0) / g_grab.r0, 2.0f);
            v3 r = v3_add(v3_scale(radial, cosf(beta) / dist), v3_scale(v3_norm(dp), sinf(beta)));
            return v3_add(g_grab.pivot, v3_add(v3_scale(a, h), v3_scale(r, g_grab.r0)));
        }
        float cand[2] = {(-B - sq) / (2.0f * A), (-B + sq) / (2.0f * A)};
        v3 best = g_grab.target;
        float best_score = 1e9f;
        for (int i = 0; i < 2; i++) {
            if (cand[i] <= 0) continue;
            v3 rel = v3_sub(v3_add(c, v3_scale(d, cand[i])), g_grab.pivot);
            float h = v3_dot(rel, a);
            v3 radial = v3_sub(rel, v3_scale(a, h));
            if (v3_len(radial) < 1e-4f) continue;
            v3 t = v3_add(g_grab.pivot, v3_add(v3_scale(a, h), v3_scale(v3_norm(radial), g_grab.r0)));
            float out = h < g_grab.hmin ? g_grab.hmin - h : h > g_grab.hmax ? h - g_grab.hmax : 0;
            float score = (out > 0.003f ? 1.0f + out : 0) + v3_len(v3_sub(t, g_grab.target));
            if (score < best_score) {
                best_score = score;
                best = t;
            }
        }
        return best;
    }
    case K_ROLL: {
        // the finger on the plane through the grab point facing the eyes: its move along the bar slides it, across
        // the bar turns it by the arc the finger travelled (the surface under the finger moves with it)
        v3 n = g_grab.roll_n, a = g_grab.axis;
        float dn = v3_dot(d, n);
        if (fabsf(dn) < 0.05f) return g_grab.target;
        float s = v3_dot(v3_sub(g_grab.point0, c), n) / dn;
        if (s <= 0) return g_grab.target;
        v3 w = v3_sub(v3_add(c, v3_scale(d, s)), g_grab.point0);
        float phi = v3_dot(w, v3_cross(a, n)) / g_grab.roll_r;
        v3 r = g_grab.radial0;  // turned about the axis by phi (Rodrigues; r is perpendicular to the axis)
        r = v3_add(v3_scale(r, cosf(phi)), v3_scale(v3_cross(a, r), sinf(phi)));
        return v3_add(g_grab.pivot, v3_add(v3_scale(a, g_grab.h0 + v3_dot(w, a)), v3_scale(r, g_grab.r0)));
    }
    default: {
        v3 t;
        if (receiver_magnet(g_grab.fx, g_grab.fy, &t)) return t;
        // A VR player carries a held object to whatever they reach for: the object follows the surface under the
        // finger (just in front of it), smoothed, within arm's reach. Over empty space it keeps its distance.
        void* held = CALLP(C_CTRL_GRABBED, fn_obj1)(rf_controller(RIGHT), NULL);
        float sd = (g_grab.frames > 6) ? surface_depth(c, d, held) : 0;
        if (g_dev && (g_grab.frames % 20) == 0) rf_log("carry: surface %.2f m, depth %.2f m", sd, g_grab.depth);
        if (sd > 0) {
            float want = sd - 0.06f;
            if (want < 0.25f) want = 0.25f;
            if (want > REACH_M) want = REACH_M;
            g_grab.depth += (want - g_grab.depth) * 0.25f;
        }
        return v3_add(c, v3_scale(d, g_grab.depth));
    }
    }
}

static void tick_grab(void) {
    if (g_grab.state == G_IDLE) return;
    void* gt = grabber_transform();
    g_grab.frames++;
    if (!gt) {
        g_hand[RIGHT].grip = 0;
        g_grab.state = G_IDLE;
        rf_hand_rest(RIGHT);
        return;
    }
    if (g_grab.state == G_HOLD) {
        g_grab.target = drag_target();
        if (g_dev && g_grab.kind == K_ROTATION && (g_grab.frames % 10) == 0 && rf_class_is(g_grab.grabbable, "RotationGrabbable")) {
            v3 o, d;
            float fx = -1, fy = -1;
            rf_world_to_screen(g_grab.target, &fx, &fy);
            rf_screen_ray(g_grab.fx, g_grab.fy, &o, &d);
            rf_log("rotate: finger(%.3f %.3f) target screen(%.3f %.3f) dn=%.2f totalRotation=%.1f", g_grab.fx, g_grab.fy, fx, fy,
                   v3_dot(d, g_grab.axis), *(float*)((char*)g_grab.grabbable + 0x118));
        }
        if (g_dev && (g_grab.kind == K_CYLINDER || g_grab.kind == K_ROLL) && (g_grab.frames % 15) == 0) {
            // hand angle about the axis (from the grab direction) and position along it, vs the children's state
            v3 a = g_grab.axis, r0v = v3_sub(g_grab.point0, g_grab.pivot), rv = v3_sub(g_grab.target, g_grab.pivot);
            r0v = v3_sub(r0v, v3_scale(a, v3_dot(r0v, a)));
            float h = v3_dot(rv, a);
            rv = v3_sub(rv, v3_scale(a, h));
            float ang = atan2f(v3_dot(v3_cross(r0v, rv), a), v3_dot(r0v, rv)) / DEG2RAD;
            void* kids = *(void**)((char*)g_grab.grabbable + 0x98);
            float rot = 0, disp = 0;
            for (int i = 0; i < rf_list_size(kids); i++) {
                char* k = rf_list_get(kids, i);
                if (rf_class_is(k, "RotationGrabbable")) rot = *(float*)(k + 0x118);
                else if (rf_class_is(k, "SliderGrabbable")) disp = *(float*)(k + 0xe8);
            }
            rf_log("combo: finger(%.3f %.3f) hand angle %.1f along %.3f (grab %.3f) -> rotation %.1f displacement %.3f",
                   g_grab.fx, g_grab.fy, ang, h, g_grab.h0, rot, disp);
        }
        if (g_grab.kind == K_TWIST && !g_grab.twist_armed && finger_off_pivot(g_grab.fx, g_grab.fy)) {
            g_grab.twist_armed = true;
            g_grab.twist0 = finger_angle(g_grab.fx, g_grab.fy);
            g_grab.twist_last = g_grab.twist0;
            g_grab.twist_acc = 0;
            g_grab.rot0 = g_grab.target_rot;
        }
        if (g_grab.kind == K_TWIST && g_grab.twist_armed) {
            // accumulate the per-frame change (unwrapped across +-180) so circling keeps turning a crank
            float a = finger_angle(g_grab.fx, g_grab.fy), step = a - g_grab.twist_last;
            while (step > (float)M_PI) step -= 2.0f * (float)M_PI;
            while (step < -(float)M_PI) step += 2.0f * (float)M_PI;
            g_grab.twist_last = a;
            g_grab.twist_acc += step;
            float da = g_grab.twist_acc;
            // the finger angle (screen, y down) grows clockwise as seen by the player, while a positive Unity
            // rotation about an axis pointing into the screen is counter-clockwise: follow the finger 1:1
            void* cam = rf_main_camera();
            v3 fwd = cam ? q_rot(rf_tr_rot(rf_transform(cam)), (v3){0, 0, 1}) : (v3){0, 0, 1};
            float sign = v3_dot(g_grab.axis, fwd) > 0 ? 1.0f : -1.0f;
            g_grab.target_rot = q_mul(q_axis(g_grab.axis, -sign * da), g_grab.rot0);
            if (g_dev && (g_grab.frames % 10) == 0 && rf_class_is(g_grab.grabbable, "RotationGrabbable")) {
                char* gr = (char*)g_grab.grabbable;
                quat cg = *(quat*)(gr + 0xe4), cc = *(quat*)(gr + 0xf4);
                rf_log("twist: finger da=%.1f deg -> totalRotation=%.1f grabOri(%.2f %.2f %.2f %.2f) curOri(%.2f %.2f %.2f %.2f)",
                       da / DEG2RAD, *(float*)(gr + 0x118), cg.x, cg.y, cg.z, cg.w, cc.x, cc.y, cc.z, cc.w);
            }
        }
    }
    if (g_grab.inv && g_grab.state == G_HOVER && pointing_tip()) {
        // inventory slots are selected by the pointing finger ray (InventoryHud.RaycastInventory: starts 0.10 m
        // behind the fingertip, 0.20 m long), so the tip hovers 5 cm in front of the slot
        rf_aim_transform(RIGHT, pointing_tip(), v3_sub(g_grab.point0, v3_scale(g_grab.inv_dir, 0.05f)), g_grab.inv_dir);
    } else {
        rf_place_transform(RIGHT, gt, g_grab.target, g_grab.target_rot);
    }
    switch (g_grab.state) {
    case G_HOVER: {
        // close the grip once the controller's own probe reports a grabbable under the hand
        // (Controller.ClosestGrabbableCollider +0x80; inventory slots highlight a few frames later)
        void* ctrl = rf_controller(RIGHT);
        void* closest = ctrl ? *(void**)((char*)ctrl + 0x80) : NULL;
        if ((g_grab.frames >= HOVER_FRAMES && rf_alive(closest)) || g_grab.frames >= 30) {
            g_hand[RIGHT].grip = 1.0f;
            g_grab.state = G_HOLD;
            g_grab.frames = 0;
            if (g_dev) rf_log("grab: grip after %d hover frames (probe %s)", g_grab.frames, rf_alive(closest) ? "ready" : "timeout");
        }
        break;
    }
    case G_HOLD: {
        // the game may hand the grab over to another object (key inserted into a lock becomes the lock's
        // rotating key): re-derive the drag constraint from what the hand really holds now
        void* held = CALLP(C_CTRL_GRABBED, fn_obj1)(rf_controller(RIGHT), NULL);
        if (rf_alive(held) && held != g_grab.grabbable && !rf_class_is(held, "InventoryItemGrabbable")) {
            // The game handed the grab to another object (an item placed into its receiver becomes e.g. the
            // lock's rotating key) without a grab start. A VR player lets go and grabs the new object to turn
            // it: release for a few frames and grab it again on the spot.
            v3 hp = rf_tr_pos(gt);
            g_grab.grabbable = held;
            g_grab.point0 = hp;
            g_grab.target = hp;
            g_grab.rot0 = g_grab.target_rot;
            g_grab.inv = false;
            v3 o, d;
            if (rf_screen_ray(g_grab.fx, g_grab.fy, &o, &d)) g_grab.depth = v3_len(v3_sub(hp, o));
            setup_constraint(held, hp, g_grab.fx, g_grab.fy);
            g_hand[RIGHT].grip = 0;
            g_grab.state = G_REGRAB;
            g_grab.frames = 0;
            rf_log("grab: transferred to %s kind=%d axis(%.2f %.2f %.2f) r0=%.3f -> re-grab", rf_class_name(held),
                   g_grab.kind, g_grab.axis.x, g_grab.axis.y, g_grab.axis.z, g_grab.r0);
            break;
        }
    }
        if (g_grab.frames == 3 || g_grab.frames == 12) {
            void* held = CALLP(C_CTRL_GRABBED, fn_obj1)(rf_controller(RIGHT), NULL);
            rf_log("grab: holding %s (%s)", rf_alive(held) ? rf_class_name(held) : "nothing",
                   held == g_grab.grabbable ? "picked" : "other/none");
        }
        break;
    case G_REGRAB:
        // grip released for 4 frames, then back to hover: the grip closes again once the probe sees it
        if (g_grab.frames >= 4) {
            g_grab.state = G_HOVER;
            g_grab.frames = 0;
            g_grab.twist0 = finger_angle(g_grab.fx, g_grab.fy);
            g_grab.twist_last = g_grab.twist0;
            g_grab.twist_acc = 0;
            g_grab.twist_armed = finger_off_pivot(g_grab.fx, g_grab.fy);
        }
        break;
    case G_RELEASE:
        g_hand[RIGHT].grip = 0;
        if (g_grab.frames > 6) {
            g_grab.state = G_IDLE;
            rf_hand_rest(RIGHT);
        }
        break;
    }
}

// ------------------------------------------------------------------------------------------ teleport
static struct {
    bool on;
    int frames;   // frames since mode on
    int aim_frames;
    void* target_loc;
    v3 target_pt;
    bool forced;  // the pointer's arc ends on another location in front: teleport through Player.TeleportTo
} g_tp;

static void* teleporter_transform(void** tp_out) {
    void* c = rf_controller(RIGHT);
    if (!c) return NULL;
    void* tp = *(void**)((char*)c + 0x50);  // Controller.Teleporter
    if (!rf_alive(tp)) return NULL;
    if (tp_out) *tp_out = tp;
    return rf_transform(tp);
}

void rf_act_teleport_mode(bool on) {
    if (on == g_tp.on) return;
    g_tp.on = on;
    g_tp.frames = 0;
    g_tp.aim_frames = 0;
    g_tp.target_loc = NULL;
    if (on) {
        // aim at the floor just ahead so the pointer starts on nothing teleportable
        g_hand[RIGHT].stick = (v2){0, 1};
    } else {
        // release with the pointer aimed at the sky: TeleportIfPossible finds no location
        void* tt = teleporter_transform(NULL);
        if (tt) rf_aim_transform(RIGHT, tt, rf_tr_pos(tt), (v3){0, 1, 0});
        g_hand[RIGHT].stick = (v2){0, 0};
    }
    rf_log("teleport mode %s", on ? "on" : "off");
}

bool rf_act_teleport_mode_on(void) { return g_tp.on; }

// Picks the active teleport location whose marker is nearest to the tap (screen space).
bool rf_act_teleport_tap(float nx, float ny) {
    void* tp = NULL;
    if (!g_tp.on || !teleporter_transform(&tp)) return false;
    void* locs = *(void**)((char*)tp + 0xb8);  // TeleportPointer.Locations
    float best = 0.09f;  // normalised distance
    void* best_loc = NULL;
    v3 best_pt = {0, 0, 0};
    for (int i = 0; i < rf_list_size(locs) && i < 64; i++) {
        void* loc = rf_list_get(locs, i);
        if (!rf_alive(loc) || *(int32_t*)((char*)loc + 0x21c) != 0 /* State.Active */) continue;
        v3 base = rf_tr_pos(rf_transform(loc));
        v3 pts[3] = {base, v3_add(base, (v3){0, 0.9f, 0}), v3_add(base, (v3){0, 1.5f, 0})};
        for (int k = 0; k < 3; k++) {
            float sx, sy;
            if (!rf_world_to_screen(pts[k], &sx, &sy)) continue;
            float dx = (sx - nx) * 2.1f, dy = sy - ny, dd = sqrtf(dx * dx + dy * dy);
            if (dd < best) {
                best = dd;
                best_loc = loc;
                best_pt = pts[1];  // inside the location's capsule collider
            }
        }
    }
    // superzoom portals (miniature worlds): the same pointer enters them through their zoom-in / zoom-out
    // trigger colliders (Superzoom.ZoomInTrigger +0xE0, PortalTrigger +0xD0, ZoomOutTrigger +0x130)
    void* zooms = *(void**)((char*)tp + 0xd0);  // TeleportPointer.SuperZooms
    for (int i = 0; i < rf_list_size(zooms) && i < 32; i++) {
        void* z = rf_list_get(zooms, i);
        if (!rf_alive(z)) continue;
        static const int offs[3] = {0xe0, 0xd0, 0x130};
        for (int k = 0; k < 3; k++) {
            void* col = *(void**)((char*)z + offs[k]);
            if (!rf_alive(col)) continue;
            void* go = rf_gameobject(col);
            if (!rf_alive(go) || !CALLP(C_GO_ACTIVEH, fn_b1)(go, NULL)) continue;
            v3 c = rf_tr_pos(rf_transform(col));
            float sx, sy;
            if (!rf_world_to_screen(c, &sx, &sy)) continue;
            float dx = (sx - nx) * 2.1f, dy = sy - ny, dd = sqrtf(dx * dx + dy * dy);
            if (dd < best) {
                best = dd;
                best_loc = z;
                best_pt = c;
            }
        }
    }
    if (!best_loc) return false;
    char nm[64];
    rf_obj_name(best_loc, nm, sizeof nm);
    rf_log("teleport: tap selects '%s' %s (d=%.3f)", nm, rf_class_name(best_loc), best);
    g_tp.target_loc = best_loc;
    g_tp.target_pt = best_pt;
    g_tp.aim_frames = 1;
    g_tp.forced = false;
    return true;
}

static void tick_teleport(void) {
    if (!g_tp.on) return;
    g_tp.frames++;
    void* tp = NULL;
    void* tt = teleporter_transform(&tp);
    if (!tt) return;
    if (g_tp.aim_frames) {
        g_tp.aim_frames++;
        v3 o = rf_tr_pos(rf_transform(rf_main_camera()));
        v3 from = v3_add(o, (v3){0, -0.25f, 0});
        if (g_tp.forced) rf_aim_transform(RIGHT, tt, from, (v3){0, 1, 0});  // at nothing while we teleport
        else rf_aim_transform(RIGHT, tt, from, v3_norm(v3_sub(g_tp.target_pt, from)));
        if (g_tp.aim_frames == 16) {
            void* pointed = *(void**)((char*)tp + 0xb0), *pointed_z = *(void**)((char*)tp + 0xc8);
            bool on_target = pointed == g_tp.target_loc || pointed_z == g_tp.target_loc;
            if (on_target || !rf_class_is(g_tp.target_loc, "PlayerLocation") || !rf_alive(g_player)) {
                rf_log("teleport: pointer on %s", on_target ? "target" : "something else");
                g_hand[RIGHT].stick = (v2){0, 0};  // release -> TeleportIfPossible
            } else {
                // a VR player bends the arc over the nearer location; a tap cannot, so the tapped (active) location
                // is used directly: aim the pointer at nothing, release, then the game's own Player.TeleportTo
                g_tp.forced = true;
            }
        }
        if (g_tp.forced && g_tp.aim_frames == 20) {
            g_hand[RIGHT].stick = (v2){0, 0};
            CALLP(C_PLAYER_TELEPORTTO, void (*)(void*, void*, const void*))(g_player, g_tp.target_loc, NULL);
            rf_log("teleport: arc ended on another location; Player.TeleportTo the tapped one");
        }
        if (g_tp.aim_frames > (g_tp.forced ? 24 : 20)) {
            g_tp.on = false;
            g_tp.aim_frames = 0;
            rf_hand_rest(RIGHT);
        }
    } else {
        // keep the (invisible) pointer aimed down at the player's feet while choosing
        v3 o = rf_tr_pos(rf_transform(rf_main_camera()));
        rf_aim_transform(RIGHT, tt, v3_add(o, (v3){0, -0.3f, 0}), (v3){0, -1, 0});
    }
}

// ------------------------------------------------------------------------------------------ buttons
static struct {
    int frames[2][3];
    int snap;   // remaining frames of a snap-turn stick push
    int snap_dir;
} g_btn;

void rf_act_button(int h, int which, int frames) { g_btn.frames[h][which] = frames; }

void rf_act_snap_turn(int dir) {
    g_btn.snap = 6;
    g_btn.snap_dir = dir;
}

static void tick_buttons(void) {
    for (int h = 0; h < 2; h++) {
        g_hand[h].btn_primary = g_btn.frames[h][0] > 0;
        g_hand[h].btn_secondary = g_btn.frames[h][1] > 0;
        g_hand[h].btn_menu = g_btn.frames[h][2] > 0;
        for (int k = 0; k < 3; k++)
            if (g_btn.frames[h][k] > 0) g_btn.frames[h][k]--;
    }
    if (g_btn.snap > 0) {
        g_hand[RIGHT].stick.x = (float)g_btn.snap_dir;
        if (--g_btn.snap == 0) g_hand[RIGHT].stick.x = 0;
    }
}

// dev: keep the pointing finger aimed through a screen point at a given depth
static struct { bool on; float x, y, depth; } g_dev_tip;
void rf_act_dev_aim_tip(bool on, float x, float y, float depth) {
    g_dev_tip.on = on;
    g_dev_tip.x = x;
    g_dev_tip.y = y;
    g_dev_tip.depth = depth;
    if (!on) rf_hand_rest(RIGHT);
}
static void tick_dev_tip(void) {
    if (!g_dev_tip.on || !pointing_tip()) return;
    v3 o, d;
    if (!rf_screen_ray(g_dev_tip.x, g_dev_tip.y, &o, &d)) return;
    rf_aim_transform(RIGHT, pointing_tip(), v3_add(o, v3_scale(d, g_dev_tip.depth)), d);
}

void rf_interact_tick(void) {
    tick_dev_tip();
    tick_buttons();
    tick_click();
    tick_teleport();
    tick_grab();
}
