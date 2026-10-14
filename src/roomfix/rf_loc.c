// Port texts through the game's own localisation: Localisation.GetString(key, context) is the single lookup
// every LocaliseText / hint / message box uses. Keys from port_strings.json (touch wording for VR-device
// instructions, labels of the port's Options rows) are answered in the current game language; "PORT_V_*" keys
// are the values shown by the port's Options rows. Everything else goes to the game's dictionary.
#include <stdio.h>
#include <string.h>

#include "gen_port_strings.h"
#include "rf.h"

typedef void* (*fn_str0)(const void*);

int rf_lang(void) {
    char name[40];
    rf_str_utf8((Il2CppString*)CALLP(C_LOC_GETLANG, fn_str0)(NULL), name, sizeof name);
    for (int i = 0; i < RF_NLANG; i++)
        if (!strcmp(name, RF_LANG_NAMES[i])) return i;
    return 0;  // English
}

static const char* table(const char* key, int lang) {
    for (size_t i = 0; i < sizeof RF_PORT_STRINGS / sizeof RF_PORT_STRINGS[0]; i++)
        if (!strcmp(RF_PORT_STRINGS[i].key, key)) return RF_PORT_STRINGS[i].text[lang];
    return NULL;
}

void* h_loc_getstring(void* key, void* ctx, const void* mi) {
    Il2CppString* k = (Il2CppString*)key;
    if (k && k->length > 4 && k->length < 48 && g_il2.string_new) {
        char ks[48];
        rf_str_utf8(k, ks, sizeof ks);
        int lang = -1;
        const char* t = NULL;
        char buf[96];
        if (!strncmp(ks, "PORT_V_", 7)) {
            lang = rf_lang();
            if (!strcmp(ks + 7, "DISPLAY")) {
                snprintf(buf, sizeof buf, "%s (%d Hz)", table("PORT_DISPLAY", lang), rf_fps_value(RF_FPS_DISPLAY));
                t = buf;
            } else {
                t = ks + 7;  // plain number
            }
        } else if (strchr(RF_PORT_KEY_FIRST, ks[0])) {  // cheap filter for hot lookups
            lang = rf_lang();
            t = table(ks, lang);
        }
        if (t) return g_il2.string_new(t);
    }
    return ORIG(H_LOC_GETSTRING, void* (*)(void*, void*, const void*))(key, ctx, mi);
}
