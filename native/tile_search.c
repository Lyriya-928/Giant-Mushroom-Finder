/*
 * Giant Mushroom Island Finder 锟?tiled streaming search + DSU merge
 *
 * Processes the sample grid in tiles (bounded memory). Each tile runs the
 * same mushroom mask + 4-neighbor CCA as the monolithic path. Regions that
 * touch tile borders are merged with Union-Find when adjacent tiles share
 * a mushroom-to-mushroom border pair.
 *
 * CCA semantics unchanged (4-neighbor). Cubiomes not modified.
 *
 * Search tiling + global merge concept inspired by multi-tile region
 * pipelines in seed-finder tools (architecture only; no source copied).
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

/* Reuse error helpers from mushroom_finder.c via extern declarations
 * matching those translation units' public API. */

/* ------------------------------------------------------------------ */
/* local types                                                         */
/* ------------------------------------------------------------------ */

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
            if (stage[0] == 't' && stage[1] == 'i') ctx->control->stage = 4; /* tile */
            else if (stage[0] == 'm') ctx->control->stage = 5; /* merge */
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
    ctx->progress(&sp, ctx->ud);
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
/* DSU                                                                 */
/* ------------------------------------------------------------------ */

typedef struct TileRegion {
    int parent;            /* DSU parent index in this array */
    int rank;
    int64_t samples;
    /* global sample-grid bounds (inclusive) */
    int min_sx, max_sx, min_sz, max_sz;
    int64_t outer_edges;   /* tile-local exposed edges */
    int64_t shared_pairs;  /* mushroom adjacencies across tile borders (internal) */
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

/* Border contact record: one mushroom cell on a tile edge. */
typedef struct BorderCell {
    int32_t region;   /* index into regions[] */
    int32_t coord;    /* row or col along the edge */
} BorderCell;

/* ------------------------------------------------------------------ */
/* tile-local CCA (same 4-neighbor semantics as monolithic path)       */
/* ------------------------------------------------------------------ */

static int tile_cca(const unsigned char *mask, int w, int h,
                    int *labels, int *n_out,
                    /* optional per-cell edge count not stored; we recount */
                    int64_t *out_samples /* array of w*h is not needed */)
{
    (void)out_samples;
    memset(labels, 0, (size_t)w * h * sizeof(int));
    int n = 0;
    int *stack = (int *)malloc((size_t)w * h * sizeof(int));
    if (!stack) return GMIF_ERR_ALLOC;

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
    int ncap = *cap ? *cap : 64;
    while (ncap < need) ncap *= 2;
    TileRegion *na = (TileRegion *)realloc(*arr, (size_t)ncap * sizeof(TileRegion));
    if (!na) return GMIF_ERR_ALLOC;
    *arr = na;
    *cap = ncap;
    return GMIF_OK;
}

static int grow_border(BorderCell **arr, int *cap, int need)
{
    if (need <= *cap) return GMIF_OK;
    int ncap = *cap ? *cap : 256;
    while (ncap < need) ncap *= 2;
    BorderCell *na = (BorderCell *)realloc(*arr, (size_t)ncap * sizeof(BorderCell));
    if (!na) return GMIF_ERR_ALLOC;
    *arr = na;
    *cap = ncap;
    return GMIF_OK;
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
    return 0;
}

static void fill_island_from_region(const TileRegion *r, int scale,
                                    int world_x0, int world_z0,
                                    MushroomIsland *out)
{
    int min_x = world_x0 + r->min_sx * scale;
    int max_x = world_x0 + (r->max_sx + 1) * scale - 1;
    int min_z = world_z0 + r->min_sz * scale;
    int max_z = world_z0 + (r->max_sz + 1) * scale - 1;
    out->center_x = (min_x + max_x) / 2;
    out->center_z = (min_z + max_z) / 2;
    out->min_x = min_x;
    out->max_x = max_x;
    out->min_z = min_z;
    out->max_z = max_z;
    out->width = max_x - min_x + 1;
    out->height = max_z - min_z + 1;
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
    out->distance = dist2d((double)out->center_x, (double)out->center_z);
    {
        double A = (double)out->area;
        double P = (double)out->perimeter;
        out->compactness = (P > 0) ? (4.0 * 3.141592653589793 * A) / (P * P) : 0.0;
    }
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

    SearchCtx ctx;
    ctx.progress = progress;
    ctx.cancel = cancel;
    ctx.ud = userdata;
    ctx.control = control;
    ctx.found = 0;

    int scale = params->scale > 0 ? params->scale : GMIF_SCALE_CHUNK;
    if (scale != 1 && scale != 4 && scale != 16 && scale != 64 && scale != 256)
        scale = GMIF_SCALE_CHUNK;
    if (params->mc < 0) {
        return gmif_set_error(GMIF_ERR_VERSION, "invalid Minecraft version id %d", params->mc);
    }

    int half = (params->radius + scale - 1) / scale;
    int sx = half * 2;
    int sz = half * 2;
    int world_x0 = -half * scale;
    int world_z0 = -half * scale;

    /* Auto tile size in sample cells (bounded memory per tile). */
    int tile = params->tile_size;
    if (tile <= 0) {
        tile = 256;
        if (scale == 1) tile = 128;
    }
    if (tile < 8) tile = 8;
    if (tile > 2048) tile = 2048;

    int nt_x = (sx + tile - 1) / tile;
    int nt_z = (sz + tile - 1) / tile;
    int64_t ntiles = (int64_t)nt_x * nt_z;

    report(&ctx, 0, "tile");

    Generator g;
    setupGenerator(&g, params->mc, 0);
    applySeed(&g, DIM_OVERWORLD, (uint64_t)params->seed);
    int y = (scale == 1) ? 63 : 15;

    TileRegion *regions = NULL;
    int reg_cap = 0, reg_n = 0;

    /* Per-tile: first global region index and count (for border lookup). */
    int *tile_reg_off = (int *)calloc((size_t)ntiles > 0 ? (size_t)ntiles : 1, sizeof(int));
    int *tile_reg_cnt = (int *)calloc((size_t)ntiles > 0 ? (size_t)ntiles : 1, sizeof(int));
    if (!tile_reg_off || !tile_reg_cnt) {
        free(tile_reg_off); free(tile_reg_cnt);
        return gmif_set_error(GMIF_ERR_ALLOC, "allocation failed: tile index arrays");
    }

    BorderCell *b_right = NULL, *b_left = NULL, *b_bottom = NULL, *b_top = NULL;
    int cap_r = 0, cap_l = 0, cap_b = 0, cap_t = 0;
    int n_r = 0, n_l = 0, n_b = 0, n_t = 0;
    /* Per tile, offsets into border arrays */
    int *off_r = (int *)calloc((size_t)ntiles, sizeof(int));
    int *off_l = (int *)calloc((size_t)ntiles, sizeof(int));
    int *off_b = (int *)calloc((size_t)ntiles, sizeof(int));
    int *off_t = (int *)calloc((size_t)ntiles, sizeof(int));
    if (!off_r || !off_l || !off_b || !off_t) {
        free(tile_reg_off); free(tile_reg_cnt);
        free(off_r); free(off_l); free(off_b); free(off_t);
        return gmif_set_error(GMIF_ERR_ALLOC, "allocation failed: border index arrays");
    }

    unsigned char *mask = (unsigned char *)malloc((size_t)tile * tile);
    int *labels = (int *)malloc((size_t)tile * tile * sizeof(int));
    int *biomeIds = NULL;
    {
        Range rr;
        rr.scale = scale;
        rr.x = 0; rr.z = 0; rr.sx = tile; rr.sz = tile; rr.y = y; rr.sy = 1;
        biomeIds = allocCache(&g, rr);
    }
    if (!mask || !labels || !biomeIds) {
        free(mask); free(labels); free(biomeIds);
        free(regions); free(tile_reg_off); free(tile_reg_cnt);
        free(off_r); free(off_l); free(off_b); free(off_t);
        free(b_right); free(b_left); free(b_bottom); free(b_top);
        return gmif_set_error(GMIF_ERR_ALLOC,
            "allocation failed: tile buffers (tile=%d cells)", tile);
    }

    int64_t done_tiles = 0;
    int fail = 0;

    for (int tz = 0; tz < nt_z && !fail; tz++) {
        for (int tx = 0; tx < nt_x; tx++) {
            if (should_stop(&ctx)) { fail = 2; break; }

            int x0 = tx * tile;
            int z0 = tz * tile;
            int w = (x0 + tile <= sx) ? tile : (sx - x0);
            int h = (z0 + tile <= sz) ? tile : (sz - z0);
            int tid = tz * nt_x + tx;

            Range r;
            r.scale = scale;
            r.x = -half + x0;
            r.z = -half + z0;
            r.sx = w;
            r.sz = h;
            r.y = y;
            r.sy = 1;
            (void)r;

            if (gmif_fill_mask_range(&g, scale, -half + x0, -half + z0, w, h,
                                     params->include_shore, mask) != GMIF_OK) {
                fail = 1;
                break;
            }

            int nloc = 0;
            if (tile_cca(mask, w, h, labels, &nloc, NULL) != GMIF_OK) {
                fail = 1;
                break;
            }

            int reg_base = reg_n;
            if (grow_regions(&regions, &reg_cap, reg_n + nloc) != GMIF_OK) {
                fail = 1;
                break;
            }
            reg_n += nloc;
            tile_reg_off[tid] = reg_base;
            tile_reg_cnt[tid] = nloc;
            tile_region_stats(mask, labels, w, h, nloc, x0, z0, regions, reg_base);

            /* Border samples for merge */
            off_r[tid] = n_r;
            for (int z = 0; z < h; z++) {
                int lab = labels[z * w + (w - 1)];
                if (lab <= 0) continue;
                if (grow_border(&b_right, &cap_r, n_r + 1) != GMIF_OK) { fail = 1; break; }
                b_right[n_r].region = reg_base + lab - 1;
                b_right[n_r].coord = z0 + z;
                n_r++;
            }
            off_l[tid] = n_l;
            for (int z = 0; z < h; z++) {
                int lab = labels[z * w + 0];
                if (lab <= 0) continue;
                if (grow_border(&b_left, &cap_l, n_l + 1) != GMIF_OK) { fail = 1; break; }
                b_left[n_l].region = reg_base + lab - 1;
                b_left[n_l].coord = z0 + z;
                n_l++;
            }
            off_b[tid] = n_b;
            for (int x = 0; x < w; x++) {
                int lab = labels[(h - 1) * w + x];
                if (lab <= 0) continue;
                if (grow_border(&b_bottom, &cap_b, n_b + 1) != GMIF_OK) { fail = 1; break; }
                b_bottom[n_b].region = reg_base + lab - 1;
                b_bottom[n_b].coord = x0 + x;
                n_b++;
            }
            off_t[tid] = n_t;
            for (int x = 0; x < w; x++) {
                int lab = labels[0 * w + x];
                if (lab <= 0) continue;
                if (grow_border(&b_top, &cap_t, n_t + 1) != GMIF_OK) { fail = 1; break; }
                b_top[n_t].region = reg_base + lab - 1;
                b_top[n_t].coord = x0 + x;
                n_t++;
            }

            done_tiles++;
            if ((done_tiles & 7) == 0 || done_tiles == ntiles) {
                int pct = (int)(done_tiles * 80 / ntiles);
                if (pct > 80) pct = 80;
                report(&ctx, pct, "tile");
            }
        }
    }

    free(mask); free(labels); free(biomeIds);
    if (fail == 2) {
        free(regions); free(tile_reg_off); free(tile_reg_cnt);
        free(off_r); free(off_l); free(off_b); free(off_t);
        free(b_right); free(b_left); free(b_bottom); free(b_top);
        return GMIF_ERR_CANCELLED;
    }
    if (fail) {
        free(regions); free(tile_reg_off); free(tile_reg_cnt);
        free(off_r); free(off_l); free(off_b); free(off_t);
        free(b_right); free(b_left); free(b_bottom); free(b_top);
        return gmif_set_error(GMIF_ERR_BIOME_GEN, "biome sampling failed inside tiled scan");
    }

    report(&ctx, 82, "merge");

    /* Merge horizontal neighbors: tile (tx,tz) right with (tx+1,tz) left */
    for (int tz = 0; tz < nt_z; tz++) {
        if (should_stop(&ctx)) {
            free(regions); free(tile_reg_off); free(tile_reg_cnt);
            free(off_r); free(off_l); free(off_b); free(off_t);
            free(b_right); free(b_left); free(b_bottom); free(b_top);
            return GMIF_ERR_CANCELLED;
        }
        for (int tx = 0; tx + 1 < nt_x; tx++) {
            int tA = tz * nt_x + tx;
            int tB = tz * nt_x + tx + 1;
            int ar0 = off_r[tA], ar1 = (tx + 1 < nt_x - 1) ? off_r[tA + 1] : n_r;
            /* end of A's right border = start of next tile's right, or n_r */
            int nextA = (tA + 1 < ntiles) ? off_r[tA + 1] : n_r;
            int nextB = (tB + 1 < ntiles) ? off_r[tB + 1] : n_r;
            (void)nextB;
            int br0 = off_l[tB];
            int br1 = (tB + 1 < ntiles) ? off_l[tB + 1] : n_l;
            (void)ar1;
            /* Linear merge two sorted-by-coord lists (coords along Z) */
            int i = ar0, j = br0;
            int i_end = (tA + 1 < ntiles) ? off_r[tA + 1] : n_r;
            /* Actually off_* stores START of each tile's chunk in insertion order
             * (row-major tiles), so end of tile t is start of next tile in that
             * tile-loop order 锟?which is exactly t+1. */
            i_end = (tA + 1 <= ntiles - 1) ? off_r[tA + 1] : n_r;
            /* Wait: off_r[t] is set when tile t is processed; tiles processed in
             * tz/tx order so off_r[tA]+count = off_r[tA+1] for sequential t.
             * Yes end = off_r[tA+1] or n_r if last. */
            while (i < i_end && j < br1) {
                if (b_right[i].coord == b_left[j].coord) {
                    int ra = dsu_find(regions, b_right[i].region);
                    int rb = dsu_find(regions, b_left[j].region);
                    if (ra != rb) dsu_union(regions, ra, rb);
                    /* One shared adjacency; attribute once (will be summed on root). */
                    regions[b_right[i].region].shared_pairs++;
                    i++; j++;
                } else if (b_right[i].coord < b_left[j].coord) {
                    i++;
                } else {
                    j++;
                }
            }
            (void)nextA;
        }
    }

    /* Merge vertical neighbors: tile (tx,tz) bottom with (tx,tz+1) top */
    for (int tz = 0; tz + 1 < nt_z; tz++) {
        if (should_stop(&ctx)) {
            free(regions); free(tile_reg_off); free(tile_reg_cnt);
            free(off_r); free(off_l); free(off_b); free(off_t);
            free(b_right); free(b_left); free(b_bottom); free(b_top);
            return GMIF_ERR_CANCELLED;
        }
        for (int tx = 0; tx < nt_x; tx++) {
            int tA = tz * nt_x + tx;
            int tB = (tz + 1) * nt_x + tx;
            int i = off_b[tA];
            int i_end = (tA + 1 < ntiles) ? off_b[tA + 1] : n_b;
            int j = off_t[tB];
            int j_end = (tB + 1 < ntiles) ? off_t[tB + 1] : n_t;
            while (i < i_end && j < j_end) {
                if (b_bottom[i].coord == b_top[j].coord) {
                    int ra = dsu_find(regions, b_bottom[i].region);
                    int rb = dsu_find(regions, b_top[j].region);
                    if (ra != rb) dsu_union(regions, ra, rb);
                    regions[b_bottom[i].region].shared_pairs++;
                    i++; j++;
                } else if (b_bottom[i].coord < b_top[j].coord) {
                    i++;
                } else {
                    j++;
                }
            }
        }
    }

    report(&ctx, 90, "refine");

    /* Aggregate roots */
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

    /* Collect root regions as islands (all of them; no per-tile TopN). */
    int nroot = 0;
    for (int i = 0; i < reg_n; i++) {
        if (regions[i].samples > 0 && dsu_find(regions, i) == i) nroot++;
    }
    MushroomIsland *islands = (MushroomIsland *)calloc((size_t)(nroot > 0 ? nroot : 1),
                                                       sizeof(MushroomIsland));
    if (!islands) {
        free(regions); free(tile_reg_off); free(tile_reg_cnt);
        free(off_r); free(off_l); free(off_b); free(off_t);
        free(b_right); free(b_left); free(b_bottom); free(b_top);
        return gmif_set_error(GMIF_ERR_ALLOC, "allocation failed: final island list");
    }

    int kept = 0;
    int fine = params->refine ? (scale > 4 ? 4 : 1) : scale;
    for (int i = 0; i < reg_n; i++) {
        if (regions[i].samples <= 0 || dsu_find(regions, i) != i) continue;
        MushroomIsland isl;
        memset(&isl, 0, sizeof(isl));
        fill_island_from_region(&regions[i], scale, world_x0, world_z0, &isl);
        if (params->refine) {
            gmif_refine_island(&g, scale, fine, params->include_shore, &isl);
        }
        if (isl.area >= params->min_area) {
            islands[kept++] = isl;
            ctx.found = kept;
        }
    }

    free(regions); free(tile_reg_off); free(tile_reg_cnt);
    free(off_r); free(off_l); free(off_b); free(off_t);
    free(b_right); free(b_left); free(b_bottom); free(b_top);

    if (kept > 1)
        qsort(islands, (size_t)kept, sizeof(MushroomIsland), cmp_area_desc);
    if (params->max_results > 0 && kept > params->max_results)
        kept = params->max_results;

    report(&ctx, 100, "done");
    *out = islands;
    *out_count = kept;
    return GMIF_OK;
}
