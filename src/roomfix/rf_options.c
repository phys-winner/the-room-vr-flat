// Port rows in the game's own image settings screen (front end and pause menu):
//   FPS LIMIT      < 30 | 45 | 60 | DISPLAY (n Hz) >   default 60
//   FIELD OF VIEW  < 70 | 80 | 90 | 100 | 110 >        default 90
// Options keeps the input and testing settings:
//   LOOK SENSITIVITY < 25% ... 300% >                  default 200%
//   POSITION INDICATOR < on | off >                    default on (round 5: the eyes' place in the location, rf_view.c)
//   QA (TESTING)   < off | level select | skip level | all >   default off (stage 8, rf_chapters.c)
// The rows are clones of the authored "turn fade" selector row (same layout, buttons, sounds and fonts) placed
// where that row was. VR-only entries are hidden: turn fade (snap-turn comfort fade), posture (sitting /
// standing) and standing-height calibration. The VR controller map button ("view controls") stays: it opens the
// port's gesture card (rf_help.c).
//
// Round 2 (operator 2026-10-16): a GYROSCOPE button (clone of "view controls") turns the screen into the gyroscope
// page, the way the main menu shows its chapter list (rf_chapters.c): the option rows and buttons give way to
//   EYE POSITION   < off | on >              default off   (rf_gyro.c: the phone's turn moves the eyes)
//   SENSITIVITY    < 25% ... 300% >          default 100%
//   TILT AND TURN  < off | on >              default off   (the phone's turn turns the camera)
//   SENSITIVITY    < 25% ... 300% >          default 100%
// and the page's own BACK button (Android Back too) brings the options back; the title reads GYROSCOPE meanwhile.
//
// The clones' arrow handlers (UiOptionSelector.OnLeftPressed/OnRightPressed) and UpdateText are answered here,
// so the authored OnChanged event of the turn-fade row never fires for them.
#include <stdio.h>
#include <string.h>

#include "rf.h"

typedef void (*fn_setb)(void*, bool, const void*);

enum { ROW_FPS = 0, ROW_FOV, ROW_LOOK_S, ROW_GAUGE, ROW_QA, ROW_GPOS, ROW_GPOS_S, ROW_GROT, ROW_GROT_S, NROW };
#define NSCREEN 4
#define NHIDE 16

// every object kept here is a weak reference: the menu scenes are rebuilt after each level (rf_ref, rf.h)
static struct {
    rf_ref screen;
    rf_ref sel[NROW];  // UiOptionSelector of the cloned rows
    bool roomy;        // room made for the rows past the fourth (front end: divider and buttons; pause: layout)
    rf_ref gyro_btn;   // MenuButton GYROSCOPE
    rf_ref gyro_back;  // MenuButton BACK of the gyroscope page
    bool page;         // the gyroscope page is shown
    rf_ref hidden[NHIDE];  // GameObjects the page hid
    int nhidden;
    rf_ref title;      // LocaliseText of the screen title
    char title_key[48];
} g_os[NSCREEN];
#define R(x) rf_ref_get(&(x))

static void os_clear(int i) {
    rf_ref_clear(&g_os[i].screen);
    for (int r = 0; r < NROW; r++) rf_ref_clear(&g_os[i].sel[r]);
    rf_ref_clear(&g_os[i].gyro_btn);
    rf_ref_clear(&g_os[i].gyro_back);
    for (int k = 0; k < NHIDE; k++) rf_ref_clear(&g_os[i].hidden[k]);
    rf_ref_clear(&g_os[i].title);
    memset(&g_os[i], 0, sizeof g_os[i]);
}

static int screen_slot(void* screen) {
    for (int i = 0; i < NSCREEN; i++)
        if (R(g_os[i].screen) == screen) return i;
    for (int i = 0; i < NSCREEN; i++)
        if (!rf_alive(R(g_os[i].screen))) {
            os_clear(i);
            rf_ref_set(&g_os[i].screen, screen);
            return i;
        }
    return -1;
}

static void* g_sel_type;  // System.Type of UiOptionSelector
static void* g_btn_type;  // System.Type of MenuButton
static void* g_txt_type;  // System.Type of LocaliseText
static char g_on_key[48] = "FE_HINTS_ON", g_off_key[48] = "FE_HINTS_OFF";  // the hints row's own words

static void* str(const char* s) { return g_il2.string_new ? g_il2.string_new(s) : NULL; }

