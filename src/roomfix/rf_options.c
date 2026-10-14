// Port rows in the game's own Options screen (front end and pause menu):
//   FPS LIMIT      < 30 | 45 | 60 | DISPLAY (n Hz) >   default 60
//   FIELD OF VIEW  < 70 | 80 | 90 | 100 | 110 >        default 100
//   QA (TESTING)   < off | level select | skip level | all >   default off (stage 8, rf_chapters.c)
// The rows are clones of the authored "turn fade" selector row (same layout, buttons, sounds and fonts) placed
// where that row was. VR-only entries are hidden: turn fade (snap-turn comfort fade), posture (sitting /
// standing), standing-height calibration and the VR controller map ("view controls").
// The clones' arrow handlers (UiOptionSelector.OnLeftPressed/OnRightPressed) and UpdateText are answered here,
// so the authored OnChanged event of the turn-fade row never fires for them.
#include <stdio.h>
#include <string.h>

#include "rf.h"

typedef void (*fn_setb)(void*, bool, const void*);

enum { ROW_FPS = 0, ROW_FOV = 1, ROW_QA = 2, NROW };
#define NSCREEN 4

static struct {
    void* screen;
    void* sel[NROW];  // UiOptionSelector of the cloned rows
} g_os[NSCREEN];

static void* str(const char* s) { return g_il2.string_new ? g_il2.string_new(s) : NULL; }

static void set_active(void* go, bool on) {
    if (rf_alive(go)) CALLP(C_GO_SETACTIVE, fn_setb)(go, on, NULL);
}

static void* find(void* t, const char* path) {
    void* s = str(path);
    return s && rf_alive(t) ? CALLP(C_TR_FIND, void* (*)(void*, void*, const void*))(t, s, NULL) : NULL;
}

static void set_key(void* loctext, const char* key) {
    if (!rf_alive(loctext)) return;
    CALLP(C_LOCTEXT_SETKEY, void (*)(void*, void*, const void*))(loctext, str(key), NULL);
    CALLP(C_LOCTEXT_REFRESH, void (*)(void*, const void*))(loctext, NULL);
}

static int row_of(void* sel) {
    if (!sel) return -1;
    for (int i = 0; i < NSCREEN; i++)
        for (int r = 0; r < NROW; r++)
            if (g_os[i].sel[r] == sel) return r;
    return -1;
}

static void show_value(void* sel, int row) {
    if (!rf_alive(sel)) return;
    char key[32];
    // QA values: the game's own words where it has them (off, level select, skip level)
    static const char* const QA_KEYS[QA_COUNT] = {"FE_HINTS_OFF", "FE_BUTTON_LEVEL_SELECT", "FE_BUTTON_SKIP_LEVEL",
                                                  "PORT_ALL"};
    if (row == ROW_QA) snprintf(key, sizeof key, "%s", QA_KEYS[g_qa >= 0 && g_qa < QA_COUNT ? g_qa : 0]);
    else if (row == ROW_FPS && g_fps_idx == RF_FPS_DISPLAY) snprintf(key, sizeof key, "PORT_V_DISPLAY");
    else snprintf(key, sizeof key, "PORT_V_%d", row == ROW_FPS ? rf_fps_value(g_fps_idx) : rf_fov_value(g_fov_idx));
    set_key(*(void**)((char*)sel + 0x28), key);  // UiOptionSelector.CurrentSelectionText
}

static void show_all(int row) {
    for (int i = 0; i < NSCREEN; i++)
        if (rf_alive(g_os[i].sel[row])) show_value(g_os[i].sel[row], row);
}

// Object.Instantiate(Transform) clones the whole row GameObject and returns the clone's Transform.
static void* clone_row(void* row, void* parent, int sibling, const char* name, const char* label_key, void* sel_type,
                       void* txt_type) {
    void* ct = CALLP(C_OBJ_INSTANTIATE, void* (*)(void*, void*, bool, const void*))(row, parent, false, NULL);
    if (!rf_alive(ct)) return NULL;
    void* go = rf_gameobject(ct);
    CALLP(C_OBJ_SETNAME, void (*)(void*, void*, const void*))(go, str(name), NULL);
    CALLP(C_TR_SETSIBLING, void (*)(void*, int32_t, const void*))(ct, sibling, NULL);
    set_active(go, true);
    void* sel = CALLP(C_GO_GETCHILDCOMP, void* (*)(void*, void*, bool, const void*))(go, sel_type, true, NULL);
    void* lt = find(ct, "Option/LEFT Text");
    void* label = lt ? CALLP(C_COMP_GETCOMP, void* (*)(void*, void*, const void*))(lt, txt_type, NULL) : NULL;
    set_key(label, label_key);
    rf_log("options: row '%s' sel=%p label=%p", name, sel, label);
    return rf_alive(sel) ? sel : NULL;
}

