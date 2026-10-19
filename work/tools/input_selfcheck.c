// Uses the production gesture code; unused engine paths are discarded by --gc-sections.
#define RF_RELEASE 1
#include "../../src/roomfix/rf_interact.c"
#include "../../src/roomfix/rf_touch.c"
#include "../../src/roomfix/rf_puzzles.c"
#include "../../src/roomfix/rf_settings.c"
// Keep release-call counters while checking the production velocity estimator.
#define rf_hand_rest rf_unity_hand_rest
#include "../../src/roomfix/rf_unity.c"
#undef rf_hand_rest
#include <assert.h>
#include <unistd.h>

rf_hand_t g_hand[2];
rf_head_t g_head;
char g_ext_dir[512];
float g_fov_h;
int g_qa, g_help_seen, g_gauge_on, g_gyro_pos, g_gyro_rot, g_gyro_pos_sens, g_gyro_rot_sens;
static int rests[2];
void rf_hand_rest(int h) { rests[h]++; }
static bool has_base;
static v3 view_base;
bool rf_view_base(v3* out) { *out = view_base; return has_base; }

int main(void) {
    assert(g_look_sens == RF_LOOK_SENS_DEFAULT && rf_sens_pct(g_look_sens) == 200);
    char temp[] = "/data/local/tmp/room_settings_XXXXXX", cfg[600];
    assert(mkdtemp(temp));
    snprintf(g_ext_dir, sizeof g_ext_dir, "%s", temp);
    path(cfg, sizeof cfg);
    FILE* f = fopen(cfg, "w");
    assert(f && fputs("fps=1\nfov=3\ngposs=4\ngrots=6\n", f) >= 0);
    fclose(f);
    rf_settings_load();
    assert(g_look_sens == RF_LOOK_SENS_DEFAULT && g_fps_idx == 1 && g_fov_idx == 3);
    assert(g_gyro_pos_sens == 4 && g_gyro_rot_sens == 6);
    for (int i = -1; i <= RF_SENS_COUNT; i++) {
        f = fopen(cfg, "w");
        assert(f && fprintf(f, "looks=%d\n", i) > 0);
        fclose(f);
        g_look_sens = RF_LOOK_SENS_DEFAULT;
        rf_settings_load();
        assert(g_look_sens == (i >= 0 && i < RF_SENS_COUNT ? i : RF_LOOK_SENS_DEFAULT));
        rf_settings_save();
        g_look_sens = -1;
        rf_settings_load();
        assert(g_look_sens == (i >= 0 && i < RF_SENS_COUNT ? i : RF_LOOK_SENS_DEFAULT));
    }
    assert(unlink(cfg) == 0 && rmdir(temp) == 0);
    g_ext_dir[0] = 0;
    const int sensitivity[] = {25, 50, 75, 100, 150, 200, 300};
    assert(rf_sens_pct(-1) == 100 && rf_sens_pct(RF_SENS_COUNT) == 100);
    for (int i = 0; i < RF_SENS_COUNT; i++) {
        assert(rf_sens_pct(i) == sensitivity[i]);
        g_look_sens = i;
        g_head = (rf_head_t){.pos = {1, 2, 3}};
        look_delta(.1f, -.1f);
        assert(fabsf(g_head.yaw - .1f * g_deg_per_x * sensitivity[i] * .01f) < 1e-5f);
        assert(fabsf(g_head.pitch - .1f * g_deg_per_y * sensitivity[i] * .01f) < 1e-5f);
        look_delta(-.1f, .1f);
        assert(fabsf(g_head.yaw) < 1e-5f && fabsf(g_head.pitch) < 1e-5f);
        assert(g_head.pos.x == 1 && g_head.pos.y == 2 && g_head.pos.z == 3);
        look_delta(0, -100);
        assert(g_head.pitch == 80);
        look_delta(0, 100);
        assert(g_head.pitch == -80);
    }
    g_look_sens = RF_LOOK_SENS_DEFAULT;
    g_hand[0].pos = (v3){1, 2, 3};
    g_hand[0].vel = (v3){4, 5, 6};
    g_hand_velocity_active[0] = true;
    rf_hand_velocity_reset(0);
    assert(!g_hand_velocity_active[0] && v3_len(g_hand[0].vel) == 0 && g_hand[0].pos.x == 1);
    for (int fps = 30; fps <= 90; fps += 30) {
        float dt = 1.0f / fps;
        v3 velocity = {0, 0, 0};
        float integral = 0, minimum = 1e9f;
        for (int i = 0; i < fps * 4; i++) {
            velocity = hand_velocity_step(velocity, (v3){i % 2 ? 0 : 2 * dt, 0, 0}, dt);
            if (i > fps) minimum = fminf(minimum, velocity.x);
            integral += velocity.x * dt;
        }
        assert(minimum > .6f && fabsf(integral - 4) < .1f);
        for (int i = 0; i < fps; i++) velocity = hand_velocity_step(velocity, (v3){0, 0, 0}, dt);
        assert(v3_len(velocity) < 1e-5f);
    }
    v3 inset = cylinder_contact((v3){3.16f, 0, -.35f}, 3.16f, 3.35f, -.35f, -.2f, (v3){1, 1, 1});
    assert(fabsf(inset.x - 3.20f) < 1e-5f && fabsf(inset.z + .3125f) < 1e-5f);
    inset = cylinder_contact((v3){3.22f, 0, -.28f}, 3.16f, 3.35f, -.35f, -.2f, (v3){1, 1, 1});
    assert(inset.x == 3.22f && inset.z == -.28f);
    inset = cylinder_contact((v3){.21f, 0, .01f}, .19f, .21f, -.01f, .01f, (v3){1, 1, 1});
    assert(fabsf(inset.x - .205f) < 1e-5f && fabsf(inset.z - .005f) < 1e-5f);
    inset = cylinder_contact((v3){.06f, 0, -.06f}, 0, .06f, -.06f, .06f, (v3){.5f, .5f, .5f});
    assert(fabsf(inset.x - .045f) < 1e-5f && fabsf(inset.z + .03f) < 1e-5f);
    inset = cylinder_contact((v3){0, 0, 0}, 0, .06f, -.06f, .06f, (v3){1, 1, 1});
    assert(v3_len(inset) == 0);
    inset = cylinder_contact((v3){.32f, 0, 0}, .33f, .32f, -.1f, .1f, (v3){1, 1, 1});
    assert(inset.x == .32f);
    inset = cylinder_contact((v3){.32f, 0, 0}, NAN, .32f, -.1f, .1f, (v3){1, 1, 1});
    assert(inset.x == .32f);
    assert(actuator_pending(.833f, 0, 5) && !actuator_pending(0, 0, 5));
    assert(actuator_pending(0, 5, 5) && !actuator_pending(1, 5, 5));
    assert(!actuator_pending(NAN, 0, 5) && !actuator_pending(INFINITY, 0, 5));
    assert(!actuator_pending(.5f, NAN, 5) && !actuator_pending(.5f, 0, NAN));
    assert(!actuator_pending(-.1f, 0, 5) && !actuator_pending(1.1f, 0, 5));
    assert(fabsf(turn_step(1, 0, 0, 1) - (float)M_PI / 2) < 1e-5f);
    assert(fabsf(turn_step(1, 0, 0, -1) + (float)M_PI / 2) < 1e-5f);
    assert(fabsf(turn_step(-1, 0.01f, -1, -0.01f)) < 0.03f);
    assert(turn_step(0, 0, 1, 0) == 0 && turn_step(1, 0, 0, 0) == 0);
    float total = 0;
    for (int i = 0; i < 72; i++) {
        float a = i * 10 * DEG2RAD, b = (i + 1) * 10 * DEG2RAD;
        total += turn_step(cosf(a), sinf(a), cosf(b), sinf(b));
    }
    assert(fabsf(total - 4 * (float)M_PI) < 1e-4f);
    for (int state = G_HOVER; state <= G_REGRAB; state++) {
        if (state == G_RELEASE) continue;
        g_grab.state = state;
        g_grab.frames = 100;
        rf_act_grab_end();
        assert(g_grab.state == G_RELEASE && g_grab.frames == 0);
    }
    g_grab.state = G_REGRAB;
    g_f.mode = F_NONE;
    g_f2.look = g_f2.turn = true;
    cancel_all();
    assert(g_grab.state == G_RELEASE && !g_f2.look && !g_f2.turn);
    g_grab.state = G_HOLD;
    g_grab.kind = K_TWIST;
    g_grab.inv = true;
    assert(rf_act_grab_can_roll());
    g_grab.kind = K_FREE;
    assert(!rf_act_grab_can_roll());
    g_grab.inv = false;
    assert(rf_act_grab_can_roll());
    g_grab.kind = K_SLIDE_TWIST;
    assert(rf_act_grab_can_roll());
    g_grab.kind = K_DRAW;
    assert(!rf_act_grab_can_roll());
    for (int i = 0; i < QN; i++) rf_touch_post(2, 0, 0.5f, 0.5f, i);
    rf_touch_post(1, 0, 0.5f, 0.5f, QN);
    touch_ev events[4];
    assert(pop(events, 4) == 2 && events[0].action == 11 && events[1].action == 1);
    v3 hit;
    float first, last;
    assert(volume_ray_span((v3){0, 0, 0}, (v3){0, 0, 1}, (v3){0, 0, 1}, .2f, .1f, 3.5f, &first, &last));
    assert(fabsf(first - .8f) < 1e-5f && fabsf(last - 1.2f) < 1e-5f);
    assert(volume_ray_span((v3){0, 0, 0}, (v3){0, 0, 1}, (v3){2.295497f, 0, -1.665637f}, 3.368234f,
                           .1f, 3.5f, &first, &last));
    assert(first == .1f && last > .53f);
    assert(!volume_ray_span((v3){0, 0, 0}, (v3){0, 0, 1}, (v3){2, 0, 1}, 1, .1f, 3.5f, &first, &last));
    assert(!volume_ray_span((v3){0, 0, 0}, (v3){0, 0, 1}, (v3){0, 0, -2}, 1, .1f, 3.5f, &first, &last));
    assert(volume_ray_span((v3){0, 0, 0}, (v3){0, 0, 1}, (v3){1, 0, 1}, 1, .1f, 3.5f, &first, &last));
    assert(first == 1 && last == 1);
    assert(sphere_point((v3){0, 0, -3}, (v3){0, 0, 1}, (v3){0, 0, 0}, 1, false, &hit));
    assert(v3_len(v3_sub(hit, (v3){0, 0, -1})) < 1e-5f);
    assert(!sphere_point((v3){2, 0, -3}, (v3){0, 0, 1}, (v3){0, 0, 0}, 1, false, &hit));
    assert(sphere_point((v3){2, 0, -3}, (v3){0, 0, 1}, (v3){0, 0, 0}, 1, true, &hit));
    assert(fabsf(v3_len(hit) - 1) < 1e-5f);
    assert(contact_in_reach((v3){0, 0, 1}, (v3){0, 0, 0}));
    assert(!contact_in_reach((v3){0, 0, 2}, (v3){0, 0, 0}));
    has_base = true;
    view_base = (v3){0, 0, 1};
    assert(contact_in_reach((v3){0, 0, 2}, (v3){0, 0, -1}));
    assert(!contact_in_reach((v3){0, 0, 3}, (v3){0, 0, 0}));
    g_ball_finger[0].down = g_ball_finger[1].down = true;
    release_hand(0);
    assert(!rf_puzzle_hand_active(0) && rf_puzzle_hand_active(1) && rests[0] == 1);
    release_hand(0);
    assert(rests[0] == 1);
    cancel_hands();
    assert(!rf_puzzle_hand_active(1) && rests[1] == 1);
    g_key_finger[0].pivot = (v3){1, 2, 3};
    g_key_finger[0].axis = (v3){1, 0, 0};
    g_key_finger[0].height = .1f;
    g_key_finger[0].radial = (v3){0, 0, .2f};
    v3 pressed = key_press_point(0, 15);
    assert(fabsf(pressed.x - 1.1f) < 1e-5f);
    assert(fabsf(pressed.y - (2 - .2f * sinf(15 * DEG2RAD))) < 1e-5f);
    assert(fabsf(pressed.z - (3 + .2f * cosf(15 * DEG2RAD))) < 1e-5f);
    g_key_finger[0].down = g_key_finger[1].down = true;
    g_key_finger[0].stage = g_key_finger[1].stage = KEY_PRESS;
    key_up(0);
    assert(!g_key_finger[0].down && g_key_finger[0].stage == KEY_RELEASE && g_key_finger[1].down);
    assert(rf_puzzle_hand_active(0) && rf_puzzle_hand_active(1));
    clear_key_hand(0);
    assert(!rf_puzzle_hand_active(0) && rf_puzzle_hand_active(1));
    g_key_finger[1].pending_key = (void*)1;
    g_key_finger[1].pending_pivot = (void*)2;
    cancel_keys();
    assert(!g_key_finger[1].pending_key && !g_key_finger[1].pending_pivot);
    assert(!rf_puzzle_hand_active(1));
    assert(rests[0] == 2 && rests[1] == 2);
    cancel_keys();
    assert(rests[0] == 2 && rests[1] == 2);
    puts("input self-check: PASS (look sensitivity, axial turns, transfer, queue overflow, volume rays, sphere geometry, reach, key press/release, hand release)");
    return 0;
}
