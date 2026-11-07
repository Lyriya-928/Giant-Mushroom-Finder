/*
 * Large Biome Finder — native search core
 *
 * Uses Cubiomes (MIT) for Minecraft Java Edition biome generation:
 *   https://github.com/Cubitect/cubiomes
 *
 * Search architecture inspired by SunnySlopes RiverFinderGUI /
 * FortressFinderGUI (Java + JNI + native worker pattern).
 * No source code copied from those projects.
 */
#include "mushroom_finder.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#define gmif_sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#define gmif_sleep_ms(ms) usleep((ms) * 1000)
#endif

#ifdef _OPENMP
#include <omp.h>
#endif

#include "generator.h"
#include "biomenoise.h"
#include "util.h"

/* ------------------------------------------------------------------ */
/* last-error diagnostics                                              */
/* ------------------------------------------------------------------ */

static char g_last_error[768];
static int g_last_error_code = 0;

const char *gmif_last_error(void)
{
    return g_last_error;
}

int gmif_last_error_code(void)
{
    return g_last_error_code;
}

static void gmif_clear_error(void)
{
    g_last_error[0] = 0;
    g_last_error_code = 0;
}

int gmif_set_error(int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, ap);
    va_end(ap);
    g_last_error_code = code;
    return code;
}

static int gmif_fail(int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, ap);
    va_end(ap);
    g_last_error_code = code;
    return code;
}

static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) q--;
    return q;
}

static int valid_scale(int scale)
{
    return scale == 1 || scale == 4 || scale == 16 || scale == 64 || scale == 256;
}

static int valid_biome_id(int id)
{
    return id >= 0 && id < 256;
}

int gmif_is_target(int biome_id, const SearchParams *p)
{
    if (!p) {
        return biome_id == mushroom_fields || biome_id == mushroom_field_shore;
    }
    if (p->biome_ids && p->biome_count > 0) {
        for (int i = 0; i < p->biome_count; i++) {
            if (p->biome_ids[i] == biome_id) return 1;
        }
        return 0;
    }
    if (biome_id == mushroom_fields) return 1;
    if (p->include_shore && biome_id == mushroom_field_shore) return 1;
    return 0;
}

int gmif_is_mushroom(int biome_id, int include_shore)
{
    if (biome_id == mushroom_fields) return 1;
    if (include_shore && biome_id == mushroom_field_shore) return 1;
    return 0;
}

/** Nonzero when the lush caves filter should run for this search. */
static int lush_filter_active(const SearchParams *p)
{
    return p && p->exclude_lush_caves && gmif_targets_mushroom(p);
}

/**
 * Coarse grid covers the full block box [center-radius, center+radius] and is
 * aligned to the sample scale.  Cells are aligned so that a search centred on
 * X=0 keeps the historical grid.
 */
int gmif_describe_grid(int center_x, int center_z, int radius, int scale,
                       int *out_sx, int *out_sz,
                       int64_t *out_cell_x0, int64_t *out_cell_z0,
                       int64_t *out_cells, int64_t *out_mask_bytes)
{
    gmif_clear_error();
    if (radius < 1) {
        return gmif_fail(GMIF_ERR_INVALID_ARG, "radius must be >= 1 (got %d)", radius);
    }
    if (!valid_scale(scale)) {
        return gmif_fail(GMIF_ERR_INVALID_ARG,
            "unsupported scale %d (use 1, 4, 16, 64 or 256)", scale);
    }
    if (center_x < -30000000 || center_x > 30000000
        || center_z < -30000000 || center_z > 30000000) {
        return gmif_fail(GMIF_ERR_INVALID_ARG,
            "search center outside the Minecraft world border: (%d, %d)",
            center_x, center_z);
    }
    int64_t x_lo = (int64_t)center_x - (int64_t)radius;
    int64_t x_hi = (int64_t)center_x + (int64_t)radius;
    int64_t z_lo = (int64_t)center_z - (int64_t)radius;
    int64_t z_hi = (int64_t)center_z + (int64_t)radius;
    if (x_lo < -30000000 || x_hi > 30000000
        || z_lo < -30000000 || z_hi > 30000000) {
        return gmif_fail(GMIF_ERR_INVALID_ARG,
            "search box [%lld,%lld]..[%lld,%lld] exceeds the Minecraft world border (+/-30000000).\n"
            "  Reduce the radius or move the search center.",
            (long long)x_lo, (long long)z_lo, (long long)x_hi, (long long)z_hi);
    }

    int64_t c0x = floor_div(x_lo, scale);
    int64_t c1x = floor_div(x_hi, scale) + 1;
    int64_t c0z = floor_div(z_lo, scale);
    int64_t c1z = floor_div(z_hi, scale) + 1;

    int64_t sx64 = c1x - c0x;
    int64_t sz64 = c1z - c0z;
    if (sx64 <= 0 || sz64 <= 0 || sx64 > INT32_MAX || sz64 > INT32_MAX) {
        return gmif_fail(GMIF_ERR_OVERFLOW,
            "grid size overflows 32-bit: %lld x %lld (radius=%d scale=%d)",
            (long long)sx64, (long long)sz64, radius, scale);
    }
    if (c0x < INT32_MIN || c0x > INT32_MAX || c0z < INT32_MIN || c0z > INT32_MAX) {
        return gmif_fail(GMIF_ERR_OVERFLOW,
            "sample origin overflows 32-bit: (%lld, %lld)", (long long)c0x, (long long)c0z);
    }

    int64_t cells64 = sx64 * sz64;

    if (out_sx) *out_sx = (int)sx64;
    if (out_sz) *out_sz = (int)sz64;
    if (out_cell_x0) *out_cell_x0 = c0x;
    if (out_cell_z0) *out_cell_z0 = c0z;
    if (out_cells) *out_cells = cells64;
    if (out_mask_bytes) *out_mask_bytes = cells64;
    return GMIF_OK;
}

