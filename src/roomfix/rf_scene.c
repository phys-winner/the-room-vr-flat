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

// dev route aid: the hint the game would show now (HintManager.Instance.CurrentHint: id, step and all hint keys)
void rf_dev_hint(void) {
    char* k = rf_find_class("TheRoomVR.dll", "", "HintManager");
    char* statics = k ? *(char**)(k + 0xb8) : NULL;  // Il2CppClass.static_fields
    char* hm = statics ? *(char**)statics : NULL;    // HintManager.Instance
    char* rh = hm ? *(char**)(hm + 0x60) : NULL;     // CurrentHint
    if (!rh) {
        rf_log("hint: none");
        return;
    }
    char* def = *(char**)(rh + 0x10);
    char id[96] = "?";
    if (def) rf_str_utf8(*(Il2CppString**)(def + 0x18), id, sizeof id);
    char* keys = *(char**)(rh + 0x38);  // List<string>
    int n = keys ? *(int32_t*)(keys + 0x18) : 0;
    char* items = keys ? *(char**)(keys + 0x10) : NULL;
    rf_log("hint: '%s' step %d of %d", id, *(int32_t*)(rh + 0x44), n);
    for (int i = 0; i < n && items; i++) {
        char key[96];
        rf_str_utf8(((Il2CppString**)(items + 0x20))[i], key, sizeof key);
        rf_log("  hint key %d: %s", i, key);
    }
}

