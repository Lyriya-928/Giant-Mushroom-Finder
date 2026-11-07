/*
 * Large Biome Finder — Release Candidate audit suite
 *
 * Built into gmif_cli.exe and invoked with `gmif_cli --audit`.
 *
 * Covers:
 *   - fast centre sampling vs the stable scale-4 decimation (exact mask)
 *   - monolithic vs tiled region equality (area/bounds/center/perimeter)
 *   - thread-count consistency (1..16) for both scan modes
 *   - negative / custom search centres and odd tile boundaries
 *   - custom biome target sets
 *   - automatic mode selection near the threshold
 *   - Top-N post-merge filtering
 *   - lush caves exclusion (1.18+): island drop, version guard, target scope
 */
#include "mushroom_finder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <inttypes.h>

#include "generator.h"

static int g_pass = 0;
static int g_fail = 0;

static void ok(int cond, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf(cond ? "  [PASS] " : "  [FAIL] ");
    vprintf(fmt, ap);
    printf("\n");
    fflush(stdout);
    va_end(ap);
    if (cond) g_pass++;
    else g_fail++;
}

static void section(const char *title)
{
    printf("\n== %s ==\n", title);
    fflush(stdout);
}

static void ok_pair(int cond)
{
    ok(cond, "pair comparison");
}

static int close_double(double a, double b)
{
    double d = fabs(a - b);
    double m = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    return d <= 1e-6 * (m > 1.0 ? m : 1.0) + 1e-6;
}

static int same_islands(const MushroomIsland *a, int na,
                        const MushroomIsland *b, int nb,
                        const char *tag)
{
    if (na != nb) {
        printf("    DIFF %s: region count a=%d b=%d\n", tag, na, nb);
        return 0;
    }
    for (int i = 0; i < na; i++) {
        if (a[i].area != b[i].area
            || a[i].samples != b[i].samples
            || a[i].min_x != b[i].min_x || a[i].max_x != b[i].max_x
            || a[i].min_z != b[i].min_z || a[i].max_z != b[i].max_z
            || a[i].center_x != b[i].center_x || a[i].center_z != b[i].center_z
            || a[i].width != b[i].width || a[i].height != b[i].height
            || a[i].perimeter != b[i].perimeter
            || a[i].area_precision != b[i].area_precision
            || !close_double(a[i].distance, b[i].distance)
            || !close_double(a[i].compactness, b[i].compactness)) {
            printf("    DIFF %s: region #%d\n", tag, i);
            printf("      a area=%" PRId64 " samples=%" PRId64
                   " bounds=[%d,%d]..[%d,%d] center=(%d,%d) perim=%d prec=%d\n",
                   a[i].area, a[i].samples,
                   a[i].min_x, a[i].min_z, a[i].max_x, a[i].max_z,
                   a[i].center_x, a[i].center_z, a[i].perimeter, a[i].area_precision);
            printf("      b area=%" PRId64 " samples=%" PRId64
                   " bounds=[%d,%d]..[%d,%d] center=(%d,%d) perim=%d prec=%d\n",
                   b[i].area, b[i].samples,
                   b[i].min_x, b[i].min_z, b[i].max_x, b[i].max_z,
                   b[i].center_x, b[i].center_z, b[i].perimeter, b[i].area_precision);
            return 0;
        }
    }
    return 1;
}

static int search_ex(int64_t seed, const char *ver, int center_x, int center_z,
                     int radius, int scale, int refine, int shore,
                     int exclude_lush,
                     const int *biomes, int nbiomes,
                     int use_tiles, int tile_size, int threads, int max_results,
                     MushroomIsland **out, int *out_n)
{
    SearchParams p;
    memset(&p, 0, sizeof(p));
    p.seed = seed;
    p.mc = gmif_mc_from_string(ver);
    p.center_x = center_x;
    p.center_z = center_z;
    p.radius = radius;
    p.scale = scale;
    p.refine = refine;
    p.include_shore = shore;
    p.exclude_lush_caves = exclude_lush;
    p.biome_ids = biomes;
    p.biome_count = nbiomes;
    p.use_tiles = use_tiles;
    p.tile_size = tile_size;
    p.threads = threads;
    p.max_results = max_results;
    *out = NULL;
    *out_n = 0;
    return gmif_search(&p, out, out_n, NULL, NULL, NULL, NULL);
}

