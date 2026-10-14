// libroomfix.so — flat-screen helper for The Room VR: A Dark Matter (PICO build, Unity 2019.4 IL2CPP).
// Loaded from UnityPlayerActivity.onCreate (port.roomfix.RoomFix.init) before Unity creates its player,
// so every inline hook into libil2cpp.so is in place before managed code runs.
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <link.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "hook.h"
#include "rf.h"

#define TAG "RoomFix"

// ---------------------------------------------------------------- logging
static pthread_mutex_t g_log_mu = PTHREAD_MUTEX_INITIALIZER;
static FILE* g_log_file;
char g_ext_dir[512];
bool g_dev;  // diagnostics enabled by <extDir>/roomfix.dev

void rf_log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    __android_log_write(ANDROID_LOG_INFO, TAG, buf);
    pthread_mutex_lock(&g_log_mu);
    if (g_log_file) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        fprintf(g_log_file, "%5ld.%03ld %s\n", (long)ts.tv_sec, ts.tv_nsec / 1000000, buf);
        fflush(g_log_file);
    }
    pthread_mutex_unlock(&g_log_mu);
}

// ---------------------------------------------------------------- strings
bool rf_str_eq(const Il2CppString* s, const char* a) {
    if (!s) return false;
    int32_t n = (int32_t)strlen(a);
    if (s->length != n) return false;
    for (int32_t i = 0; i < n; i++)
        if (s->chars[i] != (uint16_t)(unsigned char)a[i]) return false;
    return true;
}

void rf_str_utf8(const Il2CppString* s, char* out, size_t n) {
    size_t k = 0;
    if (s && n) {
        for (int32_t i = 0; i < s->length && k + 1 < n; i++) {
            uint16_t c = s->chars[i];
            out[k++] = c < 128 ? (char)c : '?';
        }
    }
    if (n) out[k] = 0;
}

// ---------------------------------------------------------------- libil2cpp location
uintptr_t g_il2cpp_base;

struct find_ctx {
    const char* name;
    uintptr_t base;
    char build_id[41];
};

static int phdr_cb(struct dl_phdr_info* info, size_t size, void* data) {
    (void)size;
    struct find_ctx* c = (struct find_ctx*)data;
    if (!info->dlpi_name || !strstr(info->dlpi_name, c->name)) return 0;
    c->base = (uintptr_t)info->dlpi_addr;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)* ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_NOTE) continue;
        const uint8_t* p = (const uint8_t*)(info->dlpi_addr + ph->p_vaddr);
        const uint8_t* end = p + ph->p_memsz;
        while (p + 12 <= end) {
            uint32_t namesz = *(const uint32_t*)p, descsz = *(const uint32_t*)(p + 4), type = *(const uint32_t*)(p + 8);
            const uint8_t* name = p + 12;
            const uint8_t* desc = name + ((namesz + 3) & ~3u);
            if (type == 3 && namesz == 4 && memcmp(name, "GNU", 4) == 0 && descsz <= 20) {
                for (uint32_t k = 0; k < descsz; k++) sprintf(c->build_id + 2 * k, "%02x", desc[k]);
            }
            p = desc + ((descsz + 3) & ~3u);
        }
    }
    return 1;
}

// ---------------------------------------------------------------- il2cpp api
rf_il2cpp_api_t g_il2;
void* g_vrpm;

bool rf_class_is(void* obj, const char* name) {
    if (!obj || !g_il2.object_get_class || !g_il2.class_get_name) return false;
    for (void* k = g_il2.object_get_class(obj); k; k = g_il2.class_get_parent ? g_il2.class_get_parent(k) : NULL) {
        const char* n = g_il2.class_get_name(k);
        if (n && !strcmp(n, name)) return true;
        if (!g_il2.class_get_parent) break;
    }
    return false;
}

