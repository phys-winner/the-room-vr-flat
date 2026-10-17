// Gyroscope control (round 2, operator requests 2026-10-16): two independent Options rows, each with its own
// sensitivity (Options -> Gyroscope page, rf_options.c):
//   eye position   - turning / tilting the phone moves the eyes (the synthetic head's position) sideways / up and
//                    down, like a VR player moving the head to peek past something; moving the phone itself away from
//                    the player / back towards them moves the eyes in / out; the view direction stays;
//   tilt and turn  - turning / tilting the phone turns the camera the same way (yaw / pitch), magic-window style.
// Java (RoomFix) runs the sensors only while an option is on and the window has focus: the gyroscope and gravity for
// both options (rotation increments in screen axes - x right, y up, z out of the screen - with the "up" direction),
// linear acceleration (gravity removed) for eye position. The turn is measured around the real vertical ("player
// space": gravity-projected yaw, relaxed by sqrt 2 towards the local axes, so it works with the phone held upright or
// tilted back); pitch is the turn about the screen's horizontal axis; turns slower than 0.7 deg/s fade out (sensor
// drift, a trembling hand).
// Forward / back: the acceleration along the screen normal is integrated twice per sensor sample. The velocity is set
// to zero whenever the phone rests (|a| below the noise level for 80 ms), so a push keeps its full length; the
// accelerometer cannot hold a position, so the shift then glides back to zero (time constant 4 s) - push the phone in to
// look closer, and the view eases back out.
// Increments are dropped in menus (the camera never turns there, R2-1) and while no player exists. Round 5: the
// sideways / up-down eye moves take the way of the two-finger pan (rf_view_move: inside the location's play area,
// clear of walls, where turning the view leaves them); the push is a passing offset along the view, reset on teleports
// and scene changes (rf_view.c).
#include <pthread.h>
#include <time.h>

#include "rf.h"

int g_gyro_pos, g_gyro_rot;        // options on / off
int g_gyro_pos_sens = 3, g_gyro_rot_sens = 3;  // sensitivity option (index into SENS)
float g_gyro_pz;  // passing eye offset from pushing the phone, metres along the view

static const int SENS[RF_GYRO_SENS_COUNT] = {25, 50, 75, 100, 150, 200, 300};  // percent

int rf_gyro_sens_pct(int idx) { return SENS[idx >= 0 && idx < RF_GYRO_SENS_COUNT ? idx : 3]; }

#define POS_PER_RAD 0.5f  // metres of eye movement per radian of phone rotation at 100 % (about 9 mm per degree)
#define YAW_RELAX 1.41f
#define TIGHT_DPS 0.7f    // tightening threshold, degrees per second
#define ACC_NOISE 0.15f   // m/s^2: linear acceleration of a phone at rest stays below this
#define ACC_REST 0.08f    // s below the noise level: the phone rests, its velocity is zero
#define ACC_TAU_X 4.0f    // s: the forward shift glides back to zero
#define ACC_GAIN 1.5f     // eye movement per metre the phone moves, at 100 %
#define POS_MAX_IN 0.35f  // like the pinch lean
#define POS_MAX_OUT 0.12f

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static float g_acc[3];
static v3 g_up = {0, 1, 0};
static int g_events;
static struct {
    float v, x;  // velocity (m/s) and shift (m), away from the player positive
    float rest;  // seconds the phone has rested
} g_push;

void rf_gyro_post(float dx, float dy, float dz, float ux, float uy, float uz) {
    pthread_mutex_lock(&g_mu);
    g_acc[0] += dx;
    g_acc[1] += dy;
    g_acc[2] += dz;
    float l = sqrtf(ux * ux + uy * uy + uz * uz);
    if (l > 1.0f) g_up = (v3){ux / l, uy / l, uz / l};  // m/s^2 from the gravity sensor; ignore an empty reading
    g_events++;
    pthread_mutex_unlock(&g_mu);
}

// linear acceleration along the screen normal (z, towards the player), m/s^2, and the sample interval
void rf_accel_post(float az, float dt) {
    if (dt <= 0 || dt > 0.1f) return;
    float a = -az;  // away from the player (into the scene) positive
    pthread_mutex_lock(&g_mu);
    if (fabsf(a) < ACC_NOISE) {
        g_push.rest += dt;
        if (g_push.rest >= ACC_REST) g_push.v = 0;  // zero-velocity update: the phone stands still
        a = 0;
    } else {
        g_push.rest = 0;
    }
    g_push.v += a * dt;
    g_push.x += g_push.v * dt;
    g_push.x *= expf(-dt / ACC_TAU_X);
    if (g_push.x > 0.5f) g_push.x = 0.5f;
    if (g_push.x < -0.5f) g_push.x = -0.5f;
    pthread_mutex_unlock(&g_mu);
}