static void set_active(void* go, bool on) {
    if (rf_alive(go)) CALLP(C_GO_SETACTIVE, fn_setb)(go, on, NULL);
}

static bool active_self(void* go) { return rf_alive(go) && CALLP(C_GO_ACTIVESELF, bool (*)(void*, const void*))(go, NULL); }

static void* find(void* t, const char* path) {
    void* s = str(path);
    return s && rf_alive(t) ? CALLP(C_TR_FIND, void* (*)(void*, void*, const void*))(t, s, NULL) : NULL;
}

static int child_count(void* t) { return rf_alive(t) ? CALLP(C_TR_CHILDCOUNT, int32_t (*)(void*, const void*))(t, NULL) : 0; }
static void* child(void* t, int i) { return CALLP(C_TR_GETCHILD, void* (*)(void*, int32_t, const void*))(t, i, NULL); }

static void* comp_in(void* go, void* type) {
    return type && rf_alive(go) ? CALLP(C_GO_GETCHILDCOMP, void* (*)(void*, void*, bool, const void*))(go, type, true, NULL)
                                : NULL;
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
            if (R(g_os[i].sel[r]) == sel) return r;
    return -1;
}

// the row GameObject of a selector ("Option" sits inside the row)
static void* row_go(void* sel) {
    if (!rf_alive(sel)) return NULL;
    void* t = rf_transform(sel);
    char nm[64];
    rf_obj_name(t, nm, sizeof nm);
    if (!strcmp(nm, "Option")) t = rf_tr_parent(t);
    return rf_alive(t) ? rf_gameobject(t) : NULL;
}

static void show_value(void* sel, int row) {
    if (!rf_alive(sel)) return;
    char key[48];
    // QA values: the game's own words where it has them (off, level select, skip level)
    static const char* const QA_KEYS[QA_COUNT] = {"FE_HINTS_OFF", "FE_BUTTON_LEVEL_SELECT", "FE_BUTTON_SKIP_LEVEL",
                                                  "PORT_ALL"};
    switch (row) {
    case ROW_QA: snprintf(key, sizeof key, "%s", QA_KEYS[g_qa >= 0 && g_qa < QA_COUNT ? g_qa : 0]); break;
    case ROW_FPS:
        if (g_fps_idx == RF_FPS_DISPLAY) snprintf(key, sizeof key, "PORT_V_DISPLAY");
        else snprintf(key, sizeof key, "PORT_V_%d", rf_fps_value(g_fps_idx));
        break;
    case ROW_FOV: snprintf(key, sizeof key, "PORT_V_%d", rf_fov_value(g_fov_idx)); break;
    case ROW_LOOK_S: snprintf(key, sizeof key, "PORT_V_%d%%", rf_sens_pct(g_look_sens)); break;
    case ROW_GAUGE: snprintf(key, sizeof key, "%s", g_gauge_on ? g_on_key : g_off_key); break;
    case ROW_GPOS: snprintf(key, sizeof key, "%s", g_gyro_pos ? g_on_key : g_off_key); break;
    case ROW_GROT: snprintf(key, sizeof key, "%s", g_gyro_rot ? g_on_key : g_off_key); break;
    case ROW_GPOS_S: snprintf(key, sizeof key, "PORT_V_%d%%", rf_sens_pct(g_gyro_pos_sens)); break;
    default: snprintf(key, sizeof key, "PORT_V_%d%%", rf_sens_pct(g_gyro_rot_sens)); break;
    }
    set_key(*(void**)((char*)sel + 0x28), key);  // UiOptionSelector.CurrentSelectionText
}

static void show_all(int row) {
    for (int i = 0; i < NSCREEN; i++)
        show_value(R(g_os[i].sel[row]), row);
}

// Object.Instantiate(Transform) clones the whole row GameObject and returns the clone's Transform.
static void* clone_row(void* row, void* parent, int sibling, const char* name, const char* label_key) {
    void* ct = CALLP(C_OBJ_INSTANTIATE, void* (*)(void*, void*, bool, const void*))(row, parent, false, NULL);
    if (!rf_alive(ct)) return NULL;
    void* go = rf_gameobject(ct);
    CALLP(C_OBJ_SETNAME, void (*)(void*, void*, const void*))(go, str(name), NULL);
    CALLP(C_TR_SETSIBLING, void (*)(void*, int32_t, const void*))(ct, sibling, NULL);
    set_active(go, true);
    void* sel = comp_in(go, g_sel_type);
    void* lt = find(ct, "Option/LEFT Text");
    void* label = lt ? CALLP(C_COMP_GETCOMP, void* (*)(void*, void*, const void*))(lt, g_txt_type, NULL) : NULL;
    set_key(label, label_key);
    if (rf_alive(lt) && !strcmp(label_key, "PORT_LOOK_SENS")) {
        // Long translations need space before the selector's left arrow.
        v3 scale = CALLP(C_TR_LOCALSCALE, v3 (*)(void*, const void*))(lt, NULL);
        CALLP(C_TR_SETLOCALSCALE, void (*)(void*, v3, const void*))(lt, v3_scale(scale, 0.85f), NULL);
    }
    rf_log("options: row '%s' sel=%p label=%p", name, sel, label);
    return rf_alive(sel) ? sel : NULL;
}

