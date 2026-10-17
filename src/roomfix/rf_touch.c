// Touch input for the flat port.
// Java (UnityPlayerActivity.dispatchTouchEvent -> RoomFix.onTouch) posts normalised touches from the
// UI thread; the Unity main thread consumes them once per frame and maps them to game actions:
//   menus (main menu, pause menu)  tap -> the game's laser pointer clicks under the finger (beam hidden);
//                                  a drag never turns the camera there (rf_menu.c frames every screen)
//   gameplay                       press on a grabbable -> grab it with the right hand, drag moves it
//                                  along its constraint, lift releases;
//                                  press elsewhere + drag -> look around (head yaw/pitch);
//                                  second finger while looking -> pinch: the eyes forward / back along the view,
//                                  both fingers moving together: sideways / up and down (inside the location,
//                                  rf_view.c; turning the view never moves them);
//                                  teleport mode (HUD) + tap on a location marker -> teleport.
// HUD buttons are hit-tested in Java; their presses arrive here as action 10 (id = button) so every game
// action still runs on the Unity main thread. Action 11 cancels all fingers (window focus lost). Action 12 is the
// hold-distance slider (round 6): x = metres from the eyes for the carried item; its finger never reaches the game.
#include <pthread.h>
#include <string.h>

#include "rf.h"

