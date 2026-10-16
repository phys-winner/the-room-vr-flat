package port.compat;

import android.content.Context;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;
import java.io.File;

/**
 * Keeps the game's data under Context.getExternalFilesDir(null).
 *
 * Unity already writes saves and its IL2CPP cache to the external app-specific root; PlayerPrefs
 * (shared_prefs/PKG.v2.playerprefs.xml) and anything written through getFilesDir() would stay internal.
 * Each listed directory under /data/data/PKG becomes a symbolic link into the external root, so
 * SharedPreferences, Java files and native paths all land there. An existing real directory (older install)
 * is moved first. Called from PortApplication.attachBaseContext before any data access.
 */
public final class DataRedirect {
    private static final String TAG = "PortDataRedirect";
    private static final boolean LOG = !BuildFlags.RELEASE;  // release builds write no log
    private static final String[] DIRECTORIES = {"files", "shared_prefs", "databases"};

    private DataRedirect() {
    }

    public static void install(Context base) {
        File external = base.getExternalFilesDir(null);
        if (external == null) {
            if (LOG) Log.e(TAG, "getExternalFilesDir(null) unavailable; data stays internal");
            return;
        }
        File internal = base.getDataDir();
        for (String name : DIRECTORIES) {
            redirect(new File(internal, name), new File(external, name));
        }
    }

    private static void redirect(File link, File target) {
        if (!target.isDirectory() && !target.mkdirs()) {
            if (LOG) Log.e(TAG, "Cannot create " + target);
            return;
        }
        String path = link.getPath();
        String current = readLink(path);
        if (target.getPath().equals(current)) {
            return;
        }
        try {
            if (current != null) {
                Os.remove(path);
            } else if (link.isDirectory()) {
                if (!moveContents(link, target)) {
                    if (LOG) Log.w(TAG, path + " could not be moved; left untouched");
                    return;
                }
            } else if (link.exists()) {
                if (LOG) Log.w(TAG, path + " is not a directory; left untouched");
                return;
            }
            Os.symlink(target.getPath(), path);
            if (LOG) Log.i(TAG, path + " -> " + target.getPath());
        } catch (ErrnoException error) {
            if (LOG) Log.e(TAG, "Redirect failed for " + path, error);
        }
    }

    /** Moves every entry of dir into target (existing entries in target win), then removes dir. */
    private static boolean moveContents(File dir, File target) {
        File[] entries = dir.listFiles();
        if (entries != null) {
            for (File f : entries) {
                File dst = new File(target, f.getName());
                if (dst.exists()) {
                    if (!deleteTree(f)) return false;
                } else if (!f.renameTo(dst) && !(copyTree(f, dst) && deleteTree(f))) {
                    return false;
                }
            }
        }
        return dir.delete();
    }

    private static boolean copyTree(File src, File dst) {
        if (src.isDirectory()) {
            if (!dst.mkdirs() && !dst.isDirectory()) return false;
            File[] entries = src.listFiles();
            if (entries != null) {
                for (File f : entries) {
                    if (!copyTree(f, new File(dst, f.getName()))) return false;
                }
            }
            return true;
        }
        try (java.io.InputStream in = new java.io.FileInputStream(src);
             java.io.OutputStream out = new java.io.FileOutputStream(dst)) {
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return true;
        } catch (java.io.IOException e) {
            if (LOG) Log.e(TAG, "copy " + src + " failed", e);
            return false;
        }
    }

    private static boolean deleteTree(File f) {
        File[] entries = f.isDirectory() ? f.listFiles() : null;
        if (entries != null) {
            for (File c : entries) deleteTree(c);
        }
        return f.delete();
    }

    private static String readLink(String path) {
        try {
            return Os.readlink(path);
        } catch (ErrnoException notALink) {
            return null;
        }
    }
}
