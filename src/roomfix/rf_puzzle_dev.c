// Explicit developer navigation starts the authored puzzle; all solving still comes from real touch.
#include <assert.h>
#include <stdio.h>
#include "rf.h"

#if !RF_RELEASE

static const char* const puzzle_types[] = {
    "PaintingPuzzle", "CrystalBallPuzzle", "SymbolDrawingPuzzle", "KeyboardActivator", "Grabbable",
    "VisibilityController", "OrganBall", "Portal", "PlayableDirector",
    "RotationGrabbable", "SliderGrabbable", "CombinationGrabbable", "GameObject", "PlayerLocation",
    "Superzoom", "GoggleDissolveController", "Goggles"
};

static int array_count(void* a) {
    uint64_t n = a ? *(uint64_t*)((char*)a + 0x18) : 0;
    // A loaded Chapel alone has over 8000 GameObjects, before shared prefabs are filtered out.
    return n <= 65536 ? (int)n : 0;
}
static void* array_item(void* a, int i) { return ((void**)((char*)a + 0x20))[i]; }
static void* ref_at(void* o, unsigned off) { return *(void**)((char*)o + off); }
static float float_at(void* o, unsigned off) { return *(float*)((char*)o + off); }
static int int_at(void* o, unsigned off) { return *(int32_t*)((char*)o + off); }
static int byte_at(void* o, unsigned off) { return *((uint8_t*)o + off); }

static int match_rank(const char* name, const char* query) {
    return !strcmp(name, query) ? 2 : strstr(name, query) != NULL;
}

enum { PD_READ, PD_START, PD_SHOW, PD_HIDE, PD_ROUNDTRIP };
static int parse_action(const char* s) {
    if (!strcmp(s, "start") || !strcmp(s, "arm")) return PD_START;
    if (!strcmp(s, "show")) return PD_SHOW;
    if (!strcmp(s, "roundtrip")) return PD_ROUNDTRIP;
    return !strcmp(s, "hide") ? PD_HIDE : PD_READ;
}
static bool action_allowed(int type, int action) {
    if (action == PD_ROUNDTRIP) return type == 4;
    return type == 5 || type == 12 || type == 15 ? action == PD_SHOW || action == PD_HIDE : action == PD_START;
}

static bool roundtrip_ready(int action, int picked, int interaction, bool owner, bool in_inventory, bool initialized) {
    return action == 0 && picked == 0 && interaction == 0 && !owner && !in_inventory && initialized;
}

static bool call_ready(int id) {
    if (g_call_ok[id]) return true;
    rf_log("puzzle refused: unverified API %s", g_call_desc[id].name);
    return false;
}

static void roundtrip_item(void* item) {
    const int calls[] = {C_PD_SAVE_GET, C_PD_SAVE_ATTR, C_PD_INV_LOAD, C_INV_ISIN};
    for (unsigned i = 0; i < sizeof calls / sizeof *calls; i++) if (!call_ready(calls[i])) return;
    uintptr_t klass = *(uintptr_t*)(g_il2cpp_base + META_INVMGR_TYPEINFO);
    if (!klass || (klass & 1) || klass < 0x10000) { rf_log("puzzle roundtrip refused: no inventory class"); return; }
    void* statics = ref_at((void*)klass, 0xb8);
    void* mgr = statics ? *(void**)statics : NULL;
    if (!rf_alive(mgr)) { rf_log("puzzle roundtrip refused: no inventory manager"); return; }
    bool in_inventory = CALLP(C_INV_ISIN, bool (*)(void*, void*, const void*))(mgr, item, NULL);
    int action = int_at(item, 0x128), picked = int_at(item, 0x190), interaction = int_at(item, 0x90);
    void* owner = ref_at(item, 0x80);
    bool initialized = byte_at(item, 0x28) && ref_at(item, 0x30) && byte_at(item, 0x1f0);
    rf_log("puzzle roundtrip before action=%d picked=%d interaction=%d owner=%p inInventory=%d initialized=%d",
           action, picked, interaction, owner, in_inventory, initialized);
    if (!roundtrip_ready(action, picked, interaction, owner != NULL, in_inventory, initialized)) {
        rf_log("puzzle roundtrip refused: initialized idle world pickup with action0/picked0 required"); return;
    }
    void* data = CALLP(C_PD_SAVE_GET, void* (*)(void*, const void*))(item, NULL);
    if (!data || !rf_class_is(data, "InventoryItemGrabbableSaveData") ||
        int_at(data, 0x20) != 0 || int_at(data, 0x24) != 0) {
        rf_log("puzzle roundtrip refused: unexpected native saved pair"); return;
    }
    rf_log("puzzle roundtrip saved action=%d picked=%d fields=%d", int_at(data, 0x20), int_at(data, 0x24),
           rf_list_size(ref_at(data, 0x18)));
    // SaveHeader.CurrentVersion=2. Only this component's captured state is reapplied; no profile/story load occurs.
    CALLP(C_PD_SAVE_ATTR, void (*)(void*, void*, int32_t, const void*))(item, data, 2, NULL);
    CALLP(C_PD_INV_LOAD, void (*)(void*, void*, int32_t, const void*))(item, data, 2, NULL);
    rf_log("puzzle roundtrip after action=%d picked=%d interaction=%d owner=%p inInventory=%d (native component, no disk)",
           int_at(item, 0x128), int_at(item, 0x190), int_at(item, 0x90), ref_at(item, 0x80),
           CALLP(C_INV_ISIN, bool (*)(void*, void*, const void*))(mgr, item, NULL));
}

