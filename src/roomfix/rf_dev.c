// Developer console: commands are written by adb into <extDir>/roomfix.cmd and executed on the
// Unity main thread from the per-frame tick. Diagnostics only; players never see it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rf.h"

typedef void* (*fn_obj0)(const void*);
typedef void* (*fn_obj1)(void*, const void*);
typedef v3 (*fn_v3_1)(void*, const void*);
typedef quat (*fn_q_1)(void*, const void*);
typedef float (*fn_f_1)(void*, const void*);
typedef int32_t (*fn_i0)(const void*);

static void log_transform(const char* label, void* tr) {
    if (!tr) { rf_log("  %s: null", label); return; }
    v3 p = CALLP(C_TR_POSITION, fn_v3_1)(tr, NULL);
    quat q = CALLP(C_TR_ROTATION, fn_q_1)(tr, NULL);
    v3 lp = CALLP(C_TR_LOCALPOS, fn_v3_1)(tr, NULL);
    v3 fwd = q_rot(q, (v3){0, 0, 1});
    char name[96] = "?";
    Il2CppString* s = (Il2CppString*)CALLP(C_OBJ_NAME, fn_obj1)(tr, NULL);
    rf_str_utf8(s, name, sizeof name);
    rf_log("  %s '%s': pos(%.3f %.3f %.3f) local(%.3f %.3f %.3f) fwd(%.2f %.2f %.2f)", label, name,
           p.x, p.y, p.z, lp.x, lp.y, lp.z, fwd.x, fwd.y, fwd.z);
}

static void cmd_dump(void) {
    int32_t frame = CALLP(C_TIME_FRAME, fn_i0)(NULL);
    int32_t sw = CALLP(C_SCREEN_W, fn_i0)(NULL), sh = CALLP(C_SCREEN_H, fn_i0)(NULL);
    int32_t scene = ((int32_t (*)(const void*))RVA(g_call_desc[C_SCENE_ACTIVE].rva))(NULL);
    // value-type instance methods take `this` by pointer in IL2CPP (dumper signatures show it by value)
    Il2CppString* sn = ((Il2CppString * (*)(int32_t*, const void*)) RVA(g_call_desc[C_SCENE_NAME].rva))(&scene, NULL);
    char sname[96] = "?";
    rf_str_utf8(sn, sname, sizeof sname);
    rf_log("dump: frame=%d screen=%dx%d scene='%s' head yaw=%.1f pitch=%.1f pos(%.2f %.2f %.2f)", frame, sw, sh,
           sname, g_head.yaw, g_head.pitch, g_head.pos.x, g_head.pos.y, g_head.pos.z);
    if (g_vrpm) {
        char* m = (char*)g_vrpm;
        void* plat = *(void**)(m + 0x50);
        bool up = ((bool (*)(void*, const void*))RVA(g_call_desc[C_VRPM_STARTEDUP].rva))(g_vrpm, NULL);
        rf_log("  VrPlatformManager=%p platform=%p(%s) startedUp=%d sitting=%d hasL=%d hasR=%d", g_vrpm, plat,
               rf_class_name(plat), up, m[0x138], m[0x120], m[0x121]);
        if (plat) {
            char* q = (char*)plat;
            rf_log("  platform: isQuest=%d delmar=%d gotType=%d initialised=%d ovrManager=%p", q[0x118], q[0x119],
                   q[0x11a], q[0x130], *(void**)(q + 0x108));
        }
    } else {
        rf_log("  VrPlatformManager not captured yet");
    }
    if (g_vrim) {
        void* rt = *(void**)((char*)g_vrim + 0x58);
        rf_log("  VrInputModule=%p rayTransform=%p cursor=%p", g_vrim, rt, *(void**)((char*)g_vrim + 0x88));
        void* p = rt;
        for (int i = 0; i < 5 && p; i++) {
            char lab[16];
            snprintf(lab, sizeof lab, i ? "ray^%d" : "ray", i);
            log_transform(lab, p);
            p = CALLP(C_TR_PARENT, fn_obj1)(p, NULL);
        }
    }
    void* cam = CALLP(C_CAM_MAIN, fn_obj0)(NULL);
    if (!cam) { rf_log("  Camera.main = null"); return; }
    float fov = CALLP(C_CAM_GET_FOV, fn_f_1)(cam, NULL);
    float aspect = CALLP(C_CAM_ASPECT, fn_f_1)(cam, NULL);
    rf_log("  Camera.main=%p fov=%.1f aspect=%.3f", cam, fov, aspect);
    void* tr = CALLP(C_COMP_TRANSFORM, fn_obj1)(cam, NULL);
    log_transform("camera", tr);
    void* p = tr;
    for (int i = 0; i < 6 && p; i++) {
        p = CALLP(C_TR_PARENT, fn_obj1)(p, NULL);
        if (p) {
            char lab[16];
            snprintf(lab, sizeof lab, "parent%d", i + 1);
            log_transform(lab, p);
        }
    }
}

