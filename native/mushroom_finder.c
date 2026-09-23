/*
 * Giant Mushroom Island Finder — native search core
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

/** Format byte counts as B/KB/MB/GB/TB (not truncated mid-unit). */
static void fmt_bytes(int64_t n, char *out, size_t cap)
{
    if (n < 1024) {
        snprintf(out, cap, "%lld B", (long long)n);
    } else if (n < 1024LL * 1024) {
        snprintf(out, cap, "%.1f KB", n / 1024.0);
    } else if (n < 1024LL * 1024 * 1024) {
        snprintf(out, cap, "%.1f MB", n / (1024.0 * 1024.0));
    } else if (n < 1024LL * 1024 * 1024 * 1024) {
        snprintf(out, cap, "%.2f GB", n / (1024.0 * 1024.0 * 1024.0));
    } else {
        snprintf(out, cap, "%.2f TB", n / (1024.0 * 1024.0 * 1024.0 * 1024.0));
    }
}

/* Max coarse cells we will even attempt to allocate.
 * mask = 1 byte/cell; biomeIds ≈ 4 bytes/cell (plus small pad).
 * 250e6 cells → ~250 MB mask + ~1 GB biome buffer. */
#define GMIF_MAX_COARSE_CELLS 250000000LL

/** Largest radius (blocks) that fits GMIF_MAX_COARSE_CELLS at this scale. */
static int max_radius_for_scale(int scale)
{
    /* half ≈ sqrt(max_cells)/2 ; radius ≈ half * scale */
    double half = sqrt((double)GMIF_MAX_COARSE_CELLS) / 2.0;
    double r = half * (double)scale * 0.98; /* margin */
    if (r < 64.0) r = 64.0;
    if (r > 30000000.0) r = 30000000.0;
    return (int)r;
}