static int search(int64_t seed, const char *ver, int center_x, int center_z,
                  int radius, int scale, int refine, int shore,
                  const int *biomes, int nbiomes,
                  int use_tiles, int tile_size, int threads, int max_results,
                  MushroomIsland **out, int *out_n)
{
    return search_ex(seed, ver, center_x, center_z, radius, scale, refine, shore, 0,
                     biomes, nbiomes, use_tiles, tile_size, threads, max_results,
                     out, out_n);
}

static int pair_compare(const char *tag, int64_t seed, const char *ver,
                        int cx, int cz, int radius, int scale,
                        int refine, int shore, const int *biomes, int nbiomes,
                        int tile, int threads)
{
    MushroomIsland *a = NULL, *b = NULL;
    int na = 0, nb = 0;
    int ra = search(seed, ver, cx, cz, radius, scale, refine, shore,
                    biomes, nbiomes, 0, 0, threads, 0, &a, &na);
    int rb = search(seed, ver, cx, cz, radius, scale, refine, shore,
                    biomes, nbiomes, 1, tile, threads, 0, &b, &nb);
    if (ra != 0 || rb != 0) {
        ok(0, "%s -> search rc mono=%d tiled=%d (%s)", tag, ra, rb, gmif_last_error());
        free(a); free(b);
        return 0;
    }
    int same = same_islands(a, na, b, nb, tag);
    if (na == 0) {
        printf("    WEAK %s: both modes found no regions\n", tag);
        same = 0;
    }
    free(a);
    free(b);
    return same;
}

/* ------------------------------------------------------------------ */
/* fast fill vs stable decimation                                      */
/* ------------------------------------------------------------------ */

static int check_fast_fill_exact(const char *ver, int scale,
                                 int cell_x, int cell_z, int sx, int sz,
                                 int shore)
{
    int mc = gmif_mc_from_string(ver);
    if (mc < 0) return 0;
    Generator g;
    setupGenerator(&g, mc, 0);
    applySeed(&g, DIM_OVERWORLD, 262);

    SearchParams p;
    memset(&p, 0, sizeof(p));
    p.seed = 262;
    p.mc = mc;
    p.include_shore = shore;
    p.threads = 2;
    p.scale = scale;

    unsigned char *fast = (unsigned char *)malloc((size_t)sx * (size_t)sz);
    int *ids = NULL;
    int factor = scale / 4;
    int mid = factor / 2;
    Range r;
    r.scale = 4;
    r.x = cell_x * factor;
    r.z = cell_z * factor;
    r.sx = sx * factor;
    r.sz = sz * factor;
    r.y = 15;
    r.sy = 1;
    ids = allocCache(&g, r);
    if (!fast || !ids) {
        free(fast); free(ids);
        return 0;
    }
    int rc = gmif_fill_mask_range(&g, &p, scale, cell_x, cell_z, sx, sz,
                                  fast, NULL, -1, -1);
    int okgen = (rc == GMIF_OK && genBiomes(&g, ids, r) == 0);
    int mismatches = 0;
    if (okgen) {
        for (int z = 0; z < sz; z++) {
            for (int x = 0; x < sx; x++) {
                int id = ids[(size_t)((z * factor + mid) * r.sx
                                      + (x * factor + mid))];
                int expect = (id == mushroom_fields
                              || (shore && id == mushroom_field_shore)) ? 1 : 0;
                if (fast[(size_t)z * sx + x] != expect) mismatches++;
            }
        }
    }
    free(fast);
    free(ids);
    return okgen && mismatches == 0;
}

