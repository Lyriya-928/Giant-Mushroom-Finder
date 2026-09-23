package dev.sakuhime.mushroomfinder;

/**
 * JNI bindings to the native cubiomes-backed search library (gmif).
 *
 * Search lifecycle pattern inspired by SunnySlopes FortressFinderGUI /
 * RiverFinderGUI. No source code copied from those projects.
 */
public final class NativeBridge {
    private static boolean loaded = false;
    private static String loadError = null;

    static {
        try {
            NativeLoader.load();
            loaded = true;
        } catch (Throwable t) {
            loadError = String.valueOf(t.getMessage());
            loaded = false;
        }
    }

    private NativeBridge() {}

    public static boolean isLoaded() {
        return loaded;
    }

    public static String getLoadError() {
        return loadError;
    }

    /** Returns flat double[count*RESULT_STRIDE] or null on failure/cancel. */
    public static native double[] nativeSearch(
            long seed, String version, int radius, long minArea,
            int scale, boolean refine, boolean includeShore,
            long controlPtr, int threads, int maxResults);

    /** out[0]=percent, out[1]=found, out[2]=stage. */
    public static native void nativePollProgress(long controlPtr, int[] out3);

    public static native String nativeVersionString(int mc);
    public static native int nativeVersionId(String version);
    public static native String[] nativeKnownVersions();

    public static native long nativeCreateControl();
    public static native void nativePause(long controlPtr);
    public static native void nativeResume(long controlPtr);
    public static native void nativeStop(long controlPtr);
    public static native void nativeDestroyControl(long controlPtr);

    /** Last native error message (empty if none). */
    public static native String nativeGetLastError();
    /** Last native error code (0 = ok). */
    public static native int nativeGetLastErrorCode();
    /** [half, sx, sz, cells, biomeBytes] or null on failure. */
    public static native long[] nativeDescribeGrid(int radius, int scale);

    public static final int RESULT_STRIDE = 15;

    public static final int ERR_OK = 0;
    public static final int ERR_INVALID_ARG = -1;
    public static final int ERR_CANCELLED = -2;
    public static final int ERR_ALLOC = -3;
    public static final int ERR_VERSION = -4;
    public static final int ERR_GRID_TOO_LARGE = -5;
    public static final int ERR_OVERFLOW = -6;
    public static final int ERR_BIOME_GEN = -7;
}