// a copy of an authored menu button (same look, sounds and pointer behaviour) in the same layout group; presses on
// it are answered by rf_options_press (h_button_press), never by the authored click listener the copy carries
static void* clone_button(void* src_btn, int sibling, const char* name, const char* key) {
    void* st = rf_transform(src_btn);
    void* ct = CALLP(C_OBJ_INSTANTIATE, void* (*)(void*, void*, bool, const void*))(st, rf_tr_parent(st), false, NULL);
    if (!rf_alive(ct)) return NULL;
    void* go = rf_gameobject(ct);
    CALLP(C_OBJ_SETNAME, void (*)(void*, void*, const void*))(go, str(name), NULL);
    CALLP(C_TR_SETSIBLING, void (*)(void*, int32_t, const void*))(ct, sibling, NULL);
    void* btn = comp_in(go, rf_type_of(src_btn));
    if (rf_alive(btn)) CALLP(C_MENUBTN_SETTEXT, void (*)(void*, void*, const void*))(btn, str(key), NULL);
    set_active(go, true);
    rf_log("options: button '%s' %s", name, rf_alive(btn) ? "added" : "FAILED");
    return rf_alive(btn) ? btn : NULL;
}

// The front end's Options screen has room for four selector rows above its divider. Its rows sit in a centred layout,
// so every further row grows it half a row pitch up towards the subtitle and half down: the
// divider and the buttons below move down by a whole pitch per row (Hints -> Language, authored), the rows' layout by a
// quarter (measured with six rows: the top row then clears the subtitle by half a pitch, the last one keeps 0.8 of a
// pitch above the divider).
static void make_room(void* screen, int rows) {
    void* st = rf_transform(screen);
    void* hints = find(st, "Layout [Profile Option types]/HintsOption");
    void* lang = find(st, "Layout [Profile Option types]/LanguageOption");
    void* lay = find(st, "Layout [Profile Option types]");
    if (!hints || !lang || !lay || rows <= 0) return;
    v3 pitch = v3_scale(v3_sub(rf_tr_pos(lang), rf_tr_pos(hints)), (float)rows);
    static const char* const BELOW[] = {"Sprite [line] (1)", "Layout [Profile Option Buttons]"};
    for (size_t i = 0; i < sizeof BELOW / sizeof BELOW[0]; i++) {
        void* t = find(st, BELOW[i]);
        if (t) CALLP(C_TR_SETPOS, void (*)(void*, v3, const void*))(t, v3_add(rf_tr_pos(t), pitch), NULL);
    }
    CALLP(C_TR_SETPOS, void (*)(void*, v3, const void*))(lay, v3_add(rf_tr_pos(lay), v3_scale(pitch, 0.25f)), NULL);
    rf_log("options: rows moved by %d quarter rows, divider and buttons by %d rows (%.3f %.3f %.3f)", rows, rows, pitch.x,
           pitch.y, pitch.z);
}

// The pause menu's Options panel keeps its rows and the buttons below them in one centred layout, so every row added
// grows it into the title as much as downwards; shift it down to keep Hints clear of the subtitle.
static void make_room_pause(void* screen, int rows) {
    void* st = rf_transform(screen);
    void* lay = find(st, "Layout [Option Selectors]");
    void* hints = find(st, "Layout [Option Selectors]/HintsOption");
    void* lang = find(st, "Layout [Option Selectors]/LanguageOption");
    if (!lay || !hints || !lang) return;
    v3 pitch = v3_scale(v3_sub(rf_tr_pos(lang), rf_tr_pos(hints)), 0.5f * (rows + 1));
    CALLP(C_TR_SETPOS, void (*)(void*, v3, const void*))(lay, v3_add(rf_tr_pos(lay), pitch), NULL);
    rf_log("options: pause layout moved down for %d extra rows (%.3f %.3f %.3f)", rows, pitch.x, pitch.y, pitch.z);
}