int gmif_describe_grid(int radius, int scale,
                       int *out_half, int *out_sx, int *out_sz,
                       int64_t *out_cells,
                       int64_t *out_mask_bytes, int64_t *out_biome_bytes)
{
    gmif_clear_error();
    if (radius < 1) {
        return gmif_fail(GMIF_ERR_INVALID_ARG, "radius must be >= 1 (got %d)", radius);
    }
    if (scale != 1 && scale != 4 && scale != 16 && scale != 64 && scale != 256) {
        return gmif_fail(GMIF_ERR_INVALID_ARG, "unsupported scale %d", scale);
    }

    /* Use 64-bit for all intermediate products. */
    int64_t half64 = ((int64_t)radius + scale - 1) / scale;
    int64_t sx64 = half64 * 2;
    int64_t sz64 = sx64;
    int64_t cells64 = sx64 * sz64;

    if (sx64 > INT32_MAX || sz64 > INT32_MAX) {
        return gmif_fail(GMIF_ERR_OVERFLOW,
            "grid size overflows 32-bit: half=%lld sx=%lld (radius=%d scale=%d)",
            (long long)half64, (long long)sx64, radius, scale);
    }
    /* world origin = -half * scale must fit in int32. */
    int64_t world0 = -half64 * (int64_t)scale;
    if (world0 < INT32_MIN || -world0 > INT32_MAX) {
        return gmif_fail(GMIF_ERR_OVERFLOW,
            "world origin overflows int32: %lld (radius=%d scale=%d)",
            (long long)world0, radius, scale);
    }

    int64_t mask_bytes = cells64; /* unsigned char */
    /* biomeIds: at least cells * sizeof(int); scale=1 voronoi adds pad. */
    int64_t biome_bytes = cells64 * 4;
    if (scale == 1) {
        int64_t px = ((sx64 + 3) / 4) + 2;
        int64_t py = 3;
        int64_t pz = ((sz64 + 3) / 4) + 2;
        biome_bytes += px * py * pz * 4;
    }

    if (out_half) *out_half = (int)half64;
    if (out_sx) *out_sx = (int)sx64;
    if (out_sz) *out_sz = (int)sz64;
    if (out_cells) *out_cells = cells64;
    if (out_mask_bytes) *out_mask_bytes = mask_bytes;
    if (out_biome_bytes) *out_biome_bytes = biome_bytes;

    if (cells64 > GMIF_MAX_COARSE_CELLS) {
        char msk[64], bio[64];
        fmt_bytes(mask_bytes, msk, sizeof(msk));
        fmt_bytes(biome_bytes, bio, sizeof(bio));
        return gmif_fail(GMIF_ERR_GRID_TOO_LARGE,
            "search grid too large for this build (memory).\n"
            "  requested: radius=%d scale=%d -> %lld x %lld samples\n"
            "  samples=%lld  mask~%s  biome buffer~%s\n"
            "  limit: %lld samples\n"
            "  max radius at scale %d is about %d blocks.\n"
            "  Reduce Radius, or use a coarser scale (CLI --scale).",
            radius, scale,
            (long long)sx64, (long long)sz64,
            (long long)cells64, msk, bio,
            (long long)GMIF_MAX_COARSE_CELLS,
            scale, max_radius_for_scale(scale));
    }

    return GMIF_OK;
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

static void report(SearchCtx *ctx, int percent, const char *stage)
{
    if (ctx && ctx->control) {
        ctx->control->percent = percent;
        ctx->control->found = ctx->found;
        if (stage) {
            if (stage[0] == 'c') ctx->control->stage = 1;
            else if (stage[0] == 'r') ctx->control->stage = 2;
            else if (stage[0] == 'd') ctx->control->stage = 3;
            else ctx->control->stage = 0;
        }
    }
    if (!ctx || !ctx->progress) return;
    SearchProgress sp;
    sp.percent = percent;
    sp.stage = stage;
    sp.found = ctx->found;
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

static int is_target_biome(int id, int include_shore)
{
    if (id == mushroom_fields) return 1;
    if (include_shore && id == mushroom_field_shore) return 1;
    return 0;
}

static double dist2d(double dx, double dz)
{
    return sqrt(dx * dx + dz * dz);
}

/* ------------------------------------------------------------------ */
/* connected components on the sample grid                             */
/* ------------------------------------------------------------------ */

typedef struct Component {
    int min_sx, max_sx, min_sz, max_sz;
    int64_t samples;
} Component;

static int extract_components(const unsigned char *mask,
                              int sx, int sz,
                              int *labels,
                              Component **out_comps, int *out_n,
                              SearchCtx *ctx)
{
    int n = 0;
    size_t cells = (size_t)sx * (size_t)sz;
    size_t comp_cap = cells / 2 + 8;
    Component *comps = (Component *)malloc(sizeof(Component) * comp_cap);
    if (!comps) return -1;

    memset(labels, 0, cells * sizeof(int));

    int *stack = (int *)malloc(sizeof(int) * cells);
    if (!stack) {
        free(comps);
        return -1;
    }

    for (int i = 0; i < (int)cells; i++) {
        if ((i & 0x3FF) == 0) {
            if (should_stop(ctx)) {
                free(stack);
                free(comps);
                return GMIF_ERR_CANCELLED;
            }
            /* connect stage: 70–78 */
            if (cells > 0) {
                int pct = 70 + (int)((int64_t)i * 8 / (int64_t)cells);
                if (pct > 78) pct = 78;
                report(ctx, pct, "connect");
            }
        }
        if (!mask[i] || labels[i]) continue;

        if ((size_t)n >= comp_cap) {
            free(stack);
            free(comps);
            return -1;
        }
        n++;
        Component c;
        c.min_sx = c.max_sx = i % sx;
        c.min_sz = c.max_sz = i / sx;
        c.samples = 0;

        int sp = 0;
        stack[sp++] = i;
        labels[i] = n;

        while (sp) {
            int idx = stack[--sp];
            int x = idx % sx;
            int z = idx / sx;
            c.samples++;
            if (x < c.min_sx) c.min_sx = x;
            if (x > c.max_sx) c.max_sx = x;
            if (z < c.min_sz) c.min_sz = z;
            if (z > c.max_sz) c.max_sz = z;

            if (x + 1 < sx && mask[idx + 1] && !labels[idx + 1]) {
                labels[idx + 1] = n;
                stack[sp++] = idx + 1;
            }
            if (x - 1 >= 0 && mask[idx - 1] && !labels[idx - 1]) {
                labels[idx - 1] = n;
                stack[sp++] = idx - 1;
            }
            if (z + 1 < sz && mask[idx + sx] && !labels[idx + sx]) {
                labels[idx + sx] = n;
                stack[sp++] = idx + sx;
            }
            if (z - 1 >= 0 && mask[idx - sx] && !labels[idx - sx]) {
                labels[idx - sx] = n;
                stack[sp++] = idx - sx;
            }
        }

        comps[n - 1] = c;
    }

    free(stack);
    *out_comps = comps;
    *out_n = n;
    return 0;
}

static int64_t count_boundary_edges(const int *labels, int label,
                                    int sx, int sz)
{
    int64_t edges = 0;
    for (int z = 0; z < sz; z++) {
        for (int x = 0; x < sx; x++) {
            int i = z * sx + x;
            if (labels[i] != label) continue;
            if (x == 0     || labels[i - 1]  != label) edges++;
            if (x == sx-1  || labels[i + 1]  != label) edges++;
            if (z == 0     || labels[i - sx] != label) edges++;
            if (z == sz-1  || labels[i + sx] != label) edges++;
        }
    }
    return edges;
}

/* ------------------------------------------------------------------ */
/* coarse scan                                                         */
/* ------------------------------------------------------------------ */

static int scan_region(const Generator *g, int scale, int radius,
                       int y, int include_shore, int threads,
                       unsigned char **out_mask,
                       int *out_sx, int *out_sz,
                       int *out_origin_x, int *out_origin_z,
                       SearchCtx *ctx)
{
    int half = 0, sx = 0, sz = 0;
    int64_t cells = 0, mask_bytes = 0, biome_bytes = 0;
    int drc = gmif_describe_grid(radius, scale,
                                 &half, &sx, &sz,
                                 &cells, &mask_bytes, &biome_bytes);
    if (drc != GMIF_OK) {
        return drc; /* message already set */
    }

    int world_x0 = -half * scale;
    int world_z0 = -half * scale;

    unsigned char *mask = (unsigned char *)malloc((size_t)cells);
    if (!mask) {
        return gmif_fail(GMIF_ERR_ALLOC,
            "allocation failed: mask %lld bytes (radius=%d scale=%d grid=%dx%d)",
            (long long)mask_bytes, radius, scale, sx, sz);
    }
    memset(mask, 0, (size_t)cells);

    Range r;
    r.scale = scale;
    r.x = -half;
    r.z = -half;
    r.sx = sx;
    r.sz = sz;
    r.y = y;
    r.sy = 1;

    int *biomeIds = allocCache(g, r);
    if (!biomeIds) {
        free(mask);
        return gmif_fail(GMIF_ERR_ALLOC,
            "allocation failed: biome cache ≥%lld bytes (radius=%d scale=%d grid=%dx%d). "
            "Check available RAM.",
            (long long)biome_bytes, radius, scale, sx, sz);
    }

    report(ctx, 0, "scan");
    if (should_stop(ctx)) {
        free(biomeIds);
        free(mask);
        return GMIF_ERR_CANCELLED;
    }

    /* Stable mask: scale>4 is decimated from scale=4 so tiled == monolithic. */
    {
        int frc = gmif_fill_mask_range(g, scale, -half, -half, sx, sz,
                                       include_shore, mask);
        if (frc != GMIF_OK) {
            free(biomeIds);
            free(mask);
            return frc;
        }
    }
    free(biomeIds);
    report(ctx, 70, "scan");

    if (should_stop(ctx)) {
        free(mask);
        return GMIF_ERR_CANCELLED;
    }

    report(ctx, 70, "connect");
    *out_mask = mask;
    *out_sx = sx;
    *out_sz = sz;
    *out_origin_x = world_x0;
    *out_origin_z = world_z0;
    return 0;
}

/* ------------------------------------------------------------------ */
/* refine                                                              */
/* ------------------------------------------------------------------ */

static int refine_component(const Generator *g,
                            int coarse_scale, int fine_scale,
                            int world_x0, int world_z0,
                            const Component *c, int include_shore,
                            MushroomIsland *out,
                            SearchCtx *ctx)
{
    int pad = coarse_scale * 2;
    int bx0 = world_x0 + c->min_sx * coarse_scale - pad;
    int bz0 = world_z0 + c->min_sz * coarse_scale - pad;
    int bx1 = world_x0 + (c->max_sx + 1) * coarse_scale + pad;
    int bz1 = world_z0 + (c->max_sz + 1) * coarse_scale + pad;

    int sx = (bx1 - bx0) / fine_scale;
    int sz = (bz1 - bz0) / fine_scale;
    if (sx < 1) sx = 1;
    if (sz < 1) sz = 1;
    if ((int64_t)sx * sz > 8000000) return 0;

    Range r;
    r.scale = fine_scale;
    r.x = bx0 / fine_scale;
    r.z = bz0 / fine_scale;
    r.sx = sx;
    r.sz = sz;
    r.y = (fine_scale == 1) ? 63 : 15;
    r.sy = 1;

    int *biomeIds = allocCache(g, r);
    if (!biomeIds) return -1;

    if (should_stop(ctx)) {
        free(biomeIds);
        return -2;
    }
    if (genBiomes(g, biomeIds, r) != 0) {
        free(biomeIds);
        return -1;
    }

    int64_t samples = 0;
    int min_sx = sx, max_sx = -1, min_sz = sz, max_sz = -1;
    unsigned char *mask = (unsigned char *)malloc((size_t)sx * sz);
    if (!mask) {
        free(biomeIds);
        return -1;
    }
    for (int z = 0; z < sz; z++) {
        for (int x = 0; x < sx; x++) {
            int i = z * sx + x;
            int hit = is_target_biome(biomeIds[i], include_shore);
            mask[i] = (unsigned char)hit;
            if (hit) {
                samples++;
                if (x < min_sx) min_sx = x;
                if (x > max_sx) max_sx = x;
                if (z < min_sz) min_sz = z;
                if (z > max_sz) max_sz = z;
            }
        }
    }
    free(biomeIds);

    if (samples == 0 || max_sx < 0) {
        free(mask);
        return 0;
    }

    int64_t area = samples * (int64_t)fine_scale * (int64_t)fine_scale;
    int w_blocks = (max_sx - min_sx + 1) * fine_scale;
    int h_blocks = (max_sz - min_sz + 1) * fine_scale;
    int cx = bx0 + ((min_sx + max_sx + 1) / 2) * fine_scale;
    int cz = bz0 + ((min_sz + max_sz + 1) / 2) * fine_scale;

    int64_t edges = 0;
    for (int z = 0; z < sz; z++) {
        for (int x = 0; x < sx; x++) {
            int i = z * sx + x;
            if (!mask[i]) continue;
            if (x == 0     || !mask[i - 1])  edges++;
            if (x == sx-1  || !mask[i + 1])  edges++;
            if (z == 0     || !mask[i - sx]) edges++;
            if (z == sz-1  || !mask[i + sx]) edges++;
        }
    }
    free(mask);

    out->center_x = cx;
    out->center_z = cz;
    out->min_x = bx0 + min_sx * fine_scale;
    out->max_x = bx0 + (max_sx + 1) * fine_scale - 1;
    out->min_z = bz0 + min_sz * fine_scale;
    out->max_z = bz0 + (max_sz + 1) * fine_scale - 1;
    out->width = w_blocks;
    out->height = h_blocks;
    out->diagonal_span = sqrt((double)w_blocks * w_blocks + (double)h_blocks * h_blocks);
    out->area = area;
    out->samples = samples;
    out->area_precision = (fine_scale == 1) ? 2 : 1;
    out->perimeter = (int)(edges * fine_scale);
    out->distance = dist2d((double)cx, (double)cz);
    {
        double A = (double)area;
        double P = (double)out->perimeter;
        out->compactness = (P > 0) ? (4.0 * 3.141592653589793 * A) / (P * P) : 0.0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* public entry                                                        */
/* ------------------------------------------------------------------ */

static int cmp_area_desc(const void *a, const void *b)
{
    const MushroomIsland *ia = (const MushroomIsland *)a;
    const MushroomIsland *ib = (const MushroomIsland *)b;
    if (ia->area != ib->area)
        return (ia->area < ib->area) ? 1 : -1;
    /* Stable tie-break so mono/tiled orders match. */
    if (ia->min_x != ib->min_x) return (ia->min_x < ib->min_x) ? -1 : 1;
    if (ia->min_z != ib->min_z) return (ia->min_z < ib->min_z) ? -1 : 1;
    return 0;
}

int gmif_search(const SearchParams *params,
                MushroomIsland **out, int *out_count,
                ProgressFn progress, CancelFn cancel, GmifControl *control,
                void *userdata)
{
    gmif_clear_error();
    if (!params || !out || !out_count) {
        return gmif_fail(GMIF_ERR_INVALID_ARG, "null argument to gmif_search");
    }

    int scale = params->scale > 0 ? params->scale : GMIF_SCALE_CHUNK;
    if (scale != 1 && scale != 4 && scale != 16 && scale != 64 && scale != 256)
        scale = GMIF_SCALE_CHUNK;

    int use_tiles = params->use_tiles;
    if (use_tiles < 0 || use_tiles > 2) use_tiles = 2;

    if (use_tiles == 1) {
        return gmif_search_tiled(params, out, out_count,
                                 progress, cancel, control, userdata);
    }
    if (use_tiles == 2) {
        /* auto: monolithic only if the *stable fill* buffer is modest.
         * scale>4 is decimated from scale=4 (16x cells at scale 16). */
        int half = 0, sx = 0, sz = 0;
        int64_t cells = 0, mb = 0, bb = 0;
        int drc = gmif_describe_grid(params->radius, scale,
                                     &half, &sx, &sz, &cells, &mb, &bb);
        int64_t fill_cells = cells;
        if (scale > 4) {
            int64_t f = (int64_t)(scale / 4);
            fill_cells = cells * f * f;
        }
        if (drc != GMIF_OK || fill_cells > 40000000LL) {
            return gmif_search_tiled(params, out, out_count,
                                     progress, cancel, control, userdata);
        }
        /* fall through to monolithic */
    }

    *out = NULL;
    *out_count = 0;

    SearchCtx ctx;
    ctx.progress = progress;
    ctx.cancel = cancel;
    ctx.ud = userdata;
    ctx.control = control;
    ctx.found = 0;

    if (params->mc < 0) {
        return gmif_fail(GMIF_ERR_VERSION, "invalid Minecraft version id %d", params->mc);
    }

    /* Validate grid geometry before touching Cubiomes. */
    {
        int half = 0, sx = 0, sz = 0;
        int64_t cells = 0, mb = 0, bb = 0;
        int drc = gmif_describe_grid(params->radius, scale,
                                     &half, &sx, &sz, &cells, &mb, &bb);
        if (drc != GMIF_OK) return drc;
    }

    Generator g;
    setupGenerator(&g, params->mc, 0);
    applySeed(&g, DIM_OVERWORLD, (uint64_t)params->seed);

    int y = (scale == 1) ? 63 : 15;

    unsigned char *mask = NULL;
    int sx = 0, sz = 0, wx0 = 0, wz0 = 0;
    int rc = scan_region(&g, scale, params->radius, y,
                         params->include_shore, params->threads,
                         &mask, &sx, &sz, &wx0, &wz0, &ctx);
    if (rc != 0) return rc;

    int *labels = (int *)malloc((size_t)sx * (size_t)sz * sizeof(int));
    if (!labels) {
        free(mask);
        return gmif_fail(GMIF_ERR_ALLOC,
            "allocation failed: CCA labels %lld bytes (grid=%dx%d)",
            (long long)((int64_t)sx * sz * 4), sx, sz);
    }

    Component *comps = NULL;
    int ncomp = 0;
    rc = extract_components(mask, sx, sz, labels, &comps, &ncomp, &ctx);
    if (rc != 0) {
        free(labels);
        free(mask);
        free(comps);
        return rc;
    }

    report(&ctx, 78, "refine");

    int cap = ncomp > 0 ? ncomp : 1;
    MushroomIsland *islands = (MushroomIsland *)calloc((size_t)cap, sizeof(MushroomIsland));
    if (!islands) {
        free(labels);
        free(mask);
        free(comps);
        return gmif_fail(GMIF_ERR_ALLOC, "allocation failed: island list");
    }

    int kept = 0;
    int fine = params->refine ? (scale > 4 ? 4 : 1) : scale;

    for (int ci = 0; ci < ncomp; ci++) {
        if (should_stop(&ctx)) {
            free(labels);
            free(mask);
            free(comps);
            free(islands);
            return GMIF_ERR_CANCELLED;
        }
        if (ncomp > 0) {
            /* refine/statistics: 78–98 */
            int pct = 78 + (int)((int64_t)ci * 20 / ncomp);
            if (pct > 98) pct = 98;
            report(&ctx, pct, "refine");
        }

        const Component *c = &comps[ci];
        MushroomIsland isl;
        memset(&isl, 0, sizeof(isl));

        int did_refine = 0;
        if (params->refine) {
            did_refine = refine_component(&g, scale, fine,
                                          wx0, wz0, c,
                                          params->include_shore,
                                          &isl, &ctx);
            if (did_refine < 0) {
                free(labels);
                free(mask);
                free(comps);
                free(islands);
                return did_refine;
            }
        }

        if (!did_refine) {
            int label = ci + 1;
            int64_t samples = c->samples;
            int64_t area = samples * (int64_t)scale * (int64_t)scale;
            int min_x = wx0 + c->min_sx * scale;
            int max_x = wx0 + (c->max_sx + 1) * scale - 1;
            int min_z = wz0 + c->min_sz * scale;
            int max_z = wz0 + (c->max_sz + 1) * scale - 1;
            int64_t bedges = count_boundary_edges(labels, label, sx, sz);

            isl.center_x = (min_x + max_x) / 2;
            isl.center_z = (min_z + max_z) / 2;
            isl.min_x = min_x;
            isl.max_x = max_x;
            isl.min_z = min_z;
            isl.max_z = max_z;
            isl.width = max_x - min_x + 1;
            isl.height = max_z - min_z + 1;
            isl.diagonal_span = sqrt((double)isl.width * isl.width
                                   + (double)isl.height * isl.height);
            isl.area = area;
            isl.samples = samples;
            isl.area_precision = 0;
            isl.perimeter = (int)(bedges * scale);
            isl.distance = dist2d((double)isl.center_x, (double)isl.center_z);
            {
                double A = (double)isl.area;
                double P = (double)isl.perimeter;
                isl.compactness = (P > 0) ? (4.0 * 3.141592653589793 * A) / (P * P) : 0.0;
            }
        }

        if (isl.area >= params->min_area) {
            islands[kept++] = isl;
            ctx.found = kept;
        }
    }

    free(labels);
    free(mask);
    free(comps);

    if (kept > 1)
        qsort(islands, (size_t)kept, sizeof(MushroomIsland), cmp_area_desc);

    /* Result-layer only: keep top-N by area. Does not change CCA. */
    if (params->max_results > 0 && kept > params->max_results)
        kept = params->max_results;

    report(&ctx, 100, "done");

    *out = islands;
    *out_count = kept;
    return 0;
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

int gmif_is_mushroom(int biome_id, int include_shore)
{
    return is_target_biome(biome_id, include_shore);
}

int gmif_fill_mask_range(const void *generator, int scale,
                         int ox, int oz, int sx, int sz,
                         int include_shore, unsigned char *mask)
{
    const Generator *g = (const Generator *)generator;
    if (!g || !mask || sx <= 0 || sz <= 0) return GMIF_ERR_INVALID_ARG;

    if (scale > 4) {
        /* Decimate from stable scale=4 samples (opt disabled at scale<=4). */
        int factor = scale / 4;
        int fx = sx * factor;
        int fz = sz * factor;
        Range r;
        r.scale = 4;
        r.x = ox * factor;
        r.z = oz * factor;
        r.sx = fx;
        r.sz = fz;
        r.y = 15;
        r.sy = 1;
        int *ids = allocCache(g, r);
        if (!ids) {
            return gmif_set_error(GMIF_ERR_ALLOC,
                "allocation failed: scale4 window %dx%d", fx, fz);
        }
        if (genBiomes(g, ids, r) != 0) {
            free(ids);
            return gmif_set_error(GMIF_ERR_BIOME_GEN,
                "biome sampling failed (scale4 window %dx%d)", fx, fz);
        }
        int mid = factor / 2;
        for (int z = 0; z < sz; z++) {
            for (int x = 0; x < sx; x++) {
                int sxi = x * factor + mid;
                int szi = z * factor + mid;
                if (sxi >= fx) sxi = fx - 1;
                if (szi >= fz) szi = fz - 1;
                mask[z * sx + x] =
                    is_target_biome(ids[(size_t)szi * fx + sxi], include_shore) ? 1 : 0;
            }
        }
        free(ids);
        return GMIF_OK;
    }

    /* scale 1 or 4: generate directly at that scale. */
    Range r;
    r.scale = scale;
    r.x = ox;
    r.z = oz;
    r.sx = sx;
    r.sz = sz;
    r.y = (scale == 1) ? 63 : 15;
    r.sy = 1;
    int *ids = allocCache(g, r);
    if (!ids) {
        return gmif_set_error(GMIF_ERR_ALLOC, "allocation failed: mask window %dx%d", sx, sz);
    }
    if (genBiomes(g, ids, r) != 0) {
        free(ids);
        return gmif_set_error(GMIF_ERR_BIOME_GEN, "biome sampling failed (%dx%d)", sx, sz);
    }
    size_t n = (size_t)sx * (size_t)sz;
    for (size_t i = 0; i < n; i++)
        mask[i] = is_target_biome(ids[i], include_shore) ? 1 : 0;
    free(ids);
    return GMIF_OK;
}

int gmif_refine_island(const void *generator, int coarse_scale, int fine_scale,
                       int include_shore, MushroomIsland *isl)
{
    const Generator *g = (const Generator *)generator;
    if (!g || !isl) return GMIF_ERR_INVALID_ARG;
    if (fine_scale != 1 && fine_scale != 4) fine_scale = 4;

    int pad = coarse_scale * 2;
    int bx0 = isl->min_x - pad;
    int bz0 = isl->min_z - pad;
    int bx1 = isl->max_x + pad;
    int bz1 = isl->max_z + pad;
    int w = (bx1 - bx0) / fine_scale + 1;
    int h = (bz1 - bz0) / fine_scale + 1;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if ((int64_t)w * h > 4000000) return 0;

    int ox = bx0 / fine_scale;
    int oz = bz0 / fine_scale;
    unsigned char *mask = (unsigned char *)malloc((size_t)w * h);
    if (!mask) return GMIF_ERR_ALLOC;
    int rc = gmif_fill_mask_range(g, fine_scale, ox, oz, w, h,
                                  include_shore, mask);
    if (rc != GMIF_OK) {
        free(mask);
        return rc;
    }

    int64_t samples = 0;
    int min_sx = w, max_sx = -1, min_sz = h, max_sz = -1;
    int64_t edges = 0;
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

    int min_x = bx0 + min_sx * fine_scale;
    int max_x = bx0 + (max_sx + 1) * fine_scale - 1;
    int min_z = bz0 + min_sz * fine_scale;
    int max_z = bz0 + (max_sz + 1) * fine_scale - 1;
    isl->min_x = min_x;
    isl->max_x = max_x;
    isl->min_z = min_z;
    isl->max_z = max_z;
    /* Same center formula as monolithic refine_component. */
    isl->center_x = bx0 + ((min_sx + max_sx + 1) / 2) * fine_scale;
    isl->center_z = bz0 + ((min_sz + max_sz + 1) / 2) * fine_scale;
    isl->width = max_x - min_x + 1;
    isl->height = max_z - min_z + 1;
    isl->diagonal_span = sqrt((double)isl->width * isl->width
                            + (double)isl->height * isl->height);
    isl->samples = samples;
    isl->area = samples * (int64_t)fine_scale * (int64_t)fine_scale;
    isl->area_precision = (fine_scale == 1) ? 2 : 1;
    isl->perimeter = (int)(edges * fine_scale);
    isl->distance = dist2d((double)isl->center_x, (double)isl->center_z);
    {
        double A = (double)isl->area;
        double P = (double)isl->perimeter;
        isl->compactness = (P > 0) ? (4.0 * 3.141592653589793 * A) / (P * P) : 0.0;
    }
    return 1;
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

    /* Use genBiomes at block scale for accuracy on small maps. */
    Range r;
    r.scale = 1;
    r.x = x0;
    r.z = z0;
    r.sx = cols;
    r.sz = rows;
    r.y = 63;
    r.sy = 1;

    /* Sample at step by generating a denser grid when step > 1:
     * generate full block range only if area is small; else sample. */
    int64_t hits = 0;
    size_t o = 0;
    if ((int64_t)cols * rows > 200000) {
        /* sparse: getBiomeAt per cell */
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

    /* Dense path: generate every block in the rect, then downsample display */
    int w = (x1 - x0) + 1;
    int h = (z1 - z0) + 1;
    r.sx = w;
    r.sz = h;
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