static void* puzzle_gameobject(void* o) { return rf_class_is(o, "GameObject") ? o : rf_gameobject(o); }

static void component_name(void* o, char* name, size_t size) {
    if (rf_alive(o)) rf_obj_name(puzzle_gameobject(o), name, size);
    else snprintf(name, size, "-");
}

static void pose(const char* label, void* o) {
    if (!rf_alive(o)) { rf_log("puzzle pose %s null", label); return; }
    void* t = rf_class_is(o, "Transform") ? o : rf_class_is(o, "GameObject")
                  ? CALLP(C_GO_TRANSFORM, void* (*)(void*, const void*))(o, NULL) : rf_transform(o);
    v3 p = rf_tr_pos(t);
    quat q = rf_tr_rot(t);
    float x = -1, y = -1;
    bool visible = rf_world_to_screen(p, &x, &y);
    char name[128];
    component_name(o, name, sizeof name);
    rf_log("puzzle pose %s '%s' world(%.6f %.6f %.6f) screen(%.6f %.6f) visible=%d rotation(%.6f %.6f %.6f %.6f)",
           label, name, p.x, p.y, p.z, x, y, visible, q.x, q.y, q.z, q.w);
}

static void bounds(const char* label, void* o, bool renderer) {
    int id = renderer ? C_REND_BOUNDS : C_COL_BOUNDS;
    if (!rf_alive(o) || !call_ready(id)) return;
    struct { v3 centre, extents; } b = CALLP(id, __typeof__(b) (*)(void*, const void*))(o, NULL);
    rf_log("puzzle bounds %s centre(%.6f %.6f %.6f) extents(%.6f %.6f %.6f)", label,
           b.centre.x, b.centre.y, b.centre.z, b.extents.x, b.extents.y, b.extents.z);
}

static void report_grab(void* o) {
    void* owner = ref_at(o, 0x80);
    rf_log("puzzle grab class=%s state=%d simulation=%d held=%d owner=%s", rf_class_name(o), int_at(o, 0x40), int_at(o, 0x44),
           byte_at(o, 0x90), owner && owner == rf_controller(0) ? "left" : owner && owner == rf_controller(1) ? "right" : "-");
    if (rf_class_is(o, "RotationGrabbable")) {
        v3 a = *(v3*)((char*)o + 0x94);
        rf_log("puzzle rotation angle=%.6f velocity=%.6f localAxis(%.6f %.6f %.6f) useVelocity=%d twist=%d",
               float_at(o, 0x118), float_at(o, 0x108), a.x, a.y, a.z, byte_at(o, 0xa0), byte_at(o, 0xa8));
    } else if (rf_class_is(o, "SliderGrabbable")) {
        v3 a = *(v3*)((char*)o + 0x94);
        rf_log("puzzle slider displacement=%.6f velocity=%.6f localAxis(%.6f %.6f %.6f)",
               float_at(o, 0xe8), float_at(o, 0xd4), a.x, a.y, a.z);
    }
}