typedef v3 (*fn_w2s)(void*, v3, const void*);

static void cmd_tele(void) {
    if (!rf_alive(g_player)) { rf_log("tele: no player"); return; }
    void* c = *(void**)((char*)g_player + 0x40);
    void* tp = rf_alive(c) ? *(void**)((char*)c + 0x50) : NULL;
    if (!rf_alive(tp)) { rf_log("tele: no teleporter"); return; }
    char* t = (char*)tp;
    void* pointed = *(void**)(t + 0xb0);
    void* locs = *(void**)(t + 0xb8);
    char nm[64] = "-";
    if (rf_alive(pointed)) rf_str_utf8((Il2CppString*)CALLP(C_OBJ_NAME, fn_obj1)(pointed, NULL), nm, sizeof nm);
    rf_log("tele: active=%d pointedAt=%s locations=%d", t[0xe8], nm, rf_list_size(locs));
    void* cam = CALLP(C_CAM_MAIN, fn_obj0)(NULL);
    for (int i = 0; i < rf_list_size(locs) && i < 40; i++) {
        void* loc = rf_list_get(locs, i);
        if (!rf_alive(loc)) continue;
        char ln[64] = "?";
        rf_str_utf8((Il2CppString*)CALLP(C_OBJ_NAME, fn_obj1)(loc, NULL), ln, sizeof ln);
        void* tr = CALLP(C_COMP_TRANSFORM, fn_obj1)(loc, NULL);
        v3 p = CALLP(C_TR_POSITION, fn_v3_1)(tr, NULL);
        v3 s = rf_alive(cam) ? CALLP(C_CAM_W2S, fn_w2s)(cam, p, NULL) : (v3){0, 0, 0};
        rf_log("  loc[%d] '%s' state=%d pos(%.2f %.2f %.2f) screen(%.0f %.0f z=%.2f)", i, ln,
               *(int32_t*)((char*)loc + 0x21c), p.x, p.y, p.z, s.x, s.y, s.z);
    }
}

static void cmd_input(void) {
    bool (*getb)(int32_t, int32_t, const void*) = (bool (*)(int32_t, int32_t, const void*))RVA(g_call_desc[C_FP_GETBUTTON].rva);
    float (*geta)(int32_t, int32_t, const void*) = (float (*)(int32_t, int32_t, const void*))RVA(g_call_desc[C_FP_GETAXIS].rva);
    rf_log("input: grabL=%.2f grabR=%.2f gripL=%.2f gripR=%.2f teleL=%d teleR=%d invL=%d invR=%d pause=%d goggles=%d menuL=%d menuR=%d",
           geta(0, 0, NULL), geta(1, 0, NULL), geta(2, 0, NULL), geta(3, 0, NULL), getb(11, 0, NULL), getb(13, 0, NULL),
           getb(12, 0, NULL), getb(21, 0, NULL), getb(22, 0, NULL), getb(28, 0, NULL), getb(25, 0, NULL), getb(26, 0, NULL));
    if (rf_alive(g_player)) {
        bool (*allowed)(void*, const void*) = (bool (*)(void*, const void*))RVA(g_call_desc[C_INPUTCTL_ALLOWED].rva);
        bool (*allowtele)(void*, const void*) = (bool (*)(void*, const void*))RVA(g_call_desc[C_CTRL_ALLOWTELE].rva);
        char* p = (char*)g_player;
        void* ti = *(void**)(p + 0xf8), *ii = *(void**)(p + 0x100), *gi = *(void**)(p + 0x108), *wi = *(void**)(p + 0x118);
        void* rc = *(void**)(p + 0x40);
        rf_log("input: player teleAllowed=%d invAllowed=%d gogglesAllowed=%d worldGrabAllowed=%d rightAllowTele=%d",
               ti ? allowed(ti, NULL) : -1, ii ? allowed(ii, NULL) : -1, gi ? allowed(gi, NULL) : -1,
               wi ? allowed(wi, NULL) : -1, rf_alive(rc) ? allowtele(rc, NULL) : -1);
    }
}

// dev helpers for scripted routes: find a teleport location by name
static void* find_location(const char* name) {
    void* c = rf_controller(1);
    void* tp = c ? *(void**)((char*)c + 0x50) : NULL;
    if (!rf_alive(tp)) return NULL;
    void* locs = *(void**)((char*)tp + 0xb8);
    for (int i = 0; i < rf_list_size(locs); i++) {
        void* loc = rf_list_get(locs, i);
        char nm[64];
        rf_obj_name(loc, nm, sizeof nm);
        if (!strcmp(nm, name)) return loc;
    }
    return NULL;
}

