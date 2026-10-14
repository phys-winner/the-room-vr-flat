// Stage-8 gesture card (operator choice C3, 2026-10-14). The card itself is a Java overlay
// (port/roomfix/HelpCard.java): one page of the flat touch gestures in the game's current language. libroomfix
// asks for it
//  - once, the first time the main menu opens (stored as help=1 in port_settings.cfg), and
//  - from Options -> "view controls": the authored button that opened the VR controller map (FE_BUTTON_VIEW_CONTROLS,
//    translated in every dictionary) opens the card instead, in the front end and in the pause menu.
// The Java side polls RoomFix.nativeHelpRequest every 100 ms; a request is the language index + 1.
#include <string.h>

#include "gen_port_strings.h"
#include "rf.h"

int g_help_seen;
static int g_help_req;

static void request(const char* why) {
    int lang = rf_lang();
    __atomic_store_n(&g_help_req, lang + 1, __ATOMIC_RELEASE);
    rf_log("help: gesture card (%s, %s)", why, RF_LANG_NAMES[lang]);
}

int rf_help_take(void) { return __atomic_exchange_n(&g_help_req, 0, __ATOMIC_ACQ_REL); }

void rf_help_first_menu(void) {
    if (g_help_seen) return;
    g_help_seen = 1;
    rf_settings_save();
    request("first start");
}

bool rf_help_press(void* btn) {
    char nm[48];
    if (!rf_alive(btn)) return false;
    rf_obj_name(btn, nm, sizeof nm);
    if (strcmp(nm, "Button [Controls]")) return false;
    request("options");
    return true;
}