int gmif_default_threads(void)
{
#ifdef _WIN32
    DWORD len = 0;
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len)
        || GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *buf =
            (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)malloc(len);
        if (buf) {
            int cores = 0;
            if (GetLogicalProcessorInformationEx(RelationProcessorCore, buf, &len)) {
                BYTE *p = (BYTE *)buf;
                BYTE *end = p + len;
                while (p < end) {
                    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *e =
                        (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)p;
                    if (e->Relationship == RelationProcessorCore) cores++;
                    p += e->Size;
                }
            }
            free(buf);
            if (cores > 0) return cores;
        }
    }
    {
        DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        if (n > 0) return (int)n;
    }
    return 1;
#elif defined(_OPENMP)
    return omp_get_num_procs();
#else
    return 1;
#endif
}

int gmif_max_radius(int scale)
{
    if (!valid_scale(scale)) return 0;
    int64_t side = (int64_t)sqrt((double)GMIF_TILED_MAX_CELLS);
    while ((side + 1) > 0 && (side + 1) <= 3037000499LL
           && (side + 1) * (side + 1) <= GMIF_TILED_MAX_CELLS) {
        side++;
    }
    while (side > 0 && side * side > GMIF_TILED_MAX_CELLS) side--;
    /* Worst-case grid span for a centre is 2*ceil(radius/scale)+1. */
    int64_t half = (side - 1) / 2;
    int64_t radius = half * (int64_t)scale;
    if (radius > 30000000LL) radius = 30000000LL;
    if (radius < 1) radius = 1;
    return (int)radius;
}

static int build_plan(const SearchParams *p, GmifSearchPlan *out)
{
    if (!p) return gmif_fail(GMIF_ERR_INVALID_ARG, "null search parameters");
    if (p->mc < 0) return gmif_fail(GMIF_ERR_VERSION, "invalid Minecraft version id %d", p->mc);

    int scale = p->scale > 0 ? p->scale : GMIF_SCALE_CHUNK;
    if (!valid_scale(scale)) {
        return gmif_fail(GMIF_ERR_INVALID_ARG,
            "unsupported scale %d (use 1, 4, 16, 64 or 256)", scale);
    }
    int max_radius = gmif_max_radius(scale);
    if (p->radius > max_radius) {
        return gmif_fail(GMIF_ERR_GRID_TOO_LARGE,
            "radius %d blocks exceeds the native search limit at scale %d.\n"
            "  maximum radius: %d blocks (about %lld coarse cells).\n"
            "  The limit is derived from int64 cell counting, int32 sample\n"
            "  coordinates and the tiled streaming buffers, not from the GUI.\n"
            "  Reduce the radius or use a coarser sample scale.",
            p->radius, scale, max_radius, (long long)GMIF_TILED_MAX_CELLS);
    }
    if (p->biome_count < 0 || p->biome_count > 256) {
        return gmif_fail(GMIF_ERR_INVALID_ARG, "invalid biome count %d", p->biome_count);
    }
    for (int i = 0; i < p->biome_count; i++) {
        if (!valid_biome_id(p->biome_ids[i])) {
            return gmif_fail(GMIF_ERR_INVALID_ARG,
                "invalid biome id %d (supported range 0..255)", p->biome_ids[i]);
        }
    }
    if (lush_filter_active(p) && !gmif_supports_lush_caves(p->mc)) {
        return gmif_fail(GMIF_ERR_VERSION,
            "excluding islands above lush caves requires Minecraft 1.18 or newer.\n"
            "  Selected version: %s. Underground (3D) biome generation was added\n"
            "  in 1.18; the 2D biome data of older versions cannot answer this\n"
            "  question, so the option is rejected instead of being faked.",
            gmif_mc_to_string(p->mc));
    }

    int sx = 0, sz = 0;
    int64_t cx0 = 0, cz0 = 0, cells = 0, mask_bytes = 0;
    int rc = gmif_describe_grid(p->center_x, p->center_z, p->radius, scale,
                                &sx, &sz, &cx0, &cz0, &cells, &mask_bytes);
    if (rc != GMIF_OK) return rc;

    GmifSearchPlan plan;
    memset(&plan, 0, sizeof(plan));
    plan.scale = scale;
    plan.center_x = p->center_x;
    plan.center_z = p->center_z;
    plan.cell_x0 = cx0;
    plan.cell_z0 = cz0;
    plan.world_x0 = cx0 * scale;
    plan.world_z0 = cz0 * scale;
    plan.sx = sx;
    plan.sz = sz;
    plan.cells = cells;
    plan.threads = p->threads > 0 ? p->threads : gmif_default_threads();
    if (plan.threads > 256) plan.threads = 256;
    if (plan.threads < 1) plan.threads = 1;

    int use_tiles = p->use_tiles;
    if (use_tiles < 0 || use_tiles > 2) use_tiles = 2;
    if (use_tiles == 2) {
        use_tiles = (cells > GMIF_MONO_MAX_CELLS) ? 1 : 0;
    }
    plan.use_tiles = use_tiles;

    int64_t factor = scale > 4 ? (scale / 4) : 1;
    int fast_samples = (p->mc >= MC_1_18 && scale >= 4);
    int64_t samples_per_cell = (fast_samples || scale <= 4) ? 1 : factor * factor;
    plan.est_samples = cells * samples_per_cell;
    int64_t per_thread_bytes = fast_samples ? (96 * 1024)
                                            : (3 * 1024 * 1024);

    if (use_tiles == 0) {
        if (cells > GMIF_MONO_MAX_CELLS) {
            return gmif_fail(GMIF_ERR_GRID_TOO_LARGE,
                "monolithic scan disabled for this range: %lld coarse cells.\n"
                "  limit for the full-grid path: %lld cells (about 60 MB peak).\n"
                "  Use automatic or tiled scanning (remove --no-tiles), or reduce the radius.",
                (long long)cells, (long long)GMIF_MONO_MAX_CELLS);
        }
        plan.tile_size = 0;
        plan.est_peak_bytes = cells * 4
                            + (int64_t)plan.threads * per_thread_bytes
                            + 6 * 1024 * 1024;
    } else {
        if (cells > GMIF_TILED_MAX_CELLS) {
            return gmif_fail(GMIF_ERR_GRID_TOO_LARGE,
                "search area is too large even for tiled streaming: %lld coarse cells.\n"
                "  limit: %lld cells. Estimated sampling work: %lld samples.\n"
                "  Reduce the radius (or use a coarser sample scale).",
                (long long)cells, (long long)GMIF_TILED_MAX_CELLS,
                (long long)plan.est_samples);
        }
        int tile = p->tile_size;
        if (tile <= 0) tile = (scale == 1) ? 128 : 256;
        if (tile < 8) tile = 8;
        if (tile > 2048) tile = 2048;
        /* Keep the per-tile index arrays bounded for very large searches. */
        for (;;) {
            int64_t ntx = (plan.sx + tile - 1) / tile;
            int64_t ntz = (plan.sz + tile - 1) / tile;
            int64_t ntiles = ntx * ntz;
            if (ntiles <= GMIF_TILED_MAX_TILES || tile >= 2048) break;
            tile *= 2;
            if (tile > 2048) tile = 2048;
        }
        {
            int64_t ntx = (plan.sx + tile - 1) / tile;
            int64_t ntz = (plan.sz + tile - 1) / tile;
            int64_t ntiles = ntx * ntz;
            if (ntiles > GMIF_TILED_MAX_TILES) {
                return gmif_fail(GMIF_ERR_GRID_TOO_LARGE,
                    "tile index would be too large: %lld tiles at tile=%d.\n"
                    "  limit: %lld tiles. Reduce the radius or increase the tile size.",
                    (long long)ntiles, tile, (long long)GMIF_TILED_MAX_TILES);
            }
            plan.est_peak_bytes = (int64_t)tile * tile * 9
                                + (int64_t)plan.threads * per_thread_bytes
                                + (int64_t)plan.sx * 40
                                + 16 * 1024 * 1024;
        }
        plan.tile_size = tile;
    }

    if (out) *out = plan;
    return GMIF_OK;
}

