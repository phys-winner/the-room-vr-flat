// Port settings: FPS limit (30 / 45 / 60 / display refresh rate, default 60) and horizontal field of view; the
// stage-8 QA row (qa=) and whether the gesture card was shown (help=); round 2: the gyroscope options (gpos= / grot=
// on or off, gposs= / grots= sensitivity option, rf_gyro.c); round 5: the position indicator (gauge= on or off,
// rf_view.c).
// Stored in <getExternalFilesDir>/port_settings.cfg as "key=value" lines; shown as rows of the game's own
// Options screen (rf_options.c).
//
// The FPS limit only caps rendering through Application.targetFrameRate; that simulation and animation speed
// stay authored in every mode is measured for FPS_LIMIT_VERIFIED (STATE.md).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rf.h"

char g_app_locale[64];  // app locale tags last applied (Android 13+ per-app language)
int g_fps_idx = RF_FPS_60;
int g_fov_idx = 2;  // 90 degrees across a 16:9 reference screen
volatile float g_display_hz = 60.0f;
volatile float g_display_max_hz = 60.0f;

static const int FPS_VALUES[RF_FPS_COUNT] = {30, 45, 60, 0};
static const int FOV_VALUES[RF_FOV_COUNT] = {70, 80, 90, 100, 110};

int rf_fps_value(int idx) {
    if (idx == RF_FPS_DISPLAY) {
        int hz = (int)(g_display_max_hz + 0.5f);
        return hz >= 30 ? hz : 60;
    }
    return FPS_VALUES[idx >= 0 && idx < RF_FPS_COUNT ? idx : RF_FPS_60];
}

int rf_fov_value(int idx) { return FOV_VALUES[idx >= 0 && idx < RF_FOV_COUNT ? idx : 2]; }

static void path(char* out, size_t n) { snprintf(out, n, "%s/port_settings.cfg", g_ext_dir); }

static void log_settings(void) {
    rf_log("settings: fps limit %d (option %d), field of view %d, position indicator %d, gyroscope eye position %d (%d%%) "
           "tilt and turn %d (%d%%)", rf_fps_value(g_fps_idx), g_fps_idx, rf_fov_value(g_fov_idx), g_gauge_on, g_gyro_pos,
           rf_gyro_sens_pct(g_gyro_pos_sens), g_gyro_rot, rf_gyro_sens_pct(g_gyro_rot_sens));
}

void rf_settings_load(void) {
    char p[600];
    path(p, sizeof p);
    FILE* f = fopen(p, "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof line, f)) {
            int v;
            if (sscanf(line, "fps=%d", &v) == 1 && v >= 0 && v < RF_FPS_COUNT) g_fps_idx = v;
            else if (sscanf(line, "fov=%d", &v) == 1 && v >= 0 && v < RF_FOV_COUNT) g_fov_idx = v;
            else if (sscanf(line, "qa=%d", &v) == 1 && v >= 0 && v < QA_COUNT) g_qa = v;  // else: off
            else if (sscanf(line, "help=%d", &v) == 1) g_help_seen = v == 1;
            else if (sscanf(line, "gpos=%d", &v) == 1) g_gyro_pos = v == 1;
            else if (sscanf(line, "grot=%d", &v) == 1) g_gyro_rot = v == 1;
            else if (sscanf(line, "gposs=%d", &v) == 1 && v >= 0 && v < RF_GYRO_SENS_COUNT) g_gyro_pos_sens = v;
            else if (sscanf(line, "grots=%d", &v) == 1 && v >= 0 && v < RF_GYRO_SENS_COUNT) g_gyro_rot_sens = v;
            else if (sscanf(line, "gauge=%d", &v) == 1) g_gauge_on = v != 0;
            else if (!strncmp(line, "applocale=", 10)) {
                snprintf(g_app_locale, sizeof g_app_locale, "%s", line + 10);
                g_app_locale[strcspn(g_app_locale, "\r\n")] = 0;
            }
        }
        fclose(f);
    }
    g_fov_h = (float)rf_fov_value(g_fov_idx);
    log_settings();
}

void rf_settings_save(void) {
    char p[600], tmp[620];
    path(p, sizeof p);
    snprintf(tmp, sizeof tmp, "%s.tmp", p);
    FILE* f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "fps=%d\nfov=%d\nqa=%d\nhelp=%d\ngpos=%d\ngposs=%d\ngrot=%d\ngrots=%d\ngauge=%d\napplocale=%s\n",
            g_fps_idx, g_fov_idx, g_qa, g_help_seen, g_gyro_pos, g_gyro_pos_sens, g_gyro_rot, g_gyro_rot_sens, g_gauge_on,
            g_app_locale);
    fclose(f);
    rename(tmp, p);
}

static int g_applied_fps = -2;

void rf_settings_changed(void) {
    g_fov_h = (float)rf_fov_value(g_fov_idx);
    g_applied_fps = -2;  // re-apply on the next tick
    rf_settings_save();
    log_settings();
}

void rf_settings_tick(void) {
    // the game or Unity may change the target (scene loads); check twice a second
    if (g_applied_fps != -2 && (g_frame % 30)) goto measure;
    {
        int want = rf_fps_value(g_fps_idx);
        int cur = CALLP(C_APP_GETFPS, int32_t (*)(const void*))(NULL);
        if (cur != want) {
            CALLP(C_APP_SETFPS, void (*)(int32_t, const void*))(want, NULL);
            if (g_applied_fps != want) rf_log("settings: Application.targetFrameRate %d -> %d", cur, want);
        }
        g_applied_fps = want;
    }
measure:
    if (g_dev) {
        // frame rate and simulation clock: game time advanced per wall-clock second (1.00 = authored speed,
        // timeScale 0 while paused), Unity's maximumDeltaTime / fixedDeltaTime as the game sets them
        typedef float (*fn_tf)(const void*);
        static struct timespec t0;
        static int f0;
        static float gt0;
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        double dt = (t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9;
        if (f0 == 0 || dt > 10.0) {
            float gt = CALLP(C_TIME_TIME, fn_tf)(NULL);
            if (f0)
                rf_log("fps: %.1f over %.1f s (limit %d) game-time/s %.3f timeScale %.2f maxDT %.4f fixedDT %.4f",
                       (g_frame - f0) / dt, dt, rf_fps_value(g_fps_idx), (gt - gt0) / dt,
                       CALLP(C_TIME_SCALE, fn_tf)(NULL), CALLP(C_TIME_MAXDT, fn_tf)(NULL),
                       CALLP(C_TIME_FIXEDDT, fn_tf)(NULL));
            t0 = t;
            f0 = g_frame;
            gt0 = gt;
        }
    }
}

void* rf_type_of(void* obj) {
    if (!obj || !g_il2.object_get_class || !g_il2.class_get_type || !g_il2.type_get_object) return NULL;
    void* k = g_il2.object_get_class(obj);
    void* t = k ? g_il2.class_get_type(k) : NULL;
    return t ? g_il2.type_get_object(t) : NULL;
}
