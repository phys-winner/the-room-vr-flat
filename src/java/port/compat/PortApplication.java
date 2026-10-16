package port.compat;

import android.app.Application;
import android.content.Context;

/** The game has no Application subclass: android:name="port.compat.PortApplication". */
public class PortApplication extends Application {
    @Override
    protected void attachBaseContext(Context base) {
        if (BuildFlags.RELEASE) {
            // release: libroomfix's JNI_OnLoad mutes the whole process in logcat before anything else runs
            System.loadLibrary("roomfix");
        }
        DataRedirect.install(base);
        super.attachBaseContext(base);
    }
}