// ---------------------------------------------------------------- event queue (UI thread -> main thread)
typedef struct {
    int action;  // 0 down, 1 up, 2 move, 3 cancel, 10 HUD button (id), 11 cancel all, 12 hold distance (x metres)
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

// ---------------------------------------------------------------- look scale
typedef float (*fn_f_1)(void*, const void*);
static float g_deg_per_x = 120.0f, g_deg_per_y = 60.0f, g_aspect = 2.1667f;

static void update_look_scale(void) {
    void* cam = rf_main_camera();
    if (!cam) return;
    float vfov = CALLP(C_CAM_GET_FOV, fn_f_1)(cam, NULL);
    float aspect = CALLP(C_CAM_ASPECT, fn_f_1)(cam, NULL);
    if (aspect > 0.2f) g_aspect = aspect;
    g_deg_per_x = 2.0f * atanf(tanf(vfov * 0.5f * DEG2RAD) * aspect) / DEG2RAD;
    g_deg_per_y = vfov;
}

// ---------------------------------------------------------------- primary finger gestures
enum { F_NONE = 0, F_PENDING, F_LOOK, F_GRAB, F_PINCH, F_MENUDRAG };

static struct {
    int mode;
    int id;
    float x0, y0, x, y;
    int64_t t0;
} g_f;

static struct {
    int id;
    float x, y;
    float d, mx, my;  // finger spread and midpoint at the last event (pinch / two-finger pan: moves since then)
    v3 moved;   // tracking-space move asked for by this gesture (log)
    bool look;  // second finger turns the view while the first one holds an object
    bool turn;  // second finger, put down next to the held object, turns it in the hand
    float tx, ty;  // turn: last position of finger 2 relative to finger 1 (ball radii)
    int turn_moves;
} g_f2;  // second finger: pinch, look while holding, or turn the held object

#define LEAN_PER_UNIT 0.5f  // metres along the view per screen height of finger spread
#define TURN_R 0.3f         // second finger this close to the holding one (screen heights) turns the object
#define SWAY_PER_UNIT 0.5f  // metres sideways / up per screen height of two-finger pan

#define TAP_MS 350
#define MOVE_X 0.012f
#define MOVE_Y 0.022f

// The camera turns only in gameplay: never in menus (round 2), and never without a player - the loading scene
// between the main menu and a level, scene changes, the start-up screens (round 4, operator 2026-10-16: "while the
// main menu switches pages the camera can be moved").
static bool look_allowed(void) { return !rf_menu_context() && rf_player(); }

static void finger_down(const touch_ev* e) {
    g_f.mode = F_PENDING;
    g_f.id = e->id;
    g_f.x0 = g_f.x = e->x;
    g_f.y0 = g_f.y = e->y;
    g_f.t0 = e->t;
    if (!rf_menu_context() && !rf_act_teleport_mode_on()) {
        int r = rf_act_grab_begin(e->x, e->y);
        if (r == RF_GRAB_OK) g_f.mode = F_GRAB;
    }
}

static void finger_move(const touch_ev* e) {
    float dx = e->x - g_f.x, dy = e->y - g_f.y;
    g_f.x = e->x;
    g_f.y = e->y;
    if (g_f.mode == F_GRAB) {
        rf_act_grab_drag(e->x, e->y);
        return;
    }
    if (g_f.mode == F_PENDING && (fabsf(e->x - g_f.x0) > MOVE_X || fabsf(e->y - g_f.y0) > MOVE_Y))
        g_f.mode = rf_menu_context() ? F_MENUDRAG : F_LOOK;  // no camera turning in menus (round 2)
    if (g_f.mode == F_LOOK && look_allowed()) {
        g_head.yaw += dx * g_deg_per_x;
        g_head.pitch -= dy * g_deg_per_y;
        if (g_head.pitch > 80) g_head.pitch = 80;
        if (g_head.pitch < -80) g_head.pitch = -80;
    }
}

static void finger_up(const touch_ev* e, bool cancel) {
    if (g_f.mode == F_GRAB) {
        rf_act_grab_end();
    } else if (g_f.mode == F_PENDING && !cancel && e->t - g_f.t0 < TAP_MS) {
        if (rf_menu_context()) {
            rf_log("touch: tap %.3f %.3f -> laser click", e->x, e->y);
            rf_act_laser_click(e->x, e->y);
        } else if (rf_act_teleport_mode_on()) {
            if (!rf_act_teleport_tap(e->x, e->y)) rf_log("touch: tap %.3f %.3f -> no teleport marker", e->x, e->y);
        }
    }
    g_f.mode = F_NONE;
}

static float pinch_dist(void) {
    float dx = (g_f.x - g_f2.x) * g_aspect, dy = g_f.y - g_f2.y;
    return sqrtf(dx * dx + dy * dy);
}

static void cancel_all(void) {
    if (g_f.mode == F_GRAB) rf_act_grab_end();
    g_f.mode = F_NONE;
    g_f2.look = false;
    g_f2.turn = false;
}

static void on_event(const touch_ev* e) {
    if (e->action == 10) {
        rf_hud_button(e->id);
        return;
    }
    if (e->action == 11) {
        cancel_all();
        return;
    }
    if (e->action == 12) {
        rf_act_hold_depth(e->x);
        return;
    }
    if (e->action == 0) {
        if (g_f.mode == F_NONE) {
            finger_down(e);
        } else if (g_f.mode == F_GRAB && !g_f2.look && !g_f2.turn && e->id != g_f.id) {
            // hold an object with one finger and look around with another (carry it, look through it); a second
            // finger put down right next to a carried object turns it instead (tip a jar, show another side)
            float dx = (e->x - g_f.x) * g_aspect, dy = e->y - g_f.y;
            g_f2.id = e->id;
            g_f2.x = e->x;
            g_f2.y = e->y;
            if (sqrtf(dx * dx + dy * dy) < TURN_R && rf_act_grab_can_roll()) {
                g_f2.turn = true;
                g_f2.tx = dx / TURN_R;
                g_f2.ty = dy / TURN_R;
                g_f2.turn_moves = 0;
            } else {
                g_f2.look = true;
            }
        } else if ((g_f.mode == F_PENDING || g_f.mode == F_LOOK) && look_allowed() && e->id != g_f.id) {
            g_f.mode = F_PINCH;
            g_f2.id = e->id;
            g_f2.x = e->x;
            g_f2.y = e->y;
            g_f2.d = pinch_dist();
            g_f2.mx = (g_f.x + g_f2.x) * 0.5f;
            g_f2.my = (g_f.y + g_f2.y) * 0.5f;
            g_f2.moved = (v3){0, 0, 0};
        }
        return;
    }
    if (g_f2.turn && e->id == g_f2.id) {
        if (e->action == 2) {
            // incremental, so circling keeps turning (pour a jar out, turn a note over and back)
            float tx = (e->x - g_f.x) * g_aspect / TURN_R, ty = (e->y - g_f.y) / TURN_R;
            rf_act_grab_turn(g_f2.tx, g_f2.ty, tx, ty);
            g_f2.tx = tx;
            g_f2.ty = ty;
            g_f2.turn_moves++;
        } else {
            g_f2.turn = false;
            rf_log("touch: turned the held object (%d moves)", g_f2.turn_moves);
        }
        return;
    }
    if (g_f2.look && e->id == g_f2.id) {
        if (e->action == 2 && look_allowed()) {
            g_head.yaw += (e->x - g_f2.x) * g_deg_per_x;
            g_head.pitch -= (e->y - g_f2.y) * g_deg_per_y;
            if (g_head.pitch > 80) g_head.pitch = 80;
            if (g_head.pitch < -80) g_head.pitch = -80;
            g_f2.x = e->x;
            g_f2.y = e->y;
        } else {
            g_f2.look = false;
        }
        return;
    }
    if (g_f.mode == F_PINCH) {
        if (e->id != g_f.id && e->id != g_f2.id) return;
        if (e->action == 2) {
            if (e->id == g_f.id) { g_f.x = e->x; g_f.y = e->y; }
            else { g_f2.x = e->x; g_f2.y = e->y; }
            // spreading the fingers moves the eyes along the view; both fingers moving together move them sideways /
            // up and down (peek around an obstacle, reach the strand behind another one) - a VR player moves the head
            // inside the play area. Each event moves by the change since the last one, so a move held back at the
            // edge of the play area comes back at once when the fingers turn round.
            float d = pinch_dist(), mx = (g_f.x + g_f2.x) * 0.5f, my = (g_f.y + g_f2.y) * 0.5f;
            float lean = (d - g_f2.d) * LEAN_PER_UNIT, sx = (mx - g_f2.mx) * g_aspect * SWAY_PER_UNIT,
                  sy = -(my - g_f2.my) * SWAY_PER_UNIT;
            g_f2.d = d;
            g_f2.mx = mx;
            g_f2.my = my;
            float yaw = g_head.yaw * DEG2RAD;
            v3 right = {cosf(yaw), 0, -sinf(yaw)};  // horizontal, follows the head's yaw only
            v3 m = v3_add(v3_scale(q_rot(rf_head_rot(), (v3){0, 0, 1}), lean),
                          v3_add(v3_scale(right, sx), (v3){0, sy, 0}));
            g_f2.moved = v3_add(g_f2.moved, m);
            rf_view_move(m, true);
        } else {
            g_f.mode = F_NONE;  // the remaining finger is ignored until it lifts
            rf_log("touch: two-finger move (%.2f %.2f %.2f) m asked for; eyes %.2f m from the location's centre",
                   g_f2.moved.x, g_f2.moved.y, g_f2.moved.z, rf_view_offset());
        }
        return;
    }
    if (g_f.mode == F_NONE || e->id != g_f.id) return;
    if (e->action == 2) finger_move(e);
    else {
        finger_up(e, e->action == 3);
        g_f2.look = false;
        g_f2.turn = false;
    }
}

void rf_touch_tick(void) {
    update_look_scale();
    touch_ev ev[64];
    int n = pop(ev, 64);
    for (int i = 0; i < n; i++) on_event(&ev[i]);
}