static void report_paint(void* o) {
    rf_log("puzzle hold visible=%d current=%.6f min=%.6f max=%.6f hud=%u", g_hold[0] > 0.5f,
           g_hold[1], g_hold[2], g_hold[3], (unsigned)g_hud_state);
    void* cam = rf_main_camera();
    if (cam) {
        v3 p = rf_tr_pos(rf_transform(cam));
        rf_log("puzzle camera world(%.6f %.6f %.6f)", p.x, p.y, p.z);
    }
    void* points = ref_at(o, 0xd8), *rings = ref_at(o, 0xe8), *blocks = ref_at(o, 0xe0);
    char current[128];
    component_name(ref_at(o, 0xc8), current, sizeof current);
    rf_log("puzzle paint current='%s' checkpoints=%d rings=%d blockers=%d connections=%d activeRings=%d collision=%d radius=%.6f",
           current, array_count(points), array_count(rings), array_count(blocks), rf_list_size(ref_at(o, 0xd0)),
           rf_list_size(ref_at(o, 0xf0)), byte_at(o, 0x48), float_at(o, 0x50));
    pose("start", ref_at(o, 0x20));
    pose("end", ref_at(o, 0x28));
    pose("brush", ref_at(o, 0x30));
    for (int i = 0; i < array_count(points) && i < 64; i++) {
        void* p = array_item(points, i);
        if (!rf_alive(p)) continue;
        rf_log("puzzle checkpoint index=%d start=%d end=%d reached=%d radius=%.6f collisionDistance=%.6f", i,
               p == ref_at(o, 0x20), p == ref_at(o, 0x28), byte_at(p, 0x70), float_at(p, 0x18), float_at(p, 0x74));
        pose("checkpoint", p);
    }
    for (int i = 0; i < array_count(rings) && i < 32; i++) {
        void* r = array_item(rings, i);
        if (!rf_alive(r)) continue;
        pose("ring", r);
        bounds("ring", ref_at(r, 0x30), false);
    }
    for (int i = 0; i < array_count(blocks) && i < 32; i++) bounds("blocker", array_item(blocks, i), false);
}

static void report_crystal(void* o) {
    void* spots = ref_at(o, 0xc0);
    rf_log("puzzle crystal progress=%.6f solveTimer=%.6f solved=%d hotspots=%d delay=%.6f", float_at(o, 0xd8),
           float_at(o, 0xdc), byte_at(o, 0xe8), array_count(spots), float_at(o, 0x40));
    bounds("crystal", ref_at(o, 0x48), true);
    for (int i = 0; i < array_count(spots) && i < 8; i++) {
        void* s = array_item(spots, i);
        if (!rf_alive(s)) continue;
        rf_log("puzzle hotspot index=%d radius=%.6f min=%.6f minOnTarget=%.6f max=%.6f zFactor=%.6f", i,
               float_at(s, 0x58), float_at(s, 0x28), float_at(s, 0x2c), float_at(s, 0x34), float_at(s, 0x30));
        pose("hotspot", s);
        pose("vfxPivot", ref_at(s, 0x50));
        if (call_ready(C_PD_HOT_PROGRESS))
            for (int h = 0; h < 2; h++)
                rf_log("puzzle hotspotProgress index=%d hand=%d progress=%.6f", i, h,
                       CALLP(C_PD_HOT_PROGRESS, float (*)(void*, int32_t, const void*))(s, h, NULL));
    }
}