// dev diagnostics: raw fields of every instance of a game class (f = float, i = int32, b = byte, p = pointer)
void rf_dev_peek(const char* cls, const char* spec, const char* filter) {
    void* type = rf_class_type(rf_find_class("TheRoomVR.dll", "", cls));
    void* arr = type ? CALLP(C_OBJ_FINDALLTYPE, void* (*)(void*, const void*))(type, NULL) : NULL;
    for (int i = 0, shown = 0; i < arr_len(arr) && shown < 12; i++) {
        char* o = arr_get(arr, i);
        if (!rf_alive(o)) continue;
        char nm[48], line[400];
        rf_obj_name(rf_gameobject(o), nm, sizeof nm);
        if (filter && *filter && !strstr(nm, filter)) continue;
        shown++;
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
            else if (kind == 'o') {  // reference to a Unity object: its GameObject's name
                char on[48] = "-";
                void* ref = *(void**)(o + off);
                if ((uintptr_t)ref < 0x10000 || ((uintptr_t)ref & 7)) ref = NULL;  // not a pointer (wrong offset or kind)
                // a component or GameObject only: rf_alive reads any C# object's first field as m_CachedPtr, and
                // get_gameObject on a plain class (HintHud.HintsRootVisibility, a VisibilityController) crashed Unity
                if (rf_class_is(ref, "Component") && rf_alive(ref)) rf_obj_name(rf_gameobject(ref), on, sizeof on);
                else if (rf_class_is(ref, "GameObject") && rf_alive(ref)) rf_obj_name(ref, on, sizeof on);
                else if (ref) snprintf(on, sizeof on, "(%s)", rf_class_name(ref));
                n += snprintf(line + n, sizeof line - n, " %s='%s'", tok, on);
            }
            else if (kind == 'l') n += snprintf(line + n, sizeof line - n, " %s=[%d]", tok,
                                                *(char**)(o + off) ? *(int32_t*)(*(char**)(o + off) + 0x18) : -1);
        }
        rf_log("%s", line);
        // List<Component> fields: every element with its GameObject and where it is
        snprintf(buf, sizeof buf, "%s", spec);
        for (char* tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
            char* lst = tok[0] == 'l' ? *(char**)(o + (unsigned)strtoul(tok + 1, NULL, 16)) : NULL;
            int sz = lst ? *(int32_t*)(lst + 0x18) : 0;
            for (int k = 0; k < sz && k < 8; k++) {
                void* e = arr_get(*(void**)(lst + 0x10), k);
                if (!rf_alive(e)) continue;
                char en[48];
                rf_obj_name(rf_gameobject(e), en, sizeof en);
                v3 p = rf_tr_pos(rf_transform(e));
                float sx = -1, sy = -1;
                bool vis = rf_world_to_screen(p, &sx, &sy);
                rf_log("  %s[%d] %s '%s' pos(%.3f %.3f %.3f) screen(%.2f %.2f) vis=%d", tok, k, rf_class_name(e), en, p.x,
                       p.y, p.z, sx, sy, vis);
                if (rf_class_is(e, "Collider")) {
                    struct { v3 c, e; } b = CALLP(C_COL_BOUNDS, __typeof__(b) (*)(void*, const void*))(e, NULL);
                    rf_log("    bounds centre(%.3f %.3f %.3f) extents(%.3f %.3f %.3f)", b.c.x, b.c.y, b.c.z, b.e.x, b.e.y, b.e.z);
                }
            }
        }
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

// dev test aid: the game's own Superzoom.SetState for a named superzoom (0 Active, 1 Inactive) — used only to reach
// the superzoom touch path ahead of the story; entering it is then done by touch (teleport mode + tap)
void rf_dev_szstate(const char* name, int state) {
    void* type = rf_class_type(rf_find_class("TheRoomVR.dll", "", "Superzoom"));
    void* arr = type ? CALLP(C_OBJ_FINDALLTYPE, void* (*)(void*, const void*))(type, NULL) : NULL;
    for (int i = 0; i < arr_len(arr); i++) {
        void* sz = arr_get(arr, i);
        char nm[64];
        rf_obj_name(rf_gameobject(sz), nm, sizeof nm);
        if (rf_alive(sz) && !strcmp(nm, name)) {
            CALLP(C_SZ_SETSTATE, void (*)(void*, int32_t, const void*))(sz, state, NULL);
            rf_log("szstate: '%s' -> %d", nm, state);
            return;
        }
    }
    rf_log("szstate: '%s' not found", name);
}

// dev: the game's level list (GameStateManager.Levels) and the active profile slot's progress
static void str8(void* s, char* out, size_t n) {
    rf_str_utf8((const Il2CppString*)s, out, n);
    if (!s) snprintf(out, n, "-");
}

// the live instance of a TheRoomVR.dll class: the game's managers are ScriptableObject singletons (SingletonBase),
// components must sit on an active GameObject (prefab assets are skipped)
void* rf_live_instance(const char* cls) {
    void* type = rf_class_type(rf_find_class("TheRoomVR.dll", "", cls));
    void* arr = type ? CALLP(C_RES_FINDALL, void* (*)(void*, const void*))(type, NULL) : NULL;
    for (int i = 0; i < arr_len(arr); i++) {
        void* o = arr_get(arr, i);
        if (!rf_alive(o)) continue;
        if (!rf_class_is(o, "Component")) return o;
        void* go = rf_gameobject(o);
        if (rf_alive(go) && CALLP(C_GO_ACTIVEH, bool (*)(void*, const void*))(go, NULL)) return o;
    }
    return NULL;
}

void rf_dev_levels(void) {
    char* gsm = rf_live_instance("GameStateManager");
    if (!rf_alive(gsm)) {
        rf_log("levels: no GameStateManager");
        return;
    }
    void* lv = *(void**)(gsm + 0x40);
    rf_log("levels: %d, state %d, current %p, disableSaveLoading %d", rf_list_size(lv), *(int32_t*)(gsm + 0x60),
           *(void**)(gsm + 0x68), gsm[0x88]);
    for (int i = 0; i < rf_list_size(lv); i++) {
        char* li = rf_list_get(lv, i);
        char sc[48], nk[64], ls[64], nx[48] = "-";
        str8(*(void**)(li + 0x18), sc, sizeof sc);
        str8(*(void**)(li + 0x20), nk, sizeof nk);
        str8(*(void**)(li + 0x30), ls, sizeof ls);
        if (rf_alive(*(void**)(li + 0x28))) str8(*(void**)(*(char**)(li + 0x28) + 0x18), nx, sizeof nx);
        rf_log("  level[%d] scene '%s' nameKey '%s' next '%s' loading '%s'", i, sc, nk, nx, ls);
    }
    char* sm = rf_live_instance("SlotManager");
    char* slot = rf_alive(sm) ? *(char**)(sm + 0x40) : NULL;
    if (!slot) {
        rf_log("levels: no active slot");
        return;
    }
    char nm[16], cs[48];
    str8(*(void**)(slot + 0x10), nm, sizeof nm);
    str8(*(void**)(slot + 0x18), cs, sizeof cs);
    void* seen = *(void**)(slot + 0x50);
    rf_log("slot '%s' currentScene '%s' gameComplete %d loadingScreensSeen %d", nm, cs, slot[0x2d], rf_list_size(seen));
    for (int i = 0; i < rf_list_size(seen); i++) {
        char sn[64];
        str8(rf_list_get(seen, i), sn, sizeof sn);
        rf_log("  seen '%s'", sn);
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

// dev (round 4): every TextMeshPro text of the scene with its words, where it is and its size; FILTER matches the
// object's name or the text
void rf_dev_texts(const char* filter) {
    void* type = rf_class_type(rf_find_class("Unity.TextMeshPro.dll", "TMPro", "TMP_Text"));
    void* arr = type ? CALLP(C_OBJ_FINDALLTYPE, void* (*)(void*, const void*))(type, NULL) : NULL;
    void* cam = rf_main_camera();
    v3 cp = cam ? rf_tr_pos(rf_transform(cam)) : (v3){0, 0, 0};
    rf_log("texts: %d", arr_len(arr));
    for (int i = 0, shown = 0; i < arr_len(arr) && shown < 60; i++) {
        void* t = arr_get(arr, i);
        if (!rf_alive(t)) continue;
        char nm[64], tx[96], root[64];
        rf_obj_name(rf_gameobject(t), nm, sizeof nm);
        rf_str_utf8((Il2CppString*)CALLP(C_TMP_GETTEXT, fn_obj1)(t, NULL), tx, sizeof tx);
        if (filter && *filter && !strstr(nm, filter) && !strstr(tx, filter)) continue;
        shown++;
        void* tr = rf_transform(t), *r = tr;
        for (int k = 0; k < 12 && rf_alive(rf_tr_parent(r)); k++) r = rf_tr_parent(r);
        rf_obj_name(r, root, sizeof root);
        v3 p = rf_tr_pos(tr);
        float sx = -1, sy = -1;
        bool vis = rf_world_to_screen(p, &sx, &sy);
        bool act = CALLP(C_GO_ACTIVEH, bool (*)(void*, const void*))(rf_gameobject(t), NULL);
        rf_log("  text '%s' act=%d root='%s' \"%s\" pos(%.2f %.2f %.2f) dist %.2f screen(%.2f %.2f) vis=%d font %.1f scale %.5f",
               nm, act, root, tx, p.x, p.y, p.z, v3_len(v3_sub(p, cp)), sx, sy, vis,
               CALLP(C_TMP_FONTSIZE, float (*)(void*, const void*))(t, NULL),
               CALLP(C_TR_LOSSYSCALE, v3 (*)(void*, const void*))(tr, NULL).y);
    }
}

// dev (round 4): inventory items' display pose (InventoryItemGrabbable.InventorySlotTransform): the item's side that
// faces -Z / +Y of that transform, against the direction to the eyes and the screen's up
void rf_dev_slotfaces(void) {
    void* type = rf_class_type(rf_find_class("TheRoomVR.dll", "", "InventoryItemGrabbable"));
    void* arr = type ? CALLP(C_OBJ_FINDALLTYPE, void* (*)(void*, const void*))(type, NULL) : NULL;
    void* cam = rf_main_camera();
    if (!cam) return;
    v3 cp = rf_tr_pos(rf_transform(cam));
    v3 up = q_rot(rf_tr_rot(rf_transform(cam)), (v3){0, 1, 0});
    for (int i = 0; i < arr_len(arr); i++) {
        char* it = arr_get(arr, i);
        if (!rf_alive(it)) continue;
        void* st = *(void**)(it + 0xa8);  // InventorySlotTransform
        char nm[64];
        rf_obj_name(it, nm, sizeof nm);
        void* t = rf_transform(it);
        v3 p = rf_tr_pos(t);
        if (!rf_alive(st)) {
            rf_log("  slotface '%s': no slot transform pos(%.2f %.2f %.2f)", nm, p.x, p.y, p.z);
            continue;
        }
        quat rs = rf_tr_rot(st);
        v3 front = q_rot(rs, (v3){0, 0, -1}), top = q_rot(rs, (v3){0, 1, 0});
        v3 to = v3_norm(v3_sub(cp, p));
        quat local = q_mul(q_conj(rf_tr_rot(t)), rs);
        v3 fl = q_rot(local, (v3){0, 0, -1}), tl = q_rot(local, (v3){0, 1, 0});
        rf_log("  slotface '%s' pos(%.2f %.2f %.2f) front.toEyes %.2f top.up %.2f local front(%.2f %.2f %.2f) top(%.2f %.2f %.2f)",
               nm, p.x, p.y, p.z, v3_dot(front, to), v3_dot(top, up), fl.x, fl.y, fl.z, tl.x, tl.y, tl.z);
    }
}

// dev (round 4): each inventory slot's item parent (InventorySlot.ItemParent) in the frame of the grid
// (HudManager.LayoutParent: +Z away from the viewer, +Y up when shown) - which of its axes faces the viewer
void rf_dev_slotaxes(void) {
    void* type = rf_class_type(rf_find_class("TheRoomVR.dll", "", "InventorySlot"));
    void* arr = type ? CALLP(C_RES_FINDALL, void* (*)(void*, const void*))(type, NULL) : NULL;  // hidden slots too
    rf_log("slotaxes: %d slots", arr_len(arr));
    uintptr_t k = *(uintptr_t*)(g_il2cpp_base + META_HUDMGR_TYPEINFO);
    void* statics = (k && !(k & 1) && k > 0x10000) ? *(void**)(k + 0xb8) : NULL;
    void* hm = statics ? *(void**)statics : NULL;
    void* lp = rf_alive(hm) ? *(void**)((char*)hm + 0x18) : NULL;
    if (!rf_alive(lp)) {
        rf_log("slotaxes: no inventory");
        return;
    }
    quat rl = rf_tr_rot(lp);
    for (int i = 0; i < arr_len(arr) && i < 12; i++) {
        char* sl = arr_get(arr, i);
        if (!rf_alive(sl)) continue;
        void* ip = *(void**)(sl + 0x18);  // ItemParent (BoxCollider)
        if (!rf_alive(ip)) continue;
        quat r = q_mul(q_conj(rf_tr_rot(rf_transform(ip))), rl);  // grid axes in the item parent's frame
        v3 view = q_rot(r, (v3){0, 0, -1}), up = q_rot(r, (v3){0, 1, 0});
        char nm[64];
        rf_obj_name(sl, nm, sizeof nm);
        rf_log("  slotaxes '%s': towards the viewer (%.2f %.2f %.2f) up (%.2f %.2f %.2f) in the item parent's axes", nm,
               view.x, view.y, view.z, up.x, up.y, up.z);
    }
}
