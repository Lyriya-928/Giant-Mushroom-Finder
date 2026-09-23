/*
 * Giant Mushroom Island Finder — CLI
 *
 * Usage:
 *   gmif_cli --seed 262 --version 1.18 --radius 500 --min-area 1000 --refine
 *   gmif_cli --self-test
 *   gmif_cli --seed 262 --version 1.18 --radius 500 --dump-region --dump-map
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
static double now_sec(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
#else
#include <time.h>
static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

#include "mushroom_finder.h"

static void on_progress(const SearchProgress *p, void *ud)
{
    (void)ud;
    fprintf(stderr, "\r[%3d%%] %-8s found=%" PRId64 "          ",
            p->percent, p->stage ? p->stage : "", p->found);
    fflush(stderr);
}

static void usage(void)
{
    fprintf(stderr,
        "Giant Mushroom Island Finder CLI v" GMIF_VERSION "\n\n"
        "Usage:\n"
        "  gmif_cli --seed <seed> [options]\n"
        "  gmif_cli --self-test\n"
        "  gmif_cli --audit\n\n"
        "Options:\n"
        "  --version-app       Print GMIF application version (v" GMIF_VERSION ")\n"
        "  --seed <n>          Minecraft Java seed\n"
        "  --version <s>       MC game version (default 1.21.3)\n"
        "  --radius <n>        Search half-size in blocks (default 5000, max 30000000)\n"
        "  --min-area <n>      Min estimated area, optional (default 0 = off)\n"
        "  --max-results <n>   Keep top-N by area (default 10, 0 = all)\n"
        "  --scale <n>         Sample scale 1|4|16|64|256 (default 16)\n"
        "  --refine            Refine candidates at scale 4\n"
        "  --include-shore     Count mushroom_field_shore as island\n"
        "  --dump-region       Print full region statistics\n"
        "  --dump-map          Print ASCII map around largest island\n"
        "  --csv <path>        Export results as CSV\n"
        "  --json <path>       Export results as JSON\n"
        "  --threads <n>       Sampling threads (default 1)\n"
        "  --tiles             Force tiled streaming search\n"
        "  --no-tiles          Force monolithic full-grid search\n"
        "  --tile-size <n>     Tile side in SAMPLES (not blocks). 256 = 256×256 samples;\n"
        "                      world coverage = 256 × scale blocks per side (scale 16 → 4096)\n"
        "  --benchmark         Run 1/2/4/8-thread timing comparison\n"
        "  --map-step <n>      ASCII map block stride (default 16)\n"
        "  --map-pad <n>       ASCII map padding around island (default 64)\n"
        "  --debug             Verbose debug output (grid + failure dump)\n"
        "  --self-test         Run regression / correctness tests\n"
        "  --audit             Run release-candidate audit suite\n"
        "  --list-versions     Print known versions and exit\n"
        "  --help              Show this help\n");
}

static const char *precision_label(int p)
{
    switch (p) {
        case 1: return "refined-scale4 (estimated)";
        case 2: return "scale1 (high precision)";
        default: return "coarse-estimate";
    }
}

static int approx_eq(double a, double b, double rel)
{
    double d = fabs(a - b);
    double m = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    if (m < 1.0) return d < 1.0;
    return d <= m * rel;
}

static int expect_true(int cond, const char *name, int *fails)
{
    if (cond) {
        printf("  [PASS] %s\n", name);
        return 1;
    }
    printf("  [FAIL] %s\n", name);
    (*fails)++;
    return 0;
}

/* ------------------------------------------------------------------ */
/* self-test / regression                                              */
/* ------------------------------------------------------------------ */

