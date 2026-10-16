// Stage-8 additions chosen by the operator (2026-10-14):
//  C1 chapter select — the main menu gets the game's own "level select" entry (FE_BUTTON_LEVEL_SELECT, translated
//     in every dictionary; the authored LevelSelectScreen never had a menu instance). It lists the chapters the
//     active profile has reached (all of them with the QA unlock) under their authored names and replays one from
//     its start through the game's level-select path, GameStateManager.LoadLevel, with the level's saved state
//     not loaded (GameStateManager.DisableLevelSaveLoading) and saving off (SaveManager.SetSavingEnabled): the
//     profile's progress is untouched, and the end of a replayed chapter (LevelComplete / SkipLevel) returns to
//     the main menu instead of moving the profile on.
//  QA (testing, operator's request) — Options row "QA": off | level select | skip level | all. "level select"
//     lists every chapter in the menu above; "skip level" shows the pause menu's authored SKIP LEVEL button
//     without the five-tap secret (PauseMenuScreen.SkipChapterButtonActivated); "off" turns everything off.
//     Stored in port_settings.cfg (qa=), an unknown value reads as off.
#include <stdio.h>
#include <string.h>

#include "rf.h"

typedef void (*fn_setb)(void*, bool, const void*);

int g_qa;  // QA_* bits
static bool g_replay;

#define NLEVEL 8
static struct {
    rf_ref screen;        // MainMenuScreen the clones belong to (weak: the menu scene is rebuilt after every level)
    rf_ref chapters;      // MenuButton "level select"
    rf_ref level[NLEVEL];  // MenuButton per chapter
    rf_ref level_info[NLEVEL];
    rf_ref back;          // MenuButton back (leaves the chapter list)
    int nlevel;
    bool listing;
} g_mm;
#define R(x) rf_ref_get(&(x))

static void mm_clear(void) {
    rf_ref_clear(&g_mm.screen);
    rf_ref_clear(&g_mm.chapters);
    rf_ref_clear(&g_mm.back);
    for (int i = 0; i < NLEVEL; i++) {
        rf_ref_clear(&g_mm.level[i]);
        rf_ref_clear(&g_mm.level_info[i]);
    }
    g_mm.nlevel = 0;
    g_mm.listing = false;
}

static void* str(const char* s) { return g_il2.string_new ? g_il2.string_new(s) : NULL; }

static void set_active(void* comp_or_go, bool on) {
    if (!rf_alive(comp_or_go)) return;
    void* go = rf_class_is(comp_or_go, "GameObject") ? comp_or_go : rf_gameobject(comp_or_go);
    if (rf_alive(go)) CALLP(C_GO_SETACTIVE, fn_setb)(go, on, NULL);
}

static void* child(void* t, const char* name) {
    void* s = str(name);
    return s && rf_alive(t) ? CALLP(C_TR_FIND, void* (*)(void*, void*, const void*))(t, s, NULL) : NULL;
}

// a copy of an authored menu button (same look, sounds and pointer behaviour) under the same layout group
static void* clone_button(void* src_btn, int sibling, const char* name, const char* key) {
    void* st = rf_transform(src_btn);
    void* ct = CALLP(C_OBJ_INSTANTIATE, void* (*)(void*, void*, bool, const void*))(st, rf_tr_parent(st), false, NULL);
    if (!rf_alive(ct)) return NULL;
    void* go = rf_gameobject(ct);
    CALLP(C_OBJ_SETNAME, void (*)(void*, void*, const void*))(go, str(name), NULL);
    CALLP(C_TR_SETSIBLING, void (*)(void*, int32_t, const void*))(ct, sibling, NULL);
    void* btn = CALLP(C_GO_GETCHILDCOMP, void* (*)(void*, void*, bool, const void*))(go, rf_type_of(src_btn), true, NULL);
    if (rf_alive(btn)) CALLP(C_MENUBTN_SETTEXT, void (*)(void*, void*, const void*))(btn, str(key), NULL);
    set_active(go, true);
    return rf_alive(btn) ? btn : NULL;
}

static char* slot(void) {
    char* sm = rf_live_instance("SlotManager");
    return rf_alive(sm) ? *(char**)(sm + 0x40) : NULL;  // SlotManager.ActiveSlot
}

// chapter i is offered: QA unlock, a finished game, or not beyond the profile's current chapter
static bool reachable(void* levels, int i) {
    if (g_qa & QA_CHAPTERS) return true;
    char* s = slot();
    if (!s) return false;
    if (s[0x2d]) return true;  // PlayerSlot.GameComplete
    char cur[64];
    rf_str_utf8(*(const Il2CppString**)(s + 0x18), cur, sizeof cur);  // PlayerSlot.CurrentSceneName
    if (!cur[0]) return false;
    for (int k = 0; k < rf_list_size(levels); k++) {
        char sc[64];
        rf_str_utf8(*(const Il2CppString**)((char*)rf_list_get(levels, k) + 0x18), sc, sizeof sc);  // SceneName
        if (!strcmp(sc, cur)) return i <= k;
    }
    return false;
}