static void patch(void* screen) {
    char* s = (char*)screen;
    int slot = -1;
    for (int i = 0; i < NSCREEN && slot < 0; i++)
        if (g_os[i].screen == screen) slot = i;
    for (int i = 0; i < NSCREEN && slot < 0; i++)
        if (!rf_alive(g_os[i].screen)) {
            slot = i;
            memset(&g_os[i], 0, sizeof g_os[i]);
            g_os[i].screen = screen;
        }
    if (slot < 0) return;

    set_active(*(void**)(s + 0x68), false);  // OptionsScreen.PostureChanger
    set_active(*(void**)(s + 0x88), false);  // OptionsScreen.CalibrateStandingButton
    // VR controller map: pause-menu and front-end layouts name the button's parent differently
    static const char* const CONTROLS[] = {"Layout [Option Selectors]/VERT Layout [Option Buttons]/Button [Controls]",
                                           "Layout [Profile Option Buttons]/Button [Controls]"};
    for (size_t i = 0; i < sizeof CONTROLS / sizeof CONTROLS[0]; i++) {
        void* controls = find(rf_transform(screen), CONTROLS[i]);
        if (controls) set_active(rf_gameobject(controls), false);
    }

    void* fade = *(void**)(s + 0x50);  // OptionsScreen.TurnFadeSelector
    if (!rf_alive(fade)) return;
    void* row = rf_transform(fade);
    char nm[64];
    rf_obj_name(row, nm, sizeof nm);
    if (!strcmp(nm, "Option")) row = rf_tr_parent(row);
    // the QA row only in the front end's Options (a pre-game test setting; the pause menu's panel has no room for
    // a third port row: its top row would run into the title)
    bool want_qa = !strcmp(g_scene, "MainMenu");
    if (!rf_alive(g_os[slot].sel[ROW_FPS]) || !rf_alive(g_os[slot].sel[ROW_FOV]) ||
        (want_qa && !rf_alive(g_os[slot].sel[ROW_QA]))) {
        void* parent = rf_tr_parent(row);
        int idx = CALLP(C_TR_GETSIBLING, int32_t (*)(void*, const void*))(row, NULL);
        void* sel_type = rf_type_of(fade);
        void* txt_type = rf_type_of(*(void**)((char*)fade + 0x28));
        if (!sel_type || !txt_type || !rf_alive(parent)) return;
        if (!rf_alive(g_os[slot].sel[ROW_FPS]))
            g_os[slot].sel[ROW_FPS] = clone_row(row, parent, idx + 1, "PortFpsOption", "PORT_FPS_LIMIT", sel_type, txt_type);
        if (!rf_alive(g_os[slot].sel[ROW_FOV]))
            g_os[slot].sel[ROW_FOV] = clone_row(row, parent, idx + 2, "PortFovOption", "PORT_FOV", sel_type, txt_type);
        if (want_qa && !rf_alive(g_os[slot].sel[ROW_QA]))
            g_os[slot].sel[ROW_QA] = clone_row(row, parent, idx + 3, "PortQaOption", "PORT_QA", sel_type, txt_type);
    }
    set_active(rf_gameobject(row), false);
    for (int r = 0; r < NROW; r++) show_value(g_os[slot].sel[r], r);
}

void* h_options_setup(void* self, const void* mi) {
    void* r = ORIG(H_OPTIONS_SETUP, void* (*)(void*, const void*))(self, mi);
    patch(self);
    return r;
}

static void step(int row, int d) {
    if (row == ROW_FPS) g_fps_idx = (g_fps_idx + d + RF_FPS_COUNT) % RF_FPS_COUNT;
    else if (row == ROW_FOV) g_fov_idx = (g_fov_idx + d + RF_FOV_COUNT) % RF_FOV_COUNT;
    else {
        g_qa = (g_qa + d + QA_COUNT) % QA_COUNT;
        rf_log("options: QA %d (level select %s, skip level %s)", g_qa, g_qa & QA_CHAPTERS ? "all" : "reached",
               g_qa & QA_SKIP ? "shown" : "secret");
    }
    rf_settings_changed();
    show_all(row);
}

void h_uiopt_left(void* self, const void* mi) {
    int r = row_of(self);
    if (r < 0) ORIG(H_UIOPT_LEFT, void (*)(void*, const void*))(self, mi);
    else step(r, -1);
}

void h_uiopt_right(void* self, const void* mi) {
    int r = row_of(self);
    if (r < 0) ORIG(H_UIOPT_RIGHT, void (*)(void*, const void*))(self, mi);
    else step(r, +1);
}

void h_uiopt_update(void* self, const void* mi) {
    int r = row_of(self);
    if (r < 0) ORIG(H_UIOPT_UPDATE, void (*)(void*, const void*))(self, mi);
    else show_value(self, r);
}
