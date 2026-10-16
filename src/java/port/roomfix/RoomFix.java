package port.roomfix;

import android.app.Activity;
import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.SystemClock;
import android.util.SparseIntArray;
import android.view.Display;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import java.io.File;

/**
 * Flat-screen port bootstrap: loads libroomfix.so before Unity creates its player, adds the touch HUD and
 * routes touches: a finger that lands on a HUD button belongs to that button, every other finger goes to
 * libroomfix (look, grab, pinch, menu taps) with coordinates normalised to the Unity view. While the gesture
 * card (HelpCard) is shown it takes every touch and Back; a tap closes it. The gyroscope options (Options ->
 * Gyroscope) are fed from here: the gyroscope and gravity sensors (and linear acceleration for "eye position") run
 * only while an option is on and the window has focus; rotation increments and the push along the screen normal go to
 * libroomfix in screen axes (x right, y up, z out of the screen).
 */
public final class RoomFix {
    private RoomFix() {}

    static native void nativeInit(String extDir, String nativeLibDir, int sdk);

    /**
     * action: 0 down, 1 up, 2 move, 3 cancel, 10 HUD button (pointerId = button), 11 cancel all fingers.
     * Coordinates are normalised to the Unity view (0..1, y down).
     */
    static native void nativeTouch(int action, int pointerId, float x, float y, long timeMs);

    /** HUD_* flags computed by libroomfix every frame. */
    static native int nativeHudState();

    /** Current and highest refresh rate of the display (FPS limit option "display"). */
    static native void nativeDisplay(float hz, float maxHz);

    /** 1 when the player chose the display refresh rate as FPS limit. */
    static native int nativeWantMaxRefresh();

    /** Per-app language tags from the Android 13+ system picker ("" when the app follows the system). */
    static native void nativeAppLocale(String tags);

    /** Pending gesture-card request: game language index + 1, or 0. Taking it clears it. */
    static native int nativeHelpRequest();

    /** Gyroscope: rotation since the last sample (radians about screen x / y / z) and the "up" direction (m/s^2). */
    static native void nativeGyro(float dx, float dy, float dz, float ux, float uy, float uz);

    /** Eye position option: linear acceleration (gravity removed) along the screen normal, m/s^2, and its interval. */
    static native void nativeAccel(float az, float dt);

    /** Sensors the gyroscope options need: bit 0 gyroscope + gravity, bit 1 linear acceleration; 0 when both off. */
    static native int nativeGyroWanted();

    private static final int A_DOWN = 0, A_UP = 1, A_MOVE = 2, A_CANCEL = 3, A_HUD = 10, A_CANCEL_ALL = 11;
    private static final int OWNER_GAME = 0;

    private static Activity sActivity;
    private static boolean sReady;
    private static HudView sHud;
    private static HelpCard sHelp;
    private static long sHelpShownAt;
    private static boolean sHelpDown;  // a finger went down on the shown card (its lift closes the card)
    private static Handler sMain;
    private static final SparseIntArray sOwner = new SparseIntArray();  // pointer id -> HUD button or OWNER_GAME
    private static final int[] sLoc = new int[2];

    // ---- gyroscope ----
    private static boolean sFocused = true;
    private static int sGyroOn;  // sensor set registered (nativeGyroWanted bits)
    private static SensorManager sSensors;
    private static Handler sSensorHandler;
    private static volatile int sRotation;  // Display.getRotation(): screen axes from the device's natural axes
    private static long sGyroTs, sLinTs;
    private static final float[] sUp = new float[3];  // gravity reaction ("up") in screen axes, m/s^2
    private static boolean sUpFromAccel;

    /** Device axes -> screen axes for the display rotation (the drawing turns opposite to the device). */
    private static void toScreen(float[] v, float[] out) {
        switch (sRotation) {
            case 1: out[0] = -v[1]; out[1] = v[0]; break;   // ROTATION_90
            case 2: out[0] = -v[0]; out[1] = -v[1]; break;  // ROTATION_180
            case 3: out[0] = v[1]; out[1] = -v[0]; break;   // ROTATION_270
            default: out[0] = v[0]; out[1] = v[1]; break;
        }
        out[2] = v[2];
    }