static void cmd_face_loc(const char* name) {
    void* loc = find_location(name);
    void* cam = rf_main_camera();
    if (!loc || !cam) { rf_log("faceloc: '%s' not found", name); return; }
    v3 lp = rf_tr_pos(rf_transform(loc));
    void* ct = rf_transform(cam);
    v3 cp = rf_tr_pos(ct);
    void* ts = rf_tr_parent(ct);  // camera rig: its rotation is the tracking-space yaw
    v3 tsf = ts ? q_rot(rf_tr_rot(ts), (v3){0, 0, 1}) : (v3){0, 0, 1};
    float want = atan2f(lp.x - cp.x, lp.z - cp.z) / DEG2RAD, base = atan2f(tsf.x, tsf.z) / DEG2RAD;
    g_head.yaw = want - base;
    while (g_head.yaw > 180) g_head.yaw -= 360;
    while (g_head.yaw < -180) g_head.yaw += 360;
    g_head.pitch = -20;
    rf_log("faceloc '%s': yaw=%.1f", name, g_head.yaw);
}

static void cmd_ttap_loc(const char* name) {
    void* loc = find_location(name);
    float sx, sy;
    if (!loc || !rf_world_to_screen(v3_add(rf_tr_pos(rf_transform(loc)), (v3){0, 0.9f, 0}), &sx, &sy)) {
        rf_log("ttaploc: '%s' not visible", name);
        return;
    }
    rf_log("ttaploc '%s' at %.3f %.3f -> %d", name, sx, sy, rf_act_teleport_tap(sx, sy));
}

static void* static_instance(uintptr_t meta) {
    uintptr_t k = *(uintptr_t*)(g_il2cpp_base + meta);
    if (!k || (k & 1) || k < 0x10000) return NULL;
    void* statics = *(void**)(k + 0xb8);
    return statics ? *(void**)statics : NULL;
}

static void cmd_inv(void) {
    void* hud = static_instance(META_INVHUD_TYPEINFO);
    void* c = rf_controller(1);
    void* closest = c ? *(void**)((char*)c + 0x80) : NULL;
    if (!rf_alive(hud)) { rf_log("inv: no InventoryHud"); return; }
    char* h = (char*)hud;
    void* slot = *(void**)(h + 0x58);
    void* sc = *(void**)(h + 0x60);
    void* hc = c ? *(void**)((char*)c + 0x38) : NULL;
    void* tip = rf_alive(hc) ? *(void**)((char*)hc + 0x20) : NULL;
    v3 tp = rf_alive(tip) ? rf_tr_pos(tip) : (v3){0, 0, 0};
    v3 tf = rf_alive(tip) ? q_rot(rf_tr_rot(tip), (v3){0, 0, 1}) : (v3){0, 0, 0};
    rf_log("inv: hud=%p len=%.2f start=%.2f probeDist=%.2f slot=%p(%s) slotCtrl=%s closest=%s probeUsePointer=%d tip(%.2f %.2f %.2f) fwd(%.2f %.2f %.2f)",
           hud, *(float*)(h + 0x1c), *(float*)(h + 0x20), *(float*)(h + 0x24), slot, rf_alive(slot) ? rf_class_name(slot) : "-",
           sc == c ? "right" : (sc ? "other" : "-"), rf_alive(closest) ? rf_class_name(closest) : "-",
           c ? ((char*)c)[0xb8] : -1, tp.x, tp.y, tp.z, tf.x, tf.y, tf.z);
}