int rf_gyro_wanted(void) { return (g_gyro_pos || g_gyro_rot ? 1 : 0) | (g_gyro_pos ? 2 : 0); }

void rf_gyro_reset_pos(void) {
    g_gyro_pz = 0;
    pthread_mutex_lock(&g_mu);
    g_push.v = g_push.x = 0;
    pthread_mutex_unlock(&g_mu);
}

void rf_gyro_tick(void) {
    float d[3], push;
    v3 up;
    int ev;
    pthread_mutex_lock(&g_mu);
    d[0] = g_acc[0];
    d[1] = g_acc[1];
    d[2] = g_acc[2];
    g_acc[0] = g_acc[1] = g_acc[2] = 0;
    up = g_up;
    ev = g_events;
    g_events = 0;
    push = g_push.x;
    pthread_mutex_unlock(&g_mu);
    if (!g_gyro_pos) {
        if (g_gyro_pz != 0) rf_gyro_reset_pos();
    } else if (!rf_menu_context()) {
        float z = push * ACC_GAIN * rf_gyro_sens_pct(g_gyro_pos_sens) * 0.01f;
        g_gyro_pz = z > POS_MAX_IN ? POS_MAX_IN : z < -POS_MAX_OUT ? -POS_MAX_OUT : z;
    }
    if (!ev || !rf_gyro_wanted()) return;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    static struct timespec last;
    float dt = (float)(now.tv_sec - last.tv_sec) + (now.tv_nsec - last.tv_nsec) * 1e-9f;
    last = now;
    if (rf_menu_context() || !rf_player()) return;
    // radians this frame: yaw > 0 turns left (counter-clockwise seen from above), pitch > 0 tilts the view up
    float world_yaw = d[1] * up.y + d[2] * up.z;
    float local = sqrtf(d[1] * d[1] + d[2] * d[2]);
    float yaw = copysignf(fminf(fabsf(world_yaw) * YAW_RELAX, local), world_yaw);
    float pitch = d[0];
    // tightening: turns slower than TIGHT_DPS fade out smoothly - a phone at rest drifts ~0.1 deg/s (measured on
    // realme-rmx3371, 34 degrees in 5 minutes) and a held phone trembles, while deliberate turns are far faster
    if (dt > 0.001f && dt < 0.2f) {
        float speed = sqrtf(yaw * yaw + pitch * pitch) / dt / DEG2RAD;  // deg/s
        if (speed < TIGHT_DPS) {
            yaw *= speed / TIGHT_DPS;
            pitch *= speed / TIGHT_DPS;
        }
    }
    if (g_gyro_rot) {
        float s = rf_gyro_sens_pct(g_gyro_rot_sens) * 0.01f;
        g_head.yaw -= yaw * s / DEG2RAD;
        g_head.pitch += pitch * s / DEG2RAD;
        if (g_head.pitch > 80) g_head.pitch = 80;
        if (g_head.pitch < -80) g_head.pitch = -80;
    }
    if (g_gyro_pos) {
        float s = rf_gyro_sens_pct(g_gyro_pos_sens) * 0.01f * POS_PER_RAD, h = g_head.yaw * DEG2RAD;
        v3 right = {cosf(h), 0, -sinf(h)};  // the view's horizontal right, tracking space
        rf_view_move(v3_add(v3_scale(right, -yaw * s), (v3){0, pitch * s, 0}), false);  // turn right -> eyes right
    }
    if (g_dev) {
        static int n;
        static float sy, sp;
        sy += yaw;
        sp += pitch;
        if (++n % 120 == 0) {
            rf_log("gyro: 2 s yaw %.1f pitch %.1f deg (up %.2f %.2f %.2f) head yaw %.1f pitch %.1f eyes(%.3f %.3f %.3f) push %.3f",
                   sy / DEG2RAD, sp / DEG2RAD, up.x, up.y, up.z, g_head.yaw, g_head.pitch, g_head.pos.x, g_head.pos.y,
                   g_head.pos.z, g_gyro_pz);
            sy = sp = 0;
        }
    }
}
