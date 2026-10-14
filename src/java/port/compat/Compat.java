package port.compat;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;

/** API-level adapters for calls whose contract changed with the raised targetSdkVersion. */
public final class Compat {
    private Compat() {
    }

    /**
     * Context.registerReceiver for targetSdk 34+: receivers registered at runtime must state whether other apps
     * may send to them. The game's receivers listen to system broadcasts (SCO audio state, battery, audio), so
     * RECEIVER_EXPORTED keeps the original behaviour.
     */
    public static Intent registerReceiver(Context context, BroadcastReceiver receiver, IntentFilter filter) {
        if (Build.VERSION.SDK_INT >= 33) {
            return context.registerReceiver(receiver, filter, Context.RECEIVER_EXPORTED);
        }
        return context.registerReceiver(receiver, filter);
    }
}
