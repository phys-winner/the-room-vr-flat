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

static void cmd_zooms(void) {
    void* c = rf_controller(1);
    void* tp = c ? *(void**)((char*)c + 0x50) : NULL;
    if (!rf_alive(tp)) { rf_log("zooms: no teleporter"); return; }
    void* zooms = *(void**)((char*)tp + 0xd0);
    rf_log("zooms: %d, pointed=%p", rf_list_size(zooms), *(void**)((char*)tp + 0xc8));
    for (int i = 0; i < rf_list_size(zooms) && i < 32; i++) {
        void* z = rf_list_get(zooms, i);
        if (!rf_alive(z)) continue;
        char nm[64];
        rf_obj_name(z, nm, sizeof nm);
        static const int offs[3] = {0xe0, 0xd0, 0x130};
        static const char* lab[3] = {"zoomIn", "portal", "zoomOut"};
        for (int k = 0; k < 3; k++) {
            void* col = *(void**)((char*)z + offs[k]);
            if (!rf_alive(col)) continue;
            void* go = rf_gameobject(col);
            float sx = -1, sy = -1;
            bool vis = rf_world_to_screen(rf_tr_pos(rf_transform(col)), &sx, &sy);
            rf_log("  zoom '%s' %s active=%d screen(%.3f %.3f) vis=%d", nm, lab[k],
                   rf_alive(go) && CALLP(C_GO_ACTIVEH, bool (*)(void*, const void*))(go, NULL), sx, sy, vis);
        }
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

// read-only: yaw/pitch that would face a teleport location (0.9 m above its base), and the current head pose
static void cmd_loc_yaw(const char* name) {
    void* loc = find_location(name);
    void* cam = rf_main_camera();
    if (!loc || !cam) { rf_log("locyaw: '%s' not found", name); return; }
    v3 lp = v3_add(rf_tr_pos(rf_transform(loc)), (v3){0, 0.9f, 0});
    void* ct = rf_transform(cam);
    void* rig = rf_tr_parent(ct);
    quat r = rig ? rf_tr_rot(rig) : (quat){0, 0, 0, 1};
    v3 l = q_rot(q_conj(r), v3_sub(lp, rf_tr_pos(ct)));
    float yaw = atan2f(l.x, l.z) / DEG2RAD, pitch = atan2f(l.y, sqrtf(l.x * l.x + l.z * l.z)) / DEG2RAD;
    float sx = -1, sy = -1;
    bool vis = rf_world_to_screen(lp, &sx, &sy);
    rf_log("locyaw '%s' want %.1f %.1f cur %.1f %.1f screen %.4f %.4f vis=%d", name, yaw, pitch, g_head.yaw, g_head.pitch, sx, sy, vis);
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

// dev: transform subtree with active flags and positions relative to the camera
static void tree(void* t, int depth, int maxdepth, v3 cam_p, quat cam_q) {
    if (!rf_alive(t) || depth > maxdepth) return;
    char nm[64];
    rf_obj_name(t, nm, sizeof nm);
    void* go = rf_gameobject(t);
    bool act = rf_alive(go) && CALLP(C_GO_ACTIVESELF, bool (*)(void*, const void*))(go, NULL);
    v3 rel = q_rot(q_conj(cam_q), v3_sub(rf_tr_pos(t), cam_p));
    v3 sc = CALLP(C_TR_LOSSYSCALE, v3 (*)(void*, const void*))(t, NULL);
    int n = CALLP(C_TR_CHILDCOUNT, int32_t (*)(void*, const void*))(t, NULL);
    rf_log("tree %*s'%s' act=%d cam(%.2f %.2f %.2f) scale(%.2f %.2f %.2f) kids=%d", depth * 2, "", nm, act, rel.x, rel.y,
           rel.z, sc.x, sc.y, sc.z, n);
    for (int i = 0; i < n && i < 40; i++)
        tree(CALLP(C_TR_GETCHILD, void* (*)(void*, int32_t, const void*))(t, i, NULL), depth + 1, maxdepth, cam_p, cam_q);
}

static void* find_named(void* t, const char* name, int depth) {
    if (!rf_alive(t) || depth > 6) return NULL;
    char nm[64];
    rf_obj_name(t, nm, sizeof nm);
    if (!strcmp(nm, name)) return t;
    int n = CALLP(C_TR_CHILDCOUNT, int32_t (*)(void*, const void*))(t, NULL);
    for (int i = 0; i < n && i < 64; i++) {
        void* r = find_named(CALLP(C_TR_GETCHILD, void* (*)(void*, int32_t, const void*))(t, i, NULL), name, depth + 1);
        if (r) return r;
    }
    return NULL;
}

// dev: setact NAME 0|1 - (de)activate a GameObject under the camera rig
static void cmd_setact(const char* name, bool on) {
    void* cam = rf_main_camera();
    if (!cam) return;
    void* rig = rf_tr_parent(rf_transform(cam));
    void* t = find_named(rig ? rig : rf_transform(cam), name, 0);
    if (!t) { rf_log("setact: '%s' not found", name); return; }
    CALLP(C_GO_SETACTIVE, void (*)(void*, bool, const void*))(rf_gameobject(t), on, NULL);
    rf_log("setact: '%s' -> %d", name, on);
}

static void cmd_menutree(int maxdepth) {
    void* mc = rf_menu_controller();
    void* scr = mc ? *(void**)((char*)mc + 0xd8) : NULL;
    void* cam = rf_main_camera();
    if (!rf_alive(scr) || !cam) { rf_log("menutree: no menu screen"); return; }
    void* ct = rf_transform(cam);
    tree(rf_transform(scr), 0, maxdepth, rf_tr_pos(ct), rf_tr_rot(ct));
}

// read-only: inventory receivers (keyholes, sockets) with their centre on screen
static void cmd_receivers(void) {
    uintptr_t k = *(uintptr_t*)(g_il2cpp_base + META_INVRECV_TYPEINFO);
    void* statics = (k && !(k & 1) && k > 0x10000) ? *(void**)(k + 0xb8) : NULL;
    void* all = statics ? *(void**)statics : NULL;
    rf_log("receivers: %d", rf_list_size(all));
    for (int i = 0; i < rf_list_size(all) && i < 64; i++) {
        void* r = rf_list_get(all, i);
        if (!rf_alive(r)) continue;
        v3 c = ((v3(*)(void*, const void*))RVA(g_call_desc[C_RECV_CENTRE].rva))(r, NULL);
        float sx = -1, sy = -1;
        bool vis = rf_world_to_screen(c, &sx, &sy);
        char nm[64];
        rf_obj_name(r, nm, sizeof nm);
        rf_log("  recv '%s' state=%d screen(%.3f %.3f) vis=%d", nm, *(int32_t*)((char*)r + 0x40), sx, sy, vis);
    }
}

// read-only: every InventoryReceiver object, active or not (Resources.FindObjectsOfTypeAll)
static void cmd_all_receivers(void) {
    uintptr_t k = *(uintptr_t*)(g_il2cpp_base + META_INVRECV_TYPEINFO);
    if (!k || (k & 1) || k < 0x10000 || !g_il2.class_get_type || !g_il2.type_get_object) return;
    void* type = g_il2.type_get_object(g_il2.class_get_type((void*)k));
    void* arr = CALLP(C_RES_FINDALL, void* (*)(void*, const void*))(type, NULL);
    int32_t n = arr ? (int32_t) * (uint64_t*)((char*)arr + 0x18) : 0;
    rf_log("allrecv: %d", n);
    for (int i = 0; i < n && i < 128; i++) {
        void* r = ((void**)((char*)arr + 0x20))[i];
        if (!rf_alive(r)) continue;
        char nm[64], items[160] = "";
        rf_obj_name(r, nm, sizeof nm);
        void* valid = *(void**)((char*)r + 0x50);
        for (int j = 0; j < rf_list_size(valid) && j < 4; j++) {
            char in[48];
            rf_obj_name(rf_list_get(valid, j), in, sizeof in);
            strncat(items, in, sizeof items - strlen(items) - 2);
            strncat(items, ",", sizeof items - strlen(items) - 1);
        }
        void* go = rf_gameobject(r);
        bool act = rf_alive(go) && CALLP(C_GO_ACTIVEH, bool (*)(void*, const void*))(go, NULL);
        float sx = -1, sy = -1;
        rf_world_to_screen(rf_tr_pos(rf_transform(r)), &sx, &sy);
        rf_log("  allrecv '%s' active=%d enabled? state=%d valid=[%s] screen(%.3f %.3f)", nm, act, *(int32_t*)((char*)r + 0x40), items, sx, sy);
    }
}

extern void* g_last_box[2];
static void cmd_boxes(void) {
    void* cam = rf_main_camera();
    if (!cam) return;
    void* ct = rf_transform(cam);
    v3 cp = rf_tr_pos(ct);
    quat cq = rf_tr_rot(ct);
    for (int i = 0; i < 2; i++) {
        char* b = (char*)g_last_box[i];
        if (!rf_alive(b)) { rf_log("box[%d]: none", i); continue; }
        void* go = rf_gameobject(b);
        bool act = CALLP(C_GO_ACTIVEH, bool (*)(void*, const void*))(go, NULL);
        void* cg = *(void**)(b + 0x48);
        float a = rf_alive(cg) ? CALLP(C_CG_ALPHA, float (*)(void*, const void*))(cg, NULL) : -1;
        void* t = rf_transform(b);
        v3 rel = q_rot(q_conj(cq), v3_sub(rf_tr_pos(t), cp));
        char* siv = *(char**)(b + 0xb0);
        char pn[64] = "-";
        rf_obj_name(rf_tr_parent(t), pn, sizeof pn);
        float sx = -1, sy = -1;
        rf_world_to_screen(rf_tr_pos(t), &sx, &sy);
        rf_log("box[%d] %s act=%d state=%d attached=%d default=%d alpha=%.2f cam(%.2f %.2f %.2f) screen(%.3f %.3f) parent='%s' stayInView=%p fov(%.1f %.1f) lerp=%.2f",
               i, rf_class_name(b), act, *(int32_t*)(b + 0x88), *(int32_t*)(b + 0xa8), *(int32_t*)(b + 0x8c), a, rel.x, rel.y, rel.z,
               sx, sy, pn, siv, rf_alive(siv) ? *(float*)(siv + 0x18) : -1, rf_alive(siv) ? *(float*)(siv + 0x1c) : -1,
               rf_alive(siv) ? *(float*)(siv + 0x20) : -1);
    }
}

static void cmd_tree(int up, int maxdepth) {
    void* cam = rf_main_camera();
    if (!cam) { rf_log("tree: no camera"); return; }
    void* ct = rf_transform(cam);
    void* t = ct;
    for (int i = 0; i < up && rf_alive(rf_tr_parent(t)); i++) t = rf_tr_parent(t);
    tree(t, 0, maxdepth, rf_tr_pos(ct), rf_tr_rot(ct));
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
    } else if (!strcmp(argv[0], "zooms")) {
        cmd_zooms();
    } else if (!strcmp(argv[0], "input")) {
        cmd_input();
    } else if (!strcmp(argv[0], "look") && argc >= 3) {
        g_head.yaw = strtof(argv[1], NULL);
        g_head.pitch = strtof(argv[2], NULL);
        rf_log("look yaw=%.1f pitch=%.1f", g_head.yaw, g_head.pitch);
    } else if (!strcmp(argv[0], "faceloc") && argc >= 2) {
        cmd_face_loc(argv[1]);
    } else if (!strcmp(argv[0], "locyaw") && argc >= 2) {
        char nm[128] = "";  // location names may contain spaces ("Start_Position 2")
        for (int i = 1; i < argc; i++) {
            if (i > 1) strncat(nm, " ", sizeof nm - strlen(nm) - 1);
            strncat(nm, argv[i], sizeof nm - strlen(nm) - 1);
        }
        cmd_loc_yaw(nm);
    } else if (!strcmp(argv[0], "head?")) {
        void* cam = rf_main_camera();
        v3 cp = cam ? rf_tr_pos(rf_transform(cam)) : (v3){0, 0, 0};
        rf_log("head? yaw %.1f pitch %.1f lean %.2f cam(%.3f %.3f %.3f)", g_head.yaw, g_head.pitch, g_lean, cp.x, cp.y, cp.z);
    } else if (!strcmp(argv[0], "ttaploc") && argc >= 2) {
        cmd_ttap_loc(argv[1]);
    } else if (!strcmp(argv[0], "aimtip") && argc >= 4) {
        rf_act_dev_aim_tip(true, strtof(argv[1], NULL), strtof(argv[2], NULL), strtof(argv[3], NULL));
    } else if (!strcmp(argv[0], "tipoff")) {
        rf_act_dev_aim_tip(false, 0, 0, 0);
    } else if (!strcmp(argv[0], "setact") && argc >= 3) {
        cmd_setact(argv[1], atoi(argv[2]) != 0);
    } else if (!strcmp(argv[0], "skiplevel")) {
        // dev navigation only: the pause menu's own Skip Level handler (shown in game after completion / secret)
        void* mc = rf_menu_controller();
        if (mc && rf_class_is(mc, "PauseMenuController")) {
            rf_log("dev: PauseMenuController.SkipLevelPressed");
            CALLP(C_PAUSE_SKIPLEVEL, void (*)(void*, const void*))(mc, NULL);
        } else {
            rf_log("dev: skiplevel needs the pause menu");
        }
    } else if (!strcmp(argv[0], "allrecv")) {
        cmd_all_receivers();
    } else if (!strcmp(argv[0], "comps") && argc >= 2) {
        char nm[128] = "";
        for (int i = 1; i < argc; i++) {
            if (i > 1) strncat(nm, " ", sizeof nm - strlen(nm) - 1);
            strncat(nm, argv[i], sizeof nm - strlen(nm) - 1);
        }
        rf_dev_comps(nm);
    } else if (!strcmp(argv[0], "szstate") && argc >= 3) {
        rf_dev_szstate(argv[1], atoi(argv[2]));
    } else if (!strcmp(argv[0], "hint?")) {
        rf_dev_hint();
    } else if (!strcmp(argv[0], "peek") && argc >= 3) {
        rf_dev_peek(argv[1], argv[2], argc >= 4 ? argv[3] : NULL);
    } else if (!strcmp(argv[0], "tpto") && argc >= 2) {
        char nm[128] = "";
        for (int i = 1; i < argc; i++) {
            if (i > 1) strncat(nm, " ", sizeof nm - strlen(nm) - 1);
            strncat(nm, argv[i], sizeof nm - strlen(nm) - 1);
        }
        rf_dev_tpto(nm);
    } else if (!strcmp(argv[0], "objs") && argc >= 2) {
        rf_dev_objs(argv[1]);
    } else if (!strcmp(argv[0], "gfx?")) {
        rf_dev_gfx();
    } else if (!strcmp(argv[0], "depthtex") && argc >= 2) {
        rf_dev_depthtex(atoi(argv[1]));
    } else if (!strcmp(argv[0], "quality") && argc >= 3) {
        rf_dev_quality(atoi(argv[1]), atoi(argv[2]));
    } else if (!strcmp(argv[0], "aa") && argc >= 3) {
        rf_dev_aa(atoi(argv[1]) != 0, atoi(argv[2]));
    } else if (!strcmp(argv[0], "rlist")) {
        rf_dev_rlist(argc >= 2 ? argv[1] : NULL);
    } else if (!strcmp(argv[0], "orphans")) {
        rf_dev_orphans();
    } else if (!strcmp(argv[0], "rhide") && argc >= 3) {
        // rhide NAME WITH SPACES 0|1 : the last word is the flag
        char nm[128] = "";
        for (int i = 1; i < argc - 1; i++) {
            if (i > 1) strncat(nm, " ", sizeof nm - strlen(nm) - 1);
            strncat(nm, argv[i], sizeof nm - strlen(nm) - 1);
        }
        rf_dev_rhide(nm, atoi(argv[argc - 1]) != 0);
    } else if (!strcmp(argv[0], "recv")) {
        cmd_receivers();
    } else if (!strcmp(argv[0], "boxes")) {
        cmd_boxes();
    } else if (!strcmp(argv[0], "menutree") && argc >= 2) {
        cmd_menutree(atoi(argv[1]));
    } else if (!strcmp(argv[0], "tree") && argc >= 3) {
        cmd_tree(atoi(argv[1]), atoi(argv[2]));
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
        // press L|R primary|secondary|menu [frames] : one pulse (default 6 frames)
        int h = argv[1][0] == 'R', k = !strcmp(argv[2], "secondary") ? 1 : !strcmp(argv[2], "menu") ? 2 : 0;
        rf_act_button(h, k, argc >= 4 ? atoi(argv[3]) : 6);
    } else if (!strcmp(argv[0], "hud") && argc >= 2) {
        rf_hud_button(atoi(argv[1]));
    } else if (!strcmp(argv[0], "lean") && argc >= 2) {
        g_lean = strtof(argv[1], NULL);
    } else if (!strcmp(argv[0], "fpsopt") && argc >= 2) {
        int v = atoi(argv[1]);
        if (v >= 0 && v < RF_FPS_COUNT) {
            g_fps_idx = v;
            rf_settings_changed();
        }
    } else if (!strcmp(argv[0], "fov") && argc >= 2) {
        g_fov_h = strtof(argv[1], NULL);
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
            g_hand_resting[argv[1][0] == 'R'] = false;
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
