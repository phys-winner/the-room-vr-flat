// Touch input for the flat port.
// Java (UnityPlayerActivity.dispatchTouchEvent -> RoomFix.onTouch) posts normalised touches from the
// UI thread; the Unity main thread consumes them once per frame and maps them to game actions:
//   menus (main menu, pause menu)  tap -> the game's laser pointer clicks under the finger
//   gameplay                       press on a grabbable -> grab it with the right hand, drag moves it
//                                  along its constraint, lift releases;
//                                  press elsewhere + drag -> look around (head yaw/pitch);
//                                  teleport mode (HUD) + tap on a location marker -> teleport.
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

// ---------------------------------------------------------------- look scale
typedef float (*fn_f_1)(void*, const void*);
static float g_deg_per_x = 120.0f, g_deg_per_y = 60.0f;

static void update_look_scale(void) {
    void* cam = rf_main_camera();
    if (!cam) return;
    float vfov = CALLP(C_CAM_GET_FOV, fn_f_1)(cam, NULL);
    float aspect = CALLP(C_CAM_ASPECT, fn_f_1)(cam, NULL);
    g_deg_per_x = 2.0f * atanf(tanf(vfov * 0.5f * DEG2RAD) * aspect) / DEG2RAD;
    g_deg_per_y = vfov;
}

// ---------------------------------------------------------------- primary finger gestures
enum { F_NONE = 0, F_PENDING, F_LOOK, F_GRAB };

static struct {
    int mode;
    int id;
    float x0, y0, x, y;
    int64_t t0;
} g_f;

#define TAP_MS 350
#define MOVE_X 0.012f
#define MOVE_Y 0.022f

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
    if (g_f.mode == F_PENDING && (fabsf(e->x - g_f.x0) > MOVE_X || fabsf(e->y - g_f.y0) > MOVE_Y)) g_f.mode = F_LOOK;
    if (g_f.mode == F_LOOK) {
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

static void on_event(const touch_ev* e) {
    if (e->action == 0) {
        if (g_f.mode != F_NONE) return;  // single-finger controls for now
        finger_down(e);
        return;
    }
    if (g_f.mode == F_NONE || e->id != g_f.id) return;
    if (e->action == 2) finger_move(e);
    else finger_up(e, e->action == 3);
}

void rf_touch_tick(void) {
    update_look_scale();
    touch_ev ev[64];
    int n = pop(ev, 64);
    for (int i = 0; i < n; i++) on_event(&ev[i]);
}