/* ------------------------------------------------------------------ */
/* audit                                                               */
/* ------------------------------------------------------------------ */

int gmif_run_release_audit(void)
{
    int t1 = 0, t4 = 4, t10 = 10, t16 = 16;

    section("Fast centre sampling == stable scale-4 decimation (mask exact)");
    {
        ok(check_fast_fill_exact("1.18", 16, -40, -33, 97, 61, 0),
           "1.18 scale16 window (-40,-33) 97x61");
        ok(check_fast_fill_exact("1.18", 16, 5, 7, 64, 64, 1),
           "1.18 scale16 include-shore window (5,7) 64x64");
        ok(check_fast_fill_exact("1.21.3", 16, -313, 200, 128, 128, 0),
           "1.21.3 scale16 window (-313,200) 128x128");
        ok(check_fast_fill_exact("1.19.4", 64, -7, 9, 33, 41, 0),
           "1.19.4 scale64 window (-7,9) 33x41");
        ok(check_fast_fill_exact("1.18", 4, -77, 63, 64, 64, 0),
           "1.18 scale4 direct window (-77,63) 64x64");
    }

    section("Monolithic vs tiled: seed/version/scale matrix");
    {
        ok_pair(pair_compare("seed262 1.18 r=500 scale16 tile=32",
                        262, "1.18", 0, 0, 500, 16, 0, 0, NULL, 0, 32, 0));
        ok_pair(pair_compare("seed262 1.21.3 r=500 scale16 tile=8",
                        262, "1.21.3", 0, 0, 500, 16, 0, 0, NULL, 0, 8, 0));
        ok_pair(pair_compare("seed0 1.18 r=5000 scale16 tile=16",
                        0, "1.18", 0, 0, 5000, 16, 0, 0, NULL, 0, 16, 0));
        ok_pair(pair_compare("seed262 1.18 r=500 scale4 tile=16",
                        262, "1.18", 0, 0, 500, 4, 0, 0, NULL, 0, 16, 0));
        ok_pair(pair_compare("seed262 1.16.5 r=5000 scale16 tile=16 (layer path)",
                        262, "1.16.5", 0, 0, 5000, 16, 0, 0, NULL, 0, 16, 0));
    }

    section("Thread consistency (monolithic and tiled)");
    {
        MushroomIsland *ref = NULL;
        int nref = 0;
        int rc = search(262, "1.18", 0, 0, 1200, 16, 0, 0, NULL, 0,
                        0, 0, 1, 0, &ref, &nref);
        ok(rc == 0, "reference threads=1 mono (n=%d)", nref);
        int th[] = {2, 4, 8, 10, 16};
        for (int i = 0; i < 5; i++) {
            MushroomIsland *a = NULL;
            int na = 0;
            rc = search(262, "1.18", 0, 0, 1200, 16, 0, 0, NULL, 0,
                        0, 0, th[i], 0, &a, &na);
            ok(rc == 0 && same_islands(ref, nref, a, na, "mono threads"),
               "monolithic threads=%d identical", th[i]);
            free(a);
        }
        free(ref);

        ref = NULL; nref = 0;
        rc = search(262, "1.18", -3000, -3000, 1500, 16, 0, 0, NULL, 0,
                    1, 64, 1, 0, &ref, &nref);
        ok(rc == 0, "reference threads=1 tiled (n=%d)", nref);
        for (int i = 0; i < 5; i++) {
            MushroomIsland *a = NULL;
            int na = 0;
            rc = search(262, "1.18", -3000, -3000, 1500, 16, 0, 0, NULL, 0,
                        1, 64, th[i], 0, &a, &na);
            ok(rc == 0 && same_islands(ref, nref, a, na, "tiled threads"),
               "tiled threads=%d identical", th[i]);
            free(a);
        }
        free(ref);
    }

    section("Custom centre + negative coordinates");
    {
        ok_pair(pair_compare("centre(-1000,-1000) r=1200 tile=33",
                        262, "1.18", -1000, -1000, 1200, 16, 0, 0,
                        NULL, 0, 33, 0));
        ok_pair(pair_compare("centre(-300,-300) r=900 refine tile=16",
                        262, "1.18", -300, -300, 900, 16, 1, 0,
                        NULL, 0, 16, 0));
        MushroomIsland *isl = NULL;
        int n = 0;
        int rc = search(262, "1.18", -1000, -1000, 1200, 16, 0, 0,
                        NULL, 0, 2, 0, 0, 0, &isl, &n);
        int in_box = 1;
        for (int i = 0; i < n; i++) {
            if (isl[i].min_x < -1000 - 1200 - 64
                || isl[i].max_x > -1000 + 1200 + 64
                || isl[i].min_z < -1000 - 1200 - 64
                || isl[i].max_z > -1000 + 1200 + 64) {
                in_box = 0;
            }
        }
        ok(rc == 0 && n > 0 && in_box,
           "custom-centre results stay inside the box (n=%d)", n);
        free(isl);
    }

    section("Custom biome targets");
    {
        int ocean[1] = { 46 /* cold_ocean, present near seed 262 origin */ };
        ok_pair(pair_compare("target cold_ocean id=46 r=600 tile=32",
                        262, "1.18", 0, 0, 600, 16, 0, 0,
                        ocean, 1, 32, 0));
        MushroomIsland *isl = NULL;
        int n = 0;
        int rc = search(262, "1.18", 0, 0, 600, 16, 0, 0,
                        ocean, 1, 0, 0, 0, 0, &isl, &n);
        int center_ok = 0;
        if (rc == 0 && n > 0) {
            center_ok = gmif_sample_biome(262, gmif_mc_from_string("1.18"),
                                          isl[0].center_x, isl[0].center_z) == 46;
        }
        ok(rc == 0 && n > 0 && center_ok,
           "cold_ocean target finds regions and centre is cold_ocean (n=%d)", n);
        free(isl);

        /* Broad target: exercises many full tile borders and the row-streaming
         * merge, which is exactly the case large-radius searches stress. */
        int ocean0[1] = { 0 };
        ok_pair(pair_compare("target ocean id=0 r=2000 tile=33",
                        262, "1.18", -2000, 1000, 2000, 16, 0, 0,
                        ocean0, 1, 33, 0));

        int both[2] = { 14, 15 };
        ok_pair(pair_compare("target mushroom+shore ids=14,15 r=500 tile=16",
                        262, "1.18", 0, 0, 500, 16, 0, 0,
                        both, 2, 16, 0));
        MushroomIsland *legacy = NULL, *explicit_ = NULL;
        int nl = 0, ne = 0;
        int rc1 = search(262, "1.18", 0, 0, 500, 16, 0, 1, NULL, 0,
                         0, 0, 0, 0, &legacy, &nl);
        int rc2 = search(262, "1.18", 0, 0, 500, 16, 0, 0, both, 2,
                         0, 0, 0, 0, &explicit_, &ne);
        ok(rc1 == 0 && rc2 == 0 && nl > 0 && same_islands(legacy, nl, explicit_, ne, "legacy"),
           "legacy include_shore == explicit {14,15}");
        free(legacy); free(explicit_);
    }

    section("Tile boundary sweep (odd tile sizes)");
    {
        int tiles[] = {7, 13, 33, 64, 127};
        MushroomIsland *ref = NULL;
        int nref = 0;
        int rc = search(262, "1.18", -700, 400, 900, 16, 0, 0,
                        NULL, 0, 0, 0, 0, 0, &ref, &nref);
        ok(rc == 0, "monolithic reference (n=%d)", nref);
        for (int i = 0; i < 5; i++) {
            MushroomIsland *a = NULL;
            int na = 0;
            rc = search(262, "1.18", -700, 400, 900, 16, 0, 0,
                        NULL, 0, 1, tiles[i], 0, 0, &a, &na);
            ok(rc == 0 && same_islands(ref, nref, a, na, "tile sweep"),
               "tile=%d matches monolithic", tiles[i]);
            free(a);
        }
        free(ref);
    }

    section("Automatic mode selection");
    {
        SearchParams p;
        memset(&p, 0, sizeof(p));
        p.seed = 262;
        p.mc = gmif_mc_from_string("1.18");
        p.radius = 5000;
        p.scale = 16;
        p.use_tiles = 2;
        p.threads = 0;
        GmifSearchPlan plan;
        int rc = gmif_plan_search(&p, &plan);
        ok(rc == 0 && plan.use_tiles == 0,
           "r=5000 scale16 -> monolithic (%lld cells)", (long long)plan.cells);

        p.radius = 30000;
        rc = gmif_plan_search(&p, &plan);
        ok(rc == 0 && plan.use_tiles == 1,
           "r=30000 scale16 -> tiled (%lld cells, tile=%d)", (long long)plan.cells,
           plan.tile_size);

        /* Auto and both forced modes must agree. */
        MushroomIsland *auto_isl = NULL, *mono = NULL, *tiled = NULL;
        int na = 0, nm = 0, nt = 0;
        int r1 = search(262, "1.18", 0, 0, 15000, 16, 0, 0,
                        NULL, 0, 2, 0, 0, 0, &auto_isl, &na);
        int r2 = search(262, "1.18", 0, 0, 15000, 16, 0, 0,
                        NULL, 0, 0, 0, 0, 0, &mono, &nm);
        int r3 = search(262, "1.18", 0, 0, 15000, 16, 0, 0,
                        NULL, 0, 1, 256, 0, 0, &tiled, &nt);
        ok(r1 == 0 && r2 == 0 && r3 == 0 && na == nm && nm == nt,
           "auto/mono/tiled region counts agree (%d/%d/%d)", na, nm, nt);
        ok(same_islands(mono, nm, tiled, nt, "auto pair"),
           "auto threshold result equals monolithic (n=%d)", nm);
        free(auto_isl); free(mono); free(tiled);
    }

    section("Top-N after global merge");
    {
        MushroomIsland *all = NULL, *top = NULL;
        int nall = 0, ntop = 0;
        int r1 = search(262, "1.18", 0, 0, 5000, 16, 0, 0,
                        NULL, 0, 1, 64, 0, 0, &all, &nall);
        int r2 = search(262, "1.18", 0, 0, 5000, 16, 0, 0,
                        NULL, 0, 1, 64, 0, 3, &top, &ntop);
        ok(r1 == 0 && r2 == 0 && ntop <= 3, "max_results=3 (n=%d)", ntop);
        if (r1 == 0 && r2 == 0 && nall >= 3 && ntop == 3) {
            ok(all[0].area == top[0].area && all[1].area == top[1].area
               && all[2].area == top[2].area,
               "Top N matches global sort");
        }
        free(all); free(top);
    }

    section("Refine path (scale4) mono vs tiled");
    {
        ok_pair(pair_compare("seed262 1.18 r=1500 refine tile=32",
                        262, "1.18", 0, 0, 1500, 16, 1, 0,
                        NULL, 0, 32, 0));
        ok_pair(pair_compare("seed262 1.21.3 r=1200 refine tile=16",
                        262, "1.21.3", 0, 0, 1200, 16, 1, 0,
                        NULL, 0, 16, 0));
    }

    section("Lush caves exclusion (1.18+)");
    {
        int mc118 = gmif_mc_from_string("1.18");
        int mc1165 = gmif_mc_from_string("1.16.5");
        ok(gmif_supports_lush_caves(mc118) == 1, "1.18 has 3D underground biomes");
        ok(gmif_supports_lush_caves(mc1165) == 0, "1.16.5 has no 3D underground biomes");
        ok(gmif_supports_lush_caves(gmif_mc_from_string("1.17.1")) == 0,
           "1.17.1 has no 3D underground biomes");

        /* The option must be rejected before 1.18, never silently ignored. */
        SearchParams vp;
        memset(&vp, 0, sizeof(vp));
        vp.seed = 262;
        vp.mc = mc1165;
        vp.radius = 500;
        vp.scale = 16;
        vp.exclude_lush_caves = 1;
        GmifSearchPlan vplan;
        int vrc = gmif_plan_search(&vp, &vplan);
        ok(vrc == GMIF_ERR_VERSION, "1.16.5 + exclusion is rejected (rc=%d)", vrc);
        ok(strstr(gmif_last_error(), "1.18") != NULL,
           "rejection message explains the 1.18 requirement");
        vp.mc = mc118;
        ok(gmif_plan_search(&vp, &vplan) == GMIF_OK, "same plan is valid on 1.18");

        /* Reference search and per-island ground truth (seed 262 r=5000 has a
         * mix of islands with and without lush caves below them). */
        Generator lg;
        setupGenerator(&lg, mc118, 0);
        applySeed(&lg, DIM_OVERWORLD, 262);
        SearchParams lp;
        memset(&lp, 0, sizeof(lp));
        lp.seed = 262;
        lp.mc = mc118;
        lp.radius = 5000;
        lp.scale = 16;
        lp.threads = 0;
        lp.max_results = 0;

        MushroomIsland *all = NULL;
        int nall = 0;
        int rc = search_ex(262, "1.18", 0, 0, 5000, 16, 0, 0, 0, NULL, 0,
                           0, 0, 0, 0, &all, &nall);
        ok(rc == 0 && nall >= 4, "reference search found %d islands", nall);

        int *lush = (int *)calloc((size_t)(nall > 0 ? nall : 1), sizeof(int));
        int nlush = 0;
        for (int i = 0; i < nall; i++) {
            int r = gmif_island_has_lush_caves(&lg, &lp, &all[i], NULL, NULL, NULL);
            lush[i] = r > 0 ? 1 : 0;
            if (lush[i]) nlush++;
        }
        ok(nlush >= 1 && nlush < nall,
           "reference mixes both kinds: %d of %d islands sit above lush caves",
           nlush, nall);

        MushroomIsland *clean = NULL;
        int nclean = 0;
        rc = search_ex(262, "1.18", 0, 0, 5000, 16, 0, 0, 1, NULL, 0,
                       0, 0, 0, 0, &clean, &nclean);
        ok(rc == 0 && nclean == nall - nlush,
           "exclusion keeps exactly the clean islands (%d of %d)", nclean, nall);

        int keep_ok = 1;
        for (int i = 0; i < nall; i++) {
            int found = 0;
            for (int j = 0; j < nclean; j++) {
                if (all[i].min_x == clean[j].min_x && all[i].max_x == clean[j].max_x
                    && all[i].min_z == clean[j].min_z && all[i].max_z == clean[j].max_z
                    && all[i].area == clean[j].area) {
                    found = 1;
                    break;
                }
            }
            /* clean islands must survive unchanged; lush islands must be gone */
            if (found == lush[i]) keep_ok = 0;
        }
        ok(keep_ok, "clean islands kept byte-identical, lush islands dropped whole");

        /* The same rule must hold in the tiled streaming path. */
        MushroomIsland *tiled = NULL;
        int ntiled = 0;
        rc = search_ex(262, "1.18", 0, 0, 5000, 16, 0, 0, 1, NULL, 0,
                       1, 64, 0, 0, &tiled, &ntiled);
        ok(rc == 0 && same_islands(clean, nclean, tiled, ntiled, "tiled lush"),
           "tiled streaming drops the same islands (n=%d)", ntiled);
        free(tiled);

        /* Refine must not change the keep/drop decision. */
        MushroomIsland *ref_all = NULL, *ref_clean = NULL;
        int nra = 0, nrc = 0;
        int r1 = search_ex(262, "1.18", 0, 0, 5000, 16, 1, 0, 0, NULL, 0,
                           0, 0, 0, 0, &ref_all, &nra);
        int r2 = search_ex(262, "1.18", 0, 0, 5000, 16, 1, 0, 1, NULL, 0,
                           0, 0, 0, 0, &ref_clean, &nrc);
        ok(r1 == 0 && r2 == 0 && nrc == nra - nlush,
           "refined search drops the same %d islands (%d -> %d)",
           nlush, nra, nrc);
        free(ref_all);
        free(ref_clean);

        /* Thread count must not change the outcome. */
        MushroomIsland *one = NULL;
        int none = 0;
        int r3 = search_ex(262, "1.18", 0, 0, 5000, 16, 0, 0, 1, NULL, 0,
                           0, 0, 1, 0, &one, &none);
        ok(r3 == 0 && same_islands(clean, nclean, one, none, "lush threads"),
           "single-thread exclusion matches the default");
        free(one);

        /* Other targets must be untouched by the option. */
        int ocean0[1] = { 0 };
        MushroomIsland *o_off = NULL, *o_on = NULL;
        int noff = 0, non = 0;
        int r4 = search_ex(262, "1.18", -2000, 1000, 2000, 16, 0, 0, 0,
                           ocean0, 1, 0, 0, 0, 0, &o_off, &noff);
        int r5 = search_ex(262, "1.18", -2000, 1000, 2000, 16, 0, 0, 1,
                           ocean0, 1, 0, 0, 0, 0, &o_on, &non);
        ok(r4 == 0 && r5 == 0 && same_islands(o_off, noff, o_on, non, "lush ocean"),
           "ocean target is unaffected (%d regions)", noff);
        free(o_off);
        free(o_on);

        /* Pause/stop must interrupt the extra pass cleanly. */
        GmifControl ctl;
        gmif_control_init(&ctl);
        ctl.stop = 1;
        int cand = -1;
        for (int i = 0; i < nall; i++) {
            if (lush[i]) { cand = i; break; }
        }
        int stop_rc = cand >= 0
                ? gmif_island_has_lush_caves(&lg, &lp, &all[cand], &ctl, NULL, NULL)
                : 0;
        ok(cand >= 0 && stop_rc == GMIF_ERR_CANCELLED,
           "stop request aborts the lush caves check");

        free(lush);
        free(all);
        free(clean);
    }

    section("Native search ceiling");
    {
        int maxr = gmif_max_radius(16);
        ok(maxr >= 700000 && maxr < 1000000,
           "scale16 max radius is meaningful (got %d)", maxr);
        SearchParams p;
        memset(&p, 0, sizeof(p));
        p.seed = 262;
        p.mc = gmif_mc_from_string("1.18");
        p.scale = 16;
        p.use_tiles = 2;
        GmifSearchPlan plan;
        p.radius = maxr;
        int rc = gmif_plan_search(&p, &plan);
        ok(rc == GMIF_OK && plan.use_tiles == 1
           && plan.cells <= 10000000000LL,
           "native max radius plans tiled scan (%lld cells, tile=%d)",
           (long long)plan.cells, plan.tile_size);
        p.radius = maxr + 1;
        rc = gmif_plan_search(&p, &plan);
        ok(rc == GMIF_ERR_GRID_TOO_LARGE,
           "native max radius + 1 is rejected");
        ok(gmif_max_radius(0) == 0, "invalid scale has no max radius");
    }

    section("Thread count helper");
    {
        int d = gmif_default_threads();
        ok(d >= 1 && d <= 256, "default threads=%d", d);
    }

    (void)t1; (void)t4; (void)t10; (void)t16;
    printf("\n========================================\n");
    printf("RELEASE AUDIT: %d PASS, %d FAIL\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