// the screen title's text ("Text [Options]" under the title art in both layouts)
static void* find_named(void* t, const char* name, int depth) {
    if (!rf_alive(t) || depth > 3) return NULL;
    char nm[64];
    rf_obj_name(t, nm, sizeof nm);
    if (!strcmp(nm, name)) return t;
    for (int i = 0; i < child_count(t) && i < 48; i++) {
        void* r = find_named(child(t, i), name, depth + 1);
        if (r) return r;
    }
    return NULL;
}

// ---- the gyroscope page ----
static bool page_row(int i, void* go) {
    for (int r = ROW_GPOS; r <= ROW_GROT_S; r++)
        if (go && row_go(R(g_os[i].sel[r])) == go) return true;
    return false;
}

static void hide(int i, void* go) {
    if (g_os[i].nhidden < NHIDE) {
        rf_ref_set(&g_os[i].hidden[g_os[i].nhidden++], go);
        set_active(go, false);
    }
}

static void page(int i, bool on) {
    if (g_os[i].page == on) return;
    void* gyro_row = row_go(R(g_os[i].sel[ROW_GPOS]));  // a GameObject: its transform through GameObject.get_transform
    void* rows = rf_alive(gyro_row) ? rf_tr_parent(CALLP(C_GO_TRANSFORM, void* (*)(void*, const void*))(gyro_row, NULL)) : NULL;
    void* gb = R(g_os[i].gyro_btn), *gk = R(g_os[i].gyro_back), *scr = R(g_os[i].screen);
    void* btns = rf_alive(gb) ? rf_tr_parent(rf_transform(gb)) : NULL;
    void* back_go = rf_alive(gk) ? rf_gameobject(gk) : NULL;
    if (on) {
        if (!rows || !btns || !back_go || !rf_alive(scr)) return;
        g_os[i].nhidden = 0;
        for (int k = 0; k < child_count(rows); k++) {  // option rows (the buttons' layout sits here in the pause menu)
            void* go = rf_gameobject(child(rows, k));
            if (active_self(go) && !page_row(i, go) && comp_in(go, g_sel_type)) hide(i, go);
        }
        for (int k = 0; k < child_count(btns); k++) {
            void* go = rf_gameobject(child(btns, k));
            if (go != back_go && active_self(go) && comp_in(go, g_btn_type)) hide(i, go);
        }
        for (int r = ROW_GPOS; r <= ROW_GROT_S; r++) {
            void* s = R(g_os[i].sel[r]);
            set_active(row_go(s), true);
            show_value(s, r);
        }
        set_active(back_go, true);
        void* tt = find_named(rf_transform(scr), "Text [Options]", 0);
        void* lt = tt ? CALLP(C_COMP_GETCOMP, void* (*)(void*, void*, const void*))(tt, g_txt_type, NULL) : NULL;
        rf_ref_set(&g_os[i].title, rf_alive(lt) ? lt : NULL);
        if (rf_alive(lt)) {
            rf_str_utf8(*(Il2CppString**)((char*)lt + 0x20), g_os[i].title_key, sizeof g_os[i].title_key);  // _Key
            set_key(lt, "PORT_GYRO");
        }
        rf_log("options: gyroscope page (%d entries hidden, title %s)", g_os[i].nhidden, rf_alive(lt) ? "set" : "-");
    } else {
        for (int r = ROW_GPOS; r <= ROW_GROT_S; r++) set_active(row_go(R(g_os[i].sel[r])), false);
        set_active(back_go, false);
        for (int k = 0; k < g_os[i].nhidden; k++) {
            set_active(R(g_os[i].hidden[k]), true);
            rf_ref_clear(&g_os[i].hidden[k]);
        }
        g_os[i].nhidden = 0;
        void* t = R(g_os[i].title);
        if (rf_alive(t) && g_os[i].title_key[0]) set_key(t, g_os[i].title_key);
        rf_log("options: back from the gyroscope page");
    }
    g_os[i].page = on;
}

bool rf_options_press(void* btn) {
    if (!btn) return false;
    for (int i = 0; i < NSCREEN; i++) {
        if (btn == R(g_os[i].gyro_btn)) {
            page(i, true);
            return true;
        }
        if (btn == R(g_os[i].gyro_back)) {
            page(i, false);
            return true;
        }
    }
    return false;
}