    private static final SensorEventListener sGyroListener = new SensorEventListener() {
        private final float[] s = new float[3];

        @Override
        public void onSensorChanged(SensorEvent e) {
            int type = e.sensor.getType();
            toScreen(e.values, s);
            if (type == Sensor.TYPE_GYROSCOPE) {
                long ts = e.timestamp;
                float dt = sGyroTs != 0 ? (ts - sGyroTs) * 1e-9f : 0f;
                sGyroTs = ts;
                if (dt > 0f && dt < 0.1f) nativeGyro(s[0] * dt, s[1] * dt, s[2] * dt, sUp[0], sUp[1], sUp[2]);
            } else if (type == Sensor.TYPE_LINEAR_ACCELERATION) {
                long ts = e.timestamp;
                float dt = sLinTs != 0 ? (ts - sLinTs) * 1e-9f : 0f;
                sLinTs = ts;
                if (dt > 0f && dt < 0.1f) nativeAccel(s[2], dt);
            } else if (type == Sensor.TYPE_GRAVITY) {
                sUp[0] = s[0];
                sUp[1] = s[1];
                sUp[2] = s[2];
            } else if (sUpFromAccel) {  // no gravity sensor: low-passed accelerometer
                for (int i = 0; i < 3; i++) sUp[i] += (s[i] - sUp[i]) * 0.1f;
            }
        }

        @Override
        public void onAccuracyChanged(Sensor sensor, int accuracy) {}
    };

    private static void setGyro(int want) {
        if (want == sGyroOn) return;
        if (sSensors == null) {
            sSensors = (SensorManager) sActivity.getSystemService(Context.SENSOR_SERVICE);
            HandlerThread t = new HandlerThread("RoomFixSensors");
            t.start();
            sSensorHandler = new Handler(t.getLooper());
        }
        if (sSensors == null) return;
        sSensors.unregisterListener(sGyroListener);
        if (want != 0) {
            Sensor gyro = sSensors.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
            if (gyro == null) return;  // no gyroscope: the options stay without effect
            Sensor grav = sSensors.getDefaultSensor(Sensor.TYPE_GRAVITY);
            sUpFromAccel = grav == null;
            if (grav == null) grav = sSensors.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
            sRotation = sActivity.getWindowManager().getDefaultDisplay().getRotation();
            sGyroTs = 0;
            sUp[0] = 0f;
            sUp[1] = 9.81f;
            sUp[2] = 0f;
            sSensors.registerListener(sGyroListener, gyro, 5000, sSensorHandler);  // 200 Hz: smooth at any frame rate
            if (grav != null) sSensors.registerListener(sGyroListener, grav, SensorManager.SENSOR_DELAY_GAME, sSensorHandler);
            Sensor lin = (want & 2) != 0 ? sSensors.getDefaultSensor(Sensor.TYPE_LINEAR_ACCELERATION) : null;
            sLinTs = 0;
            if (lin != null) sSensors.registerListener(sGyroListener, lin, 5000, sSensorHandler);  // eye position: push
        }
        sGyroOn = want;
    }

    public static void init(Activity activity) {
        sActivity = activity;
        miuiCutout(activity);
        System.loadLibrary("roomfix");
        File ext = activity.getExternalFilesDir(null);
        String dir = ext != null ? ext.getAbsolutePath() : activity.getFilesDir().getAbsolutePath();
        nativeInit(dir, activity.getApplicationInfo().nativeLibraryDir, android.os.Build.VERSION.SDK_INT);
        if (Build.VERSION.SDK_INT >= 33) {
            android.app.LocaleManager lm = activity.getSystemService(android.app.LocaleManager.class);
            nativeAppLocale(lm != null ? lm.getApplicationLocales().toLanguageTags() : "");
        }
        sReady = true;
        sMain = new Handler(Looper.getMainLooper());
        // runs after onCreate has set the Unity player as the content view
        sMain.post(new Runnable() {
            @Override
            public void run() {
                attachHud();
            }
        });
    }

