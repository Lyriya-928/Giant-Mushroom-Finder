package dev.sakuhime.mushroomfinder;

/** Immutable search parameters from the UI. */
public final class SearchSettings {
    public final long seed;
    public final String version;
    public final int centerX;
    public final int centerZ;
    public final int radius;
    public final long minArea;
    public final int scale;
    public final boolean refine;
    public final boolean includeShore;
    /**
     * Drop an island when lush caves generate below it. Only applies to
     * mushroom-fields targets on 1.18+.
     */
    public final boolean excludeLushCaves;
    /** 0 = automatic (physical core count). */
    public final int threads;
    /** Keep top-N by area; 0 = all. */
    public final int maxResults;
    /** 0=monolithic, 1=tiled, 2=auto (default). */
    public final int useTiles;
    /** Tile side in samples; 0 = auto. */
    public final int tileSize;
    /** Target biome ids; empty/null keeps the mushroom-fields default. */
    public final int[] biomes;

    public SearchSettings(long seed, String version, int radius, long minArea,
                          int scale, boolean refine, boolean includeShore) {
        this(seed, version, 0, 0, radius, minArea, scale, refine, includeShore,
                false, 0, 10, 2, 0, null);
    }

    public SearchSettings(long seed, String version, int radius, long minArea,
                          int scale, boolean refine, boolean includeShore,
                          int threads, int maxResults) {
        this(seed, version, 0, 0, radius, minArea, scale, refine, includeShore,
                false, threads, maxResults, 2, 0, null);
    }

    /** Full form without the lush caves option (kept for older callers). */
    public SearchSettings(long seed, String version, int centerX, int centerZ,
                          int radius, long minArea, int scale,
                          boolean refine, boolean includeShore,
                          int threads, int maxResults, int useTiles, int tileSize,
                          int[] biomes) {
        this(seed, version, centerX, centerZ, radius, minArea, scale, refine,
                includeShore, false, threads, maxResults, useTiles, tileSize, biomes);
    }

    public SearchSettings(long seed, String version, int centerX, int centerZ,
                          int radius, long minArea, int scale,
                          boolean refine, boolean includeShore,
                          boolean excludeLushCaves,
                          int threads, int maxResults, int useTiles, int tileSize,
                          int[] biomes) {
        this.seed = seed;
        this.version = version;
        this.centerX = centerX;
        this.centerZ = centerZ;
        this.radius = radius;
        this.minArea = Math.max(0, minArea);
        this.scale = scale;
        this.refine = refine;
        this.includeShore = includeShore;
        this.excludeLushCaves = excludeLushCaves;
        this.threads = threads < 0 ? 0 : threads;
        this.maxResults = Math.max(0, maxResults);
        this.useTiles = (useTiles < 0 || useTiles > 2) ? 2 : useTiles;
        this.tileSize = tileSize;
        this.biomes = biomes == null ? null : biomes.clone();
    }
}