static void report_symbol(void* o) {
    void* c = rf_controller(1);
    if (c) {
        pose("fingerTip", ref_at(c, 0x40));
        pose("pokeProbe", ref_at(c, 0x60));
        pose("grabber", ref_at(c, 0x18));
    }
    void* segments = ref_at(o, 0xe0), *solutions = ref_at(o, 0xa8);
    rf_log("puzzle symbol segments=%d solutions=%d drawing=%d fingerLength=%.6f commitDelay=%.6f",
           array_count(segments), array_count(solutions), byte_at(o, 0x108), float_at(o, 0xb4), float_at(o, 0xb0));
    pose("drawingPlane", ref_at(o, 0xa0));
    v3 normal = *(v3*)((char*)o + 0xe8);
    rf_log("puzzle cachedDrawingPlane normal(%.6f %.6f %.6f) distance=%.6f", normal.x, normal.y, normal.z,
           float_at(o, 0xf4));
    for (int i = 0; i < array_count(segments) && i < 64; i++) {
        void* s = array_item(segments, i);
        if (!rf_alive(s)) continue;
        void* verts = ref_at(s, 0x40), *t = rf_transform(s);
        rf_log("puzzle segment index=%d coloured=%d lastPainted=%.6f vertices=%d", i, byte_at(s, 0x38),
               float_at(s, 0x3c), array_count(verts));
        pose("segment", s);
        if (g_call_ok[C_TR_POINT])
            for (int j = 0; j < array_count(verts) && j < 8; j++) {
                v3 local = ((v3*)((char*)verts + 0x20))[j];
                v3 world = CALLP(C_TR_POINT, v3 (*)(void*, v3, const void*))(t, local, NULL);
                rf_log("puzzle vertex segment=%d index=%d world(%.6f %.6f %.6f)", i, j, world.x, world.y, world.z);
            }
    }
    for (int i = 0; i < array_count(solutions) && i < 8; i++) {
        void* s = array_item(solutions, i), *required = rf_alive(s) ? ref_at(s, 0x18) : NULL;
        rf_log("puzzle solution index=%d requiredSegments=%d", i, rf_list_size(required));
        for (int j = 0; j < rf_list_size(required) && j < 64; j++) pose("requiredSegment", rf_list_get(required, j));
    }
}

static void report_keyboard(void* o) {
    void* keys = ref_at(o, 0x40), *volume = ref_at(o, 0x50);
    rf_log("puzzle keyboard functional=%d keysActive=%d keys=%d inVolume=%d", byte_at(o, 0x68), byte_at(o, 0x69),
           array_count(keys), rf_alive(volume) ? byte_at(volume, 0x54) : -1);
    pose("organPlayLocation", ref_at(o, 0x70));
    pose("keyboardVolume", volume);
    if (rf_alive(volume)) {
        v3 c = *(v3*)((char*)volume + 0x38), e = *(v3*)((char*)volume + 0x44);
        rf_log("puzzle keyboardBounds localCentre(%.6f %.6f %.6f) localExtents(%.6f %.6f %.6f) probeRadius=%.6f", c.x, c.y,
               c.z, e.x, e.y, e.z, float_at(volume, 0x50));
    }
    for (int i = 0; i < array_count(keys) && i < 64; i++) {
        void* k = array_item(keys, i);
        if (!rf_alive(k)) continue;
        pose("key", k);
        report_grab(k);
        void* infos = rf_alive(volume) ? ref_at(volume, 0x58) : NULL;
        for (int j = 0; j < rf_list_size(infos) && j < 64; j++) {
            void* info = rf_list_get(infos, j);
            if (!info) continue;
            void* gc = ref_at(info, 0x38);
            if (!rf_alive(gc) || CALLP(C_GC_GRABBABLE, void* (*)(void*, const void*))(gc, NULL) != k) continue;
            void* anchor = ref_at(info, 0x30);
            if (rf_alive(anchor)) pose("keyContact", ref_at(anchor, 0x28));
            break;
        }
    }
}

