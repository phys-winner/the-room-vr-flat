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

static void exec_line(char* line) {
    char* argv[8];
    int argc = 0;
    for (char* t = strtok(line, " \t\r\n"); t && argc < 8; t = strtok(NULL, " \t\r\n")) argv[argc++] = t;
    if (!argc) return;
    if (!strcmp(argv[0], "dump")) {
        cmd_dump();
    } else if (!strcmp(argv[0], "look") && argc >= 3) {
        g_head.yaw = strtof(argv[1], NULL);
        g_head.pitch = strtof(argv[2], NULL);
        rf_log("look yaw=%.1f pitch=%.1f", g_head.yaw, g_head.pitch);
    } else if (!strcmp(argv[0], "aim") && argc >= 3) {
        rf_touch_dev_aim(true, strtof(argv[1], NULL), strtof(argv[2], NULL));
    } else if (!strcmp(argv[0], "rest")) {
        rf_touch_dev_aim(false, 0, 0);
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
            bool v = atoi(argv[3]) != 0;
            if (!strcmp(argv[2], "primary")) h->btn_primary = v;
            else if (!strcmp(argv[2], "secondary")) h->btn_secondary = v;
            else if (!strcmp(argv[2], "menu")) h->btn_menu = v;
            else if (!strcmp(argv[2], "stick")) h->stick_click = v;
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
