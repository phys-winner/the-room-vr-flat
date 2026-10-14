package port.roomfix;

import android.app.Activity;
import android.view.MotionEvent;
import android.view.View;
import java.io.File;

/** Flat-screen port bootstrap: loads libroomfix.so before Unity creates its player and feeds touches to it. */
public final class RoomFix {
    private RoomFix() {}

    static native void nativeInit(String extDir, String nativeLibDir, int sdk);

    /** action: 0 down, 1 up, 2 move, 3 cancel. Coordinates are normalised to the game view (0..1, y down). */
    static native void nativeTouch(int action, int pointerId, float x, float y, long timeMs);

    private static Activity sActivity;
    private static boolean sReady;

    public static void init(Activity activity) {
        sActivity = activity;
        System.loadLibrary("roomfix");
        File ext = activity.getExternalFilesDir(null);
        String dir = ext != null ? ext.getAbsolutePath() : activity.getFilesDir().getAbsolutePath();
        nativeInit(dir, activity.getApplicationInfo().nativeLibraryDir, android.os.Build.VERSION.SDK_INT);
        sReady = true;
    }

    /** Called from UnityPlayerActivity.dispatchTouchEvent; returns true when the port consumed the event. */
    public static boolean onTouch(MotionEvent ev) {
        if (!sReady || sActivity == null) return false;
        View root = sActivity.getWindow().getDecorView();
        float w = Math.max(1, root.getWidth());
        float h = Math.max(1, root.getHeight());
        long t = ev.getEventTime();
        int masked = ev.getActionMasked();
        switch (masked) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int i = ev.getActionIndex();
                boolean down = masked == MotionEvent.ACTION_DOWN || masked == MotionEvent.ACTION_POINTER_DOWN;
                nativeTouch(down ? 0 : 1, ev.getPointerId(i), ev.getX(i) / w, ev.getY(i) / h, t);
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < ev.getPointerCount(); i++) {
                    nativeTouch(2, ev.getPointerId(i), ev.getX(i) / w, ev.getY(i) / h, t);
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                for (int i = 0; i < ev.getPointerCount(); i++) {
                    nativeTouch(3, ev.getPointerId(i), ev.getX(i) / w, ev.getY(i) / h, t);
                }
                break;
            default:
                break;
        }
        return true;
    }
}
