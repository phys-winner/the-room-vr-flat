// Startup progress comes from the game's completed shader/material variants; other stages are indeterminate.
#include <assert.h>

#include "rf.h"

enum { LOAD_DONE, LOAD_ENGINE, LOAD_SHADERS, LOAD_MENU, LOAD_UNKNOWN = 255 };
static bool g_loading = true;
static int g_loading_stage = LOAD_ENGINE;
static int g_loading_snapshot = LOAD_ENGINE | (LOAD_UNKNOWN << 8) | (LOAD_UNKNOWN << 16);
static rf_ref g_shader_manager;

static int shader_percent(float p) {
    if (!isfinite(p) || p < 0) return LOAD_UNKNOWN;
    return p >= 1 ? 100 : (int)(p * 100);
}

void h_loading_shaders(void* self, const void* mi) {
    if (g_loading) {
        rf_ref_set(&g_shader_manager, self);
        g_loading_stage = LOAD_SHADERS;
    }
    ORIG(H_LOADING_SHADERS, void (*)(void*, const void*))(self, mi);
}

int rf_loading_state(void) { return __atomic_load_n(&g_loading_snapshot, __ATOMIC_ACQUIRE); }

void rf_loading_tick(void) {
    if (!g_loading) return;
#if !RF_RELEASE
    static bool checked;
    if (!checked) {
        checked = true;
        assert(shader_percent(NAN) == LOAD_UNKNOWN && shader_percent(INFINITY) == LOAD_UNKNOWN &&
               shader_percent(-1) == LOAD_UNKNOWN);
        assert(shader_percent(0) == 0 && shader_percent(0.5f) == 50 && shader_percent(1) == 100);
    }
#endif
    int p = LOAD_UNKNOWN, lang = LOAD_UNKNOWN;
    uintptr_t k = *(uintptr_t*)(g_il2cpp_base + META_LOCALISATION_TYPEINFO);
    char* loc = k && !(k & 1) && k > 0x10000 ? *(char**)(k + 0xb8) : NULL;
    if (loc && loc[0x38] && g_call_ok[C_LOC_GETLANG]) lang = rf_lang();  // Localisation.IsInitialised
    void* manager = rf_ref_get(&g_shader_manager);
    if (g_loading_stage == LOAD_SHADERS && rf_alive(manager) && g_call_ok[C_SHADER_PROGRESS]) {
        p = shader_percent(CALLP(C_SHADER_PROGRESS, float (*)(void*, const void*))(manager, NULL));
        if (p == 100) {
            g_loading_stage = LOAD_MENU;
            p = LOAD_UNKNOWN;
        }
    }
    void* mc = rf_menu_controller();
    if (!strcmp(g_scene, "MainMenu") && rf_alive(mc) && rf_class_is(mc, "FrontendMenuController") &&
        *((char*)mc + 0x108) && !*((char*)mc + 0x120)) {  // InputEnabled, ScreenTransitionInProgress
        void* screen = *(void**)((char*)mc + 0xd8);
        void* cg = rf_alive(screen) ? *(void**)((char*)screen + 0x18) : NULL;
        if (rf_alive(screen) && (!rf_alive(cg) || (g_call_ok[C_CG_ALPHA] &&
                CALLP(C_CG_ALPHA, float (*)(void*, const void*))(cg, NULL) >= 0.99f))) {
            g_loading = false;
            g_loading_stage = LOAD_DONE;
            rf_ref_clear(&g_shader_manager);
        }
    }
    int state = g_loading_stage | (p << 8) | (lang << 16);
    int prev = rf_loading_state();
    if ((prev & 255) != g_loading_stage || (p <= 100 && p / 10 != ((prev >> 8) & 255) / 10))
        rf_log("loading: stage=%d shader=%d lang=%d", g_loading_stage, p <= 100 ? p : -1, lang);
    __atomic_store_n(&g_loading_snapshot, state, __ATOMIC_RELEASE);
}
