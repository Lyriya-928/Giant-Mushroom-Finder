/*
 * Large Biome Finder — native search core
 *
 * Search architecture inspired by:
 *   SunnySlopes/RiverFinderGUI
 *   https://github.com/SunnySlopes/RiverFinderGUI
 *
 *   SunnySlopes/FortressFinderGUI
 *   https://github.com/SunnySlopes/FortressFinderGUI
 *
 * No source code copied from those projects. Cubiomes (MIT) is used for
 * Minecraft biome generation.
 */
#ifndef MUSHROOM_FINDER_H_
#define MUSHROOM_FINDER_H_

/** Application version (keep in sync with /VERSION and Java AppInfo). */
#define GMIF_VERSION "1.2.0"

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sampling scales supported by cubiomes Range */
enum {
    GMIF_SCALE_BLOCK = 1,
    GMIF_SCALE_BIOME = 4,
    GMIF_SCALE_CHUNK = 16,
    GMIF_SCALE_QUAD  = 64
};

/**
 * Automatic tile threshold, in coarse sample cells.
 *
 * Measured on Windows (i5-13490F, 16 logical / 10 physical cores), seed 262,
 * scale 16, no refine:
 *   - monolithic r=5000 (0.39 M cells): 2.7 s at 1 thread, 0.35 s at 16;
 *     peak working set 6 MB (was 29 MB before the CCA rewrite);
 *   - monolithic r=15000 (3.5 M cells): 30.7 s at 1 thread, 3.4 s at 16;
 *     peak working set 9 MB;
 *   - tiled r=30000 (14 M cells, tile 256): 16–22 s at 10 threads, peak 6 MB.
 *
 * Monolithic memory is ≈ 4 B per coarse cell plus one fill band per worker,
 * so 12 M cells predicts ≈ 50–60 MB peak, while tiled stays at 6–30 MB for
 * any radius. 12 M cells is therefore the point where the full-grid path
 * stops being the safer choice; at scale 16 that is a radius of ≈ 27,700
 * blocks, at scale 4 ≈ 6,900 blocks.
 */
#define GMIF_MONO_MAX_CELLS 12000000LL

/**
 * Hard stop for tiled work. The row-streaming merge keeps border memory
 * independent of the tile count, so the practical ceiling is time rather than
 * RAM. 10e9 coarse cells is 5x the previous limit; at scale 16 it is a radius
 * of 799,984 blocks (the GUI shows this value). The plan resolves the actual
 * tile size and rejects unsupported radii before any allocation.
 */
#define GMIF_TILED_MAX_CELLS 10000000000LL
#define GMIF_TILED_MAX_TILES 2000000LL
typedef struct SearchParams {
    int64_t seed;          /* Minecraft Java seed */
    int     mc;            /* cubiomes MCVersion enum value */
    int     center_x;      /* search center X in blocks */
    int     center_z;      /* search center Z in blocks */
    int     radius;        /* search half-size in blocks around center */
    int64_t min_area;      /* min estimated area in blocks^2; 0 = no filter */
    int     scale;         /* coarse sample scale (16 recommended) */
    int     refine;        /* 0 = coarse only, 1 = refine candidates */
    int     include_shore; /* legacy: also count mushroom_field_shore */
    /**
     * 1 = drop an island entirely when lush caves generate below its
     * footprint. Applies only when the target set contains mushroom_fields
     * (id 14) and the Minecraft version has 3D biome generation (1.18+).
     */
    int     exclude_lush_caves;
    int     threads;       /* worker threads; 0 = auto (physical cores) */
    int     max_results;   /* keep top-N by area after sort; 0 = all */
    /* 0 = monolithic (legacy full-grid), 1 = tiled streaming, 2 = auto */
    int     use_tiles;
    /* Tile side in sample cells; 0 = auto. Used when use_tiles != 0. */
    int     tile_size;
    /**
     * Optional target biome set (cubiomes biome ids, 0..255). When NULL or
     * empty the historical behaviour is used: mushroom_fields, plus
     * mushroom_field_shore when include_shore is set.
     */
    const int *biome_ids;
    int     biome_count;
} SearchParams;

typedef struct MushroomIsland {
    int     center_x, center_z;
    int     min_x, max_x, min_z, max_z;
    int     width, height;
    double  diagonal_span; /* sqrt(w^2 + h^2) */
    int64_t area;          /* estimated area in blocks^2 (sample-based) */
    int64_t samples;       /* number of positive samples */
    int     area_precision;/* 0=coarse estimate, 1=refined scale4, 2=scale1 */
    double  distance;      /* Euclidean distance from the search center */
    double  compactness;   /* 4*pi*A / P^2 estimate */
    int     perimeter;     /* estimated perimeter in blocks */
} MushroomIsland;