void rf_dev_puzzle(int argc, char* const argv[]) {
    if (!g_dev) return;
    assert(match_rank("Crystal Ball Puzzle", "Crystal Ball Puzzle") == 2 && match_rank("Puzzle", "uzz") == 1 &&
           match_rank("Puzzle", "NoMatch") == 0 && match_rank("Puzzle Copy", "Puzzle") == 1);
    assert(parse_action("start") == PD_START && parse_action("arm") == PD_START && parse_action("show") == PD_SHOW &&
           parse_action("hide") == PD_HIDE && parse_action("read") == PD_READ && action_allowed(5, PD_SHOW) &&
           !action_allowed(5, PD_START) && !action_allowed(0, PD_HIDE) && action_allowed(8, PD_START) &&
           action_allowed(9, PD_START) && action_allowed(10, PD_START) && action_allowed(11, PD_START) &&
           !action_allowed(9, PD_SHOW) && !action_allowed(10, PD_HIDE) && !action_allowed(11, PD_SHOW) &&
           action_allowed(12, PD_SHOW) && action_allowed(12, PD_HIDE) && !action_allowed(12, PD_START) &&
           action_allowed(13, PD_START) && !action_allowed(13, PD_SHOW) && action_allowed(14, PD_START) &&
           !action_allowed(14, PD_HIDE) && action_allowed(15, PD_SHOW) && !action_allowed(15, PD_START) &&
           action_allowed(16, PD_START) && !action_allowed(16, PD_SHOW) && !action_allowed(16, PD_HIDE) &&
           parse_action("roundtrip") == PD_ROUNDTRIP && action_allowed(4, PD_ROUNDTRIP) &&
           !action_allowed(5, PD_ROUNDTRIP) && !action_allowed(9, PD_ROUNDTRIP) && !action_allowed(16, PD_ROUNDTRIP));
    assert(roundtrip_ready(0, 0, 0, false, false, true) && !roundtrip_ready(1, 0, 0, false, false, true) &&
           !roundtrip_ready(0, 1, 0, false, false, true) && !roundtrip_ready(0, 0, 1, false, false, true) &&
           !roundtrip_ready(0, 0, 0, true, false, true) && !roundtrip_ready(0, 0, 0, false, true, true) &&
           !roundtrip_ready(0, 0, 0, false, false, false));
    int type = -1;
    for (unsigned i = 0; i < sizeof puzzle_types / sizeof *puzzle_types; i++)
        if (argc && !strcmp(argv[0], puzzle_types[i])) type = (int)i;
    if (type < 0) {
        rf_log("puzzle usage: TYPE [NAME] [start|arm]; VisibilityController, GameObject and GoggleDissolveController accept show|hide");
        rf_log("puzzle types: PaintingPuzzle CrystalBallPuzzle SymbolDrawingPuzzle KeyboardActivator Grabbable VisibilityController OrganBall Portal PlayableDirector RotationGrabbable SliderGrabbable CombinationGrabbable GameObject PlayerLocation Superzoom GoggleDissolveController Goggles");
        return;
    }
    int action = argc > 1 ? parse_action(argv[argc - 1]) : PD_READ;
    bool start = action != PD_READ;
    if (start && !action_allowed(type, action)) { rf_log("puzzle refused: action is not allowed for this type"); return; }
    char query[160] = "";
    for (int i = 1; i < argc - (start ? 1 : 0); i++) {
        if (strlen(query) + strlen(argv[i]) + 2 > sizeof query) { rf_log("puzzle refused: NAME too long"); return; }
        if (query[0]) strcat(query, " ");
        strcat(query, argv[i]);
    }
    if (start && !query[0]) { rf_log("puzzle refused: start requires one named instance"); return; }
    const int common[] = {C_RES_FINDALL, C_PD_GO_SCENE, C_PD_SCENE_LOADED, C_OBJ_NAME, C_GO_ACTIVEH,
                          C_GO_TRANSFORM, C_TR_POSITION, C_TR_ROTATION, C_CAM_W2S};
    for (unsigned i = 0; i < sizeof common / sizeof *common; i++) if (!call_ready(common[i])) return;
    if (type == 12) {
        if (!call_ready(C_GO_ACTIVESELF)) return;
    } else if (!call_ready(C_COMP_GO) || !call_ready(C_COMP_TRANSFORM) || !call_ready(C_BEH_ACTIVEENABLED)) return;
    const char* image = type == 8 ? "UnityEngine.DirectorModule.dll" : type == 12 ? "UnityEngine.CoreModule.dll" : "TheRoomVR.dll";
    const char* ns = type == 8 ? "UnityEngine.Playables" : type == 12 ? "UnityEngine" : "";
    void* t = rf_class_type(rf_find_class(image, ns, puzzle_types[type]));
    void* all = t ? CALLP(C_RES_FINDALL, void* (*)(void*, const void*))(t, NULL) : NULL;
    void* selected = NULL;
    int rank = 0, matches = 0, listed = 0;
    for (int i = 0; i < array_count(all); i++) {
        void* o = array_item(all, i);
        if (!rf_alive(o)) continue;
        if (type >= 9 && type <= 11 && strcmp(rf_class_name(o), puzzle_types[type])) continue;
        void* go = puzzle_gameobject(o);
        int32_t scene = CALLP(C_PD_GO_SCENE, int32_t (*)(void*, const void*))(go, NULL);
        if (!scene || !CALLP(C_PD_SCENE_LOADED, bool (*)(int32_t*, const void*))(&scene, NULL)) continue;
        char name[160];
        rf_obj_name(go, name, sizeof name);
        int r = query[0] ? match_rank(name, query) : 1;
        if (!r) continue;
        if (!query[0] && listed++ < 96)
            rf_log("puzzle instance type=%s name='%s' active=%d", puzzle_types[type], name,
                   CALLP(C_GO_ACTIVEH, bool (*)(void*, const void*))(go, NULL));
        if (r > rank) { rank = r; matches = 0; }
        if (r == rank) { selected = o; matches++; }
    }
    rf_log("puzzle selection type=%s query='%s' matches=%d rank=%d mutation=%d", puzzle_types[type], query, matches, rank, start);
    if (!query[0] || matches != 1) {
        if (query[0]) rf_log("puzzle refused: NAME must match exactly one loaded scene instance");
        return;
    }
    void* selected_go = puzzle_gameobject(selected);
    bool active = CALLP(C_GO_ACTIVEH, bool (*)(void*, const void*))(selected_go, NULL);
    if (start) {
        if ((!active && type != 12) || !rf_player() || rf_menu_context()) { rf_log("puzzle refused: active gameplay instance required"); return; }
        if (action == PD_ROUNDTRIP) {
            if (rank != 2 || strcmp(rf_class_name(selected), "InventoryItemGrabbable")) {
                rf_log("puzzle roundtrip refused: exact InventoryItemGrabbable name required"); return;
            }
            roundtrip_item(selected);
            return;
        }
        const int calls[] = {C_PD_PAINT_START, C_PD_CRYSTAL_START, C_PD_GRAB_STATE, C_PD_ORGAN_FUNC, C_PD_GRAB_STATE,
                             C_PD_VIS_BOLT, C_PD_BALL_ACTIVE, C_PD_PORTAL_ACTIVE, C_PD_DIRECTOR_PLAY,
                             C_PD_GRAB_STATE, C_PD_GRAB_STATE, C_PD_GRAB_STATE, C_GO_SETACTIVE, C_PD_LOC_STATE,
                             C_SZ_SETSTATE, C_PD_DISSOLVE_ACTIVE, C_PD_GOGGLE_LENS};
        if (!call_ready(calls[type])) return;
        if ((type == 0 && !array_count(ref_at(selected, 0xd8))) ||
            (type == 1 && (array_count(ref_at(selected, 0xc0)) != 2 || !rf_alive(ref_at(selected, 0xc8))))) {
            rf_log("puzzle refused: authored Start initialization has not finished"); return;
        }
        if (type == 0 || type == 1 || type == 7 || type == 8 || type == 16)
            CALLP(calls[type], void (*)(void*, const void*))(selected, NULL);
        else if (type == 3) {
            if (!call_ready(C_PD_ORGAN_KEYS)) return;
            CALLP(C_PD_ORGAN_FUNC, void (*)(void*, bool, const void*))(selected, true, NULL);
            CALLP(C_PD_ORGAN_KEYS, void (*)(void*, int32_t, const void*))(selected, 0, NULL);
        } else if (type == 5 || type == 6 || type == 12 || type == 15)
            CALLP(calls[type], void (*)(void*, bool, const void*))(selected, type == 6 || action == PD_SHOW, NULL);
        else CALLP(calls[type], void (*)(void*, int32_t, const void*))(selected, 0, NULL);
        rf_log("puzzle started type=%s query='%s' action=%d via authored API; solve still requires touch",
               puzzle_types[type], query, action);
    }
    active = CALLP(C_GO_ACTIVEH, bool (*)(void*, const void*))(selected_go, NULL);
    if (type == 12)
        rf_log("puzzle gameObject activeSelf=%d activeInHierarchy=%d",
               CALLP(C_GO_ACTIVESELF, bool (*)(void*, const void*))(selected, NULL), active);
    else
        rf_log("puzzle state type=%s active=%d enabled=%d", puzzle_types[type], active,
               CALLP(C_BEH_ACTIVEENABLED, bool (*)(void*, const void*))(selected, NULL));
    pose("root", selected);
    pose("camera", rf_main_camera());
    switch (type) {
        case 0: report_paint(selected); break;
        case 1: report_crystal(selected); break;
        case 2: report_grab(selected); report_symbol(selected); break;
        case 3: report_keyboard(selected); break;
        case 4: case 9: case 10: report_grab(selected); break;
        case 5: {
            void* reasons = ref_at(selected, 0x58);
            if (call_ready(C_PD_VIS_VISIBLE))
                rf_log("puzzle visibility visible=%d reasons=%d",
                       CALLP(C_PD_VIS_VISIBLE, bool (*)(void*, const void*))(selected, NULL), rf_list_size(reasons));
            for (int i = 0; i < rf_list_size(reasons) && i < 32; i++) {
                char reason[160];
                rf_str_utf8(rf_list_get(reasons, i), reason, sizeof reason);
                rf_log("puzzle invisibleReason '%s'", reason);
            }
            break;
        }
        case 6:
            rf_log("puzzle organBall active=%d resetting=%d", byte_at(selected, 0x61), byte_at(selected, 0x60));
            pose("ballPlayLocation", ref_at(selected, 0x38));
            pose("ballStart", ref_at(selected, 0x40));
            break;
        case 7:
            rf_log("puzzle portal active=%d gogglesOnly=%d requiresLens=%d wasEnabled=%d distance=%.6f", byte_at(selected, 0x38),
                   byte_at(selected, 0x39), byte_at(selected, 0x3a), byte_at(selected, 0x50), float_at(selected, 0x3c));
            break;
        case 8:
            if (call_ready(C_PD_STATE) && call_ready(C_PD_DIRECTOR_TIME) && call_ready(C_PD_DIRECTOR_DURATION))
                rf_log("puzzle director state=%d time=%.6f duration=%.6f",
                       CALLP(C_PD_STATE, int32_t (*)(void*, const void*))(selected, NULL),
                       CALLP(C_PD_DIRECTOR_TIME, double (*)(void*, const void*))(selected, NULL),
                       CALLP(C_PD_DIRECTOR_DURATION, double (*)(void*, const void*))(selected, NULL));
            break;
        case 11: {
            report_grab(selected);
            void* children = ref_at(selected, 0x98);
            int count = rf_list_size(children);
            rf_log("puzzle combination children=%d", count);
            for (int i = 0; i < count && i < 4; i++) {
                void* child = rf_list_get(children, i);
                if (!rf_alive(child)) continue;
                rf_log("puzzle combination child index=%d", i);
                pose("comboChild", child);
                report_grab(child);
            }
            break;
        }
        case 13:
            rf_log("puzzle location state=%d initial=%d", int_at(selected, 0x21c), int_at(selected, 0x38));
            pose("eyeLevel", ref_at(selected, 0x60));
            break;
        case 14:
            rf_log("puzzle superzoom state=%d entrySafe=%d type=%d entryScale=%.6f scale=%.6f",
                   int_at(selected, 0x254), byte_at(selected, 0x250), int_at(selected, 0x38),
                   float_at(selected, 0x50), float_at(selected, 0xb4));
            pose("zoomIn", ref_at(selected, 0xe0));
            pose("portalTrigger", ref_at(selected, 0xd0));
            pose("zoomOut", ref_at(selected, 0x130));
            pose("mainLocation", ref_at(selected, 0x110));
            pose("startLocation", ref_at(selected, 0x150));
            break;
        case 15:
            rf_log("puzzle dissolve enabled=%d", byte_at(selected, 0x38));
            pose("dissolveSphere", ref_at(selected, 0x50));
            break;
        case 16:
            rf_log("puzzle goggles active=%d hasLens=%d", byte_at(selected, 0xf8), byte_at(selected, 0x108));
            break;
    }
}

#endif
