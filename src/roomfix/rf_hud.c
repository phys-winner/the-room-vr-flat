// Touch HUD support: the Java overlay (port.roomfix.HudView) draws the buttons and polls g_hud_state from the
// UI thread; button presses come back through the touch queue and run here on the Unity main thread.
// Button visibility follows the game's own input gates (Player.TeleportInput / InventoryInput / GogglesInput),
// so a button is only shown when the VR player could use the same control at that moment.
#include "rf.h"

volatile int g_hud_state;

#define LEFT 0
#define RIGHT 1

typedef bool (*fn_b1)(void*, const void*);

static void* static_field0(uintptr_t meta) {
    uintptr_t k = *(uintptr_t*)(g_il2cpp_base + meta);
    if (!k || (k & 1) || k < 0x10000) return NULL;  // metadata slot not initialised yet
    void* statics = *(void**)(k + 0xb8);
    return statics ? *(void**)statics : NULL;
}

// Player.InputControl.IsAllowed for the control stored at Player+off
static bool input_allowed(int off) {
    void* ic = *(void**)((char*)g_player + off);
    return ic && CALLP(C_INPUTCTL_ALLOWED, fn_b1)(ic, NULL);
}

static bool gameplay(void) { return !rf_menu_context() && rf_alive(g_player) && g_frame - g_player_frame <= 2; }

bool rf_inventory_open(void) {
    void* hm = static_field0(META_HUDMGR_TYPEINFO);  // HudManager.Instance
    return rf_alive(hm) && *((char*)hm + 0x88);      // HudManager.Showing
}

// The eyepiece control exists once the story has given it: GogglesHudManager.Instance with its HUD enabled
// (GogglesHudManager.EnableGogglesHud sets Enabled +0x7C).
static bool goggles_owned(void) {
    void* gh = static_field0(META_GOGGLESHUD_TYPEINFO);
    return rf_alive(gh) && *((char*)gh + 0x7c);
}

bool rf_goggles_on(void) {
    void* g = static_field0(META_GOGGLES_TYPEINFO);  // Goggles.Instance
    return rf_alive(g) && *((char*)g + 0xf8);        // Goggles.GogglesActive
}

void rf_hud_tick(void) {
    int s = 0;
    if (rf_menu_context()) s |= HUD_MENU;
    if (gameplay()) {
        s |= HUD_VISIBLE;
        if (input_allowed(0xf8) || rf_act_teleport_mode_on()) s |= HUD_TELEPORT;     // TeleportInput
        if (rf_act_teleport_mode_on()) s |= HUD_TELEPORT_ON;
        if (input_allowed(0x100)) s |= HUD_INVENTORY;                                  // InventoryInput
        if (rf_inventory_open()) s |= HUD_INV_OPEN;
        if (input_allowed(0x108) && goggles_owned()) s |= HUD_GOGGLES;                 // GogglesInput
        if (rf_goggles_on()) s |= HUD_GOGGLES_ON;
        // snap turn is covered by swipe-look; its buttons appear while the tutorial asks for it
        if (g_tut_action == 6 && input_allowed(0x110)) s |= HUD_SNAP;                // SnapTurnInput
    }
    if (s != g_hud_state) {
        if (g_dev) rf_log("hud: state %#x -> %#x", g_hud_state, s);
        g_hud_state = s;
    }
}

void rf_hud_button(int id) {
    rf_log("hud: button %d (state %#x)", id, g_hud_state);
    switch (id) {
    case HB_TELEPORT:
        if (gameplay()) rf_act_teleport_mode(!rf_act_teleport_mode_on());
        break;
    case HB_INVENTORY:  // A on the right controller (RwAction 21) -> HudManager.ToggleOnOff
        if (gameplay()) {
            if (rf_act_teleport_mode_on()) rf_act_teleport_mode(false);
            rf_act_button(RIGHT, 0, 6);
        }
        break;
    case HB_GOGGLES:
        // the eyepiece button path of Player.UpdateGogglesInput (RwAction 28, gated by GogglesInput):
        // GogglesHudManager.ReleaseGrab, then Activate/DeactivateWithFade by Goggles.GogglesActive
        if (gameplay() && input_allowed(0x108)) {
            void* gh = static_field0(META_GOGGLESHUD_TYPEINFO);
            if (rf_alive(gh)) {
                CALLP(C_GOGGLESHUD_RELEASE, void (*)(void*, const void*))(gh, NULL);
                if (rf_goggles_on()) CALLP(C_GOGGLESHUD_OFF, void (*)(void*, const void*))(gh, NULL);
                else CALLP(C_GOGGLESHUD_ON, void (*)(void*, const void*))(gh, NULL);
            }
        }
        break;
    case HB_BACK:
        // menus: previous screen; pause menu root: resume; gameplay: pause
        if (rf_menu_context() && rf_menu_back()) break;
        if (rf_menu_context() && g_frame - g_pause_frame > 2) break;  // front-end root: nothing to go back to
        // fall through
    case HB_PAUSE:  // left menu button: RwAction 22 (GameStateManager.Pause), RwAction 27 in the pause menu (UnPause)
        if (gameplay() || g_frame - g_pause_frame <= 2) {
            if (rf_act_teleport_mode_on()) rf_act_teleport_mode(false);
            rf_act_button(LEFT, 2, 6);
        }
        break;
    case HB_TURN_L:
        if (gameplay()) rf_act_snap_turn(-1);
        break;
    case HB_TURN_R:
        if (gameplay()) rf_act_snap_turn(1);
        break;
    default:
        break;
    }
}
