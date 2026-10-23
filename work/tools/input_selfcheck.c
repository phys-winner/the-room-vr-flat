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
    v3 repaired = {7, 8, 9};
    assert(repair_box_size((v3){.11f, .08f, -.07f}, &repaired));
    assert(repaired.x == .11f && repaired.y == .08f && repaired.z == .07f);
    assert(repair_box_size((v3){-.11f, -.08f, -.07f}, &repaired));
    assert(repaired.x == .11f && repaired.y == .08f && repaired.z == .07f);
    assert(!repair_box_size((v3){.11f, .08f, .07f}, &repaired));
    assert(repaired.x == .11f && repaired.y == .08f && repaired.z == .07f);
    assert(!repair_box_size((v3){0, 0, 0}, &repaired));
    assert(repair_box_size((v3){-0.0f, 0, -.07f}, &repaired));
    assert(signbit(repaired.x) && repaired.y == 0 && repaired.z == .07f);
    assert(inventory_full_return(0, false, false, false));
    assert(!inventory_full_return(0, false, true, false));  // an existing reserved slot or stackable match
    assert(!inventory_full_return(0, false, false, true));  // room to archive a new item
    assert(!inventory_full_return(0, false, true, true));
    assert(!inventory_full_return(0, true, false, false));  // a hand switch does not archive
    assert(!inventory_full_return(1, false, false, false));  // authored return-to-pickup
    assert(!inventory_full_return(2, false, false, false));  // authored discard
    assert(!inventory_full_return(-1, false, false, false));
    assert(!inventory_full_return(3, false, false, false));
    assert(inventory_pickup_returned(1));
    assert(!inventory_pickup_returned(0));
    assert(!inventory_pickup_returned(2));  // preserve a successful receiver placement
    assert(!inventory_pickup_returned(3));
    assert(!inventory_pickup_returned(-1));
    assert(!repair_box_size((v3){NAN, .08f, -.07f}, &repaired));
    assert(!repair_box_size((v3){-.11f, INFINITY, .07f}, &repaired));
    assert(!repair_box_size((v3){.11f, .08f, -INFINITY}, &repaired));
    assert(signbit(repaired.x) && repaired.y == 0 && repaired.z == .07f);
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
    assert(rotation_roll(false, 0) && rotation_roll(false, -.59f));
    assert(!rotation_roll(true, 0) && !rotation_roll(false, .6f) && !rotation_roll(false, -1));
    assert(!rotation_roll(false, NAN));
    // Native surface contact can lead/lag the virtual hand; the lower stop hid the crank's 35.8 degree offset.
    v3 turn_axis = {0, 0, 1}, turn_zero = {1, 0, 0};
    for (int sign = -1; sign <= 1; sign += 2) {
        float offset = sign * 35.8f;
        for (int start = 0; start <= 1800; start += 900) {
            for (int turns = -2; turns <= 2; turns++) {
                float current = start + turns * 360, base = 9999;
                v3 contact = q_rot(q_axis(turn_axis, (offset + turns * 360) * DEG2RAD), turn_zero);
                // Scale and an axial component do not change the radial contact phase.
                contact = v3_add(v3_scale(contact, .061f), (v3){0, 0, .13f});
                assert(rotation_contact_base(contact, turn_axis, turn_zero, start, current, &base));
                assert(fabsf(base - (start - offset)) < .001f);
                v2 limits = rotation_hand_limits(0, 1800, base);
                assert(limits.x <= 0 && limits.y >= 0);  // acquisition never moves a stationary hand into a bound
                assert(base + limits.x <= -ROT_OVER + .001f && base + limits.y >= 1800 + ROT_OVER - .001f);
                float hand = 0;
                for (int i = 0; i < 1000; i++) hand = fminf(limits.y, hand + ROT_STEP);
                assert(hand == limits.y && fminf(1800, fmaxf(0, base + hand)) == 1800);
                for (int i = 0; i < 1000; i++) hand = fmaxf(limits.x, hand - ROT_STEP);
                assert(hand == limits.x && fminf(1800, fmaxf(0, base + hand)) == 0);
                // The calibrated range stays fixed after a stop: continued winding cannot ratchet it farther.
                assert(limits.x == rotation_hand_limits(0, 1800, base).x);
                assert(limits.y == rotation_hand_limits(0, 1800, base).y);
            }
        }
    }
    float contact_base = 777;
    assert(rotation_contact_base(turn_zero, turn_axis, turn_zero, 720, 720, &contact_base) && contact_base == 720);
    v2 same_limits = rotation_hand_limits(0, 1800, contact_base);
    assert(same_limits.x == -730 && same_limits.y == 1090);
    contact_base = 777;
    assert(!rotation_contact_base((v3){0, 0, 0}, turn_axis, turn_zero, 0, 0, &contact_base));
    assert(!rotation_contact_base(turn_axis, turn_axis, turn_zero, 0, 0, &contact_base));
    assert(!rotation_contact_base(turn_zero, (v3){0, 0, 0}, turn_zero, 0, 0, &contact_base));
    assert(!rotation_contact_base(turn_zero, turn_axis, turn_axis, 0, 0, &contact_base));
    assert(!rotation_contact_base((v3){NAN, 0, 0}, turn_axis, turn_zero, 0, 0, &contact_base));
    assert(!rotation_contact_base(turn_zero, (v3){0, INFINITY, 1}, turn_zero, 0, 0, &contact_base));
    assert(!rotation_contact_base(turn_zero, turn_axis, (v3){1, 0, -INFINITY}, 0, 0, &contact_base));
    assert(!rotation_contact_base(turn_zero, turn_axis, turn_zero, NAN, 0, &contact_base));
    assert(!rotation_contact_base(turn_zero, turn_axis, turn_zero, 0, INFINITY, &contact_base));
    assert(contact_base == 777);  // invalid native geometry leaves the original limits available
    g_grab.pivot = (v3){1, 2, 3};
    g_grab.axis = (v3){1, 0, 0};
    g_grab.h0 = .02f;
    g_grab.r0 = .045f;
    g_grab.rot_u = (v3){0, 0, -1};
    g_grab.rot_v = v3_cross(g_grab.axis, g_grab.rot_u);
    v3 initial_roll = rot_point(0);
    float roll = 0, wanted_roll = 0;
    for (int i = 0; i < 72; i++) {
        wanted_roll += rotation_roll_degrees((v2){0, -.05f * 10 * DEG2RAD}, (v2){0, -.05f}, 2.2f);
        roll = rotation_step(roll, wanted_roll);
        v3 hand = rot_point(roll), radius = v3_sub(hand, v3_add(g_grab.pivot, (v3){g_grab.h0, 0, 0}));
        assert(fabsf(v3_len(radius) - g_grab.r0) < 1e-6f);
        assert(fabsf(roll - 10 * (i + 1)) < .001f);
    }
    assert(fabsf(roll - 720) < .001f && v3_len(v3_sub(rot_point(roll), initial_roll)) < 1e-5f);
    for (int i = 0; i < 72; i++) {
        wanted_roll += rotation_roll_degrees((v2){0, .05f * 10 * DEG2RAD}, (v2){0, -.05f}, 2.2f);
        roll = rotation_step(roll, wanted_roll);
    }
    assert(fabsf(roll) < .001f);
    assert(fabsf(rotation_roll_degrees((v2){.02f, -.03f}, (v2){.02f, -.03f}, 2.2f) - 1 / DEG2RAD) < 1e-4f);
    assert(fabsf(rotation_roll_degrees((v2){0, -.035f}, (v2){0, -.001f}, 2.2f) - 1 / DEG2RAD) < 1e-4f);
    assert(rotation_roll_degrees((v2){0, 0}, (v2){-.1f, .2f}, 1.3f) == 0);  // camera moved, finger did not
    assert(rotation_roll_degrees((v2){.1f, .1f}, (v2){0, 0}, 2.2f) == 0);
    assert(rotation_roll_degrees((v2){NAN, 0}, (v2){0, -.05f}, 2.2f) == 0);
    assert(rotation_roll_degrees((v2){0, .1f}, (v2){0, NAN}, 2.2f) == 0);
    assert(rotation_roll_degrees((v2){0, .1f}, (v2){0, -.05f}, INFINITY) == 0);
    roll = 0;
    for (int i = 0; i < 24; i++) roll = rotation_step(roll, 720);
    assert(roll == 720 && rotation_step(roll, roll) == roll);  // a batched swipe loses no turn
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
    puts("input self-check: PASS (look sensitivity, negative box repair, full inventory fallback, complete wheel rolls, axial turns, transfer, queue overflow, volume rays, sphere geometry, reach, key press/release, hand release)");
    return 0;
}