static void exec_line(char* line) {
    char* argv[8];
    int argc = 0;
    for (char* t = strtok(line, " \t\r\n"); t && argc < 8; t = strtok(NULL, " \t\r\n")) argv[argc++] = t;
    if (!argc) return;
    if (!strcmp(argv[0], "dump")) {
        cmd_dump();
    } else if (!strcmp(argv[0], "tele")) {
        cmd_tele();
    } else if (!strcmp(argv[0], "input")) {
        cmd_input();
    } else if (!strcmp(argv[0], "look") && argc >= 3) {
        g_head.yaw = strtof(argv[1], NULL);
        g_head.pitch = strtof(argv[2], NULL);
        rf_log("look yaw=%.1f pitch=%.1f", g_head.yaw, g_head.pitch);
    } else if (!strcmp(argv[0], "faceloc") && argc >= 2) {
        cmd_face_loc(argv[1]);
    } else if (!strcmp(argv[0], "ttaploc") && argc >= 2) {
        cmd_ttap_loc(argv[1]);
    } else if (!strcmp(argv[0], "aimtip") && argc >= 4) {
        rf_act_dev_aim_tip(true, strtof(argv[1], NULL), strtof(argv[2], NULL), strtof(argv[3], NULL));
    } else if (!strcmp(argv[0], "tipoff")) {
        rf_act_dev_aim_tip(false, 0, 0, 0);
    } else if (!strcmp(argv[0], "inv")) {
        cmd_inv();
    } else if (!strcmp(argv[0], "vols")) {
        rf_act_list_volumes();
    } else if (!strcmp(argv[0], "probe") && argc >= 3) {
        rf_act_probe(strtof(argv[1], NULL), strtof(argv[2], NULL));
    } else if (!strcmp(argv[0], "grab") && argc >= 3) {
        rf_log("grab -> %d", rf_act_grab_begin(strtof(argv[1], NULL), strtof(argv[2], NULL)));
    } else if (!strcmp(argv[0], "drag") && argc >= 3) {
        rf_act_grab_drag(strtof(argv[1], NULL), strtof(argv[2], NULL));
    } else if (!strcmp(argv[0], "depth") && argc >= 2) {
        rf_act_grab_depth(strtof(argv[1], NULL));
    } else if (!strcmp(argv[0], "release")) {
        rf_act_grab_end();
    } else if (!strcmp(argv[0], "tmode") && argc >= 2) {
        rf_act_teleport_mode(atoi(argv[1]) != 0);
    } else if (!strcmp(argv[0], "ttap") && argc >= 3) {
        rf_log("ttap -> %d", rf_act_teleport_tap(strtof(argv[1], NULL), strtof(argv[2], NULL)));
    } else if (!strcmp(argv[0], "snap") && argc >= 2) {
        rf_act_snap_turn(atoi(argv[1]) < 0 ? -1 : 1);
    } else if (!strcmp(argv[0], "press") && argc >= 3) {
        // press L|R primary|secondary|menu : one 6-frame pulse
        int h = argv[1][0] == 'R', k = !strcmp(argv[2], "secondary") ? 1 : !strcmp(argv[2], "menu") ? 2 : 0;
        rf_act_button(h, k, 6);
    } else if (!strcmp(argv[0], "tap") && argc >= 3) {
        rf_touch_post(0, 99, strtof(argv[1], NULL), strtof(argv[2], NULL), 0);
        rf_touch_post(1, 99, strtof(argv[1], NULL), strtof(argv[2], NULL), 50);
    } else if (!strcmp(argv[0], "head") && argc >= 4) {
        g_head.pos = (v3){strtof(argv[1], NULL), strtof(argv[2], NULL), strtof(argv[3], NULL)};
        rf_log("head pos %.2f %.2f %.2f", g_head.pos.x, g_head.pos.y, g_head.pos.z);
    } else if (argc >= 3 && (argv[1][0] == 'L' || argv[1][0] == 'R') &&
               (!strcmp(argv[0], "hand") || !strcmp(argv[0], "trig") || !strcmp(argv[0], "grip") ||
                !strcmp(argv[0], "btn") || !strcmp(argv[0], "stick"))) {
        rf_hand_t* h = &g_hand[argv[1][0] == 'R'];
        if (!strcmp(argv[0], "hand") && argc >= 5) {
            h->pos = (v3){strtof(argv[2], NULL), strtof(argv[3], NULL), strtof(argv[4], NULL)};
        } else if (!strcmp(argv[0], "trig")) {
            h->trigger = strtof(argv[2], NULL);
        } else if (!strcmp(argv[0], "grip")) {
            h->grip = strtof(argv[2], NULL);
        } else if (!strcmp(argv[0], "stick") && argc >= 4) {
            h->stick = (v2){strtof(argv[2], NULL), strtof(argv[3], NULL)};
        } else if (!strcmp(argv[0], "btn") && argc >= 4) {
            if (!strcmp(argv[2], "stick")) h->stick_click = atoi(argv[3]) != 0;
        }
        rf_log("%s hand: pos(%.2f %.2f %.2f) trig=%.2f grip=%.2f stick(%.2f %.2f) btn p%d s%d m%d c%d", argv[1],
               h->pos.x, h->pos.y, h->pos.z, h->trigger, h->grip, h->stick.x, h->stick.y, h->btn_primary,
               h->btn_secondary, h->btn_menu, h->stick_click);
    } else {
        rf_log("dev: unknown command '%s'", argv[0]);
    }
}

void rf_dev_tick(void) {
    static unsigned n;
    if (!g_dev || (++n % 15)) return;
    char path[600], tmp[620];
    snprintf(path, sizeof path, "%s/roomfix.cmd", g_ext_dir);
    snprintf(tmp, sizeof tmp, "%s.run", path);
    if (rename(path, tmp) != 0) return;
    FILE* f = fopen(tmp, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        rf_log("dev> %s", strtok(line, "\r\n") ? line : "");
        exec_line(line);
    }
    fclose(f);
    unlink(tmp);
}
