/*
 * Large Biome Finder — tiled streaming search + DSU merge
 *
 * Processes the sample grid in tiles (bounded memory). Each tile runs the
 * same target mask + 4-neighbor CCA as the monolithic path. Regions that
 * touch tile borders are merged with Union-Find when adjacent tiles share
 * a target-to-target border pair.
 *
 * The biome sampling inside each tile is parallel over row bands (OpenMP).
 * Border contacts are streamed one tile row at a time: only the current row
 * and the previous row's bottom contacts are kept, so memory depends on the
 * grid width and tile size, not on the total number of tiles. This is what
 * makes very large radii safe for broad targets such as ocean/plains.
 *
 * CCA semantics unchanged (4-neighbor). Cubiomes not modified.
 */
#include "mushroom_finder.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#define gmif_sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#define gmif_sleep_ms(ms) usleep((ms) * 1000)
#endif

#include "generator.h"
#include "util.h"

/** Safety cap for the global DSU list (40 bytes per region). */
#define GMIF_MAX_REGIONS 4000000

/* ------------------------------------------------------------------ */
/* local types                                                         */
/* ------------------------------------------------------------------ */

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

static void report(SearchCtx *ctx, int percent, const char *stage)
{
    if (ctx && ctx->control) {
        ctx->control->percent = percent;
        ctx->control->found = ctx->found;
        ctx->control->mode = ctx->mode;
        if (stage) {
            if (stage[0] == 't' && stage[1] == 'i') ctx->control->stage = 4;
            else if (stage[0] == 'm') ctx->control->stage = 5;
            else if (stage[0] == 's') ctx->control->stage = 0;
            else if (stage[0] == 'c') ctx->control->stage = 1;
            else if (stage[0] == 'r') ctx->control->stage = 2;
            else if (stage[0] == 'd') ctx->control->stage = 3;
        }
    }
    if (!ctx || !ctx->progress) return;
    SearchProgress sp;
    sp.percent = percent;
    sp.stage = stage;
    sp.found = ctx->found;
    sp.mode = ctx->mode;
    ctx->progress(&sp, ctx->ud);
}

static double dist_from_center(int x, int z, int cx, int cz)
{
    double dx = (double)x - (double)cx;
    double dz = (double)z - (double)cz;
    return sqrt(dx * dx + dz * dz);
}

/* ------------------------------------------------------------------ */
/* DSU                                                                 */
/* ------------------------------------------------------------------ */

typedef struct TileRegion {
    int parent;            /* DSU parent index in this array */
    int rank;
    int64_t samples;
    /* global sample-grid bounds (inclusive) */
    int min_sx, max_sx, min_sz, max_sz;
    int64_t outer_edges;   /* tile-local exposed edges */
    int64_t shared_pairs;  /* target adjacencies across tile borders (internal) */
} TileRegion;

static int dsu_find(TileRegion *a, int i)
{
    while (a[i].parent != i) {
        a[i].parent = a[a[i].parent].parent;
        i = a[i].parent;
    }
    return i;
}

static int dsu_union(TileRegion *a, int i, int j)
{
    i = dsu_find(a, i);
    j = dsu_find(a, j);
    if (i == j) return i;
    if (a[i].rank < a[j].rank) {
        int t = i; i = j; j = t;
    }
    a[j].parent = i;
    if (a[i].rank == a[j].rank) a[i].rank++;
    return i;
}

/* Border contact record: one target cell on a tile edge. */
typedef struct BorderCell {
    int32_t region;   /* index into regions[] */
    int32_t coord;    /* global row or column along the edge */
} BorderCell;

/* ------------------------------------------------------------------ */
/* tile-local CCA (same 4-neighbor semantics as monolithic path)       */
/* ------------------------------------------------------------------ */

