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
char g_ext_dir[512];
#if !RF_RELEASE
static pthread_mutex_t g_log_mu = PTHREAD_MUTEX_INITIALIZER;
static FILE* g_log_file;
bool g_dev;  // diagnostics enabled by <extDir>/roomfix.dev
#define LOG_MAX_LINES 3000

void rf_log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    __android_log_write(ANDROID_LOG_INFO, TAG, buf);
    pthread_mutex_lock(&g_log_mu);
    // the file keeps start-up provenance and the session's first events; for players it is bounded (and rewritten
    // at every start), unbounded only with the developer flag file
    static int lines;
    if (g_log_file && (g_dev || lines < LOG_MAX_LINES)) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        fprintf(g_log_file, "%5ld.%03ld %s\n", (long)ts.tv_sec, ts.tv_nsec / 1000000, buf);
        if (!g_dev && ++lines == LOG_MAX_LINES) fprintf(g_log_file, "log: %d lines, the rest only in logcat\n", lines);
        fflush(g_log_file);
    }
    pthread_mutex_unlock(&g_log_mu);
}
#endif

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

void rf_ref_clear(rf_ref* r) {
    if (r->h && g_il2.gchandle_free) g_il2.gchandle_free(r->h);
    r->h = 0;
}

void rf_ref_set(rf_ref* r, void* obj) {
    rf_ref_clear(r);
    if (obj && g_il2.gchandle_new_weakref) r->h = g_il2.gchandle_new_weakref(obj, false);
}

void* rf_ref_get(const rf_ref* r) {
    return r->h && g_il2.gchandle_get_target ? g_il2.gchandle_get_target(r->h) : NULL;
}

bool rf_class_is(void* obj, const char* name) {
    if (!obj || !g_il2.object_get_class || !g_il2.class_get_name) return false;
    for (void* k = g_il2.object_get_class(obj); k; k = g_il2.class_get_parent ? g_il2.class_get_parent(k) : NULL) {
        const char* n = g_il2.class_get_name(k);
        if (n && !strcmp(n, name)) return true;
        if (!g_il2.class_get_parent) break;
    }
    return false;
}

void* rf_find_class(const char* image, const char* ns, const char* name) {
    if (!g_il2.domain_get || !g_il2.domain_get_assemblies || !g_il2.assembly_get_image || !g_il2.image_get_name ||
        !g_il2.class_from_name)
        return NULL;
    size_t n = 0;
    void** as = g_il2.domain_get_assemblies(g_il2.domain_get(), &n);
    for (size_t i = 0; as && i < n; i++) {
        void* img = g_il2.assembly_get_image(as[i]);
        const char* in = img ? g_il2.image_get_name(img) : NULL;
        if (in && !strcmp(in, image)) return g_il2.class_from_name(img, ns, name);
    }
    return NULL;
}

void* rf_class_type(void* klass) {
    if (!klass || !g_il2.class_get_type || !g_il2.type_get_object) return NULL;
    void* t = g_il2.class_get_type(klass);
    return t ? g_il2.type_get_object(t) : NULL;
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
    g_il2.string_new = (void* (*)(const char*))dlsym(h, "il2cpp_string_new");
    g_il2.class_get_type = (void* (*)(void*))dlsym(h, "il2cpp_class_get_type");
    g_il2.type_get_object = (void* (*)(void*))dlsym(h, "il2cpp_type_get_object");
    g_il2.domain_get = (void* (*)(void))dlsym(h, "il2cpp_domain_get");
    g_il2.domain_get_assemblies = (void** (*)(void*, size_t*))dlsym(h, "il2cpp_domain_get_assemblies");
    g_il2.assembly_get_image = (void* (*)(void*))dlsym(h, "il2cpp_assembly_get_image");
    g_il2.image_get_name = (const char* (*)(void*))dlsym(h, "il2cpp_image_get_name");
    g_il2.class_from_name = (void* (*)(void*, const char*, const char*))dlsym(h, "il2cpp_class_from_name");
    g_il2.resolve_icall = (void* (*)(const char*))dlsym(h, "il2cpp_resolve_icall");
    g_il2.object_new = (void* (*)(void*))dlsym(h, "il2cpp_object_new");
    g_il2.gchandle_new_weakref = (uint32_t (*)(void*, bool))dlsym(h, "il2cpp_gchandle_new_weakref");
    g_il2.gchandle_get_target = (void* (*)(uint32_t))dlsym(h, "il2cpp_gchandle_get_target");
    g_il2.gchandle_free = (void (*)(uint32_t))dlsym(h, "il2cpp_gchandle_free");
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

// PICO platform verify service (com.pvr.verify) does not exist on phones; the port's store entitlement is
// answered in h_store_init, so the binding attempt from PXR_Manager / PVRManager is skipped.
static void h_pxr_bind_verify(void* object_name, const void* mi) {
    (void)object_name; (void)mi;
    rf_log("pxr: verify service binding skipped");
}

// Round 4 (operator 2026-10-16: "the dialog asking whether the player sits or stands must not show at all, not just be
// skipped"): VrPlatformManager.IsStandingSupported answers false - the game's own path for a platform without standing
// play. New game / Continue go straight on (MainMenuScreen.ContinueGamePressed calls ContinueGame instead of opening
// PostureSelectScreen), SetSittingCoroutine keeps the player seated and Options shows no posture rows. The wizard's
// automatic answer (rf_menu.c) stays as a fallback.
static bool h_vrpm_standing(void* self, const void* mi) {
    (void)self; (void)mi;
    return false;
}

// Logger.IsLogTypeAllowed: developer runs let the shipped logger reach logcat; release builds let nothing through
// (no managed log output and no stack trace work for it)
static bool h_log_allowed(void* self, int32_t type, const void* mi) {
    (void)self; (void)type; (void)mi;
    return !RF_RELEASE;
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
        rf_chapters_scene(g_scene);
    }
    static bool had_player;
    bool has = g_player && g_frame - g_player_frame < 30 && rf_alive(g_player);
    if (has != had_player) {
        had_player = has;
        rf_log("player %s (scene '%s')", has ? "ready" : "gone", g_scene);
    }
}

