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
static struct {
    int frames;
    float x, y;
} g_click;

void rf_act_laser_click(float nx, float ny) {
    g_click.frames = 1;
    g_click.x = nx;
    g_click.y = ny;
}

static void* menu_ray_transform(void) {
    void* rt = rf_alive(g_vrim) ? *(void**)((char*)g_vrim + 0x58) : NULL;  // VrInputModule.rayTransform
    return rf_alive(rt) ? rt : NULL;
}

static void tick_click(void) {
    if (!g_click.frames) return;
    int f = g_click.frames++;
    v3 o, d;
    void* rt = menu_ray_transform();
    if (f <= 14 && rt && rf_screen_ray(g_click.x, g_click.y, &o, &d)) {
        // laser origin 30 cm behind the eye on the same ray: the beam passes exactly under the finger
        rf_aim_transform(RIGHT, rt, v3_sub(o, v3_scale(d, 0.30f)), d);
    }
    g_hand[RIGHT].trigger = (f >= 4 && f < 10) ? 1.0f : 0.0f;
    if (f > 16) {
        g_click.frames = 0;
        g_hand[RIGHT].trigger = 0;
        rf_hand_rest(RIGHT);
    }
}

// ------------------------------------------------------------------------------------------ grabbing
enum { K_FREE = 0, K_SLIDER = 1, K_ROTATION = 2, K_TWIST = 3 };
enum { G_IDLE = 0, G_HOVER, G_HOLD, G_RELEASE };

static struct {
    int state, frames, kind;
    void* grabbable;
    v3 point0;   // grab point (world) at grab time
    v3 cam0;     // camera position at grab time
    float depth; // camera distance to the grab point
    v3 axis;     // slider direction / rotation axis (world, unit)
    v3 pivot;    // rotation pivot (world)
    float h0, r0;
    quat rot0;   // grabber rotation at grab time
    float fx0, fy0, fx, fy;
    float twist0;  // finger angle around the projected pivot at grab time
    v3 target;
    quat target_rot;
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
    for (int i = 0; i < g_ngc && shown < 40; i++) {
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

static float finger_angle(float nx, float ny) {
    float px, py;
    if (!rf_world_to_screen(g_grab.pivot, &px, &py)) return 0;
    return atan2f((ny - py), (nx - px) * 2.1f);  // aspect-corrected screen angle
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
    void* tr = rf_transform(gr);
    if (rf_class_is(gr, "SliderGrabbable")) {
        g_grab.kind = K_SLIDER;
        g_grab.axis = v3_norm(rf_tr_dir(tr, *(v3*)((char*)gr + 0x94)));  // LocalSlideAxis
    } else if (rf_class_is(gr, "RotationGrabbable")) {
        g_grab.kind = *((char*)gr + 0xa8) /* TwistControl */ ? K_TWIST : K_ROTATION;
        g_grab.axis = v3_norm(rf_tr_dir(tr, *(v3*)((char*)gr + 0x94)));  // RotateAxis
        g_grab.pivot = rf_tr_pos(tr);
        v3 rel = v3_sub(p, g_grab.pivot);
        g_grab.h0 = v3_dot(rel, g_grab.axis);
        v3 radial = v3_sub(rel, v3_scale(g_grab.axis, g_grab.h0));
        g_grab.r0 = v3_len(radial);
        if (g_grab.kind == K_ROTATION && g_grab.r0 < 0.015f) g_grab.kind = K_TWIST;  // grabbed on the axis
        g_grab.twist0 = finger_angle(nx, ny);
    }
    g_grab.target = p;
    g_grab.target_rot = g_grab.rot0;
    g_grab.state = G_HOVER;
    g_grab.frames = 0;
    rf_log("grab: %s kind=%d dist=%.2f axis(%.2f %.2f %.2f) r0=%.3f", rf_class_name(gr), g_grab.kind, dist,
           g_grab.axis.x, g_grab.axis.y, g_grab.axis.z, g_grab.r0);
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
    default:
        return v3_add(c, v3_scale(d, g_grab.depth));
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
        if (g_grab.kind == K_TWIST) {
            float da = finger_angle(g_grab.fx, g_grab.fy) - g_grab.twist0;
            // screen angle grows counter-clockwise as seen by the player; the axis may point either way
            void* cam = rf_main_camera();
            v3 fwd = cam ? q_rot(rf_tr_rot(rf_transform(cam)), (v3){0, 0, 1}) : (v3){0, 0, 1};
            float sign = v3_dot(g_grab.axis, fwd) > 0 ? 1.0f : -1.0f;
            g_grab.target_rot = q_mul(q_axis(g_grab.axis, sign * da), g_grab.rot0);
        }
    }
    rf_place_transform(RIGHT, gt, g_grab.target, g_grab.target_rot);
    switch (g_grab.state) {
    case G_HOVER:
        if (g_grab.frames >= HOVER_FRAMES) {
            g_hand[RIGHT].grip = 1.0f;
            g_grab.state = G_HOLD;
            g_grab.frames = 0;
        }
        break;
    case G_HOLD:
        if (g_grab.frames == 3) {
            void* held = CALLP(C_CTRL_GRABBED, fn_obj1)(rf_controller(RIGHT), NULL);
            rf_log("grab: holding %s (%s)", rf_alive(held) ? rf_class_name(held) : "nothing",
                   held == g_grab.grabbable ? "picked" : "other/none");
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
    if (!best_loc) return false;
    char nm[64];
    rf_obj_name(best_loc, nm, sizeof nm);
    rf_log("teleport: tap selects '%s' (d=%.3f)", nm, best);
    g_tp.target_loc = best_loc;
    g_tp.target_pt = best_pt;
    g_tp.aim_frames = 1;
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
        rf_aim_transform(RIGHT, tt, from, v3_norm(v3_sub(g_tp.target_pt, from)));
        if (g_tp.aim_frames == 16) {
            void* pointed = *(void**)((char*)tp + 0xb0);
            rf_log("teleport: pointer on %s", pointed == g_tp.target_loc ? "target" : "something else");
            g_hand[RIGHT].stick = (v2){0, 0};  // release -> TeleportIfPossible
        }
        if (g_tp.aim_frames > 20) {
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

void rf_interact_tick(void) {
    tick_buttons();
    tick_click();
    tick_teleport();
    tick_grab();
}