static int tile_cca(const unsigned char *mask, int w, int h,
                    int *labels, int *n_out)
{
    memset(labels, 0, (size_t)w * h * sizeof(int));
    int n = 0;
    int *stack = (int *)malloc((size_t)w * h * sizeof(int));
    if (!stack) {
        return gmif_set_error(GMIF_ERR_ALLOC,
            "allocation failed: tile CCA stack (%d x %d)", w, h);
    }

    for (int i = 0; i < w * h; i++) {
        if (!mask[i] || labels[i]) continue;
        n++;
        int sp = 0;
        stack[sp++] = i;
        labels[i] = n;
        while (sp) {
            int idx = stack[--sp];
            int x = idx % w;
            int y = idx / w;
            if (x + 1 < w && mask[idx + 1] && !labels[idx + 1]) {
                labels[idx + 1] = n;
                stack[sp++] = idx + 1;
            }
            if (x - 1 >= 0 && mask[idx - 1] && !labels[idx - 1]) {
                labels[idx - 1] = n;
                stack[sp++] = idx - 1;
            }
            if (y + 1 < h && mask[idx + w] && !labels[idx + w]) {
                labels[idx + w] = n;
                stack[sp++] = idx + w;
            }
            if (y - 1 >= 0 && mask[idx - w] && !labels[idx - w]) {
                labels[idx - w] = n;
                stack[sp++] = idx - w;
            }
        }
    }
    free(stack);
    *n_out = n;
    return GMIF_OK;
}