bool rf_options_back(void) {
    void* mc = rf_menu_controller();
    void* cur = mc ? *(void**)((char*)mc + 0xd8) : NULL;  // MenuController.CurrentScreen
    for (int i = 0; i < NSCREEN; i++)
        if (g_os[i].page && cur && R(g_os[i].screen) == cur && rf_alive(cur)) {
            page(i, false);
            return true;
        }
    return false;
}

static void patch(void* screen) {
    char* s = (char*)screen;
    int slot = screen_slot(screen);
    if (slot < 0) return;
    page(slot, false);  // every visit starts on the options

    set_active(*(void**)(s + 0x68), false);  // OptionsScreen.PostureChanger
    set_active(*(void**)(s + 0x88), false);  // OptionsScreen.CalibrateStandingButton
    for (int k = 0; k < 2; k++) {            // OptionsScreen.HintsOnKey / HintsOffKey: ON / OFF in the game's words
        Il2CppString* key = *(Il2CppString**)(s + 0x58 + 8 * k);
        if (key && key->length > 0) rf_str_utf8(key, k ? g_off_key : g_on_key, sizeof g_on_key);
    }
    // "view controls" (the gesture card): pause-menu and front-end layouts name the button's parent differently
    static const char* const CONTROLS[] = {"Layout [Option Selectors]/VERT Layout [Option Buttons]/Button [Controls]",
                                           "Layout [Profile Option Buttons]/Button [Controls]"};
    void* controls = NULL;
    for (size_t i = 0; i < sizeof CONTROLS / sizeof CONTROLS[0] && !controls; i++) {
        controls = find(rf_transform(screen), CONTROLS[i]);
        if (controls) set_active(rf_gameobject(controls), true);
    }

    void* fade = *(void**)(s + 0x50);  // OptionsScreen.TurnFadeSelector
    if (!rf_alive(fade)) return;
    g_sel_type = rf_type_of(fade);
    g_btn_type = rf_type_of(*(void**)((char*)fade + 0x18));  // UiOptionSelector.LeftButton: a MenuButton
    g_txt_type = rf_type_of(*(void**)((char*)fade + 0x28));  // CurrentSelectionText: a LocaliseText
    void* row = rf_transform(fade);
    char nm[64];
    rf_obj_name(row, nm, sizeof nm);
    if (!strcmp(nm, "Option")) row = rf_tr_parent(row);
    // QA is a pre-game test setting; the pause menu keeps only the player-facing rows.
    bool want_qa = !strcmp(g_scene, "MainMenu");
    static const struct { int row; const char* name; const char* label; } ROWS[NROW] = {
        {ROW_FPS, "PortFpsOption", "PORT_FPS_LIMIT"},      {ROW_FOV, "PortFovOption", "PORT_FOV"},
        {ROW_LOOK_S, "PortLookSensOption", "PORT_LOOK_SENS"},
        {ROW_GAUGE, "PortGaugeOption", "PORT_GAUGE"},
        {ROW_QA, "PortQaOption", "PORT_QA"},               {ROW_GPOS, "PortGyroPosOption", "PORT_GYRO_POS"},
        {ROW_GPOS_S, "PortGyroPosSensOption", "PORT_SENSITIVITY"}, {ROW_GROT, "PortGyroRotOption", "PORT_GYRO_ROT"},
        {ROW_GROT_S, "PortGyroRotSensOption", "PORT_SENSITIVITY"},
    };
    void* parent = rf_tr_parent(row);
    int idx = CALLP(C_TR_GETSIBLING, int32_t (*)(void*, const void*))(row, NULL);
    if (!g_sel_type || !g_txt_type || !rf_alive(parent)) return;
    // consecutive places right after the turn-fade row (in the pause menu the buttons' layout follows the rows in the
    // same parent, so every row has to stay above it)
    int next = idx + 1;
    for (int r = ROW_LOOK_S; r < NROW; r++) {
        if (r == ROW_QA && !want_qa) continue;
        if (!rf_alive(R(g_os[slot].sel[r]))) {
            rf_ref_set(&g_os[slot].sel[r], clone_row(row, parent, next, ROWS[r].name, ROWS[r].label));
            if (r >= ROW_GPOS) set_active(row_go(R(g_os[slot].sel[r])), false);  // shown on the gyroscope page only
        }
        next++;
    }
    set_active(rf_gameobject(row), false);
    if (!g_os[slot].roomy) {
        int extra = rf_alive(R(g_os[slot].sel[ROW_LOOK_S])) + rf_alive(R(g_os[slot].sel[ROW_GAUGE])) - 2;
        if (want_qa)  // the front end
            make_room(screen, extra + rf_alive(R(g_os[slot].sel[ROW_QA])));
        else if (extra > 0)
            make_room_pause(screen, extra);
        g_os[slot].roomy = true;
    }
    // GYROSCOPE after "view controls", the page's BACK last in the same button layout
    void* ctl_btn = controls ? comp_in(rf_gameobject(controls), g_btn_type) : NULL;
    if (rf_alive(ctl_btn) && !rf_alive(R(g_os[slot].gyro_btn))) {
        int ci = CALLP(C_TR_GETSIBLING, int32_t (*)(void*, const void*))(controls, NULL);
        rf_ref_set(&g_os[slot].gyro_btn, clone_button(ctl_btn, ci + 1, "Button [Gyro]", "PORT_GYRO"));
        rf_ref_set(&g_os[slot].gyro_back,
                   clone_button(ctl_btn, child_count(rf_tr_parent(controls)), "Button [Gyro Back]", "FE_BUTTON_BACK"));
        void* gk = R(g_os[slot].gyro_back);
        if (rf_alive(gk)) set_active(rf_gameobject(gk), false);
    }
    for (int r = 0; r < NROW; r++) show_value(R(g_os[slot].sel[r]), r);
}