// dev: the camera really renders with the pose the port computes for the flat texts (rig * synthetic head)
static void dev_check_camera_pose(void) {
    if (!g_dev || g_frame % 300) return;
    void* cam = rf_main_camera();
    if (!cam) return;
    void* ct = rf_transform(cam);
    void* rig = rf_tr_parent(ct);
    if (!rf_alive(rig)) return;
    v3 want = CALLP(C_TR_POINT, v3 (*)(void*, v3, const void*))(rig, g_head.pos, NULL);
    quat wq = q_mul(rf_tr_rot(rig), rf_head_rot()), cq = rf_tr_rot(ct);
    float dot = fabsf(wq.x * cq.x + wq.y * cq.y + wq.z * cq.z + wq.w * cq.w);
    rf_log("dev: camera vs head pose: %.4f m, %.3f deg; near clip %.3f", v3_len(v3_sub(want, rf_tr_pos(ct))),
           2.0f * acosf(dot > 1.0f ? 1.0f : dot) / DEG2RAD, CALLP(C_CAM_NEAR, float (*)(void*, const void*))(cam, NULL));
}

void rf_frame_tick(void) {
    g_frame++;
    track_scene();
    rf_dev_tick();
    rf_dev_beam_watch();
    dev_check_camera_pose();  // before this frame's input changes the head
    g_hand_prev_pos[0] = g_hand[0].pos;
    g_hand_prev_pos[1] = g_hand[1].pos;
    rf_touch_tick();
    rf_gyro_tick();
    rf_view_tick();
    rf_hand_rest_apply();
    rf_interact_tick();
    rf_menu_tick();
    rf_hud_tick();
    rf_settings_tick();
    rf_flatui_pin_all(true);  // after every change of the head this frame
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

// Message boxes: MessageBox.AttachPosition None=0 (box default), Controller=1, Head=2. Boxes on a controller
// follow the VR player's hand, which is out of view in flat mode: they are shown in front of the eyes instead.
// None keeps the box's own default (world-placed tutorial notes stay where the level designer put them).
#define ATTACH_HEAD 2
static int32_t flat_attach(int32_t a) { return a == 1 ? ATTACH_HEAD : a; }

// A box left unattached (None) stays where it spawned: at the VR player's hand, i.e. behind the flat camera
// (resting hands are out of view). Put it in front of the eyes instead, world-anchored like the original.
static void box_into_view(void* box) {
    if (!rf_alive(box) || *(int32_t*)((char*)box + 0xa8) != 0) return;  // MessageBox.AttachedTo == None
    void* cam = rf_main_camera();
    if (!cam) return;
    void* ct = rf_transform(cam);
    // lower third of the view (17 degrees below the view axis): the centre stays free for the puzzle in front
    quat q = rf_tr_rot(ct);
    v3 f = q_rot(q, (v3){0, 0, 1});
    v3 down = q_rot(q, (v3){0, -1, 0});
    v3 d = v3_norm(v3_add(f, v3_scale(down, 0.3057f)));  // tan 17
    v3 pos = v3_add(rf_tr_pos(ct), v3_scale(d, 0.55f));
    void* t = rf_transform(box);
    CALLP(C_TR_SETPOS, void (*)(void*, v3, const void*))(t, pos, NULL);
    CALLP(C_TR_SETROT, void (*)(void*, quat, const void*))(t, rf_q_look(d, q_rot(q, (v3){0, 1, 0})), NULL);
}

int g_tut_action = -1;  // RwAction the current tutorial box waits for (-1 none)
void* g_last_box[2];      // last tutorial box / monologue box (dev diagnostics)
static void h_tut_msg(void* self, Il2CppString* title, Il2CppString* text, int32_t action, int32_t attach,
                      int32_t alt, const void* mi) {
    g_last_box[0] = self;
    char a[96], b[160];
    rf_str_utf8(title, a, sizeof a);
    rf_str_utf8(text, b, sizeof b);
    g_tut_action = action;
    rf_log("tutorial: '%s' / '%s' action=%d alt=%d attach=%d", a, b, action, alt, attach);
    ORIG(H_TUT_MSG, void (*)(void*, Il2CppString*, Il2CppString*, int32_t, int32_t, int32_t, const void*))(
        self, title, text, action, flat_attach(attach), alt, mi);
    box_into_view(self);
    rf_flatui_add(self, RF_FLAT_TOP);  // round 2: flat on the screen, instructions at the top
    // the box illustrates the VR controller and the button to use (TutorialMessageBox.LargeImage/SmallImage);
    // the touch wording comes from the port's strings instead
    for (int off = 0xc8; off <= 0xd0; off += 8) {
        void* img = *(void**)((char*)self + off);
        if (rf_alive(img)) CALLP(C_GO_SETACTIVE, void (*)(void*, bool, const void*))(rf_gameobject(img), false, NULL);
    }
}

static void h_msg_box(void* self, Il2CppString* text, int32_t attach, const void* mi) {
    if (self != g_last_box[0]) g_last_box[1] = self;
    char b[200];
    rf_str_utf8(text, b, sizeof b);
    rf_log("message: '%s' attach=%d", b, attach);
    ORIG(H_MSG_BOX, void (*)(void*, Il2CppString*, int32_t, const void*))(self, text, flat_attach(attach), mi);
    box_into_view(self);
    rf_flatui_add(self, rf_class_is(self, "TutorialMessageBox") ? RF_FLAT_TOP : RF_FLAT_BOTTOM);  // remarks below
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
    {H_OPTIONS_SETUP, (void*)h_options_setup, true, false},
    {H_MAINMENU_SETUP, (void*)h_mainmenu_setup, true, false},
    {H_BUTTON_PRESS, (void*)h_button_press, true, false},
    {H_GSM_LEVELCOMPLETE, (void*)h_gsm_level_complete, true, false},
    {H_GSM_SKIPLEVEL, (void*)h_gsm_skip_level, true, false},
    {H_GSM_STORYLOAD, (void*)h_gsm_story_load, true, false},
    {H_PAUSE_SETUP, (void*)h_pause_setup, true, false},
    {H_LOADSCENE_NAME, (void*)h_loadscene_name, true, false},
    {H_UIOPT_LEFT, (void*)h_uiopt_left, true, false},
    {H_UIOPT_RIGHT, (void*)h_uiopt_right, true, false},
    {H_UIOPT_UPDATE, (void*)h_uiopt_update, true, false},
    {H_LOC_GETSTRING, (void*)h_loc_getstring, true, false},
    {H_INV_SHOWXFORM, (void*)h_inv_show_xform, true, false},
    {H_INV_SHOW, (void*)h_inv_show, true, false},
    {H_QSM_POSTFX, (void*)h_qsm_postfx, true, true},
    {H_PXR_CONFIGINT, (void*)h_pxr_config_int, true, false},
    {H_GFX_SETUP, (void*)h_gfx_setup, true, false},
    {H_QSM_DEPTHMODE, (void*)h_qsm_depthmode, false, false},
    {H_LOC_LOADINITIAL, (void*)h_loc_load_initial, true, false},
    {H_PXR_BINDVERIFY, (void*)h_pxr_bind_verify, false, false},
    {H_FEP_SETLENGTH, (void*)h_fep_setlength, false, false},  // menu beam: full replacement
    {H_SIV_UPDATE, (void*)h_siv_update, false, false},
    {H_VRPM_STANDING, (void*)h_vrpm_standing, false, false},  // full replacement: no posture wizard
    {H_LOG_ALLOWED, (void*)h_log_allowed, false, !RF_RELEASE},  // release: always installed (silent logger)
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
    rf_gfx_init();
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
#if RF_RELEASE
        snprintf(p, sizeof p, "%s/roomfix.log", g_ext_dir);
        unlink(p);  // a log left by an earlier developer / player build
#else
        snprintf(p, sizeof p, "%s/roomfix.dev", g_ext_dir);
        g_dev = access(p, F_OK) == 0;
        snprintf(p, sizeof p, "%s/roomfix.log", g_ext_dir);
        g_log_file = fopen(p, "w");
#endif
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
    rf_settings_load();
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

static void native_display(JNIEnv* env, jclass cls, jfloat hz, jfloat max_hz) {
    (void)env; (void)cls;
    if (hz != g_display_hz || max_hz != g_display_max_hz) rf_log("display: %.1f Hz (max %.1f Hz)", hz, max_hz);
    g_display_hz = hz;
    g_display_max_hz = max_hz;
}

static void native_app_locale(JNIEnv* env, jclass cls, jstring jtags) {
    (void)cls;
    const char* t = jtags ? (*env)->GetStringUTFChars(env, jtags, NULL) : NULL;
    rf_loc_app_locale(t ? t : "");
    if (t) (*env)->ReleaseStringUTFChars(env, jtags, t);
}

static void native_metrics(JNIEnv* env, jclass cls, jfloat ydpi, jint height) {
    (void)env; (void)cls;
    if (ydpi != g_disp_ydpi || height != g_disp_h) rf_log("display: %.0f dpi, game view %d px high", ydpi, (int)height);
    g_disp_ydpi = ydpi;
    g_disp_h = height;
}

static jint native_want_max_refresh(JNIEnv* env, jclass cls) {
    (void)env; (void)cls;
    return g_fps_idx == RF_FPS_DISPLAY;
}

static jint native_help_request(JNIEnv* env, jclass cls) {
    (void)env; (void)cls;
    return rf_help_take();
}

static void native_gyro(JNIEnv* env, jclass cls, jfloat dx, jfloat dy, jfloat dz, jfloat ux, jfloat uy, jfloat uz) {
    (void)env; (void)cls;
    rf_gyro_post(dx, dy, dz, ux, uy, uz);
}

static void native_accel(JNIEnv* env, jclass cls, jfloat az, jfloat dt) {
    (void)env; (void)cls;
    rf_accel_post(az, dt);
}

static jint native_gyro_wanted(JNIEnv* env, jclass cls) {
    (void)env; (void)cls;
    return rf_gyro_wanted();
}

// round 5: the position gauge (rf_view.c) - copies g_gauge into out, returns the bits of the axes to draw
static jint native_gauge(JNIEnv* env, jclass cls, jfloatArray out) {
    (void)cls;
    float g[RF_GAUGE_N];
    for (int i = 0; i < RF_GAUGE_N; i++) g[i] = g_gauge[i];
    if (out && (*env)->GetArrayLength(env, out) >= RF_GAUGE_N) (*env)->SetFloatArrayRegion(env, out, 0, RF_GAUGE_N, g);
    return (jint)g[0];
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
#if RF_RELEASE
    // release: the whole process stays out of logcat - the game's engine, its SDKs, Java and the port (liblog drops
    // every message below SILENT; Android 11+). PortApplication loads this library first thing for that.
    int32_t (*set_min_prio)(int32_t) = (int32_t (*)(int32_t))dlsym(RTLD_DEFAULT, "__android_log_set_minimum_priority");
    if (set_min_prio) set_min_prio(ANDROID_LOG_SILENT);
#endif
    JNIEnv* env = NULL;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass cls = (*env)->FindClass(env, "port/roomfix/RoomFix");
    if (!cls) return JNI_ERR;
    static const JNINativeMethod m[] = {
        {"nativeInit", "(Ljava/lang/String;Ljava/lang/String;I)V", (void*)native_init},
        {"nativeTouch", "(IIFFJ)V", (void*)native_touch},
        {"nativeHudState", "()I", (void*)native_hud_state},
        {"nativeDisplay", "(FF)V", (void*)native_display},
        {"nativeWantMaxRefresh", "()I", (void*)native_want_max_refresh},
        {"nativeAppLocale", "(Ljava/lang/String;)V", (void*)native_app_locale},
        {"nativeHelpRequest", "()I", (void*)native_help_request},
        {"nativeGyro", "(FFFFFF)V", (void*)native_gyro},
        {"nativeGyroWanted", "()I", (void*)native_gyro_wanted},
        {"nativeAccel", "(FF)V", (void*)native_accel},
        {"nativeMetrics", "(FI)V", (void*)native_metrics},
        {"nativeGauge", "([F)I", (void*)native_gauge},
    };
    if ((*env)->RegisterNatives(env, cls, m, sizeof m / sizeof m[0]) != 0) return JNI_ERR;
    return JNI_VERSION_1_6;
}