static void tile_region_stats(const unsigned char *mask, const int *labels,
                              int w, int h, int nloc,
                              int ox, int oy, /* global sample offset of tile */
                              TileRegion *regs, int reg_base)
{
    for (int i = 0; i < nloc; i++) {
        TileRegion *r = &regs[reg_base + i];
        r->parent = reg_base + i;
        r->rank = 0;
        r->samples = 0;
        r->min_sx = INT_MAX; r->max_sx = INT_MIN;
        r->min_sz = INT_MAX; r->max_sz = INT_MIN;
        r->outer_edges = 0;
        r->shared_pairs = 0;
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            int lab = labels[i];
            if (lab <= 0) continue;
            TileRegion *r = &regs[reg_base + lab - 1];
            r->samples++;
            int gx = ox + x, gy = oy + y;
            if (gx < r->min_sx) r->min_sx = gx;
            if (gx > r->max_sx) r->max_sx = gx;
            if (gy < r->min_sz) r->min_sz = gy;
            if (gy > r->max_sz) r->max_sz = gy;

            if (x == 0     || !mask[i - 1]) r->outer_edges++;
            if (x == w - 1 || !mask[i + 1]) r->outer_edges++;
            if (y == 0     || !mask[i - w]) r->outer_edges++;
            if (y == h - 1 || !mask[i + w]) r->outer_edges++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* helper to grow arrays                                               */
/* ------------------------------------------------------------------ */

static int grow_regions(TileRegion **arr, int *cap, int need)
{
    if (need <= *cap) return GMIF_OK;
    if (need > GMIF_MAX_REGIONS) {
        return gmif_set_error(GMIF_ERR_GRID_TOO_LARGE,
            "target biome is too fragmented for a single search:\n"
            "  more than %d connected regions would be required.\n"
            "  Reduce the radius or use a coarser sample scale.",
            GMIF_MAX_REGIONS);
    }
    int ncap = *cap ? *cap : 64;
    while (ncap < need) ncap *= 2;
    TileRegion *na = (TileRegion *)realloc(*arr, (size_t)ncap * sizeof(TileRegion));
    if (!na) {
        return gmif_set_error(GMIF_ERR_ALLOC,
            "allocation failed: region list (%d entries)", ncap);
    }
    *arr = na;
    *cap = ncap;
    return GMIF_OK;
}

/**
 * Merge two sorted border contact lists (one tile edge against its neighbour).
 * Matching coordinates are a 4-neighbour target adjacency and union the two
 * regions. The pair is counted once; it is subtracted from the summed perimeter
 * when the final island is built.
 */
static void merge_border_lists(TileRegion *regions,
                               const BorderCell *a, int na,
                               const BorderCell *b, int nb)
{
    int i = 0, j = 0;
    while (i < na && j < nb) {
        if (a[i].coord == b[j].coord) {
            int ra = dsu_find(regions, a[i].region);
            int rb = dsu_find(regions, b[j].region);
            if (ra != rb) dsu_union(regions, ra, rb);
            regions[a[i].region].shared_pairs++;
            i++; j++;
        } else if (a[i].coord < b[j].coord) {
            i++;
        } else {
            j++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* main tiled search                                                   */
/* ------------------------------------------------------------------ */

static int cmp_area_desc(const void *a, const void *b)
{
    const MushroomIsland *ia = (const MushroomIsland *)a;
    const MushroomIsland *ib = (const MushroomIsland *)b;
    if (ia->area != ib->area)
        return (ia->area < ib->area) ? 1 : -1;
    if (ia->min_x != ib->min_x) return (ia->min_x < ib->min_x) ? -1 : 1;
    if (ia->min_z != ib->min_z) return (ia->min_z < ib->min_z) ? -1 : 1;
    if (ia->max_x != ib->max_x) return (ia->max_x < ib->max_x) ? -1 : 1;
    if (ia->max_z != ib->max_z) return (ia->max_z < ib->max_z) ? -1 : 1;
    return 0;
}

static void fill_island_from_region(const TileRegion *r, const GmifSearchPlan *plan,
                                    const SearchParams *p, MushroomIsland *out)
{
    int scale = plan->scale;
    int64_t min_x = plan->world_x0 + (int64_t)r->min_sx * scale;
    int64_t max_x = plan->world_x0 + (int64_t)(r->max_sx + 1) * scale - 1;
    int64_t min_z = plan->world_z0 + (int64_t)r->min_sz * scale;
    int64_t max_z = plan->world_z0 + (int64_t)(r->max_sz + 1) * scale - 1;
    out->center_x = (int)((min_x + max_x) / 2);
    out->center_z = (int)((min_z + max_z) / 2);
    out->min_x = (int)min_x;
    out->max_x = (int)max_x;
    out->min_z = (int)min_z;
    out->max_z = (int)max_z;
    out->width = (int)(max_x - min_x + 1);
    out->height = (int)(max_z - min_z + 1);
    out->diagonal_span = sqrt((double)out->width * out->width
                            + (double)out->height * out->height);
    out->samples = r->samples;
    out->area = r->samples * (int64_t)scale * (int64_t)scale;
    out->area_precision = 0;
    /* Each shared pair made 2 tile-local "outer" edges that are actually internal. */
    {
        int64_t e = r->outer_edges - 2 * r->shared_pairs;
        if (e < 0) e = 0;
        out->perimeter = (int)(e * scale);
    }
    out->distance = dist_from_center(out->center_x, out->center_z,
                                     p->center_x, p->center_z);
    {
        double A = (double)out->area;
        double P = (double)out->perimeter;
        out->compactness = (P > 0) ? (4.0 * 3.141592653589793 * A) / (P * P) : 0.0;
    }
}

static int64_t tile_refine_upper(const TileRegion *r, int scale)
{
    int64_t w = ((int64_t)r->max_sx - r->min_sx + 1) * scale + 4LL * scale;
    int64_t h = ((int64_t)r->max_sz - r->min_sz + 1) * scale + 4LL * scale;
    return w * h;
}

static void free_row_buffers(BorderCell *a, BorderCell *b, BorderCell *c,
                             BorderCell *d, BorderCell *e,
                             int *ia, int *ib, int *ic, int *id, int *ie)
{
    free(a); free(b); free(c); free(d); free(e);
    free(ia); free(ib); free(ic); free(id); free(ie);
}

int gmif_search_tiled(const SearchParams *params,
                      MushroomIsland **out, int *out_count,
                      ProgressFn progress, CancelFn cancel, GmifControl *control,
                      void *userdata)
{
    if (!params || !out || !out_count) {
        return gmif_set_error(GMIF_ERR_INVALID_ARG, "null argument to gmif_search_tiled");
    }
    *out = NULL;
    *out_count = 0;

    GmifSearchPlan plan;
    int rc = gmif_plan_search(params, &plan);
    if (rc != GMIF_OK) return rc;
    if (!plan.use_tiles) {
        /* This public entry always streams, even when the planner would pick
         * the monolithic path for a small area. */
        SearchParams forced = *params;
        forced.use_tiles = 1;
        rc = gmif_plan_search(&forced, &plan);
        if (rc != GMIF_OK) return rc;
    }

    SearchCtx ctx;
    ctx.progress = progress;
    ctx.cancel = cancel;
    ctx.ud = userdata;
    ctx.control = control;
    ctx.found = 0;
    ctx.mode = 1;

    if (control) {
        control->mode = 1;
        control->percent = 0;
        control->stage = 4;
        control->found = 0;
    }

    int scale = plan.scale;
    int tile = plan.tile_size;
    int sx = (int)plan.sx;
    int sz = (int)plan.sz;
    if (sx <= 0 || sz <= 0 || tile <= 0) {
        return gmif_set_error(GMIF_ERR_INVALID_ARG, "invalid tiled scan geometry");
    }
    int ntx = (sx + tile - 1) / tile;
    int ntz = (sz + tile - 1) / tile;
    int64_t ntiles = (int64_t)ntx * ntz;
    if (ntx <= 0 || ntz <= 0) {
        return gmif_set_error(GMIF_ERR_INVALID_ARG, "invalid tile count");
    }

    report(&ctx, 0, "tile");

    Generator g;
    setupGenerator(&g, params->mc, 0);
    applySeed(&g, DIM_OVERWORLD, (uint64_t)params->seed);

    TileRegion *regions = NULL;
    int reg_cap = 0, reg_n = 0;

    /* One row of border contacts: one slot per (tile, edge cell). */
    size_t row_cap = (size_t)ntx * (size_t)tile;
    if (row_cap == 0) row_cap = 1;
    BorderCell *b_right = (BorderCell *)calloc(row_cap, sizeof(BorderCell));
    BorderCell *b_left = (BorderCell *)calloc(row_cap, sizeof(BorderCell));
    BorderCell *b_top = (BorderCell *)calloc(row_cap, sizeof(BorderCell));
    BorderCell *b_bottom_cur = (BorderCell *)calloc(row_cap, sizeof(BorderCell));
    BorderCell *b_bottom_prev = (BorderCell *)calloc(row_cap, sizeof(BorderCell));
    int *n_right = (int *)calloc((size_t)ntx, sizeof(int));
    int *n_left = (int *)calloc((size_t)ntx, sizeof(int));
    int *n_top = (int *)calloc((size_t)ntx, sizeof(int));
    int *n_bottom_cur = (int *)calloc((size_t)ntx, sizeof(int));
    int *n_bottom_prev = (int *)calloc((size_t)ntx, sizeof(int));
    unsigned char *mask = (unsigned char *)malloc((size_t)tile * (size_t)tile);
    int *labels = (int *)malloc((size_t)tile * (size_t)tile * sizeof(int));

    if (!b_right || !b_left || !b_top || !b_bottom_cur || !b_bottom_prev
        || !n_right || !n_left || !n_top || !n_bottom_cur || !n_bottom_prev
        || !mask || !labels) {
        free_row_buffers(b_right, b_left, b_top, b_bottom_cur, b_bottom_prev,
                         n_right, n_left, n_top, n_bottom_cur, n_bottom_prev);
        free(mask); free(labels);
        return gmif_set_error(GMIF_ERR_ALLOC,
            "allocation failed: tile buffers (tile=%d, tiles=%lld)",
            tile, (long long)ntiles);
    }

    int64_t done_tiles = 0;
    int fail = 0;       /* 1 = native error, 2 = cancelled */
    int fail_rc = GMIF_OK;
    int report_stride = (int)(ntiles / 200);
    if (report_stride < 8) report_stride = 8;

    for (int tz = 0; tz < ntz && !fail; tz++) {
        for (int tx = 0; tx < ntx; tx++) {
            if (should_stop(&ctx)) { fail = 2; break; }

            int x0 = tx * tile;
            int z0 = tz * tile;
            int w = (x0 + tile <= sx) ? tile : (sx - x0);
            int h = (z0 + tile <= sz) ? tile : (sz - z0);

            rc = gmif_fill_mask_range(&g, params, scale,
                                      plan.cell_x0 + x0, plan.cell_z0 + z0,
                                      w, h, mask, control, -1, -1);
            if (rc != GMIF_OK) {
                fail = (rc == GMIF_ERR_CANCELLED) ? 2 : 1;
                fail_rc = rc;
                break;
            }

            int nloc = 0;
            rc = tile_cca(mask, w, h, labels, &nloc);
            if (rc != GMIF_OK) { fail = 1; fail_rc = rc; break; }

            if (nloc > GMIF_MAX_REGIONS - reg_n) {
                fail = 1;
                fail_rc = gmif_set_error(GMIF_ERR_GRID_TOO_LARGE,
                    "target biome is too fragmented for a single search:\n"
                    "  more than %d connected regions would be required.\n"
                    "  Reduce the radius or use a coarser sample scale.",
                    GMIF_MAX_REGIONS);
                break;
            }
            int reg_base = reg_n;
            rc = grow_regions(&regions, &reg_cap, reg_n + nloc);
            if (rc != GMIF_OK) { fail = 1; fail_rc = rc; break; }
            reg_n += nloc;
            tile_region_stats(mask, labels, w, h, nloc, x0, z0, regions, reg_base);

            /* Collect this tile's four edges into the current row buffers. */
            int base = tx * tile;
            for (int z = 0; z < h; z++) {
                int lab = labels[z * w + (w - 1)];
                if (lab <= 0) continue;
                BorderCell *c = &b_right[base + n_right[tx]++];
                c->region = reg_base + lab - 1;
                c->coord = z0 + z;
            }
            for (int z = 0; z < h; z++) {
                int lab = labels[z * w];
                if (lab <= 0) continue;
                BorderCell *c = &b_left[base + n_left[tx]++];
                c->region = reg_base + lab - 1;
                c->coord = z0 + z;
            }
            for (int x = 0; x < w; x++) {
                int lab = labels[(h - 1) * w + x];
                if (lab <= 0) continue;
                BorderCell *c = &b_bottom_cur[base + n_bottom_cur[tx]++];
                c->region = reg_base + lab - 1;
                c->coord = x0 + x;
            }
            for (int x = 0; x < w; x++) {
                int lab = labels[x];
                if (lab <= 0) continue;
                BorderCell *c = &b_top[base + n_top[tx]++];
                c->region = reg_base + lab - 1;
                c->coord = x0 + x;
            }

            done_tiles++;
            if ((done_tiles % report_stride) == 0 || done_tiles == ntiles) {
                int pct = (int)(done_tiles * 78 / ntiles);
                if (pct > 78) pct = 78;
                report(&ctx, pct, "tile");
            }
        }
        if (fail) break;

        /* Horizontal merge inside this row. */
        for (int tx = 0; tx + 1 < ntx; tx++) {
            int base = tx * tile;
            merge_border_lists(regions,
                               &b_right[base], n_right[tx],
                               &b_left[base + tile], n_left[tx + 1]);
        }
        if (should_stop(&ctx)) { fail = 2; break; }

        /* Vertical merge against the previous row. */
        for (int tx = 0; tx < ntx; tx++) {
            int base = tx * tile;
            merge_border_lists(regions,
                               &b_bottom_prev[base], n_bottom_prev[tx],
                               &b_top[base], n_top[tx]);
        }
        if (should_stop(&ctx)) { fail = 2; break; }

        /* The current bottom becomes the previous bottom for the next row. */
        BorderCell *tmpc = b_bottom_cur;
        b_bottom_cur = b_bottom_prev;
        b_bottom_prev = tmpc;
        int *tmpn = n_bottom_cur;
        n_bottom_cur = n_bottom_prev;
        n_bottom_prev = tmpn;

        /* Reset current-row buffers for the next row. */
        for (int tx = 0; tx < ntx; tx++) {
            n_right[tx] = 0;
            n_left[tx] = 0;
            n_top[tx] = 0;
            n_bottom_cur[tx] = 0;
        }
    }

    free(mask);
    free(labels);

    if (fail) {
        free(regions);
        free_row_buffers(b_right, b_left, b_top, b_bottom_cur, b_bottom_prev,
                         n_right, n_left, n_top, n_bottom_cur, n_bottom_prev);
        if (fail == 2) return GMIF_ERR_CANCELLED;
        return fail_rc != GMIF_OK ? fail_rc
             : gmif_set_error(GMIF_ERR_BIOME_GEN, "tiled scan failed");
    }
    if (should_stop(&ctx)) {
        free(regions);
        free_row_buffers(b_right, b_left, b_top, b_bottom_cur, b_bottom_prev,
                         n_right, n_left, n_top, n_bottom_cur, n_bottom_prev);
        return GMIF_ERR_CANCELLED;
    }

    free_row_buffers(b_right, b_left, b_top, b_bottom_cur, b_bottom_prev,
                     n_right, n_left, n_top, n_bottom_cur, n_bottom_prev);

    report(&ctx, 80, "merge");
    report(&ctx, 84, "refine");

    /* Aggregate roots. */
    for (int i = 0; i < reg_n; i++) {
        int root = dsu_find(regions, i);
        if (root == i) continue;
        TileRegion *a = &regions[root];
        TileRegion *b = &regions[i];
        a->samples += b->samples;
        a->outer_edges += b->outer_edges;
        a->shared_pairs += b->shared_pairs;
        if (b->samples > 0) {
            if (b->min_sx < a->min_sx) a->min_sx = b->min_sx;
            if (b->max_sx > a->max_sx) a->max_sx = b->max_sx;
            if (b->min_sz < a->min_sz) a->min_sz = b->min_sz;
            if (b->max_sz > a->max_sz) a->max_sz = b->max_sz;
        }
        b->samples = 0; /* consumed */
    }

    int nroot = 0;
    for (int i = 0; i < reg_n; i++) {
        if (regions[i].samples > 0 && dsu_find(regions, i) == i) nroot++;
    }
    MushroomIsland *islands = (MushroomIsland *)calloc(
        (size_t)(nroot > 0 ? nroot : 1), sizeof(MushroomIsland));
    if (!islands) {
        free(regions);
        return gmif_set_error(GMIF_ERR_ALLOC, "allocation failed: final island list");
    }

    int kept = 0;
    int processed = 0;
    int fine = params->refine ? (scale > 4 ? 4 : 1) : scale;
    for (int i = 0; i < reg_n; i++) {
        if (should_stop(&ctx)) {
            free(regions);
            free(islands);
            return GMIF_ERR_CANCELLED;
        }
        if (regions[i].samples <= 0 || dsu_find(regions, i) != i) continue;
        processed++;
        if (nroot > 0 && ((processed & 15) == 0 || processed == nroot)) {
            int pct = 84 + (int)((int64_t)processed * 14 / nroot);
            if (pct > 98) pct = 98;
            report(&ctx, pct, "refine");
        }
        MushroomIsland isl;
        memset(&isl, 0, sizeof(isl));
        fill_island_from_region(&regions[i], &plan, params, &isl);
        if (params->refine) {
            if (!(params->min_area > 0
                  && tile_refine_upper(&regions[i], scale) < params->min_area)) {
                int rrc = gmif_refine_island(&g, params, scale, fine, &isl);
                if (rrc < 0) {
                    free(regions);
                    free(islands);
                    return rrc;
                }
            }
        }
        /* Drop the whole merged region when lush caves sit below it. */
        if (params->exclude_lush_caves && gmif_targets_mushroom(params)) {
            int lush = gmif_island_has_lush_caves(&g, params, &isl, ctx.control,
                                                  ctx.cancel, ctx.ud);
            if (lush < 0) {
                free(regions);
                free(islands);
                return lush;
            }
            if (lush > 0) continue;
        }
        if (isl.area >= params->min_area) {
            islands[kept++] = isl;
            ctx.found = kept;
        }
    }

    free(regions);

    if (kept > 1)
        qsort(islands, (size_t)kept, sizeof(MushroomIsland), cmp_area_desc);
    if (params->max_results > 0 && kept > params->max_results)
        kept = params->max_results;

    report(&ctx, 100, "done");
    *out = islands;
    *out_count = kept;
    return GMIF_OK;
}