typedef struct SearchProgress {
    int     percent;       /* 0..100 */
    const char *stage;     /* "scan", "tile", "connect", "refine", "done" */
    int64_t found;         /* islands found so far */
    int     mode;          /* 0=monolithic, 1=tiled */
} SearchProgress;

typedef void (*ProgressFn)(const SearchProgress *p, void *userdata);
typedef int  (*CancelFn)(void *userdata); /* return nonzero to cancel */

/**
 * Shared control block for pause / resume / stop.
 * The search thread checks these periodically. Written from another thread
 * (e.g. Java UI) using the gmif_control_* helpers.
 */
typedef struct GmifControl {
    volatile int pause;   /* nonzero = paused */
    volatile int stop;    /* nonzero = stop requested */
    /* Progress snapshot (written by search thread, read by UI). */
    volatile int percent;
    volatile int stage;   /* 0 scan, 1 connect, 2 refine, 3 done, 4 tile, 5 merge */
    volatile int64_t found;
    volatile int mode;    /* 0=monolithic, 1=tiled */
} GmifControl;

void gmif_control_init(GmifControl *c);
void gmif_control_pause(GmifControl *c);
void gmif_control_resume(GmifControl *c);
void gmif_control_stop(GmifControl *c);
/** Poll while paused; returns nonzero if stop was requested. */
int  gmif_control_wait_if_paused(GmifControl *c);

/**
 * Run a mushroom-fields island search.
 *
 * On success returns 0 and stores results in *out (malloc'd array of
 * *out_count islands, sorted by area descending). Caller must free(*out).
 * Returns negative GMIF_ERR_* on failure (-2 if cancelled/stopped).
 *
 * `control` may be NULL. `cancel` may be NULL. `params->threads == 0`
 * selects the physical-core default.
 *
 * When `params->exclude_lush_caves` is set and the target set contains
 * mushroom_fields, every candidate island is additionally checked for lush
 * caves below its footprint (see gmif_island_has_lush_caves) and dropped as a
 * whole when one is found. Other targets are unaffected.
 */
int gmif_search(const SearchParams *params,
                MushroomIsland **out, int *out_count,
                ProgressFn progress, CancelFn cancel, GmifControl *control,
                void *userdata);

/** Map version string like "1.21.3" to cubiomes MCVersion, or -1. */
int gmif_mc_from_string(const char *s);

/** Human-readable version string for an MCVersion. */
const char *gmif_mc_to_string(int mc);

/** List known version strings (null-terminated). */
const char * const *gmif_known_versions(void);

/* Error codes from gmif_search / helpers. 0 = success. */
enum {
    GMIF_OK = 0,
    GMIF_ERR_INVALID_ARG   = -1,
    GMIF_ERR_CANCELLED     = -2,
    GMIF_ERR_ALLOC         = -3,
    GMIF_ERR_VERSION       = -4,
    GMIF_ERR_GRID_TOO_LARGE= -5,
    GMIF_ERR_OVERFLOW      = -6,
    GMIF_ERR_BIOME_GEN     = -7,
    GMIF_ERR_UNKNOWN       = -99
};

/** Last error message (empty string if none). Not thread-safe. */
const char *gmif_last_error(void);

/** Last error code (0 if none). */
int gmif_last_error_code(void);

/** Set last error (for library-internal modules). Returns `code`. */
int gmif_set_error(int code, const char *fmt, ...);

/**
 * Recommended worker count: physical cores on Windows, available CPUs
 * elsewhere. Never returns less than 1.
 */
int gmif_default_threads(void);

/**
 * Maximum radius (blocks) accepted by the tiled planner for a sample scale.
 * Derived from GMIF_TILED_MAX_CELLS and capped by the world border. This is
 * the value the GUI should display as the real capability of the build.
 */
int gmif_max_radius(int scale);

/** Search plan produced before touching Cubiomes. */
typedef struct GmifSearchPlan {
    int     scale;
    int     center_x, center_z;
    int64_t world_x0, world_z0; /* first sampled cell origin, in blocks */
    int64_t cell_x0, cell_z0;   /* first sampled cell, in scaled units */
    int64_t sx, sz;             /* coarse grid size in sample cells */
    int64_t cells;              /* sx * sz */
    int     use_tiles;          /* resolved 0/1 */
    int     tile_size;          /* resolved tile side, in samples */
    int     threads;            /* resolved worker count */
    int64_t est_samples;        /* approximate biome samples required */
    int64_t est_peak_bytes;     /* approximate peak working set */
} GmifSearchPlan;

/**
 * Resolve the coarse grid + scan mode + thread count for a search.
 * Returns 0 on success (plan filled), or a GMIF_ERR_* with a message.
 * Passing NULL for `out` is a validation-only call.
 */
int gmif_plan_search(const SearchParams *params, GmifSearchPlan *out);

