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
            loadError = t.getMessage() != null ? t.getMessage() : String.valueOf(t);
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
            long seed, String version, int centerX, int centerZ,
            int radius, long minArea,
            int scale, boolean refine, boolean includeShore,
            long controlPtr, int threads, int maxResults, int[] biomeIds);

    /** out[0]=percent, out[1]=found, out[2]=stage, out[3]=mode (0 mono / 1 tiled). */
    public static native void nativePollProgress(long controlPtr, int[] out4);

    /** Physical-core suggestion (0 is also accepted by nativeSearch). */
    public static native int nativeDefaultThreads();

    /** Native radius ceiling for a sample scale (world-border capped). */
    public static native int nativeMaxRadius(int scale);

    public static int maxRadius(int scale) {
        try {
            return nativeMaxRadius(scale);
        } catch (Throwable t) {
            return 0;
        }
    }

    public static native String nativeVersionString(int mc);
    public static native int nativeVersionId(String version);
    public static native String[] nativeKnownVersions();
    /** Biome id at a block position (scale 1, y=63), or -1 on failure. */
    public static native int nativeSampleBiome(long seed, String version, int x, int z);

    public static native long nativeCreateControl();
    public static native void nativePause(long controlPtr);
    public static native void nativeResume(long controlPtr);
    public static native void nativeStop(long controlPtr);
    public static native void nativeDestroyControl(long controlPtr);

    /** Last native error message (empty if none). */
    public static native String nativeGetLastError();
    /** Last native error code (0 = ok). */
    public static native int nativeGetLastErrorCode();

    /** [sx, sz, cells, maskBytes] or null on failure. */
    public static native long[] nativeDescribeGrid(int centerX, int centerZ,
                                                   int radius, int scale);

    /**
     * [mode, tileSize, threads, sx, sz, cells, estSamples, estPeakBytes, scale]
     * or null with the reason in nativeGetLastError().
     */
    private static native long[] nativePlanSearch(String version,
                                                  int centerX, int centerZ,
                                                  int radius, int scale,
                                                  int useTiles, int tileSize,
                                                  int threads);

    public static final int RESULT_STRIDE = 15;

    public static final int ERR_OK = 0;
    public static final int ERR_INVALID_ARG = -1;
    public static final int ERR_CANCELLED = -2;
    public static final int ERR_ALLOC = -3;
    public static final int ERR_VERSION = -4;
    public static final int ERR_GRID_TOO_LARGE = -5;
    public static final int ERR_OVERFLOW = -6;
    public static final int ERR_BIOME_GEN = -7;

    /** Resolved native search plan, used for the pre-search estimate. */
    public record SearchPlan(boolean tiled, int tileSize, int threads,
                             long sx, long sz, long cells,
                             long estSamples, long estBytes, int scale) {
        public double estMb() {
            return estBytes / (1024.0 * 1024.0);
        }

        public String gridText() {
            return sx + " × " + sz;
        }
    }

    /** Returns null when the plan is invalid; message in last error. */
    public static SearchPlan plan(String version, int centerX, int centerZ,
                                  int radius, int scale, int useTiles,
                                  int tileSize, int threads) {
        long[] a = nativePlanSearch(version, centerX, centerZ, radius, scale,
                useTiles, tileSize, threads);
        if (a == null || a.length < 9) return null;
        return new SearchPlan(a[0] != 0, (int) a[1], (int) a[2],
                a[3], a[4], a[5], a[6], a[7], (int) a[8]);
    }

    public static int defaultThreads() {
        try {
            return Math.max(1, nativeDefaultThreads());
        } catch (Throwable t) {
            return Math.max(1, Runtime.getRuntime().availableProcessors());
        }
    }
}