static const char* level_key(void* li) {
    static char key[64];
    rf_str_utf8(*(const Il2CppString**)((char*)li + 0x20), key, sizeof key);  // LevelInfo.NameKey
    // the dictionaries have no name for the last two chapters: the port's strings stand in
    if (!strcmp(key, "LEVEL_NAME_DESERT")) return "PORT_LEVEL_DESERT";
    if (!strcmp(key, "LEVEL_NAME_OFFICE_ENDGAME")) return "PORT_LEVEL_EPILOGUE";
    return key;
}

static void show_list(bool on) {
    void* ch = R(g_mm.chapters);
    void* parent = rf_alive(ch) ? rf_tr_parent(rf_transform(ch)) : NULL;
    if (!parent) return;
    static const char* const MAIN[] = {"MenuButton Continue", "MenuButton Profile Settings", "MenuButton Options"};
    for (size_t i = 0; i < sizeof MAIN / sizeof MAIN[0]; i++) set_active(child(parent, MAIN[i]), !on);
    set_active(ch, !on);
    char* gsm = rf_live_instance("GameStateManager");
    void* levels = gsm ? *(void**)(gsm + 0x40) : NULL;  // GameStateManager.Levels
    for (int i = 0; i < g_mm.nlevel; i++) set_active(R(g_mm.level[i]), on && reachable(levels, i));
    set_active(R(g_mm.back), on);
    g_mm.listing = on;
}

static void open_list(void) {
    char* gsm = rf_live_instance("GameStateManager");
    void* levels = gsm ? *(void**)(gsm + 0x40) : NULL;
    void* ch = R(g_mm.chapters);
    if (!rf_alive(R(g_mm.back)) && levels && rf_alive(ch)) {
        int base = CALLP(C_TR_GETSIBLING, int32_t (*)(void*, const void*))(rf_transform(ch), NULL);
        g_mm.nlevel = 0;
        for (int i = 0; i < rf_list_size(levels) && i < NLEVEL; i++) {
            void* li = rf_list_get(levels, i);
            char nm[32];
            snprintf(nm, sizeof nm, "MenuButton Chapter %d", i);
            rf_ref_set(&g_mm.level[i], clone_button(ch, base + 1 + i, nm, level_key(li)));
            rf_ref_set(&g_mm.level_info[i], li);
            g_mm.nlevel = i + 1;
        }
        rf_ref_set(&g_mm.back, clone_button(ch, base + 1 + g_mm.nlevel, "MenuButton Chapters Back", "FE_BUTTON_BACK"));
    }
    show_list(true);
    int n = 0;
    for (int i = 0; i < g_mm.nlevel; i++) n += reachable(levels, i);
    rf_log("chapters: list opened, %d of %d chapters offered (qa %d)", n, g_mm.nlevel, g_qa);
}

static void start_replay(void* level) {
    char* gsm = rf_live_instance("GameStateManager");
    void* save = rf_live_instance("SaveManager");
    if (!gsm || !save) return;
    char sc[64];
    rf_str_utf8(*(const Il2CppString**)((char*)level + 0x18), sc, sizeof sc);
    g_replay = true;
    gsm[0x88] = 1;  // GameStateManager.DisableLevelSaveLoading: start the chapter afresh
    CALLP(C_SAVE_SETENABLED, void (*)(void*, bool, const void*))(save, false, NULL);
    rf_log("chapters: replay '%s' from its start (level save not loaded, saving off, profile progress kept)", sc);
    CALLP(C_GSM_LOADLEVEL, void (*)(void*, void*, const void*))(gsm, level, NULL);
}

static void end_replay(void) {
    char* gsm = rf_live_instance("GameStateManager");
    void* save = rf_live_instance("SaveManager");
    if (gsm) gsm[0x88] = 0;
    if (save) CALLP(C_SAVE_SETENABLED, void (*)(void*, bool, const void*))(save, true, NULL);
    g_replay = false;
    rf_log("chapters: replay over, saving on");
}

// back in the front end (quit, skip or the end of the replayed chapter): saving and level saves on again
void rf_chapters_scene(const char* scene) {
    if (g_replay && !strcmp(scene, "MainMenu")) end_replay();
}