const char* rf_class_name(void* obj) {
    if (!obj || !g_il2.object_get_class || !g_il2.class_get_name) return "?";
    void* k = g_il2.object_get_class(obj);
    return k ? g_il2.class_get_name(k) : "?";
}

static void resolve_api(void* h) {
    g_il2.object_get_class = (void* (*)(void*))dlsym(h, "il2cpp_object_get_class");
    g_il2.class_get_name = (const char* (*)(void*))dlsym(h, "il2cpp_class_get_name");
    g_il2.class_get_namespace = (const char* (*)(void*))dlsym(h, "il2cpp_class_get_namespace");
    g_il2.class_get_parent = (void* (*)(void*))dlsym(h, "il2cpp_class_get_parent");
}

// ---------------------------------------------------------------- core hooks
void* g_orig[H_COUNT];
bool g_hook_ok[H_COUNT];
bool g_call_ok[C_COUNT];

static void h_xr_init_onload(const void* mi) {
    (void)mi;
    rf_log("xr: XRGeneralSettings.AttemptInitializeXRSDKOnLoad skipped - flat mode, no XR loader");
}

static void h_xr_start_splash(const void* mi) {
    (void)mi;
    rf_log("xr: XRGeneralSettings.AttemptStartXRSDKOnBeforeSplashScreen skipped - flat mode");
}

// StorePlatform_Oculus.Initialise would start PICO platform Core.AsyncInitialize + entitlement check
// (the PICO platform loader rejects non-PICO devices with a dialog). Report OnSuccess to the caller.
static void h_store_init(void* self, const void* mi) {
    (void)mi;
    if (self) *(int32_t*)((char*)self + OFF_STORE_RESULT) = 1;  // EntitlementCheckResult.Success
    rf_log("store: entitlement forced to Success, PICO platform not initialised");
}

static bool h_log_allowed(void* self, int32_t type, const void* mi) {
    (void)self; (void)type; (void)mi;
    return true;
}

// VrPlatformManager.Update runs every frame on the main thread before the game reads tracked nodes.
static void h_vrpm_update(void* self, const void* mi) {
    g_vrpm = self;
    rf_frame_tick();
    ORIG(H_VRPM_UPDATE, void (*)(void*, const void*))(self, mi);
}

char g_scene[64];

static void track_scene(void) {
    if (g_frame % 30) return;
    int32_t scene = ((int32_t (*)(const void*))RVA(g_call_desc[C_SCENE_ACTIVE].rva))(NULL);
    Il2CppString* sn = ((Il2CppString * (*)(int32_t*, const void*)) RVA(g_call_desc[C_SCENE_NAME].rva))(&scene, NULL);
    char name[64];
    rf_str_utf8(sn, name, sizeof name);
    if (strcmp(name, g_scene) != 0) {
        snprintf(g_scene, sizeof g_scene, "%s", name);
        rf_log("scene -> '%s' (frame %d)", g_scene, g_frame);
    }
    static bool had_player;
    bool has = rf_alive(g_player) && g_frame - g_player_frame < 30;
    if (has != had_player) {
        had_player = has;
        rf_log("player %s (scene '%s')", has ? "ready" : "gone", g_scene);
    }
}

void rf_frame_tick(void) {
    g_frame++;
    track_scene();
    rf_dev_tick();
    g_hand_prev_pos[0] = g_hand[0].pos;
    g_hand_prev_pos[1] = g_hand[1].pos;
    rf_touch_tick();
    rf_view_tick();
    rf_hand_rest_apply();
    rf_interact_tick();
    rf_menu_tick();
    rf_hud_tick();
}

void* g_vrim;    // VrInputModule (menus' laser pointer input), captured in Process
void* g_player;  // gameplay Player, captured in Update
int g_player_frame, g_vrim_frame, g_frame;

static void h_vrim_process(void* self, const void* mi) {
    g_vrim = self;
    g_vrim_frame = g_frame;
    ORIG(H_VRIM_PROCESS, void (*)(void*, const void*))(self, mi);
}