static int run_self_test(void)
{
    int fails = 0;
    printf("GMIF self-test / regression\n\n");

    printf("== Version mapping ==\n");
    expect_true(gmif_mc_from_string("1.18") >= 0, "parse 1.18", &fails);
    expect_true(gmif_mc_from_string("1.21.3") >= 0, "parse 1.21.3", &fails);
    expect_true(gmif_mc_from_string("not-a-version") < 0, "reject junk version", &fails);

    printf("\n== Coordinate conversion / sample agreement ==\n");
    /* For seed 262 / 1.18, origin is mushroom_fields (cubiomes README). */
    {
        int id00 = gmif_sample_biome(262, gmif_mc_from_string("1.18"), 0, 0);
        expect_true(id00 == 14 /* mushroom_fields */, "seed262 @ (0,0) is mushroom_fields", &fails);

        int id_far = gmif_sample_biome(262, gmif_mc_from_string("1.18"), 2000, 2000);
        expect_true(id_far != 14, "seed262 @ (2000,2000) is not mushroom (sanity)", &fails);
    }

    printf("\n== Scale 4 vs Scale 16+refine agreement (seed 262, r=500) ==\n");
    {
        SearchParams p4, p16;
        memset(&p4, 0, sizeof(p4));
        memset(&p16, 0, sizeof(p16));
        p4.seed = 262; p4.mc = gmif_mc_from_string("1.18");
        p4.radius = 500; p4.min_area = 1000; p4.scale = 4; p4.refine = 0;
        p16 = p4; p16.scale = 16; p16.refine = 1;

        MushroomIsland *a = NULL, *b = NULL;
        int na = 0, nb = 0;
        int ra = gmif_search(&p4, &a, &na, NULL, NULL, NULL, NULL);
        int rb = gmif_search(&p16, &b, &nb, NULL, NULL, NULL, NULL);
        expect_true(ra == 0 && rb == 0 && na >= 1 && nb >= 1, "both searches found islands", &fails);
        if (ra == 0 && rb == 0 && na >= 1 && nb >= 1) {
            /* largest island should match within 15% area and ~scale blocks on center */
            expect_true(approx_eq((double)a[0].area, (double)b[0].area, 0.15),
                        "largest area within 15%", &fails);
            expect_true(abs(a[0].center_x - b[0].center_x) <= 32
                        && abs(a[0].center_z - b[0].center_z) <= 32,
                        "largest center within 32 blocks", &fails);
            printf("    scale4   center=(%d,%d) area=%" PRId64 " span=%dx%d\n",
                   a[0].center_x, a[0].center_z, a[0].area, a[0].width, a[0].height);
            printf("    scale16+ center=(%d,%d) area=%" PRId64 " span=%dx%d\n",
                   b[0].center_x, b[0].center_z, b[0].area, b[0].width, b[0].height);
        }
        free(a); free(b);
    }

    printf("\n== Boundary spot-check vs Cubiomes getBiomeAt ==\n");
    {
        SearchParams p;
        memset(&p, 0, sizeof(p));
        p.seed = 262;
        p.mc = gmif_mc_from_string("1.18");
        p.radius = 500;
        p.min_area = 5000;
        p.scale = 16;
        p.refine = 1;
        MushroomIsland *isl = NULL;
        int n = 0;
        int rc = gmif_search(&p, &isl, &n, NULL, NULL, NULL, NULL);
        expect_true(rc == 0 && n >= 1, "search ok", &fails);
        if (rc == 0 && n >= 1) {
            const MushroomIsland *m = &isl[0];
            /* center should be mushroom */
            int c = gmif_sample_biome(p.seed, p.mc, m->center_x, m->center_z);
            expect_true(c == 14, "center is mushroom_fields", &fails);
            /* a point well outside bounds should usually not be mushroom;
             * sample corners of expanded bbox (not guaranteed, so only warn) */
            int outside = gmif_sample_biome(p.seed, p.mc,
                                            m->min_x - 80, m->min_z - 80);
            printf("    corner-80 biome=%d (info, not asserted)\n", outside);
            /* point just outside max edge */
            int east = gmif_sample_biome(p.seed, p.mc, m->max_x + 32, m->center_z);
            printf("    max_x+32 biome=%d (info)\n", east);
            /* At least: several points inside the bbox that are labeled
             * mushroom by sampling should mostly agree with island membership.
             * Sample 5x5 inside bbox. */
            int inside_hits = 0, inside_total = 0;
            for (int iz = 0; iz < 5; iz++) {
                for (int ix = 0; ix < 5; ix++) {
                    int x = m->min_x + (m->width - 1) * ix / 4;
                    int z = m->min_z + (m->height - 1) * iz / 4;
                    /* skip if outside island-shaped area — just count rate */
                    int id = gmif_sample_biome(p.seed, p.mc, x, z);
                    inside_total++;
                    if (id == 14) inside_hits++;
                }
            }
            printf("    bbox interior mushroom ratio: %d/%d\n",
                   inside_hits, inside_total);
            expect_true(inside_hits >= inside_total / 3,
                        "bbox interior has substantial mushroom coverage", &fails);
        }
        free(isl);
    }

    printf("\n== Radius is half-size (not diameter) ==\n");
    {
        /* With radius 200, all results must lie within roughly [-200-pad, 200+pad]
         * where pad is refine padding (~2*coarse scale). */
        SearchParams p;
        memset(&p, 0, sizeof(p));
        p.seed = 262;
        p.mc = gmif_mc_from_string("1.18");
        p.radius = 200;
        p.min_area = 0;
        p.scale = 16;
        p.refine = 1;
        MushroomIsland *isl = NULL;
        int n = 0;
        int rc = gmif_search(&p, &isl, &n, NULL, NULL, NULL, NULL);
        expect_true(rc == 0, "radius200 search ok", &fails);
        int bounds_ok = 1;
        if (rc == 0) {
            int lim = 200 + 80; /* allow refine pad */
            for (int i = 0; i < n; i++) {
                if (isl[i].min_x < -lim || isl[i].max_x > lim
                 || isl[i].min_z < -lim || isl[i].max_z > lim) {
                    bounds_ok = 0;
                    printf("    island %d bounds exceed radius: [%d,%d]..[%d,%d]\n",
                           i, isl[i].min_x, isl[i].min_z, isl[i].max_x, isl[i].max_z);
                }
            }
        }
        expect_true(bounds_ok, "results within radius+pad", &fails);
        free(isl);
    }

    printf("\n== Negative coordinates path ==\n");
    {
        SearchParams p;
        memset(&p, 0, sizeof(p));
        p.seed = 262;
        p.mc = gmif_mc_from_string("1.18");
        p.radius = 1000;
        p.min_area = 20000;
        p.scale = 16;
        p.refine = 1;
        MushroomIsland *isl = NULL;
        int n = 0;
        int rc = gmif_search(&p, &isl, &n, NULL, NULL, NULL, NULL);
        expect_true(rc == 0 && n >= 1, "r=1000 finds main island", &fails);
        if (rc == 0 && n >= 1) {
            /* main island of seed 262 is near origin, slightly negative X */
            expect_true(abs(isl[0].center_x) < 100 && abs(isl[0].center_z) < 100,
                        "largest island near origin", &fails);
            printf("    main island center=(%d,%d) area=%" PRId64
                   " diag=%.1f precision=%s\n",
                   isl[0].center_x, isl[0].center_z, isl[0].area,
                   isl[0].diagonal_span, precision_label(isl[0].area_precision));
        }
        free(isl);
    }

    printf("\n== Grid describe / overflow guards ==\n");
    {
        int half = 0, sx = 0, sz = 0;
        int64_t cells = 0, mb = 0, bb = 0;
        expect_true(gmif_describe_grid(5000, 16, &half, &sx, &sz, &cells, &mb, &bb) == 0,
                    "describe r=5000 scale16 ok", &fails);
        expect_true(cells > 0 && sx > 0 && half > 0, "r=5000 geometry positive", &fails);

        expect_true(gmif_describe_grid(100000, 16, &half, &sx, &sz, &cells, &mb, &bb) == 0,
                    "describe r=100000 scale16 ok", &fails);

        /* 10M at scale 16 → 1.25e6² cells — must fail with GRID_TOO_LARGE, not version. */
        int rc10m = gmif_describe_grid(10000000, 16, &half, &sx, &sz, &cells, &mb, &bb);
        expect_true(rc10m == GMIF_ERR_GRID_TOO_LARGE,
                    "r=10M scale16 → GRID_TOO_LARGE (not version)", &fails);
        printf("    r=10M error:\n%s\n", gmif_last_error());
        expect_true(gmif_last_error_code() == GMIF_ERR_GRID_TOO_LARGE, "code is GRID_TOO_LARGE", &fails);
        expect_true(strstr(gmif_last_error(), "version string") == NULL,
                    "error text does not claim version invalid", &fails);
        expect_true(strstr(gmif_last_error(), "max radius") != NULL,
                    "error text includes max radius hint", &fails);

        int rc30m = gmif_describe_grid(30000000, 16, &half, &sx, &sz, &cells, &mb, &bb);
        expect_true(rc30m == GMIF_ERR_GRID_TOO_LARGE || rc30m == GMIF_ERR_OVERFLOW,
                    "r=30M scale16 fails with grid/overflow (not crash)", &fails);
        printf("    r=30M error: %s\n", gmif_last_error());

        /* Full search at 10M must surface the same real reason. */
        SearchParams big;
        memset(&big, 0, sizeof(big));
        big.seed = 262;
        big.mc = gmif_mc_from_string("1.18");
        big.radius = 10000000;
        big.min_area = 0;
        big.scale = 16;
        big.max_results = 10;
        MushroomIsland *tmp = NULL;
        int tn = 0;
        int rcbig = gmif_search(&big, &tmp, &tn, NULL, NULL, NULL, NULL);
        expect_true(rcbig == GMIF_ERR_GRID_TOO_LARGE,
                    "search r=10M returns GRID_TOO_LARGE", &fails);
        printf("    search r=10M: %s\n", gmif_last_error());
        free(tmp);
    }

    printf("\n== Tiled vs monolithic regression ==\n");
    {
        SearchParams mono, tiled;
        memset(&mono, 0, sizeof(mono));
        memset(&tiled, 0, sizeof(tiled));
        mono.seed = 262;
        mono.mc = gmif_mc_from_string("1.18");
        mono.radius = 500;
        mono.min_area = 0;
        mono.scale = 16;
        mono.refine = 0;
        mono.max_results = 0;
        mono.use_tiles = 0;
        tiled = mono;
        tiled.use_tiles = 1;
        tiled.tile_size = 32; /* force island to span tiles */

        MushroomIsland *a = NULL, *b = NULL;
        int na = 0, nb = 0;
        int ra = gmif_search(&mono, &a, &na, NULL, NULL, NULL, NULL);
        int rb = gmif_search(&tiled, &b, &nb, NULL, NULL, NULL, NULL);
        expect_true(ra == 0 && rb == 0, "mono and tiled both ok", &fails);
        printf("    mono regions=%d  tiled regions=%d\n", na, nb);
        if (ra == 0 && rb == 0) {
            expect_true(na == nb, "same region count (cross-tile merged)", &fails);
            if (na == nb && na > 0) {
                /* sort already area desc — compare top island */
                expect_true(a[0].area == b[0].area, "top area identical", &fails);
                expect_true(a[0].center_x == b[0].center_x
                            && a[0].center_z == b[0].center_z,
                            "top center identical", &fails);
                expect_true(a[0].min_x == b[0].min_x && a[0].max_x == b[0].max_x
                            && a[0].min_z == b[0].min_z && a[0].max_z == b[0].max_z,
                            "top bounds identical", &fails);
                printf("    top: area=%" PRId64 " center=(%d,%d) bounds=[%d,%d]..[%d,%d]\n",
                       a[0].area, a[0].center_x, a[0].center_z,
                       a[0].min_x, a[0].min_z, a[0].max_x, a[0].max_z);
                printf("    tiled top: area=%" PRId64 " center=(%d,%d)\n",
                       b[0].area, b[0].center_x, b[0].center_z);
            }
            /* all regions should match pairwise after sort */
            if (na == nb) {
                int same = 1;
                for (int i = 0; i < na; i++) {
                    if (a[i].area != b[i].area
                        || a[i].min_x != b[i].min_x || a[i].max_x != b[i].max_x
                        || a[i].min_z != b[i].min_z || a[i].max_z != b[i].max_z) {
                        same = 0;
                        printf("    mismatch #%d mono area=%" PRId64 " tiled=%" PRId64 "\n",
                               i, a[i].area, b[i].area);
                    }
                }
                expect_true(same, "all regions match area/bounds", &fails);
            }
        }
        free(a);
        free(b);
    }

    printf("\n== Tiled cross-boundary merge (small tiles) ==\n");
    {
        /* Seed 262 main island spans roughly 260x412 blocks.
         * tile_size=8 cells * scale 16 = 128 blocks -> island spans multiple tiles. */
        SearchParams p;
        memset(&p, 0, sizeof(p));
        p.seed = 262;
        p.mc = gmif_mc_from_string("1.18");
        p.radius = 400;
        p.min_area = 1000;
        p.scale = 16;
        p.refine = 0;
        p.max_results = 0;
        p.use_tiles = 1;
        p.tile_size = 8;
        MushroomIsland *isl = NULL;
        int n = 0;
        int rc = gmif_search(&p, &isl, &n, NULL, NULL, NULL, NULL);
        expect_true(rc == 0 && n >= 1, "small-tile search finds main island", &fails);
        if (rc == 0 && n >= 1) {
            printf("    regions=%d top area=%" PRId64 " span=%dx%d\n",
                   n, isl[0].area, isl[0].width, isl[0].height);
            /* If merge failed, main island would split into several small regions. */
            expect_true(isl[0].area > 40000,
                        "merged island is large (not split at tile borders)", &fails);
            expect_true(n == 1 || isl[0].area > isl[1].area * 3,
                        "dominant single island (merge worked)", &fails);
        }
        free(isl);
    }

    printf("\n== ASCII map dump smoke ==\n");
    {
        size_t cap = 128 * 128 * 2;
        char *buf = (char *)malloc(cap);
        if (buf) {
            int64_t hits = gmif_dump_ascii_map(262, gmif_mc_from_string("1.18"),
                                               -160, -160, 160, 160, 16, buf, cap);
            expect_true(hits > 0, "ascii map has mushroom samples", &fails);
            free(buf);
        }
    }

    printf("\n========================================\n");
    if (fails == 0) {
        printf("SELF-TEST PASS\n");
        return 0;
    }
    printf("SELF-TEST FAIL (%d failure(s))\n", fails);
    return 1;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    SearchParams p;
    memset(&p, 0, sizeof(p));
    p.seed = 0;
    p.mc = gmif_mc_from_string("1.21.3");
    p.radius = 5000;
    p.min_area = 0;          /* optional filter; 0 = no filter */
    p.scale = 16;
    p.refine = 0;
    p.include_shore = 0;
    p.threads = 1;
    p.max_results = 10;      /* top-N by area */
    p.use_tiles = 2;         /* auto */
    p.tile_size = 0;
    int have_seed = 0;
    int dump_region = 0;
    int dump_map = 0;
    int debug = 0;
    int map_step = 16;
    int map_pad = 64;
    const char *csv_path = NULL;
    const char *json_path = NULL;
    int do_benchmark = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage();
            return 0;
        } else if (!strcmp(argv[i], "--version-app") || !strcmp(argv[i], "--app-version")) {
            printf("%s\n", GMIF_VERSION);
            return 0;
        } else if (!strcmp(argv[i], "--self-test")) {
            return run_self_test();
        } else if (!strcmp(argv[i], "--audit")) {
            return gmif_run_release_audit();
        } else if (!strcmp(argv[i], "--list-versions")) {
            const char *const *v = gmif_known_versions();
            for (; *v; v++) printf("%s\n", *v);
            return 0;
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            p.seed = strtoll(argv[++i], NULL, 10);
            have_seed = 1;
        } else if (!strcmp(argv[i], "--version") && i + 1 < argc) {
            int mc = gmif_mc_from_string(argv[++i]);
            if (mc < 0) {
                fprintf(stderr, "Unknown MC version: %s\n", argv[i]);
                fprintf(stderr, "Use --list-versions for supported versions.\n");
                return 1;
            }
            p.mc = mc;
        } else if (!strcmp(argv[i], "--radius") && i + 1 < argc) {
            p.radius = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--min-area") && i + 1 < argc) {
            p.min_area = strtoll(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--max-results") && i + 1 < argc) {
            p.max_results = atoi(argv[++i]);
            if (p.max_results < 0) p.max_results = 0;
        } else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            p.scale = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--refine")) {
            p.refine = 1;
        } else if (!strcmp(argv[i], "--include-shore")) {
            p.include_shore = 1;
        } else if (!strcmp(argv[i], "--dump-region")) {
            dump_region = 1;
        } else if (!strcmp(argv[i], "--dump-map")) {
            dump_map = 1;
        } else if (!strcmp(argv[i], "--debug")) {
            debug = 1;
        } else if (!strcmp(argv[i], "--csv") && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (!strcmp(argv[i], "--json") && i + 1 < argc) {
            json_path = argv[++i];
        } else if (!strcmp(argv[i], "--threads") && i + 1 < argc) {
            p.threads = atoi(argv[++i]);
            if (p.threads < 1) p.threads = 1;
            if (p.threads > 64) p.threads = 64;
        } else if (!strcmp(argv[i], "--tiles")) {
            p.use_tiles = 1;
        } else if (!strcmp(argv[i], "--no-tiles")) {
            p.use_tiles = 0;
        } else if (!strcmp(argv[i], "--tile-size") && i + 1 < argc) {
            p.tile_size = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--benchmark")) {
            do_benchmark = 1;
        } else if (!strcmp(argv[i], "--map-step") && i + 1 < argc) {
            map_step = atoi(argv[++i]);
            if (map_step < 1) map_step = 1;
        } else if (!strcmp(argv[i], "--map-pad") && i + 1 < argc) {
            map_pad = atoi(argv[++i]);
            if (map_pad < 0) map_pad = 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            usage();
            return 1;
        }
    }

    if (!have_seed) {
        usage();
        return 1;
    }
    if (p.radius <= 0 || p.radius > 30000000) {
        fprintf(stderr, "Radius must be in 1..30000000 (half-size in blocks, world border)\n");
        return 1;
    }
    if (p.min_area < 0) p.min_area = 0;

    if (debug || do_benchmark) {
        int half = 0, gsz = 0, gsz2 = 0;
        int64_t cells = 0, mb = 0, bb = 0;
        int grc = gmif_describe_grid(p.radius, p.scale > 0 ? p.scale : 16,
                                     &half, &gsz, &gsz2, &cells, &mb, &bb);
        fprintf(stderr, "DEBUG grid: radius=%d scale=%d half=%d grid=%dx%d "
                "cells=%lld maskMB=%lld biomeMB=%lld threads=%d version=%s rc=%d\n",
                p.radius, p.scale > 0 ? p.scale : 16, half, gsz, gsz2,
                (long long)cells, (long long)(mb / (1024 * 1024)),
                (long long)(bb / (1024 * 1024)), p.threads,
                gmif_mc_to_string(p.mc), grc);
        if (grc != 0) {
            fprintf(stderr, "DEBUG grid error: %s\n", gmif_last_error());
            if (!do_benchmark) return 1;
        }
    }

    if (do_benchmark) {
        int threads_try[] = {1, 2, 4, 8};
        printf("Benchmark seed=%" PRId64 " version=%s radius=%d scale=%d refine=%d\n\n",
               p.seed, gmif_mc_to_string(p.mc), p.radius, p.scale, p.refine);
        for (int ti = 0; ti < 4; ti++) {
            SearchParams bp = p;
            bp.threads = threads_try[ti];
            MushroomIsland *isl = NULL;
            int n = 0;
            double t0 = now_sec();
            int rc = gmif_search(&bp, &isl, &n, NULL, NULL, NULL, NULL);
            double t1 = now_sec();
            if (rc != 0) {
                printf("Threads: %d  FAILED (rc=%d)\n", bp.threads, rc);
            } else {
                printf("Threads: %d  Time: %.3f s  Found: %d  "
                       "Top area: %" PRId64 "\n",
                       bp.threads, t1 - t0, n,
                       n > 0 ? isl[0].area : 0);
            }
            free(isl);
        }
        return 0;
    }

    fprintf(stderr,
        "Giant Mushroom Island Finder\n"
        "  seed=%" PRId64 "  version=%s  radius=%d (half-size)  min_area=%" PRId64 "\n"
        "  scale=%d  refine=%d  include_shore=%d\n\n",
        p.seed, gmif_mc_to_string(p.mc), p.radius, p.min_area,
        p.scale, p.refine, p.include_shore);

    if (debug) {
        fprintf(stderr, "DEBUG: world search box approximately "
                "[%d,%d]..[%d,%d]\n",
                -p.radius, -p.radius, p.radius, p.radius);
    }

    MushroomIsland *islands = NULL;
    int count = 0;
    int rc = gmif_search(&p, &islands, &count, on_progress, NULL, NULL, NULL);
    fprintf(stderr, "\n");

    if (rc != 0) {
        fprintf(stderr, "Search failed (rc=%d code=%d)\n", rc, gmif_last_error_code());
        fprintf(stderr, "%s\n",
                gmif_last_error()[0] ? gmif_last_error() : "(no detail)");
        if (debug) {
            int half = 0, gsz = 0, gsz2 = 0;
            int64_t cells = 0, mb = 0, bb = 0;
            gmif_describe_grid(p.radius, p.scale, &half, &gsz, &gsz2,
                               &cells, &mb, &bb);
            fprintf(stderr,
                "DEBUG fail dump:\n"
                "  radius = %d\n  scale = %d\n  grid_width = %d\n"
                "  grid_height = %d\n  sample_count = %lld\n"
                "  mask_bytes = %lld\n  biome_bytes = %lld\n"
                "  threads = %d\n  version = %s (%d)\n",
                p.radius, p.scale, gsz, gsz2, (long long)cells,
                (long long)mb, (long long)bb, p.threads,
                gmif_mc_to_string(p.mc), p.mc);
        }
        return 1;
    }

    printf("Giant Mushroom Island Finder v" GMIF_VERSION "\n\n");
    printf("Seed: %" PRId64 "\n", p.seed);
    printf("Version: %s\n", gmif_mc_to_string(p.mc));
    printf("Search Radius (half-size): %d blocks\n", p.radius);
    printf("World box: [%d, %d] .. [%d, %d]\n",
           -p.radius, -p.radius, p.radius, p.radius);
    if (p.min_area > 0)
        printf("Min Area: %" PRId64 " blocks²\n", p.min_area);
    else
        printf("Min Area: (optional, off)\n");
    if (p.max_results > 0)
        printf("Max results (top-N): %d\n", p.max_results);
    else
        printf("Max results (top-N): (all)\n");
    printf("Sample scale: %d%s\n", p.scale, p.refine ? " (refined to 4)" : "");
    printf("\nFound: %d\n\n", count);

    for (int i = 0; i < count; i++) {
        const MushroomIsland *isl = &islands[i];
        double area_km2 = (double)isl->area / 1000000.0;
        printf("#%d\n", i + 1);
        printf("  Center: %d, %d\n", isl->center_x, isl->center_z);
        printf("  Area (%s): %" PRId64 " blocks²  (≈ %.3f km²)\n",
               precision_label(isl->area_precision), isl->area, area_km2);
        if (dump_region) {
            printf("  Region ID: %d\n", i + 1);
            printf("  Center X: %d\n", isl->center_x);
            printf("  Center Z: %d\n", isl->center_z);
            printf("  Min X: %d\n", isl->min_x);
            printf("  Max X: %d\n", isl->max_x);
            printf("  Min Z: %d\n", isl->min_z);
            printf("  Max Z: %d\n", isl->max_z);
            printf("  X Span: %d\n", isl->width);
            printf("  Z Span: %d\n", isl->height);
            printf("  Diagonal Span: %.1f\n", isl->diagonal_span);
            printf("  Sample Count: %" PRId64 "\n", isl->samples);
            printf("  Area Precision: %s\n", precision_label(isl->area_precision));
            printf("  Compactness: %.4f\n", isl->compactness);
            printf("  Perimeter (est): %d\n", isl->perimeter);
            printf("  Distance: %.1f\n", isl->distance);
        } else {
            printf("  Span: %d x %d (diag %.0f)\n",
                   isl->width, isl->height, isl->diagonal_span);
            printf("  Bounds: [%d, %d] .. [%d, %d]\n",
                   isl->min_x, isl->min_z, isl->max_x, isl->max_z);
            printf("  Distance: %.0f\n", isl->distance);
            printf("  Compactness: %.4f (perimeter≈%d)\n",
                   isl->compactness, isl->perimeter);
        }
        printf("\n");
    }

    if (dump_map && count > 0) {
        const MushroomIsland *m = &islands[0];
        int x0 = m->min_x - map_pad;
        int z0 = m->min_z - map_pad;
        int x1 = m->max_x + map_pad;
        int z1 = m->max_z + map_pad;
        /* cap map size */
        int cols = (x1 - x0) / map_step + 1;
        int rows = (z1 - z0) / map_step + 1;
        if (cols > 200 || rows > 200) {
            map_step = (x1 - x0) / 200 + 1;
            if ((z1 - z0) / map_step + 1 > 200)
                map_step = (z1 - z0) / 200 + 1;
            cols = (x1 - x0) / map_step + 1;
            rows = (z1 - z0) / map_step + 1;
        }
        size_t cap = (size_t)rows * ((size_t)cols + 1) + 16;
        char *buf = (char *)malloc(cap);
        if (buf) {
            printf("ASCII map around island #1 (M=mushroom_fields, .=other)\n");
            printf("World X: %d .. %d, Z: %d .. %d, step=%d\n\n",
                   x0, x1, z0, z1, map_step);
            int64_t hits = gmif_dump_ascii_map(p.seed, p.mc,
                                               x0, z0, x1, z1, map_step,
                                               buf, cap);
            fputs(buf, stdout);
            printf("\nMap mushroom samples: %" PRId64 "\n", hits);
            free(buf);
        }
    }

    if (csv_path && count > 0) {
        FILE *f = fopen(csv_path, "w");
        if (!f) {
            fprintf(stderr, "Cannot write CSV: %s\n", csv_path);
        } else {
            fprintf(f, "seed,version,rank,center_x,center_z,"
                       "min_x,max_x,min_z,max_z,"
                       "x_span,z_span,diagonal_span,"
                       "area_estimated,area_precision,samples,"
                       "compactness,perimeter,distance\n");
            for (int i = 0; i < count; i++) {
                const MushroomIsland *isl = &islands[i];
                fprintf(f, "%" PRId64 ",%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,"
                           "%" PRId64 ",%d,%" PRId64 ",%.4f,%d,%.2f\n",
                        p.seed, gmif_mc_to_string(p.mc), i + 1,
                        isl->center_x, isl->center_z,
                        isl->min_x, isl->max_x, isl->min_z, isl->max_z,
                        isl->width, isl->height, isl->diagonal_span,
                        isl->area, isl->area_precision, isl->samples,
                        isl->compactness, isl->perimeter, isl->distance);
            }
            fclose(f);
            printf("CSV written: %s\n", csv_path);
        }
    }

    if (json_path && count >= 0) {
        FILE *f = fopen(json_path, "w");
        if (!f) {
            fprintf(stderr, "Cannot write JSON: %s\n", json_path);
        } else {
            fprintf(f, "{\n");
            fprintf(f, "  \"seed\": %" PRId64 ",\n", p.seed);
            fprintf(f, "  \"version\": \"%s\",\n", gmif_mc_to_string(p.mc));
            fprintf(f, "  \"radius\": %d,\n", p.radius);
            fprintf(f, "  \"minArea\": %" PRId64 ",\n", p.min_area);
            fprintf(f, "  \"scale\": %d,\n", p.scale);
            fprintf(f, "  \"refine\": %s,\n", p.refine ? "true" : "false");
            fprintf(f, "  \"count\": %d,\n", count);
            fprintf(f, "  \"islands\": [\n");
            for (int i = 0; i < count; i++) {
                const MushroomIsland *isl = &islands[i];
                fprintf(f,
                    "    {\"rank\": %d, \"centerX\": %d, \"centerZ\": %d, "
                    "\"minX\": %d, \"maxX\": %d, \"minZ\": %d, \"maxZ\": %d, "
                    "\"xSpan\": %d, \"zSpan\": %d, \"diagonalSpan\": %.2f, "
                    "\"areaEstimated\": %" PRId64 ", \"areaPrecision\": %d, "
                    "\"samples\": %" PRId64 ", \"compactness\": %.4f, "
                    "\"perimeter\": %d, \"distance\": %.2f}%s\n",
                    i + 1, isl->center_x, isl->center_z,
                    isl->min_x, isl->max_x, isl->min_z, isl->max_z,
                    isl->width, isl->height, isl->diagonal_span,
                    isl->area, isl->area_precision, isl->samples,
                    isl->compactness, isl->perimeter, isl->distance,
                    (i + 1 < count) ? "," : "");
            }
            fprintf(f, "  ]\n}\n");
            fclose(f);
            printf("JSON written: %s\n", json_path);
        }
    }

    free(islands);
    return 0;
}