int gmif_plan_search(const SearchParams *params, GmifSearchPlan *out)
{
    gmif_clear_error();
    return build_plan(params, out);
}

/* ------------------------------------------------------------------ */
/* control + helpers                                                   */
/* ------------------------------------------------------------------ */

void gmif_control_init(GmifControl *c)
{
    if (!c) return;
    c->pause = 0;
    c->stop = 0;
    c->percent = 0;
    c->stage = 0;
    c->found = 0;
    c->mode = 0;
}

void gmif_control_pause(GmifControl *c)
{
    if (c) c->pause = 1;
}

void gmif_control_resume(GmifControl *c)
{
    if (c) c->pause = 0;
}

void gmif_control_stop(GmifControl *c)
{
    if (c) c->stop = 1;
}

int gmif_control_wait_if_paused(GmifControl *c)
{
    if (!c) return 0;
    while (c->pause && !c->stop) {
        gmif_sleep_ms(50);
    }
    return c->stop ? 1 : 0;
}

typedef struct SearchCtx {
    ProgressFn progress;
    CancelFn cancel;
    void *ud;
    GmifControl *control;
    int64_t found;
    int mode;
} SearchCtx;

static int should_stop(SearchCtx *ctx)
{
    if (!ctx) return 0;
    if (ctx->control) {
        if (ctx->control->stop) return 1;
        if (ctx->control->pause) {
            if (gmif_control_wait_if_paused(ctx->control)) return 1;
        }
    }
    if (ctx->cancel && ctx->cancel(ctx->ud)) return 1;
    return 0;
}

static int raw_should_stop(GmifControl *control, CancelFn cancel, void *ud)
{
    if (control) {
        if (control->stop) return 1;
        if (control->pause) {
            if (gmif_control_wait_if_paused(control)) return 1;
        }
    }
    if (cancel && cancel(ud)) return 1;
    return 0;
}

static void set_stage_code(GmifControl *control, const char *stage)
{
    if (!control || !stage) return;
    if (stage[0] == 't' && stage[1] == 'i') control->stage = 4;
    else if (stage[0] == 'm') control->stage = 5;
    else if (stage[0] == 's') control->stage = 0;
    else if (stage[0] == 'c') control->stage = 1;
    else if (stage[0] == 'r') control->stage = 2;
    else if (stage[0] == 'd') control->stage = 3;
}

static void report(SearchCtx *ctx, int percent, const char *stage)
{
    if (ctx && ctx->control) {
        ctx->control->percent = percent;
        ctx->control->found = ctx->found;
        ctx->control->mode = ctx->mode;
        set_stage_code(ctx->control, stage);
    }
    if (!ctx || !ctx->progress) return;
    SearchProgress sp;
    sp.percent = percent;
    sp.stage = stage;
    sp.found = ctx->found;
    sp.mode = ctx->mode;
    ctx->progress(&sp, ctx->ud);
}

int gmif_mc_from_string(const char *s)
{
    if (!s) return -1;
    int mc = str2mc(s);
    return mc == MC_UNDEF ? -1 : mc;
}

const char *gmif_mc_to_string(int mc)
{
    return mc2str(mc);
}

static const char *const kVersions[] = {
    "1.21.3", "1.21.1", "1.20", "1.19.4", "1.19.2",
    "1.18.2", "1.18", "1.17.1", "1.16.5", "1.16.1",
    "1.15.2", "1.14.4", "1.13.2", "1.12.2", "1.8.9",
    NULL
};

const char * const *gmif_known_versions(void)
{
    return kVersions;
}

static double dist_from_center(int x, int z, int cx, int cz)
{
    double dx = (double)x - (double)cx;
    double dz = (double)z - (double)cz;
    return sqrt(dx * dx + dz * dz);
}