// The authored image page already has room for seven rows; the port adds FPS and FOV beside its live detail/MSAA rows.
void rf_options_graphics(void* screen) {
    int slot = screen_slot(screen);
    void* sel = *(void**)((char*)screen + 0x60);  // GraphicsScreen.TextureQualitySelector (unused in this build)
    if (slot < 0 || !rf_alive(sel)) return;
    g_sel_type = rf_type_of(sel);
    g_txt_type = rf_type_of(*(void**)((char*)sel + 0x28));
    void* row = rf_transform(sel);
    char nm[64];
    rf_obj_name(row, nm, sizeof nm);
    if (!strcmp(nm, "Option")) row = rf_tr_parent(row);
    void* parent = rf_tr_parent(row);
    if (!g_sel_type || !g_txt_type || !rf_alive(parent)) return;
    int idx = CALLP(C_TR_GETSIBLING, int32_t (*)(void*, const void*))(row, NULL);
    static const char* const NAME[] = {"PortFpsOption", "PortFovOption"};
    static const char* const KEY[] = {"PORT_FPS_LIMIT", "PORT_FOV"};
    for (int r = ROW_FPS; r <= ROW_FOV; r++) {
        if (!rf_alive(R(g_os[slot].sel[r])))
            rf_ref_set(&g_os[slot].sel[r], clone_row(row, parent, idx + 1 + r, NAME[r], KEY[r]));
        show_value(R(g_os[slot].sel[r]), r);
    }
}

void* h_options_setup(void* self, const void* mi) {
    void* r = ORIG(H_OPTIONS_SETUP, void* (*)(void*, const void*))(self, mi);
    patch(self);
    return r;
}

static void step(int row, int d) {
    switch (row) {
    case ROW_FPS: g_fps_idx = (g_fps_idx + d + RF_FPS_COUNT) % RF_FPS_COUNT; break;
    case ROW_FOV: g_fov_idx = (g_fov_idx + d + RF_FOV_COUNT) % RF_FOV_COUNT; break;
    case ROW_LOOK_S: g_look_sens = (g_look_sens + d + RF_SENS_COUNT) % RF_SENS_COUNT; break;
    case ROW_QA:
        g_qa = (g_qa + d + QA_COUNT) % QA_COUNT;
        rf_log("options: QA %d (level select %s, skip level %s)", g_qa, g_qa & QA_CHAPTERS ? "all" : "reached",
               g_qa & QA_SKIP ? "shown" : "secret");
        break;
    case ROW_GAUGE: g_gauge_on = !g_gauge_on; break;
    case ROW_GPOS: g_gyro_pos = !g_gyro_pos; break;
    case ROW_GROT: g_gyro_rot = !g_gyro_rot; break;
    case ROW_GPOS_S: g_gyro_pos_sens = (g_gyro_pos_sens + d + RF_SENS_COUNT) % RF_SENS_COUNT; break;
    default: g_gyro_rot_sens = (g_gyro_rot_sens + d + RF_SENS_COUNT) % RF_SENS_COUNT; break;
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