    private static void attachHud() {
        if (sActivity == null || sHud != null) return;
        sHud = new HudView(sActivity);
        if (android.os.Build.VERSION.SDK_INT >= 29) {
            // system / vendor dark modes must not recolour the HUD (MIUI darkens light app colours)
            sHud.setForceDarkAllowed(false);
            sActivity.getWindow().getDecorView().setForceDarkAllowed(false);
        }
        sActivity.addContentView(sHud, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        sHelp = new HelpCard(sActivity);
        if (android.os.Build.VERSION.SDK_INT >= 29) sHelp.setForceDarkAllowed(false);
        sActivity.addContentView(sHelp, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        sMain.post(sPoll);
    }

    private static int sPollCount;
    private static int sMaxModeId;
    private static boolean sWantMax;

    private static final Runnable sPoll = new Runnable() {
        @Override
        public void run() {
            if (sHud != null) {
                int s = nativeHudState();
                if ((s & HudView.S_VISIBLE) == 0 && (sHud.state() & HudView.S_VISIBLE) != 0) releaseHudFingers();
                sHud.setState(s);
            }
            int help = nativeHelpRequest();
            if (help > 0 && sHelp != null) showHelp(help - 1);
            setGyro(sFocused ? nativeGyroWanted() : 0);
            if (sPollCount++ % 20 == 0) updateDisplay();
            sMain.postDelayed(this, 100);
        }
    };

    /** Reports the display rates; with FPS limit "display" asks for the highest refresh mode of this size. */
    private static void updateDisplay() {
        Display d = sActivity.getWindowManager().getDefaultDisplay();
        float hz = d.getRefreshRate(), max = hz;
        int maxId = 0;
        if (Build.VERSION.SDK_INT >= 23) {
            Display.Mode cur = d.getMode();
            for (Display.Mode m : d.getSupportedModes()) {
                if (m.getPhysicalWidth() == cur.getPhysicalWidth() && m.getPhysicalHeight() == cur.getPhysicalHeight()
                        && m.getRefreshRate() >= max) {
                    max = m.getRefreshRate();
                    maxId = m.getModeId();
                }
            }
        }
        sMaxModeId = maxId;
        sRotation = d.getRotation();
        nativeDisplay(hz, max);
        boolean want = nativeWantMaxRefresh() != 0;
        if (want != sWantMax && Build.VERSION.SDK_INT >= 23) {
            sWantMax = want;
            WindowManager.LayoutParams lp = sActivity.getWindow().getAttributes();
            lp.preferredDisplayModeId = want ? sMaxModeId : 0;
            sActivity.getWindow().setAttributes(lp);
        }
    }

    /** MIUI letterboxes the cutout side in landscape unless the window asks for it with its own extra flags. */
    private static void miuiCutout(Activity activity) {
        try {
            java.lang.reflect.Method m = Window.class.getMethod("addExtraFlags", int.class);
            m.invoke(activity.getWindow(), 0x100 | 0x200 | 0x400);  // notch: use, portrait, landscape
        } catch (Exception ignored) {
            // not MIUI
        }
    }

    /** The Unity player view (first child of the content frame), or the decor view as a fallback. */
    private static View gameView() {
        View content = sActivity.findViewById(android.R.id.content);
        if (content instanceof ViewGroup && ((ViewGroup) content).getChildCount() > 0) {
            View v = ((ViewGroup) content).getChildAt(0);
            if (v != sHud && v.getWidth() > 0) return v;
        }
        return sActivity.getWindow().getDecorView();
    }

    /** Shows the gesture card; fingers already on the game or the HUD are cancelled. */
    private static void showHelp(int language) {
        sOwner.clear();
        if (sHud != null) sHud.clearPressed();
        nativeTouch(A_CANCEL_ALL, 0, 0, 0, 0);
        sHelpDown = false;
        sHelpShownAt = SystemClock.uptimeMillis();
        sHelp.show(language);
    }

    private static void releaseHudFingers() {
        for (int i = sOwner.size() - 1; i >= 0; i--) {
            if (sOwner.valueAt(i) != OWNER_GAME) sOwner.removeAt(i);
        }
        if (sHud != null) sHud.clearPressed();
    }

    /** Called from UnityPlayerActivity.dispatchTouchEvent; returns true when the port consumed the event. */
    public static boolean onTouch(MotionEvent ev) {
        if (!sReady || sActivity == null) return false;
        if (sHelp != null && sHelp.showing()) {
            // a tap that starts on the card closes it (not the lift of a finger that was down before it opened)
            int m = ev.getActionMasked();
            if (m == MotionEvent.ACTION_DOWN) sHelpDown = ev.getEventTime() - sHelpShownAt > 300;
            else if (m == MotionEvent.ACTION_CANCEL) sHelpDown = false;
            else if (m == MotionEvent.ACTION_UP && sHelpDown) {
                sHelpDown = false;
                sHelp.hide();
            }
            return true;
        }
        View gv = gameView();
        gv.getLocationInWindow(sLoc);
        float gx = sLoc[0], gy = sLoc[1];
        float w = Math.max(1, gv.getWidth()), h = Math.max(1, gv.getHeight());
        float hx = 0, hy = 0;
        if (sHud != null) {
            sHud.getLocationInWindow(sLoc);
            hx = sLoc[0];
            hy = sLoc[1];
        }
        // MotionEvent coordinates in Activity.dispatchTouchEvent are window coordinates
        long t = ev.getEventTime();
        int masked = ev.getActionMasked();
        switch (masked) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int i = ev.getActionIndex();
                int id = ev.getPointerId(i);
                float x = ev.getX(i), y = ev.getY(i);
                int b = sHud != null ? sHud.hit(x - hx, y - hy) : 0;
                if (b != 0) {
                    sOwner.put(id, b);
                    sHud.setPressed(b, true);
                    nativeTouch(A_HUD, b, 0, 0, t);
                } else {
                    sOwner.put(id, OWNER_GAME);
                    nativeTouch(A_DOWN, id, (x - gx) / w, (y - gy) / h, t);
                }
                break;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int i = ev.getActionIndex();
                int id = ev.getPointerId(i);
                int owner = sOwner.get(id, -1);
                sOwner.delete(id);
                if (owner > 0) {
                    if (sHud != null) sHud.setPressed(owner, false);
                } else if (owner == OWNER_GAME) {
                    nativeTouch(A_UP, id, (ev.getX(i) - gx) / w, (ev.getY(i) - gy) / h, t);
                }
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < ev.getPointerCount(); i++) {
                    int id = ev.getPointerId(i);
                    if (sOwner.get(id, -1) == OWNER_GAME) {
                        nativeTouch(A_MOVE, id, (ev.getX(i) - gx) / w, (ev.getY(i) - gy) / h, t);
                    }
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                for (int i = 0; i < ev.getPointerCount(); i++) {
                    int id = ev.getPointerId(i);
                    if (sOwner.get(id, -1) == OWNER_GAME) {
                        nativeTouch(A_CANCEL, id, (ev.getX(i) - gx) / w, (ev.getY(i) - gy) / h, t);
                    }
                }
                sOwner.clear();
                if (sHud != null) sHud.clearPressed();
                break;
            default:
                break;
        }
        return true;
    }

    /** Called from UnityPlayerActivity.dispatchKeyEvent: Back opens / closes the game's pause menu. */
    public static boolean onKey(KeyEvent ev) {
        if (!sReady || ev.getKeyCode() != KeyEvent.KEYCODE_BACK) return false;
        if (sHelp != null && sHelp.showing()) {
            if (ev.getAction() == KeyEvent.ACTION_UP && !ev.isCanceled()) sHelp.hide();
            return true;
        }
        if (ev.getAction() == KeyEvent.ACTION_UP && !ev.isCanceled()) {
            nativeTouch(A_HUD, 7 /* HB_BACK */, 0, 0, ev.getEventTime());
        }
        return true;
    }

    /** Called from UnityPlayerActivity.onWindowFocusChanged. */
    public static void onFocus(boolean hasFocus) {
        sFocused = hasFocus;
        if (!sReady) return;
        if (!hasFocus) setGyro(0);  // no sensors in the background; the poll turns them on again
        if (hasFocus) return;
        sOwner.clear();
        if (sHud != null) sHud.clearPressed();
        nativeTouch(A_CANCEL_ALL, 0, 0, 0, 0);
    }
}