void* h_mainmenu_setup(void* self, const void* mi) {
    void* r = ORIG(H_MAINMENU_SETUP, void* (*)(void*, const void*))(self, mi);
    if (g_replay) end_replay();
    void* parent_go = *(void**)((char*)self + 0x80);  // MainMenuScreen.MenuButtonParent (a GameObject)
    void* parent = rf_alive(parent_go) ? CALLP(C_GO_TRANSFORM, void* (*)(void*, const void*))(parent_go, NULL) : NULL;
    void* options = parent ? child(parent, "MenuButton Options") : NULL;
    void* opt_btn = options ? CALLP(C_GO_GETCHILDCOMP, void* (*)(void*, void*, bool, const void*))(
                                  rf_gameobject(options), rf_type_of(*(void**)((char*)self + 0x40)), true, NULL)
                            : NULL;  // same class as ContinueGameButton (MenuButton)
    if (!rf_alive(opt_btn)) return r;
    if (R(g_mm.screen) != self || !rf_alive(R(g_mm.chapters))) {
        mm_clear();
        rf_ref_set(&g_mm.screen, self);
        int idx = CALLP(C_TR_GETSIBLING, int32_t (*)(void*, const void*))(options, NULL);
        rf_ref_set(&g_mm.chapters, clone_button(opt_btn, idx + 1, "MenuButton Level Select", "FE_BUTTON_LEVEL_SELECT"));
        rf_log("chapters: main menu entry %s", R(g_mm.chapters) ? "added" : "FAILED");
    }
    show_list(false);
    // offered once the profile has a game in progress (or with the QA unlock)
    char* s = slot();
    char cur[64] = "";
    if (s) rf_str_utf8(*(const Il2CppString**)(s + 0x18), cur, sizeof cur);
    set_active(R(g_mm.chapters), (g_qa & QA_CHAPTERS) || (s && (s[0x2d] || cur[0])));
    rf_help_first_menu();
    return r;
}

// presses on the port's menu buttons (clones keep the authored button's click listener, so it is not called) and
// on Options "view controls" (rf_help.c)
void h_button_press(void* self, const void* mi) {
    if (self && (self == R(g_mm.chapters) || self == R(g_mm.back))) {
        if (self == R(g_mm.chapters)) open_list();
        else show_list(false);
        return;
    }
    for (int i = 0; i < g_mm.nlevel; i++)
        if (self && self == R(g_mm.level[i]) && R(g_mm.level_info[i])) {
            start_replay(R(g_mm.level_info[i]));
            return;
        }
    if (rf_options_press(self)) return;  // Options: gyroscope page and its BACK (round 2)
    if (rf_help_press(self)) return;
    ORIG(H_BUTTON_PRESS, void (*)(void*, const void*))(self, mi);
}

void h_gsm_level_complete(void* self, const void* mi) {
    if (g_replay) {
        rf_log("chapters: replayed chapter finished -> main menu");
        CALLP(C_GSM_RETURNTOMENU, void (*)(void*, bool, const void*))(self, false, NULL);
        return;
    }
    ORIG(H_GSM_LEVELCOMPLETE, void (*)(void*, const void*))(self, mi);
}

void h_gsm_skip_level(void* self, const void* mi) {
    if (g_replay) {
        rf_log("chapters: skip during a replay -> main menu");
        CALLP(C_GSM_RETURNTOMENU, void (*)(void*, bool, const void*))(self, false, NULL);
        return;
    }
    ORIG(H_GSM_SKIPLEVEL, void (*)(void*, const void*))(self, mi);
}

// safety net: no story-mode load (it moves the profile on) while a chapter is replayed
void h_gsm_story_load(void* self, void* level, const void* mi) {
    if (g_replay) {
        rf_log("chapters: story-mode load during a replay -> main menu");
        CALLP(C_GSM_RETURNTOMENU, void (*)(void*, bool, const void*))(self, false, NULL);
        return;
    }
    ORIG(H_GSM_STORYLOAD, void (*)(void*, void*, const void*))(self, level, mi);
}

// a replay starts in the chapter itself: the plain loading scene instead of the between-chapter hub that the
// game shows when the profile has not seen it yet (LevelInfo.FirstTimeLoadingScreen)
void* h_loadscene_name(void* level, const void* mi) {
    if (g_replay) return str("Loading");
    return ORIG(H_LOADSCENE_NAME, void* (*)(void*, const void*))(level, mi);
}

void* h_pause_setup(void* self, const void* mi) {
    if (g_qa & QA_SKIP) *((char*)self + 0x70) = 1;  // PauseMenuScreen.SkipChapterButtonActivated
    return ORIG(H_PAUSE_SETUP, void* (*)(void*, const void*))(self, mi);
}
