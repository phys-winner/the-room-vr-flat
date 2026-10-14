// Front-end and pause menus on a flat screen.
//
// Menu screens are world-space panels placed around the VR player's body; in flat mode the camera turns to
// each new screen (MenuController.CurrentScreen) so it is in view, like a VR player turning to the menu.
//
// VR setup wizards in the front end are answered with the flat defaults the way a player would answer them.
//
// PostureSelectScreen ("Select Posture: Sitting / Standing") appears before every level load. Flat play is
// seated (camera at the authored seated eye height), so once the screen has faded in the port presses its
// own Sitting button handler (PostureSelectScreen.SittingPressed: SlotManager sitting mode + continue).
#include "rf.h"

typedef float (*fn_f_1)(void*, const void*);

static struct {
    void* screen;
    int frame;
} g_posture;

void* h_posture_setup(void* self, const void* mi) {
    void* r = ORIG(H_POSTURE_SETUP, void* (*)(void*, const void*))(self, mi);
    g_posture.screen = self;
    g_posture.frame = g_frame;
    rf_log("menu: posture wizard shown");
    return r;
}

static struct {
    void* ctrl;     // MenuController updated this frame (FrontendMenuController / PauseMenuController)
    int frame;
    void* screen;   // CurrentScreen we turned to
    int since;      // frames since the screen appeared
} g_menu;

void h_menu_update(void* self, const void* mi) {
    ORIG(H_MENU_UPDATE, void (*)(void*, const void*))(self, mi);
    if (self != g_menu.ctrl || g_frame - g_menu.frame > 5) g_menu.screen = NULL;  // (re)activated
    g_menu.ctrl = self;
    g_menu.frame = g_frame;
}

void* rf_menu_controller(void) {
    return g_menu.ctrl && g_frame - g_menu.frame <= 2 && rf_alive(g_menu.ctrl) ? g_menu.ctrl : NULL;
}

static void tick_face_menu(void) {
    if (!g_menu.ctrl || g_frame - g_menu.frame > 2 || !rf_alive(g_menu.ctrl)) return;
    void* scr = *(void**)((char*)g_menu.ctrl + 0xd8);  // MenuController.CurrentScreen
    if (!rf_alive(scr)) return;
    if (scr != g_menu.screen) {
        g_menu.screen = scr;
        g_menu.since = 0;
    }
    g_menu.since++;
    // turn right away and once more after the screen's transition / position reset has settled
    if (g_menu.since == 2 || g_menu.since == 45) {
        v3 p = rf_tr_pos(rf_transform(scr));
        rf_face_point(p);
        if (g_menu.since == 2) {
            char nm[64];
            rf_obj_name(scr, nm, sizeof nm);
            rf_log("menu: screen '%s' (%s) -> camera yaw %.1f pitch %.1f", nm, rf_class_name(scr), g_head.yaw, g_head.pitch);
        }
    }
}

void rf_menu_tick(void) {
    tick_face_menu();
    if (!g_posture.screen) return;
    void* s = g_posture.screen;
    if (!rf_alive(s) || g_frame - g_posture.frame > 1800) {
        g_posture.screen = NULL;
        return;
    }
    void* cg = *(void**)((char*)s + 0x18);  // MenuScreen.CanvasGroup
    float a = rf_alive(cg) ? CALLP(C_CG_ALPHA, fn_f_1)(cg, NULL) : 1.0f;
    if (g_frame - g_posture.frame < 20 || a < 0.99f) return;  // still fading in: buttons not live yet
    g_posture.screen = NULL;
    rf_log("menu: posture wizard -> Sitting (flat default), alpha %.2f after %d frames", a, g_frame - g_posture.frame);
    CALLP(C_POSTURE_SITTING, void (*)(void*, const void*))(s, NULL);
}