/* ------------------------------------------------------------------ */
/* parallel mask fill                                                  */
/* ------------------------------------------------------------------ */

typedef struct FillJob {
    const Generator *g;
    const SearchParams *p;
    int scale;
    int64_t ox, oz;
    int sx, sz;
    unsigned char *mask;
    unsigned char hit[256];
    int threads;
    GmifControl *control;
    CancelFn cancel;
    void *ud;
    ProgressFn progress;
    int64_t found;
    int pct_from, pct_to;
    volatile int stop;
    volatile int err;
    int band_rows;
    int bands;
    int done;
} FillJob;

static int fill_should_stop(FillJob *j)
{
    return raw_should_stop(j->control, j->cancel, j->ud);
}

static void fill_report(FillJob *j, int done)
{
    if (j->pct_from < 0 || j->pct_to < j->pct_from) return;
    int span = j->pct_to - j->pct_from;
    int pct = j->pct_from;
    if (j->bands > 0) pct += (int)((int64_t)span * done / j->bands);
    if (pct > j->pct_to) pct = j->pct_to;
    if (j->control) {
        j->control->percent = pct;
        j->control->found = j->found;
        j->control->mode = 0;
        j->control->stage = 0;
    }
    if (j->progress) {
        SearchProgress sp;
        sp.percent = pct;
        sp.stage = "scan";
        sp.found = j->found;
        sp.mode = 0;
        j->progress(&sp, j->ud);
    }
}

/**
 * Direct centre sampling for MC >= 1.18.
 *
 * The historical "stable" mask decimated a scale-4 window: coarse cell c took
 * scale-4 cell (c * scale/4 + scale/8), i.e. the 4-block cell at the centre of
 * the coarse cell.  That is exactly sampleBiomeNoise(centre_x4, 15, centre_z4),
 * so generating the other 15 sub-samples was wasted work.  This path is
 * bit-for-bit identical to the old decimation (verified by --audit).
 */
static int fill_band_fast(FillJob *j, int z0, int z1)
{
    const int factor = j->scale / 4;
    const int mid = factor / 2;
    const int sx = j->sx;
    unsigned char *mask = j->mask;
    for (int z = z0; z < z1; z++) {
        int64_t z4 = (j->oz + z) * factor + mid;
        if (z4 < INT32_MIN || z4 > INT32_MAX) return GMIF_ERR_OVERFLOW;
        size_t row = (size_t)z * (size_t)sx;
        for (int x = 0; x < sx; x++) {
            int64_t x4 = (j->ox + x) * factor + mid;
            if (x4 < INT32_MIN || x4 > INT32_MAX) return GMIF_ERR_OVERFLOW;
            int id = sampleBiomeNoise(&j->g->bn, NULL, (int)x4, 15, (int)z4, NULL, 0);
            mask[row + (size_t)x] = (id >= 0 && id < 256) ? j->hit[id] : 0;
        }
    }
    return GMIF_OK;
}

/**
 * Cached path: generate a scale-4 (or scale-1) window and decimate its centre.
 * Used for MC <= 1.17 (layer stack) and for scale 1.
 */
static int fill_band_cached(FillJob *j, int z0, int z1)
{
    const int gen_scale = j->scale > 4 ? 4 : j->scale;
    const int factor = j->scale > 4 ? (j->scale / 4) : 1;
    const int mid = factor / 2;
    int64_t oxs = j->ox * factor;
    int64_t ozs = (j->oz + z0) * factor;
    int64_t fxs = (int64_t)j->sx * factor;
    int64_t fzs = (int64_t)(z1 - z0) * factor;
    if (oxs < INT32_MIN || oxs > INT32_MAX
        || ozs < INT32_MIN || ozs > INT32_MAX
        || fxs > INT32_MAX || fzs > INT32_MAX) {
        return GMIF_ERR_OVERFLOW;
    }
    Range r;
    r.scale = gen_scale;
    r.x = (int)oxs;
    r.z = (int)ozs;
    r.sx = (int)fxs;
    r.sz = (int)fzs;
    r.y = (gen_scale == 1) ? 63 : 15;
    r.sy = 1;

    int *ids = allocCache(j->g, r);
    if (!ids) {
        return gmif_set_error(GMIF_ERR_ALLOC,
            "allocation failed: sampling window %dx%d at scale %d (%.1f MB)",
            (int)fxs, (int)fzs, gen_scale, (double)fxs * fzs * 4.0 / (1024 * 1024));
    }
    if (genBiomes(j->g, ids, r) != 0) {
        free(ids);
        return gmif_set_error(GMIF_ERR_BIOME_GEN,
            "biome sampling failed (window %dx%d scale %d)", (int)fxs, (int)fzs, gen_scale);
    }
    for (int z = z0; z < z1; z++) {
        size_t row = (size_t)z * (size_t)j->sx;
        size_t srow = (size_t)((z - z0) * factor + mid) * (size_t)fxs;
        for (int x = 0; x < j->sx; x++) {
            int id = ids[srow + (size_t)(x * factor + mid)];
            j->mask[row + (size_t)x] = (id >= 0 && id < 256) ? j->hit[id] : 0;
        }
    }
    free(ids);
    return GMIF_OK;
}

static int fill_one_band(FillJob *j, int z0, int z1)
{
    if (j->g->mc >= MC_1_18 && j->scale >= 4) {
        return fill_band_fast(j, z0, z1);
    }
    return fill_band_cached(j, z0, z1);
}

