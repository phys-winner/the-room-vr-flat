// mtinject: multi-touch injection into the real touchscreen evdev node (rooted test device only).
// Writes Linux multi-touch protocol B events, so the whole Android input pipeline (InputReader, dispatcher,
// window, app) sees genuine simultaneous fingers - which `adb shell input` cannot produce.
//
// build: <ndk>/toolchains/llvm/prebuilt/<host>/bin/clang --target=aarch64-linux-android24 -O2 mtinject.c -o mtinject
//        (then adb push mtinject /data/local/tmp/; the binary is not tracked)
// usage: mtinject /dev/input/eventN W H ROT  < script
//   W H : panel size in its natural (portrait) orientation, e.g. 1080 2340
//   ROT : display rotation of the screen coordinates used in the script (0, 90, 270)
// script lines (screen pixels in the current orientation):
//   down SLOT X Y | move SLOT X Y | up SLOT | sync | sleep MS
//   "sync" ends a frame (SYN_REPORT); down/move/up of several slots before one sync are simultaneous.
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int g_fd, g_w, g_h, g_rot;
static int g_tracking;  // seeded per run so ids never collide with contacts left down by an earlier run
static int g_touching;

static void emit(int type, int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    gettimeofday(&ev.time, NULL);
    ev.type = (unsigned short)type;
    ev.code = (unsigned short)code;
    ev.value = value;
    if (write(g_fd, &ev, sizeof ev) != (ssize_t)sizeof ev) perror("write");
}

// screen (rotated) -> panel (natural) coordinates
static void to_panel(int sx, int sy, int* px, int* py) {
    switch (g_rot) {
    case 90:  *px = g_w - 1 - sy; *py = sx; break;
    case 270: *px = sy; *py = g_h - 1 - sx; break;
    default:  *px = sx; *py = sy; break;
    }
}

int main(int argc, char** argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s /dev/input/eventN W H ROT < script\n", argv[0]);
        return 2;
    }
    g_fd = open(argv[1], O_RDWR);
    if (g_fd < 0) { perror("open"); return 1; }
    g_w = atoi(argv[2]);
    g_h = atoi(argv[3]);
    g_rot = atoi(argv[4]);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    g_tracking = 100 + (int)((ts.tv_sec * 7 + ts.tv_nsec / 1000000) % 60000);
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        char cmd[16];
        int slot = 0, x = 0, y = 0;
        int n = sscanf(line, "%15s %d %d %d", cmd, &slot, &x, &y);
        if (n < 1) continue;
        if (!strcmp(cmd, "down") && n == 4) {
            int px, py;
            to_panel(x, y, &px, &py);
            emit(EV_ABS, ABS_MT_SLOT, slot);
            emit(EV_ABS, ABS_MT_TRACKING_ID, g_tracking++);
            emit(EV_ABS, ABS_MT_POSITION_X, px);
            emit(EV_ABS, ABS_MT_POSITION_Y, py);
            emit(EV_ABS, ABS_MT_TOUCH_MAJOR, 6);
            if (g_touching++ == 0) emit(EV_KEY, BTN_TOUCH, 1);
        } else if (!strcmp(cmd, "move") && n == 4) {
            int px, py;
            to_panel(x, y, &px, &py);
            emit(EV_ABS, ABS_MT_SLOT, slot);
            emit(EV_ABS, ABS_MT_POSITION_X, px);
            emit(EV_ABS, ABS_MT_POSITION_Y, py);
        } else if (!strcmp(cmd, "up") && n >= 2) {
            emit(EV_ABS, ABS_MT_SLOT, slot);
            emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
            if (--g_touching == 0) emit(EV_KEY, BTN_TOUCH, 0);
        } else if (!strcmp(cmd, "sync")) {
            emit(EV_SYN, SYN_REPORT, 0);
        } else if (!strcmp(cmd, "sleep") && n >= 2) {
            usleep((useconds_t)slot * 1000);
        }
    }
    close(g_fd);
    return 0;
}
