/*
 * Giant Mushroom Island Finder — native search core
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
#define GMIF_VERSION "1.0.0"

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

typedef struct SearchParams {
    int64_t seed;          /* Minecraft Java seed */
    int     mc;            /* cubiomes MCVersion enum value */
    int     radius;        /* search half-size in blocks around origin ([-R,R]) */
    int64_t min_area;      /* min estimated area in blocks^2; 0 = no filter */
    int     scale;         /* coarse sample scale (16 recommended) */
    int     refine;        /* 0 = coarse only, 1 = refine candidates */
    int     include_shore; /* if nonzero, treat shore as part of island */
    int     threads;       /* sampling threads (OpenMP) */
    int     max_results;   /* keep top-N by area after sort; 0 = all */
    /* 0 = monolithic (legacy full-grid), 1 = tiled streaming, 2 = auto */
    int     use_tiles;
    /* Tile side in sample cells; 0 = auto. Used when use_tiles != 0. */
    int     tile_size;
} SearchParams;

typedef struct MushroomIsland {
    int     center_x, center_z;
    int     min_x, max_x, min_z, max_z;
    int     width, height;
    double  diagonal_span; /* sqrt(w^2 + h^2) */
    int64_t area;          /* estimated area in blocks^2 (sample-based) */
    int64_t samples;       /* number of positive samples */
    int     area_precision;/* 0=coarse estimate, 1=refined scale4, 2=scale1 */
    double  distance;      /* Euclidean distance from (0,0) */
    double  compactness;   /* 4*pi*A / P^2 estimate */
    int     perimeter;     /* estimated perimeter in blocks */
} MushroomIsland;

typedef struct SearchProgress {
    int     percent;       /* 0..100 */
    const char *stage;     /* "scan", "connect", "refine", "done" */
    int64_t found;         /* islands found so far */
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
    volatile int stage;   /* 0 scan, 1 connect, 2 refine, 3 done */
    volatile int64_t found;
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
 * Returns -1 on allocation/setup failure, -2 if cancelled/stopped.
 *
 * `control` may be NULL. `cancel` may be NULL.
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
 * Compute coarse grid geometry without running a search.
 * Returns 0 on success. On failure sets last error and returns a GMIF_ERR_*.
 * Outputs: half (samples per side), sx/sz (grid size), cells, mask_bytes, biome_bytes.
 */
int gmif_describe_grid(int radius, int scale,
                       int *out_half, int *out_sx, int *out_sz,
                       int64_t *out_cells,
                       int64_t *out_mask_bytes, int64_t *out_biome_bytes);

/**
 * Sample biome at a world block position via Cubiomes (scale=1, y=63).
 * Returns biome id, or -1 on failure.
 */
int gmif_sample_biome(int64_t seed, int mc, int x, int z);

/**
 * Tiled streaming search: processes the range in tiles (constant memory)
 * and merges regions across tile borders with Union-Find.
 * Same output contract as gmif_search.
 *
 * Architecture note: tile-local CCA + global DSU merge. No Cubiomes changes.
 * Inspired by multi-stage region search patterns (architecture only).
 */
int gmif_search_tiled(const SearchParams *params,
                      MushroomIsland **out, int *out_count,
                      ProgressFn progress, CancelFn cancel, GmifControl *control,
                      void *userdata);

/** Run the release-candidate audit suite. Returns number of failures. */
int gmif_run_release_audit(void);

/**
 * Fill a mushroom mask for a sample-grid window.
 *
 * When scale > 4, samples at biome scale 4 (stable) and decimates so that
 * monolithic and tiled layouts see identical cell values. Cubiomes' scale>4
 * fast path is layout-dependent and cannot be used for exact equivalence.
 *
 * Window: sample cells [ox, ox+sx) x [oz, oz+sz) in scaled units of `scale`.
 * mask must hold sx*sz bytes. Returns 0 or GMIF_ERR_*.
 */
int gmif_fill_mask_range(const void *generator, int scale,
                         int ox, int oz, int sx, int sz,
                         int include_shore, unsigned char *mask);

/**
 * Refine one island's area/bounds/perimeter at finer scale (4 or 1)
 * using a padded bounding box. Updates `isl` in place.
 * Returns 1 if refined, 0 if skipped (bbox too large), <0 on error.
 */
int gmif_refine_island(const void *generator, int coarse_scale, int fine_scale,
                       int include_shore, MushroomIsland *isl);

/** Nonzero if biome id is mushroom_fields (or shore if include_shore). */
int gmif_is_mushroom(int biome_id, int include_shore);

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