int gmif_fill_mask_range(const void *generator, const SearchParams *params,
                         int scale, int64_t ox, int64_t oz,
                         int sx, int sz, unsigned char *mask,
                         GmifControl *ctl, int pct_from, int pct_to)
{
    const Generator *g = (const Generator *)generator;
    if (!g || !params || !mask || sx <= 0 || sz <= 0) {
        return gmif_set_error(GMIF_ERR_INVALID_ARG, "invalid mask fill arguments");
    }
    if (!valid_scale(scale)) {
        return gmif_set_error(GMIF_ERR_INVALID_ARG, "unsupported scale %d", scale);
    }

    FillJob j;
    memset(&j, 0, sizeof(j));
    j.g = g;
    j.p = params;
    j.scale = scale;
    j.ox = ox;
    j.oz = oz;
    j.sx = sx;
    j.sz = sz;
    j.mask = mask;
    j.threads = params->threads > 0 ? params->threads : gmif_default_threads();
    if (j.threads > 256) j.threads = 256;
    j.control = ctl;
    j.cancel = NULL;
    j.ud = NULL;
    j.progress = NULL;
    j.found = 0;
    j.pct_from = pct_from;
    j.pct_to = pct_to;

    for (int i = 0; i < 256; i++) j.hit[i] = 0;
    if (params->biome_ids && params->biome_count > 0) {
        for (int i = 0; i < params->biome_count; i++) {
            int id = params->biome_ids[i];
            if (id >= 0 && id < 256) j.hit[id] = 1;
        }
    } else {
        j.hit[mushroom_fields] = 1;
        if (params->include_shore) j.hit[mushroom_field_shore] = 1;
    }

    if (g->mc >= MC_1_18 && scale >= 4) {
        /* Enough row bands to keep a hybrid CPU (P+E cores) balanced, but
         * not so many that progress reporting dominates. */
        int target_bands = j.threads * 8;
        int br = sz / (target_bands > 0 ? target_bands : 1);
        if (br < 1) br = 1;
        if (br > 32) br = 32;
        j.band_rows = br;
    } else {
        int factor = scale > 4 ? (scale / 4) : 1;
        int64_t fx = (int64_t)sx * factor;
        int64_t bytes_per_row = fx * 4;
        int64_t target = 2LL * 1024 * 1024;
        int br = (int)(bytes_per_row > 0 ? (target / bytes_per_row) : sz);
        if (br < 1) br = 1;
        if (br > 64) br = 64;
        if (br > sz) br = sz;
        j.band_rows = br;
    }
    if (j.band_rows < 1) j.band_rows = 1;
    j.bands = (sz + j.band_rows - 1) / j.band_rows;

    int threads = j.threads;
    if (threads > j.bands) threads = j.bands;
    if (threads < 1) threads = 1;

#ifdef _OPENMP
    if (threads > 1 && j.bands > 1) {
        #pragma omp parallel for schedule(dynamic, 1) num_threads(threads)
        for (int b = 0; b < j.bands; b++) {
            if (j.stop) continue;
            if (fill_should_stop(&j)) { j.stop = 1; continue; }
            int z0 = b * j.band_rows;
            int z1 = z0 + j.band_rows;
            if (z1 > sz) z1 = sz;
            int rc = fill_one_band(&j, z0, z1);
            if (rc != GMIF_OK) {
                j.err = rc;
                j.stop = 1;
                continue;
            }
            int done;
            #pragma omp atomic capture
            done = ++j.done;
            if (j.pct_from >= 0 && (done == j.bands || (done & 3) == 0)) {
                #pragma omp critical(gmif_fill_report)
                fill_report(&j, done);
            }
        }
    } else
#endif
    {
        for (int b = 0; b < j.bands; b++) {
            if (fill_should_stop(&j)) return GMIF_ERR_CANCELLED;
            int z0 = b * j.band_rows;
            int z1 = z0 + j.band_rows;
            if (z1 > sz) z1 = sz;
            int rc = fill_one_band(&j, z0, z1);
            if (rc != GMIF_OK) return rc;
            j.done = b + 1;
            if (j.pct_from >= 0 && (j.done == j.bands || (j.done & 3) == 0)) {
                fill_report(&j, j.done);
            }
        }
    }

    if (j.err != GMIF_OK) return j.err;
    if (j.stop) return GMIF_ERR_CANCELLED;
    return GMIF_OK;
}

/* ------------------------------------------------------------------ */
/* connected components on the sample grid                             */
/* ------------------------------------------------------------------ */

typedef struct Component {
    int min_sx, max_sx, min_sz, max_sz;
    int64_t samples;
    int64_t edges;
} Component;

typedef struct CompVec {
    Component *a;
    int n;
    int cap;
} CompVec;

static int comp_push(CompVec *v, const Component *c)
{
    if (v->n >= v->cap) {
        int ncap = v->cap ? v->cap * 2 : 256;
        Component *na = (Component *)realloc(v->a, (size_t)ncap * sizeof(Component));
        if (!na) return GMIF_ERR_ALLOC;
        v->a = na;
        v->cap = ncap;
    }
    v->a[v->n++] = *c;
    return GMIF_OK;
}

static int push_cell(int **stack, int *cap, int *sp, int idx, uint64_t *visited)
{
    if (visited[(size_t)idx >> 6] & (1ULL << (idx & 63))) return GMIF_OK;
    if (*sp >= *cap) {
        int ncap = *cap * 2;
        int *ns = (int *)realloc(*stack, (size_t)ncap * sizeof(int));
        if (!ns) return GMIF_ERR_ALLOC;
        *stack = ns;
        *cap = ncap;
    }
    visited[(size_t)idx >> 6] |= 1ULL << (idx & 63);
    (*stack)[(*sp)++] = idx;
    return GMIF_OK;
}

/**
 * 4-neighbour connected components with a visited bitset and a growable DFS
 * stack.  Perimeter edges are counted in the same pass.  The old
 * implementation kept a full int label grid and rescanned the entire grid for
 * every component to count its perimeter (O(cells * components)); this pass is
 * O(cells) and uses less memory.
 */