int g_pause_frame = -100;
static void h_pause_update(void* self, const void* mi) {
    g_pause_frame = g_frame;
    ORIG(H_PAUSE_UPDATE, void (*)(void*, const void*))(self, mi);
}

int g_tut_action = -1;  // RwAction the current tutorial box waits for (-1 none)
static void h_tut_msg(void* self, Il2CppString* title, Il2CppString* text, int32_t action, int32_t attach,
                      int32_t alt, const void* mi) {
    char a[96], b[160];
    rf_str_utf8(title, a, sizeof a);
    rf_str_utf8(text, b, sizeof b);
    g_tut_action = action;
    rf_log("tutorial: '%s' / '%s' action=%d alt=%d attach=%d", a, b, action, alt, attach);
    ORIG(H_TUT_MSG, void (*)(void*, Il2CppString*, Il2CppString*, int32_t, int32_t, int32_t, const void*))(
        self, title, text, action, attach, alt, mi);
}

static void h_msg_box(void* self, Il2CppString* text, int32_t attach, const void* mi) {
    char b[200];
    rf_str_utf8(text, b, sizeof b);
    rf_log("message: '%s' attach=%d", b, attach);
    ORIG(H_MSG_BOX, void (*)(void*, Il2CppString*, int32_t, const void*))(self, text, attach, mi);
}

static void h_gc_awake(void* self, const void* mi) {
    ORIG(H_GC_AWAKE, void (*)(void*, const void*))(self, mi);
    rf_gc_add(self);
}

static void h_gc_destroy(void* self, const void* mi) {
    rf_gc_remove(self);
    ORIG(H_GC_DESTROY, void (*)(void*, const void*))(self, mi);
}

static void h_player_update(void* self, const void* mi) {
    g_player = self;
    g_player_frame = g_frame;
    ORIG(H_PLAYER_UPDATE, void (*)(void*, const void*))(self, mi);
}

static const struct rf_hook_entry g_core_hooks[] = {
    {H_XR_INIT_ONLOAD, (void*)h_xr_init_onload, false, false},
    {H_XR_START_SPLASH, (void*)h_xr_start_splash, false, false},
    {H_STORE_INIT, (void*)h_store_init, false, false},
    {H_VRPM_UPDATE, (void*)h_vrpm_update, true, false},
    {H_VRIM_PROCESS, (void*)h_vrim_process, true, false},
    {H_PLAYER_UPDATE, (void*)h_player_update, true, false},
    {H_PAUSE_UPDATE, (void*)h_pause_update, true, false},
    {H_TUT_MSG, (void*)h_tut_msg, true, false},
    {H_GC_AWAKE, (void*)h_gc_awake, true, false},
    {H_GC_DESTROY, (void*)h_gc_destroy, true, false},
    {H_MSG_BOX, (void*)h_msg_box, true, false},
    {H_POSTURE_SETUP, (void*)h_posture_setup, true, false},
    {H_MENU_UPDATE, (void*)h_menu_update, true, false},
    {H_LOG_ALLOWED, (void*)h_log_allowed, false, true},
};

static int install_table(const struct rf_hook_entry* t, size_t n, int* fail) {
    int ok = 0;
    for (size_t i = 0; i < n; i++) {
        const struct rf_hook_entry* h = &t[i];
        if (h->dev_only && !g_dev) continue;
        const struct rf_hook_desc* d = &g_hook_desc[h->id];
        char err[160] = {0};
        void* r = rf_hook_install(RVA(d->rva), h->repl, d->pre, h->want_orig, err, sizeof err);
        if (r) {
            g_orig[h->id] = h->want_orig ? r : NULL;
            g_hook_ok[h->id] = true;
            ok++;
        } else {
            (*fail)++;
            rf_log("hook FAILED %s @%#lx: %s", d->name, (unsigned long)d->rva, err);
        }
    }
    return ok;
}

