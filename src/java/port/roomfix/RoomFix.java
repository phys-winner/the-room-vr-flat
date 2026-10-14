package port.roomfix;

import android.app.Activity;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
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
 * libroomfix (look, grab, pinch, menu taps) with coordinates normalised to the Unity view.
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

    private static final int A_DOWN = 0, A_UP = 1, A_MOVE = 2, A_CANCEL = 3, A_HUD = 10, A_CANCEL_ALL = 11;
    private static final int OWNER_GAME = 0;

    private static Activity sActivity;
    private static boolean sReady;
    private static HudView sHud;
    private static Handler sMain;
    private static final SparseIntArray sOwner = new SparseIntArray();  // pointer id -> HUD button or OWNER_GAME
    private static final int[] sLoc = new int[2];

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

    private static void releaseHudFingers() {
        for (int i = sOwner.size() - 1; i >= 0; i--) {
            if (sOwner.valueAt(i) != OWNER_GAME) sOwner.removeAt(i);
        }
        if (sHud != null) sHud.clearPressed();
    }

    /** Called from UnityPlayerActivity.dispatchTouchEvent; returns true when the port consumed the event. */
    public static boolean onTouch(MotionEvent ev) {
        if (!sReady || sActivity == null) return false;
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
        if (ev.getAction() == KeyEvent.ACTION_UP && !ev.isCanceled()) {
            nativeTouch(A_HUD, 7 /* HB_BACK */, 0, 0, ev.getEventTime());
        }
        return true;
    }

    /** Called from UnityPlayerActivity.onWindowFocusChanged. */
    public static void onFocus(boolean hasFocus) {
        if (!sReady || hasFocus) return;
        sOwner.clear();
        if (sHud != null) sHud.clearPressed();
        nativeTouch(A_CANCEL_ALL, 0, 0, 0, 0);
    }
}
