// Scene inspection (dev console): components, renderer materials/shaders, renderers on objects whose script was
// stripped from the PICO build (Hx volumetric lights), renderer toggles for A/B screenshots.
// Used for DEVICE_EFFECTS (work/device_effects.md): the main-menu disc turned out to be a FastGlow renderer whose
// shader produced NaN on Mali, fixed in the shader assets (work/tools/patch_shaders.py), not here.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rf.h"

typedef void* (*fn_obj1)(void*, const void*);

static void* g_t_component, *g_t_renderer;

static bool types_ready(void) {
    if (!g_t_component) g_t_component = rf_class_type(rf_find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Component"));
    if (!g_t_renderer) g_t_renderer = rf_class_type(rf_find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Renderer"));
    return g_t_component && g_t_renderer;
}

static int32_t arr_len(void* a) { return a ? (int32_t) * (uint64_t*)((char*)a + 0x18) : 0; }
static void* arr_get(void* a, int i) { return ((void**)((char*)a + 0x20))[i]; }

// components of a GameObject; returns the number of missing (null) script entries
static int missing_scripts(void* go) {
    void* comps = CALLP(C_GO_GETCOMPS, void* (*)(void*, void*, const void*))(go, g_t_component, NULL);
    int miss = 0;
    for (int i = 0; i < arr_len(comps); i++)
        if (!arr_get(comps, i)) miss++;
    return miss;
}

static void renderer_shaders(void* r, char* out, size_t n) {
    out[0] = 0;
    void* mats = CALLP(C_REND_MATERIALS, fn_obj1)(r, NULL);
    for (int i = 0; i < arr_len(mats) && i < 4; i++) {
        void* m = arr_get(mats, i);
        char mn[64] = "-", sn[80] = "-";
        if (rf_alive(m)) {
            rf_obj_name(m, mn, sizeof mn);
            void* sh = CALLP(C_MAT_SHADER, fn_obj1)(m, NULL);
            if (rf_alive(sh)) rf_obj_name(sh, sn, sizeof sn);
        }
        size_t l = strlen(out);
        snprintf(out + l, n - l, "%s%s[%s]", i ? ", " : "", mn, sn);
    }
}

void rf_dev_comps(const char* name) {
    if (!types_ready() || !g_il2.string_new) return;
    void* go = CALLP(C_GO_FIND, void* (*)(void*, const void*))(g_il2.string_new(name), NULL);
    if (!rf_alive(go)) {
        rf_log("comps: '%s' not found", name);
        return;
    }
    void* comps = CALLP(C_GO_GETCOMPS, void* (*)(void*, void*, const void*))(go, g_t_component, NULL);
    rf_log("comps '%s': %d", name, arr_len(comps));
    for (int i = 0; i < arr_len(comps); i++) {
        void* c = arr_get(comps, i);
        char sh[256] = "";
        if (c && rf_class_is(c, "Renderer")) renderer_shaders(c, sh, sizeof sh);
        rf_log("  [%d] %s %s", i, c ? rf_class_name(c) : "(missing script)", sh);
    }
}

// every active renderer whose GameObject has a missing script
void rf_dev_orphans(void) {
    if (!types_ready()) return;
    void* rs = CALLP(C_OBJ_FINDALLTYPE, void* (*)(void*, const void*))(g_t_renderer, NULL);
    int n = arr_len(rs), shown = 0;
    rf_log("orphans: %d renderers", n);
    for (int i = 0; i < n && shown < 60; i++) {
        void* r = arr_get(rs, i);
        if (!rf_alive(r)) continue;
        void* go = rf_gameobject(r);
        int miss = missing_scripts(go);
        if (!miss) continue;
        char nm[64], sh[256];
        rf_obj_name(go, nm, sizeof nm);
        renderer_shaders(r, sh, sizeof sh);
        float sx = -1, sy = -1;
        bool vis = rf_world_to_screen(rf_tr_pos(rf_transform(r)), &sx, &sy);
        rf_log("  orphan '%s' %s missing=%d enabled=%d shaders=%s screen(%.2f %.2f) vis=%d", nm, rf_class_name(r), miss,
               CALLP(C_REND_GETENABLED, bool (*)(void*, const void*))(r, NULL), sh, sx, sy, vis);
        shown++;
    }
}

// instances of a game class (TheRoomVR.dll) with GameObject name and screen position — route planning
void rf_dev_objs(const char* cls) {
    void* type = rf_class_type(rf_find_class("TheRoomVR.dll", "", cls));
    if (!type) {
        rf_log("objs: class %s not found", cls);
        return;
    }
    void* arr = CALLP(C_OBJ_FINDALLTYPE, void* (*)(void*, const void*))(type, NULL);
    int n = arr_len(arr);
    rf_log("objs %s: %d", cls, n);
    for (int i = 0; i < n && i < 60; i++) {
        void* c = arr_get(arr, i);
        if (!rf_alive(c)) continue;
        char nm[64];
        rf_obj_name(rf_gameobject(c), nm, sizeof nm);
        v3 p = rf_tr_pos(rf_transform(c));
        float sx = -1, sy = -1;
        bool vis = rf_world_to_screen(p, &sx, &sy);
        rf_log("  obj '%s' pos(%.2f %.2f %.2f) screen(%.2f %.2f) vis=%d", nm, p.x, p.y, p.z, sx, sy, vis);
    }
}

// dev diagnostics: raw fields of every instance of a game class (f = float, i = int32, b = byte, p = pointer)
void rf_dev_peek(const char* cls, const char* spec) {
    void* type = rf_class_type(rf_find_class("TheRoomVR.dll", "", cls));
    void* arr = type ? CALLP(C_OBJ_FINDALLTYPE, void* (*)(void*, const void*))(type, NULL) : NULL;
    for (int i = 0; i < arr_len(arr) && i < 12; i++) {
        char* o = arr_get(arr, i);
        if (!rf_alive(o)) continue;
        char nm[48], line[400];
        rf_obj_name(rf_gameobject(o), nm, sizeof nm);
        int n = snprintf(line, sizeof line, "peek %s '%s':", cls, nm);
        char buf[160];
        snprintf(buf, sizeof buf, "%s", spec);
        for (char* tok = strtok(buf, ","); tok && n < (int)sizeof line - 40; tok = strtok(NULL, ",")) {
            char kind = tok[0];
            unsigned off = (unsigned)strtoul(tok + 1, NULL, 16);
            if (kind == 'f') n += snprintf(line + n, sizeof line - n, " %s=%.3f", tok, *(float*)(o + off));
            else if (kind == 'i') n += snprintf(line + n, sizeof line - n, " %s=%d", tok, *(int32_t*)(o + off));
            else if (kind == 'b') n += snprintf(line + n, sizeof line - n, " %s=%d", tok, *(uint8_t*)(o + off));
            else if (kind == 'p') n += snprintf(line + n, sizeof line - n, " %s=%p", tok, *(void**)(o + off));
        }
        rf_log("%s", line);
    }
}

// dev test aid: the game's own Player.TeleportTo for a named PlayerLocation, also one not yet unlocked by the
// story — used only to reach a mechanic for a touch test; the action itself is then done by real touch
void rf_dev_tpto(const char* name) {
    void* type = rf_class_type(rf_find_class("TheRoomVR.dll", "", "PlayerLocation"));
    void* arr = type ? CALLP(C_OBJ_FINDALLTYPE, void* (*)(void*, const void*))(type, NULL) : NULL;
    for (int i = 0; i < arr_len(arr); i++) {
        void* loc = arr_get(arr, i);
        char nm[64];
        rf_obj_name(rf_gameobject(loc), nm, sizeof nm);
        if (rf_alive(loc) && !strcmp(nm, name) && rf_alive(g_player)) {
            CALLP(C_PLAYER_TELEPORTTO, void (*)(void*, void*, const void*))(g_player, loc, NULL);
            rf_log("tpto: '%s'", nm);
            return;
        }
    }
    rf_log("tpto: '%s' not found", name);
}

void rf_dev_rhide(const char* name, bool hide) {
    if (!types_ready() || !g_il2.string_new) return;
    void* go = CALLP(C_GO_FIND, void* (*)(void*, const void*))(g_il2.string_new(name), NULL);
    if (!rf_alive(go)) {
        rf_log("rhide: '%s' not found", name);
        return;
    }
    void* comps = CALLP(C_GO_GETCOMPS, void* (*)(void*, void*, const void*))(go, g_t_component, NULL);
    for (int i = 0; i < arr_len(comps); i++) {
        void* c = arr_get(comps, i);
        if (c && rf_class_is(c, "Renderer")) CALLP(C_REND_SETENABLED, void (*)(void*, bool, const void*))(c, !hide, NULL);
    }
    rf_log("rhide: '%s' renderers %s", name, hide ? "off" : "on");
}

// active renderers with their materials/shaders and screen position; filter: substring of object, material or
// shader name (NULL or "" = the first 200)
void rf_dev_rlist(const char* filter) {
    if (!types_ready()) return;
    void* rs = CALLP(C_OBJ_FINDALLTYPE, void* (*)(void*, const void*))(g_t_renderer, NULL);
    int n = arr_len(rs), shown = 0;
    bool all = !filter || !*filter;
    rf_log("rlist: %d renderers%s%s", n, all ? "" : " matching ", all ? "" : filter);
    for (int i = 0; i < n && shown < 200; i++) {
        void* r = arr_get(rs, i);
        if (!rf_alive(r)) continue;
        char nm[64], sh[256];
        rf_obj_name(rf_gameobject(r), nm, sizeof nm);
        renderer_shaders(r, sh, sizeof sh);
        if (!all && !strstr(nm, filter) && !strstr(sh, filter)) continue;
        shown++;
        float sx = -1, sy = -1;
        bool vis = rf_world_to_screen(rf_tr_pos(rf_transform(r)), &sx, &sy);
        rf_log("  r '%s' %s en=%d %s screen(%.2f %.2f) vis=%d", nm, rf_class_name(r),
               CALLP(C_REND_GETENABLED, bool (*)(void*, const void*))(r, NULL), sh, sx, sy, vis);
    }
}