static int extract_components(unsigned char *mask, int sx, int sz,
                              Component **out_comps, int *out_n,
                              SearchCtx *ctx)
{
    size_t cells = (size_t)sx * (size_t)sz;
    size_t words = (cells + 63) / 64;
    uint64_t *visited = (uint64_t *)calloc(words ? words : 1, sizeof(uint64_t));
    if (!visited) return gmif_fail(GMIF_ERR_ALLOC, "allocation failed: visited bitset");

    int stack_cap = 4096;
    int *stack = (int *)malloc((size_t)stack_cap * sizeof(int));
    if (!stack) {
        free(visited);
        return gmif_fail(GMIF_ERR_ALLOC, "allocation failed: component stack");
    }

    CompVec comps;
    memset(&comps, 0, sizeof(comps));

    int rc = GMIF_OK;
    for (int i = 0; i < (int)cells; i++) {
        if ((i & 0xFFF) == 0) {
            if (should_stop(ctx)) { rc = GMIF_ERR_CANCELLED; goto done; }
            int pct = 60 + (int)((int64_t)i * 10 / (int64_t)cells);
            if (pct > 70) pct = 70;
            report(ctx, pct, "connect");
        }
        if (!mask[i]) continue;
        if (visited[(size_t)i >> 6] & (1ULL << (i & 63))) continue;

        Component c;
        c.min_sx = c.max_sx = i % sx;
        c.min_sz = c.max_sz = i / sx;
        c.samples = 0;
        c.edges = 0;

        int sp = 0;
        rc = push_cell(&stack, &stack_cap, &sp, i, visited);
        if (rc != GMIF_OK) goto done;

        while (sp) {
            int idx = stack[--sp];
            int x = idx % sx;
            int z = idx / sx;
            c.samples++;

            if (x == 0 || !mask[idx - 1]) c.edges++;
            else {
                rc = push_cell(&stack, &stack_cap, &sp, idx - 1, visited);
                if (rc != GMIF_OK) goto done;
            }
            if (x == sx - 1 || !mask[idx + 1]) c.edges++;
            else {
                rc = push_cell(&stack, &stack_cap, &sp, idx + 1, visited);
                if (rc != GMIF_OK) goto done;
            }
            if (z == 0 || !mask[idx - sx]) c.edges++;
            else {
                rc = push_cell(&stack, &stack_cap, &sp, idx - sx, visited);
                if (rc != GMIF_OK) goto done;
            }
            if (z == sz - 1 || !mask[idx + sx]) c.edges++;
            else {
                rc = push_cell(&stack, &stack_cap, &sp, idx + sx, visited);
                if (rc != GMIF_OK) goto done;
            }

            if (x < c.min_sx) c.min_sx = x;
            if (x > c.max_sx) c.max_sx = x;
            if (z < c.min_sz) c.min_sz = z;
            if (z > c.max_sz) c.max_sz = z;
        }

        rc = comp_push(&comps, &c);
        if (rc != GMIF_OK) goto done;
    }

done:
    free(stack);
    free(visited);
    if (rc == GMIF_ERR_ALLOC) {
        free(comps.a);
        return gmif_fail(GMIF_ERR_ALLOC, "allocation failed while growing component list");
    }
    if (rc != GMIF_OK) {
        free(comps.a);
        return rc;
    }
    *out_comps = comps.a;
    *out_n = comps.n;
    return GMIF_OK;
}

/* ------------------------------------------------------------------ */
/* refine                                                              */
/* ------------------------------------------------------------------ */

static void finalize_island(MushroomIsland *isl, const SearchParams *p,
                            int64_t area, int64_t samples, int precision)
{
    isl->area = area;
    isl->samples = samples;
    isl->area_precision = precision;
    isl->width = isl->max_x - isl->min_x + 1;
    isl->height = isl->max_z - isl->min_z + 1;
    isl->diagonal_span = sqrt((double)isl->width * isl->width
                            + (double)isl->height * isl->height);
    isl->distance = dist_from_center(isl->center_x, isl->center_z,
                                     p->center_x, p->center_z);
    {
        double A = (double)isl->area;
        double P = (double)isl->perimeter;
        isl->compactness = (P > 0) ? (4.0 * 3.141592653589793 * A) / (P * P) : 0.0;
    }
}

int gmif_refine_island(const void *generator, const SearchParams *params,
                       int coarse_scale, int fine_scale, MushroomIsland *isl)
{
    const Generator *g = (const Generator *)generator;
    if (!g || !params || !isl) return GMIF_ERR_INVALID_ARG;
    if (fine_scale != 1 && fine_scale != 4) fine_scale = 4;

    int pad = coarse_scale * 2;
    int64_t bx0 = (int64_t)isl->min_x - pad;
    int64_t bz0 = (int64_t)isl->min_z - pad;
    int64_t bx1 = (int64_t)isl->max_x + pad;
    int64_t bz1 = (int64_t)isl->max_z + pad;
    if (bx0 < -30000000) bx0 = -30000000;
    if (bz0 < -30000000) bz0 = -30000000;
    if (bx1 > 30000000) bx1 = 30000000;
    if (bz1 > 30000000) bz1 = 30000000;
    if (bx1 < bx0 || bz1 < bz0) return 0;

    int64_t ox = floor_div(bx0, fine_scale);
    int64_t oz = floor_div(bz0, fine_scale);
    int64_t aligned_x0 = ox * fine_scale;
    int64_t aligned_z0 = oz * fine_scale;
    int64_t w64 = (bx1 - aligned_x0) / fine_scale + 1;
    int64_t h64 = (bz1 - aligned_z0) / fine_scale + 1;
    if (w64 < 1) w64 = 1;
    if (h64 < 1) h64 = 1;
    if (w64 * h64 > 4000000LL) return 0;
    if (w64 > INT32_MAX || h64 > INT32_MAX) return 0;

    int w = (int)w64;
    int h = (int)h64;
    unsigned char *mask = (unsigned char *)malloc((size_t)w * (size_t)h);
    if (!mask) return gmif_fail(GMIF_ERR_ALLOC,
        "allocation failed: refine mask %dx%d", w, h);

    int rc = gmif_fill_mask_range(g, params, fine_scale, ox, oz, w, h,
                                  mask, NULL, -1, -1);
    if (rc != GMIF_OK) {
        free(mask);
        return rc;
    }

    int64_t samples = 0, edges = 0;
    int min_sx = w, max_sx = -1, min_sz = h, max_sz = -1;
    for (int z = 0; z < h; z++) {
        for (int x = 0; x < w; x++) {
            int i = z * w + x;
            if (!mask[i]) continue;
            samples++;
            if (x < min_sx) min_sx = x;
            if (x > max_sx) max_sx = x;
            if (z < min_sz) min_sz = z;
            if (z > max_sz) max_sz = z;
            if (x == 0     || !mask[i - 1])  edges++;
            if (x == w - 1 || !mask[i + 1])  edges++;
            if (z == 0     || !mask[i - w])  edges++;
            if (z == h - 1 || !mask[i + w])  edges++;
        }
    }
    free(mask);
    if (samples == 0 || max_sx < 0) return 0;

    isl->min_x = (int)(aligned_x0 + (int64_t)min_sx * fine_scale);
    isl->max_x = (int)(aligned_x0 + (int64_t)(max_sx + 1) * fine_scale - 1);
    isl->min_z = (int)(aligned_z0 + (int64_t)min_sz * fine_scale);
    isl->max_z = (int)(aligned_z0 + (int64_t)(max_sz + 1) * fine_scale - 1);
    isl->center_x = (int)(aligned_x0 + ((int64_t)(min_sx + max_sx + 1) / 2) * fine_scale);
    isl->center_z = (int)(aligned_z0 + ((int64_t)(min_sz + max_sz + 1) / 2) * fine_scale);
    isl->perimeter = (int)(edges * fine_scale);
    finalize_island(isl, params,
                    samples * (int64_t)fine_scale * (int64_t)fine_scale,
                    samples, (fine_scale == 1) ? 2 : 1);
    return 1;
}

