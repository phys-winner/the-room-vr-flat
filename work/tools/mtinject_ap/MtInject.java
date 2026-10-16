package port.tools;

import android.os.SystemClock;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.MotionEvent;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.lang.reflect.Method;
import java.util.ArrayList;

/**
 * Multi-touch injector for devices without root (round-2 test device realme-rmx3371).
 *
 * Runs as the adb shell user through app_process and injects genuine MotionEvents with the shell's INJECT_EVENTS
 * permission (InputManagerGlobal.injectInputEvent, the same path scrcpy uses), so InputDispatcher, the focused window
 * and the app see real fingers with stable pointer ids. Stdin protocol, compatible with work/tools/mtinject (evdev):
 *
 *   down SLOT X Y   finger SLOT goes down (ACTION_DOWN / ACTION_POINTER_DOWN at once)
 *   move SLOT X Y   new position, sent with the next sync
 *   up SLOT         finger lifts (ACTION_UP / ACTION_POINTER_UP at once)
 *   sync            one ACTION_MOVE with every finger when any position changed
 *   sleep MS
 *   quit
 *
 * X and Y are pixels of the logical display in its current rotation (what screencap shows). Pointer id = SLOT.
 * Stdin end or quit lifts every finger still down (ACTION_CANCEL), so a dead route never leaves a stuck touch.
 */
public final class MtInject {
    private static final int MAX = 10;
    private static Object sIm;
    private static Method sInject;

    private static final class Finger {
        int slot;
        float x, y;
    }

    private static final ArrayList<Finger> sDown = new ArrayList<>();
    private static long sDownTime;
    private static boolean sMoved;

    public static void main(String[] args) throws Exception {
        Class<?> c;
        try {
            c = Class.forName("android.hardware.input.InputManagerGlobal");  // Android 14+
        } catch (ClassNotFoundException e) {
            c = Class.forName("android.hardware.input.InputManager");
        }
        sIm = c.getDeclaredMethod("getInstance").invoke(null);
        sInject = c.getMethod("injectInputEvent", InputEvent.class, int.class);
        BufferedReader in = new BufferedReader(new InputStreamReader(System.in));
        String line;
        try {
            while ((line = in.readLine()) != null) {
                String[] a = line.trim().split("\\s+");
                if (a.length == 0 || a[0].isEmpty()) continue;
                switch (a[0]) {
                    case "down": down(Integer.parseInt(a[1]), Float.parseFloat(a[2]), Float.parseFloat(a[3])); break;
                    case "move": move(Integer.parseInt(a[1]), Float.parseFloat(a[2]), Float.parseFloat(a[3])); break;
                    case "up": up(Integer.parseInt(a[1])); break;
                    case "sync": sync(); break;
                    case "sleep": Thread.sleep(Long.parseLong(a[1])); break;
                    case "quit": return;
                    default: System.err.println("mtinject: unknown command " + a[0]);
                }
            }
        } finally {
            cancelAll();
        }
    }

    private static int index(int slot) {
        for (int i = 0; i < sDown.size(); i++) if (sDown.get(i).slot == slot) return i;
        return -1;
    }

    private static void down(int slot, float x, float y) throws Exception {
        if (index(slot) >= 0 || sDown.size() >= MAX) return;
        Finger f = new Finger();
        f.slot = slot;
        f.x = x;
        f.y = y;
        sDown.add(f);
        long now = SystemClock.uptimeMillis();
        if (sDown.size() == 1) {
            sDownTime = now;
            send(MotionEvent.ACTION_DOWN, now);
        } else {
            int i = sDown.size() - 1;
            send(MotionEvent.ACTION_POINTER_DOWN | (i << MotionEvent.ACTION_POINTER_INDEX_SHIFT), now);
        }
    }

    private static void move(int slot, float x, float y) {
        int i = index(slot);
        if (i < 0) return;
        Finger f = sDown.get(i);
        if (f.x != x || f.y != y) sMoved = true;
        f.x = x;
        f.y = y;
    }

    private static void sync() throws Exception {
        if (!sMoved || sDown.isEmpty()) return;
        sMoved = false;
        send(MotionEvent.ACTION_MOVE, SystemClock.uptimeMillis());
    }

    private static void up(int slot) throws Exception {
        int i = index(slot);
        if (i < 0) return;
        sync();
        long now = SystemClock.uptimeMillis();
        if (sDown.size() == 1) send(MotionEvent.ACTION_UP, now);
        else send(MotionEvent.ACTION_POINTER_UP | (i << MotionEvent.ACTION_POINTER_INDEX_SHIFT), now);
        sDown.remove(i);
    }

    private static void cancelAll() {
        if (sDown.isEmpty()) return;
        try {
            send(MotionEvent.ACTION_CANCEL, SystemClock.uptimeMillis());
        } catch (Exception e) {
            System.err.println("mtinject: cancel failed: " + e);
        }
        sDown.clear();
    }

    private static void send(int action, long now) throws Exception {
        int n = sDown.size();
        MotionEvent.PointerProperties[] props = new MotionEvent.PointerProperties[n];
        MotionEvent.PointerCoords[] coords = new MotionEvent.PointerCoords[n];
        for (int i = 0; i < n; i++) {
            Finger f = sDown.get(i);
            props[i] = new MotionEvent.PointerProperties();
            props[i].id = f.slot;
            props[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
            coords[i] = new MotionEvent.PointerCoords();
            coords[i].x = f.x;
            coords[i].y = f.y;
            coords[i].pressure = 1f;
            coords[i].size = 0.05f;
            coords[i].touchMajor = coords[i].touchMinor = 20f;
        }
        MotionEvent ev = MotionEvent.obtain(sDownTime, now, action, n, props, coords, 0, 0, 1f, 1f, 0, 0,
                InputDevice.SOURCE_TOUCHSCREEN, 0);
        boolean ok = (Boolean) sInject.invoke(sIm, ev, 0 /* INJECT_INPUT_EVENT_MODE_ASYNC */);
        ev.recycle();
        if (!ok) System.err.println("mtinject: inject failed, action " + action);
    }
}
