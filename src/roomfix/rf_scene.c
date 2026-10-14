// Scene inspection (dev console) and the volumetric-light leftovers of the PICO build.
//
// The PICO build strips the Hx volumetric lighting scripts (HxVolumetricLight / HxDummyLight / HxVolumetricCamera)
// but keeps their GameObjects. Those objects still carry a renderer for the light volume; without the script that
// drives it the volume is drawn as a flat translucent shape (the blue disc / orange wedge of the main menu).
#include <stdio.h>
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

// every active renderer with its materials/shaders and screen position
void rf_dev_rlist(void) {
    if (!types_ready()) return;
    void* rs = CALLP(C_OBJ_FINDALLTYPE, void* (*)(void*, const void*))(g_t_renderer, NULL);
    int n = arr_len(rs);
    rf_log("rlist: %d renderers", n);
    for (int i = 0; i < n && i < 200; i++) {
        void* r = arr_get(rs, i);
        if (!rf_alive(r)) continue;
        char nm[64], sh[256];
        rf_obj_name(rf_gameobject(r), nm, sizeof nm);
        renderer_shaders(r, sh, sizeof sh);
        float sx = -1, sy = -1;
        bool vis = rf_world_to_screen(rf_tr_pos(rf_transform(r)), &sx, &sy);
        rf_log("  r '%s' %s en=%d %s screen(%.2f %.2f) vis=%d", nm, rf_class_name(r),
               CALLP(C_REND_GETENABLED, bool (*)(void*, const void*))(r, NULL), sh, sx, sy, vis);
    }
}
