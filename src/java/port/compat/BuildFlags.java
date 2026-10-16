package port.compat;

/** Build switches. work/tools/build_port.py --release compiles this class with RELEASE = true. */
public final class BuildFlags {
    private BuildFlags() {
    }

    /** Release build: no log output from the port's Java code (and libroomfix mutes the process). */
    public static final boolean RELEASE = false;
}
