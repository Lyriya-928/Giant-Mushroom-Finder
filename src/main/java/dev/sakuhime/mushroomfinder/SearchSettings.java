package dev.sakuhime.mushroomfinder;

/** Immutable search parameters from the UI. */
public final class SearchSettings {
    public final long seed;
    public final String version;
    public final int radius;
    public final long minArea;
    public final int scale;
    public final boolean refine;
    public final boolean includeShore;
    public final int threads;
    /** Keep top-N by area; 0 = all. */
    public final int maxResults;
    /** 0=monolithic, 1=tiled, 2=auto (default). */
    public final int useTiles;
    /** Tile side in samples; 0 = auto. */
    public final int tileSize;

    public SearchSettings(long seed, String version, int radius, long minArea,
                          int scale, boolean refine, boolean includeShore) {
        this(seed, version, radius, minArea, scale, refine, includeShore, 1, 0, 2, 0);
    }

    public SearchSettings(long seed, String version, int radius, long minArea,
                          int scale, boolean refine, boolean includeShore,
                          int threads, int maxResults) {
        this(seed, version, radius, minArea, scale, refine, includeShore,
                threads, maxResults, 2, 0);
    }

    public SearchSettings(long seed, String version, int radius, long minArea,
                          int scale, boolean refine, boolean includeShore,
                          int threads, int maxResults, int useTiles, int tileSize) {
        this.seed = seed;
        this.version = version;
        this.radius = radius;
        this.minArea = Math.max(0, minArea);
        this.scale = scale;
        this.refine = refine;
        this.includeShore = includeShore;
        this.threads = threads < 1 ? 1 : threads;
        this.maxResults = Math.max(0, maxResults);
        this.useTiles = useTiles;
        this.tileSize = tileSize;
    }
}