/* ------------------------------------------------------------------ */
/* monolithic search                                                   */
/* ------------------------------------------------------------------ */

static int cmp_area_desc(const void *a, const void *b)
{
    const MushroomIsland *ia = (const MushroomIsland *)a;
    const MushroomIsland *ib = (const MushroomIsland *)b;
    if (ia->area != ib->area)
        return (ia->area < ib->area) ? 1 : -1;
    /* Total tie-break so mono/tiled and thread counts always order equally. */
    if (ia->min_x != ib->min_x) return (ia->min_x < ib->min_x) ? -1 : 1;
    if (ia->min_z != ib->min_z) return (ia->min_z < ib->min_z) ? -1 : 1;
    if (ia->max_x != ib->max_x) return (ia->max_x < ib->max_x) ? -1 : 1;
    if (ia->max_z != ib->max_z) return (ia->max_z < ib->max_z) ? -1 : 1;
    return 0;
}

/** Upper bound on the area a coarse component can reach after refinement. */
static int64_t refined_area_upper_bound(const Component *c, int scale)
{
    int pad = scale * 2;
    int64_t w = ((int64_t)c->max_sx - c->min_sx + 1) * scale + 2LL * pad;
    int64_t h = ((int64_t)c->max_sz - c->min_sz + 1) * scale + 2LL * pad;
    return w * h;
}

static int search_monolithic(const SearchParams *p, const GmifSearchPlan *plan,
                             MushroomIsland **out, int *out_count,
                             SearchCtx *ctx, Generator *g)
{
    *out = NULL;
    *out_count = 0;

    int sx = (int)plan->sx;
    int sz = (int)plan->sz;
    unsigned char *mask = (unsigned char *)malloc((size_t)sx * (size_t)sz);
    if (!mask) {
        return gmif_fail(GMIF_ERR_ALLOC,
            "allocation failed: mask %.1f MB (grid %dx%d)",
            (double)sx * sz / (1024.0 * 1024.0), sx, sz);
    }
    memset(mask, 0, (size_t)sx * (size_t)sz);

    int rc = gmif_fill_mask_range(g, p, plan->scale, plan->cell_x0, plan->cell_z0,
                                  sx, sz, mask, ctx->control, 0, 60);
    if (rc != GMIF_OK) {
        free(mask);
        return rc;
    }
    if (should_stop(ctx)) {
        free(mask);
        return GMIF_ERR_CANCELLED;
    }

    report(ctx, 60, "connect");
    Component *comps = NULL;
    int ncomp = 0;
    rc = extract_components(mask, sx, sz, &comps, &ncomp, ctx);
    free(mask);
    if (rc != GMIF_OK) return rc;

    report(ctx, 70, "refine");

    int cap = ncomp > 0 ? ncomp : 1;
    MushroomIsland *islands = (MushroomIsland *)calloc((size_t)cap, sizeof(MushroomIsland));
    if (!islands) {
        free(comps);
        return gmif_fail(GMIF_ERR_ALLOC, "allocation failed: island list");
    }

    int kept = 0;
    int fine = p->refine ? (plan->scale > 4 ? 4 : 1) : plan->scale;

    for (int ci = 0; ci < ncomp; ci++) {
        if (should_stop(ctx)) {
            free(comps);
            free(islands);
            return GMIF_ERR_CANCELLED;
        }
        if ((ci & 15) == 0 || ci + 1 == ncomp) {
            int pct = 70 + (ncomp > 0 ? (int)((int64_t)ci * 28 / ncomp) : 0);
            if (pct > 98) pct = 98;
            report(ctx, pct, "refine");
        }

        const Component *c = &comps[ci];
        MushroomIsland isl;
        memset(&isl, 0, sizeof(isl));

        /* Coarse geometry is needed both as the fallback result and as the
         * bounding box handed to the refiner. */
        {
            int64_t samples = c->samples;
            int64_t area = samples * (int64_t)plan->scale * (int64_t)plan->scale;
            int64_t min_x = plan->world_x0 + (int64_t)c->min_sx * plan->scale;
            int64_t max_x = plan->world_x0 + (int64_t)(c->max_sx + 1) * plan->scale - 1;
            int64_t min_z = plan->world_z0 + (int64_t)c->min_sz * plan->scale;
            int64_t max_z = plan->world_z0 + (int64_t)(c->max_sz + 1) * plan->scale - 1;
            isl.min_x = (int)min_x;
            isl.max_x = (int)max_x;
            isl.min_z = (int)min_z;
            isl.max_z = (int)max_z;
            isl.center_x = (int)((min_x + max_x) / 2);
            isl.center_z = (int)((min_z + max_z) / 2);
            isl.perimeter = (int)(c->edges * plan->scale);
            finalize_island(&isl, p, area, samples, 0);
        }

        int did_refine = 0;
        if (p->refine) {
            if (p->min_area > 0
                && refined_area_upper_bound(c, plan->scale) < p->min_area) {
                continue; /* cannot possibly pass the filter */
            }
            did_refine = gmif_refine_island(g, p, plan->scale, fine, &isl);
            if (did_refine < 0) {
                free(comps);
                free(islands);
                return did_refine;
            }
        }
        (void)did_refine;

        /* The lush caves rule drops the whole island, never individual
         * samples, so the region shape stays exactly as the surface pass
         * found it. */
        if (lush_filter_active(p)) {
            int lush = gmif_island_has_lush_caves(g, p, &isl, ctx->control,
                                                  ctx->cancel, ctx->ud);
            if (lush < 0) {
                free(comps);
                free(islands);
                return lush;
            }
            if (lush > 0) continue;
        }

        if (isl.area >= p->min_area) {
            islands[kept++] = isl;
            ctx->found = kept;
        }
    }

    free(comps);

    if (kept > 1)
        qsort(islands, (size_t)kept, sizeof(MushroomIsland), cmp_area_desc);

    if (p->max_results > 0 && kept > p->max_results)
        kept = p->max_results;

    report(ctx, 100, "done");
    *out = islands;
    *out_count = kept;
    return GMIF_OK;
}