/**
 * Coarse grid geometry without running a search.
 * Returns 0 on success. On failure sets last error and returns a GMIF_ERR_*.
 * Origin/size are in scaled sample units; sx/sz may be large for tiled search.
 */
int gmif_describe_grid(int center_x, int center_z, int radius, int scale,
                       int *out_sx, int *out_sz,
                       int64_t *out_cell_x0, int64_t *out_cell_z0,
                       int64_t *out_cells, int64_t *out_mask_bytes);

/**
 * Sample biome at a world block position via Cubiomes (scale=1, y=63).
 * Returns biome id, or -1 on failure.
 */
int gmif_sample_biome(int64_t seed, int mc, int x, int z);

/**
 * Tiled streaming search: processes the range in tiles (constant memory)
 * and merges regions across tile borders with Union-Find.
 * Same output contract as gmif_search.
 */
int gmif_search_tiled(const SearchParams *params,
                      MushroomIsland **out, int *out_count,
                      ProgressFn progress, CancelFn cancel, GmifControl *control,
                      void *userdata);

/** Run the release-candidate audit suite. Returns number of failures. */
int gmif_run_release_audit(void);

/**
 * Fill a target mask for a sample-grid window.
 *
 * The mask is filled in parallel over row bands.  For MC >= 1.18 and
 * scale > 4 the centre sample of each coarse cell is evaluated directly
 * (exactly the same value the stable scale-4 decimation produced), which
 * avoids generating the other 15 sub-samples.  For older versions the
 * scale-4 window is generated and decimated, still in parallel.
 *
 * Window: sample cells [ox, ox+sx) x [oz, oz+sz) in scaled units of `scale`.
 * mask must hold sx*sz bytes.  `ctl` may be NULL; when given, pause/stop and
 * progress in [pct_from, pct_to] are honoured.
 */
int gmif_fill_mask_range(const void *generator, const SearchParams *params,
                         int scale, int64_t ox, int64_t oz,
                         int sx, int sz, unsigned char *mask,
                         GmifControl *ctl, int pct_from, int pct_to);

/**
 * Refine one island's area/bounds/perimeter at finer scale (4 or 1)
 * using a padded bounding box. Updates `isl` in place.
 * Returns 1 if refined, 0 if skipped (bbox too large), <0 on error.
 */
int gmif_refine_island(const void *generator, const SearchParams *params,
                       int coarse_scale, int fine_scale, MushroomIsland *isl);

/** Nonzero if biome id is part of the search target set. */
int gmif_is_target(int biome_id, const SearchParams *params);

/** Nonzero if biome id is mushroom_fields (or shore if include_shore). */
int gmif_is_mushroom(int biome_id, int include_shore);

/* ------------------------------------------------------------------ */
/* lush caves exclusion (MC 1.18+)                                     */
/* ------------------------------------------------------------------ */

/** Cubiomes id of the lush caves biome (1.18+). */
#define GMIF_BIOME_LUSH_CAVES 175

/** Nonzero if the MC version has 3D (underground) biome generation. */
int gmif_supports_lush_caves(int mc);

/** Nonzero if the search target set contains mushroom_fields (id 14). */
int gmif_targets_mushroom(const SearchParams *params);

/** Horizontal/vertical grid used by the lush caves check (documentation). */
#define GMIF_LUSH_SAMPLE_SCALE 4
#define GMIF_LUSH_Y4_MIN (-16) /* block y = -64 */
#define GMIF_LUSH_Y4_MAX (79)  /* block y = 316..319 */

/**
 * Check whether lush caves (id 175) generate below an island footprint.
 *
 * The footprint is rebuilt at scale 4 (4x4 blocks per column) from the same
 * target mask as the search, so only columns that belong to the island are
 * tested. Each column is sampled over the full underground height
 * (block y -64..319) in 4-block steps, i.e. at the native vertical resolution
 * of the 1.18+ biome noise. Columns are processed in row bands, so the working
 * set is bounded (no full 3D array is ever allocated).
 *
 * Returns 1 if lush caves were found (the island must be dropped), 0 if the
 * island is clean, or a negative GMIF_ERR_* value (GMIF_ERR_VERSION before
 * 1.18, GMIF_ERR_CANCELLED when paused/stopped). `control` may be NULL.
 */
int gmif_island_has_lush_caves(const void *generator, const SearchParams *params,
                               const MushroomIsland *island,
                               GmifControl *control, CancelFn cancel,
                               void *userdata);

/**
 * Dump an ASCII map of mushroom coverage for a world rect.
 * step is the block stride between samples. Writes to `out` (size sx*sz+1).
 * Returns number of mushroom samples.
 */
int64_t gmif_dump_ascii_map(int64_t seed, int mc,
                            int x0, int z0, int x1, int z1,
                            int step, char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* MUSHROOM_FINDER_H_ */