static void install_hooks(void) {
    int fail = 0, ok = 0, calls = 0;
    for (int i = 0; i < C_COUNT; i++) {
        g_call_ok[i] = memcmp(RVA(g_call_desc[i].rva), g_call_desc[i].pre, 8) == 0;
        if (g_call_ok[i]) calls++;
        else rf_log("call preimage mismatch: %s", g_call_desc[i].name);
    }
    ok += install_table(g_core_hooks, sizeof g_core_hooks / sizeof g_core_hooks[0], &fail);
    ok += install_table(g_xr_hooks, g_xr_hook_count, &fail);
    rf_log("hooks ready: %d ok, %d failed; calls %d/%d verified (dev=%d)", ok, fail, calls, (int)C_COUNT, g_dev);
}

// ---------------------------------------------------------------- init
static bool g_inited;

static void native_init(JNIEnv* env, jclass cls, jstring jext, jstring jlibdir, jint sdk) {
    (void)cls;
    if (g_inited) return;
    g_inited = true;
    const char* ext = jext ? (*env)->GetStringUTFChars(env, jext, NULL) : NULL;
    const char* libdir = jlibdir ? (*env)->GetStringUTFChars(env, jlibdir, NULL) : NULL;
    if (ext) {
        snprintf(g_ext_dir, sizeof g_ext_dir, "%s", ext);
        mkdir(g_ext_dir, 0771);
        char p[600];
        snprintf(p, sizeof p, "%s/roomfix.dev", g_ext_dir);
        g_dev = access(p, F_OK) == 0;
        snprintf(p, sizeof p, "%s/roomfix.log", g_ext_dir);
        g_log_file = fopen(p, "w");
    }
    rf_log("libroomfix init: sdk=%d ext=%s libdir=%s", (int)sdk, ext ? ext : "(null)", libdir ? libdir : "(null)");

    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h && libdir) {
        char p[600];
        snprintf(p, sizeof p, "%s/libil2cpp.so", libdir);
        h = dlopen(p, RTLD_NOW);
    }
    struct find_ctx c = {.name = "libil2cpp.so"};
    if (h) dl_iterate_phdr(phdr_cb, &c);
    if (!h || !c.base) {
        rf_log("FATAL: libil2cpp.so not loaded (%s); flat-mode hooks disabled", dlerror());
    } else if (strcmp(c.build_id, RF_IL2CPP_BUILD_ID) != 0) {
        rf_log("FATAL: libil2cpp build-id %s != expected %s; hooks disabled", c.build_id, RF_IL2CPP_BUILD_ID);
    } else {
        g_il2cpp_base = c.base;
        resolve_api(h);
        rf_log("libil2cpp base=%#lx build-id ok", (unsigned long)g_il2cpp_base);
        install_hooks();
    }
    if (ext) (*env)->ReleaseStringUTFChars(env, jext, ext);
    if (libdir) (*env)->ReleaseStringUTFChars(env, jlibdir, libdir);
}

static void native_touch(JNIEnv* env, jclass cls, jint action, jint id, jfloat x, jfloat y, jlong t) {
    (void)env; (void)cls;
    rf_touch_post(action, id, x, y, (int64_t)t);
}

static jint native_hud_state(JNIEnv* env, jclass cls) {
    (void)env; (void)cls;
    return g_hud_state;
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    JNIEnv* env = NULL;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass cls = (*env)->FindClass(env, "port/roomfix/RoomFix");
    if (!cls) return JNI_ERR;
    static const JNINativeMethod m[] = {
        {"nativeInit", "(Ljava/lang/String;Ljava/lang/String;I)V", (void*)native_init},
        {"nativeTouch", "(IIFFJ)V", (void*)native_touch},
        {"nativeHudState", "()I", (void*)native_hud_state},
    };
    if ((*env)->RegisterNatives(env, cls, m, sizeof m / sizeof m[0]) != 0) return JNI_ERR;
    return JNI_VERSION_1_6;
}