/* ------------------------------------------------------------------ */
/* public entry                                                        */
/* ------------------------------------------------------------------ */

int gmif_search(const SearchParams *params,
                MushroomIsland **out, int *out_count,
                ProgressFn progress, CancelFn cancel, GmifControl *control,
                void *userdata)
{
    gmif_clear_error();
    if (!params || !out || !out_count) {
        return gmif_fail(GMIF_ERR_INVALID_ARG, "null argument to gmif_search");
    }

    GmifSearchPlan plan;
    int rc = gmif_plan_search(params, &plan);
    if (rc != GMIF_OK) return rc;

    if (plan.use_tiles) {
        return gmif_search_tiled(params, out, out_count,
                                 progress, cancel, control, userdata);
    }

    SearchCtx ctx;
    ctx.progress = progress;
    ctx.cancel = cancel;
    ctx.ud = userdata;
    ctx.control = control;
    ctx.found = 0;
    ctx.mode = 0;

    if (control) {
        control->mode = 0;
        control->percent = 0;
        control->stage = 0;
        control->found = 0;
    }

    Generator g;
    setupGenerator(&g, params->mc, 0);
    applySeed(&g, DIM_OVERWORLD, (uint64_t)params->seed);

    report(&ctx, 0, "scan");
    rc = search_monolithic(params, &plan, out, out_count, &ctx, &g);
    if (rc != GMIF_OK) {
        *out = NULL;
        *out_count = 0;
    }
    return rc;
}

/* ------------------------------------------------------------------ */
/* verification helpers (Cubiomes cross-check)                         */
/* ------------------------------------------------------------------ */

int gmif_sample_biome(int64_t seed, int mc, int x, int z)
{
    if (mc < 0) return -1;
    Generator g;
    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, (uint64_t)seed);
    return getBiomeAt(&g, 1, x, 63, z);
}

int64_t gmif_dump_ascii_map(int64_t seed, int mc,
                            int x0, int z0, int x1, int z1,
                            int step, char *out, size_t out_cap)
{
    if (!out || out_cap == 0 || step < 1 || x1 < x0 || z1 < z0) return 0;
    if (mc < 0) return 0;

    Generator g;
    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, (uint64_t)seed);

    int cols = (x1 - x0) / step + 1;
    int rows = (z1 - z0) / step + 1;
    size_t need = (size_t)rows * ((size_t)cols + 1) + 1;
    if (need > out_cap) return 0;

    int64_t hits = 0;
    size_t o = 0;
    if ((int64_t)cols * rows > 200000) {
        for (int zi = 0; zi < rows; zi++) {
            for (int xi = 0; xi < cols; xi++) {
                int id = getBiomeAt(&g, 1, x0 + xi * step, 63, z0 + zi * step);
                char ch = (id == mushroom_fields) ? 'M' : '.';
                if (id == mushroom_fields) hits++;
                out[o++] = ch;
            }
            out[o++] = '\n';
        }
        out[o] = 0;
        return hits;
    }

    int w = (x1 - x0) + 1;
    int h = (z1 - z0) + 1;
    Range r;
    r.scale = 1;
    r.x = x0;
    r.z = z0;
    r.sx = w;
    r.sz = h;
    r.y = 63;
    r.sy = 1;
    int *ids = allocCache(&g, r);
    if (!ids) return 0;
    if (genBiomes(&g, ids, r) != 0) {
        free(ids);
        return 0;
    }
    for (int zi = 0; zi < rows; zi++) {
        for (int xi = 0; xi < cols; xi++) {
            int bx = xi * step;
            int bz = zi * step;
            if (bx >= w) bx = w - 1;
            if (bz >= h) bz = h - 1;
            int id = ids[(size_t)bz * w + bx];
            char ch = (id == mushroom_fields) ? 'M' : '.';
            if (id == mushroom_fields) hits++;
            out[o++] = ch;
        }
        out[o++] = '\n';
    }
    out[o] = 0;
    free(ids);
    return hits;
}
