/*
 * Large Biome Finder — CLI
 *
 * Usage:
 *   gmif_cli --seed 262 --version 1.18 --radius 500 --min-area 1000 --refine
 *   gmif_cli --seed 262 --version 1.18 --center-x -12000 --center-z 8000 --radius 2000
 *   gmif_cli --seed 262 --version 1.18 --biomes 14,15
 *   gmif_cli --self-test
 *   gmif_cli --audit
 *   gmif_cli --seed 262 --version 1.18 --radius 5000 --benchmark
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "mushroom_finder.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
static double now_sec(void)
{
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}
static long peak_rss_mb(void)
{
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return (long)(pmc.PeakWorkingSetSize / (1024 * 1024));
    return -1;
}
#else
#include <time.h>
#include <sys/resource.h>
static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
static long peak_rss_mb(void)
{
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) return (long)(ru.ru_maxrss / 1024);
    return -1;
}
#endif

static void on_progress(const SearchProgress *p, void *ud)
{
    (void)ud;
    fprintf(stderr, "\r[%3d%%] %-8s found=%" PRId64 " mode=%s          ",
            p->percent, p->stage ? p->stage : "", p->found,
            p->mode ? "tiled" : "mono");
    fflush(stderr);
}

static void usage(void)
{
    fprintf(stderr,
        "Large Biome Finder CLI v" GMIF_VERSION "\n\n"
        "Usage:\n"
        "  gmif_cli --seed <seed> [options]\n"
        "  gmif_cli --self-test\n"
        "  gmif_cli --audit\n\n"
        "Options:\n"
        "  --version-app       Print application version (v" GMIF_VERSION ")\n"
        "  --seed <n>          Minecraft Java seed\n"
        "  --version <s>       MC game version (default 1.21.3)\n"
        "  --center-x <n>      Search center X in blocks (default 0)\n"
        "  --center-z <n>      Search center Z in blocks (default 0)\n"
        "  --radius <n>        Search half-size in blocks (default 5000)\n"
        "  --max-radius        Print the native maximum radius for scale 16 and exit\n"
        "  --min-area <n>      Min estimated area; optional (default 0 = off)\n"
        "  --max-results <n>   Keep top-N by area (default 10, 0 = all)\n"
        "  --scale <n>         Sample scale 1|4|16|64|256 (default 16)\n"
        "  --biomes <ids>      Target biome ids, comma separated (default 14 = mushroom;\n"
        "                      common: 14 mushroom, 15 shore, 1 plains, 4 forest,\n"
        "                      2 desert, 21 jungle, 35 savanna, 37 badlands, 0 ocean)\n"
        "  --refine            Refine candidates at scale 4\n"
        "  --include-shore     Count mushroom_field_shore as island (legacy)\n"
        "  --dump-region       Print full region statistics\n"
        "  --dump-map          Print ASCII map around largest island\n"
        "  --csv <path>        Export results as CSV\n"
        "  --json <path>       Export results as JSON\n"
        "  --threads <n>       Worker threads; 0 = auto (physical cores)\n"
        "  --tiles             Force tiled streaming search\n"
        "  --no-tiles          Force monolithic full-grid search\n"
        "  --tile-size <n>     Tile side in SAMPLES (not blocks). 256 = 256x256 samples;\n"
        "                      world coverage = 256 x scale blocks per side (scale 16 -> 4096)\n"
        "  --benchmark         Compare thread counts (time, peak RAM, consistency)\n"
        "  --threads-list <l>  Comma separated thread counts for --benchmark\n"
        "  --describe          Print the resolved scan plan and exit\n"
        "  --map-step <n>      ASCII map block stride (default 16)\n"
        "  --map-pad <n>       ASCII map padding around island (default 64)\n"
        "  --debug             Verbose debug output\n"
        "  --self-test         Run regression / correctness tests\n"
        "  --audit             Run release-candidate audit suite\n"
        "  --list-versions     Print known versions and exit\n"
        "  --help              Show this help\n");
}

static const char *precision_label(int p)
{
    switch (p) {
        case 1: return "refined scale 4 estimate";
        case 2: return "scale 1 high precision";
        default: return "coarse estimate";
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

static void print_plan(const GmifSearchPlan *plan, const SearchParams *p)
{
    char mb[64];
    snprintf(mb, sizeof(mb), "%.1f MB", (double)plan->est_peak_bytes / (1024.0 * 1024.0));
    printf("plan: mode=%s grid=%lldx%lld cells=%lld scale=%d tile=%d threads=%d\n",
           plan->use_tiles ? "tiled" : "monolithic",
           (long long)plan->sx, (long long)plan->sz, (long long)plan->cells,
           plan->scale, plan->tile_size, plan->threads);
    printf("      box=[%lld,%lld]..[%lld,%lld] center=(%d,%d) radius=%d\n",
           (long long)plan->world_x0,
           (long long)plan->world_z0,
           (long long)(plan->world_x0 + plan->sx * plan->scale - 1),
           (long long)(plan->world_z0 + plan->sz * plan->scale - 1),
           p->center_x, p->center_z, p->radius);
    printf("      est samples=%lld  est peak=%s\n",
           (long long)plan->est_samples, mb);
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
        p4.threads = 0;
        p16 = p4; p16.scale = 16; p16.refine = 1;

        MushroomIsland *a = NULL, *b = NULL;
        int na = 0, nb = 0;
        int ra = gmif_search(&p4, &a, &na, NULL, NULL, NULL, NULL);
        int rb = gmif_search(&p16, &b, &nb, NULL, NULL, NULL, NULL);
        expect_true(ra == 0 && rb == 0 && na >= 1 && nb >= 1, "both searches found islands", &fails);
        if (ra == 0 && rb == 0 && na >= 1 && nb >= 1) {
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
        p.threads = 0;
        MushroomIsland *isl = NULL;
        int n = 0;
        int rc = gmif_search(&p, &isl, &n, NULL, NULL, NULL, NULL);
        expect_true(rc == 0 && n >= 1, "search ok", &fails);
        if (rc == 0 && n >= 1) {
            const MushroomIsland *m = &isl[0];
            int c = gmif_sample_biome(p.seed, p.mc, m->center_x, m->center_z);
            expect_true(c == 14, "center is mushroom_fields", &fails);
            printf("    bounds=[%d,%d]..[%d,%d] area=%" PRId64 " (%s)\n",
                   m->min_x, m->min_z, m->max_x, m->max_z, m->area,
                   precision_label(m->area_precision));
            int inside_hits = 0, inside_total = 0;
            for (int iz = 0; iz < 5; iz++) {
                for (int ix = 0; ix < 5; ix++) {
                    int x = m->min_x + (m->width - 1) * ix / 4;
                    int z = m->min_z + (m->height - 1) * iz / 4;
                    int id = gmif_sample_biome(p.seed, p.mc, x, z);
                    inside_total++;
                    if (id == 14) inside_hits++;
                }
            }
            printf("    bbox interior mushroom ratio: %d/%d\n", inside_hits, inside_total);
            expect_true(inside_hits >= inside_total / 3,
                        "bbox interior has substantial mushroom coverage", &fails);
        }
        free(isl);
    }

    printf("\n== Radius is half-size (not diameter) ==\n");
    {
        SearchParams p;
        memset(&p, 0, sizeof(p));
        p.seed = 262;
        p.mc = gmif_mc_from_string("1.18");
        p.radius = 200;
        p.scale = 16;
        p.refine = 1;
        p.threads = 0;
        MushroomIsland *isl = NULL;
        int n = 0;
        int rc = gmif_search(&p, &isl, &n, NULL, NULL, NULL, NULL);
        expect_true(rc == 0, "radius200 search ok", &fails);
        int bounds_ok = 1;
        if (rc == 0) {
            int lim = 200 + 80;
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

    printf("\n== Custom center (negative coordinates) ==\n");
    {
        SearchParams p;
        memset(&p, 0, sizeof(p));
        p.seed = 262;
        p.mc = gmif_mc_from_string("1.18");
        p.center_x = -1000;
        p.center_z = -1000;
        p.radius = 1200;
        p.min_area = 0;
        p.scale = 16;
        p.refine = 1;
        p.threads = 0;
        p.max_results = 5;
        MushroomIsland *isl = NULL;
        int n = 0;
        int rc = gmif_search(&p, &isl, &n, NULL, NULL, NULL, NULL);
        expect_true(rc == 0 && n > 0, "custom-center search ok", &fails);
        int ok = n > 0;
        if (rc == 0) {
            int lim = p.radius + 80;
            for (int i = 0; i < n; i++) {
                if (abs(isl[i].center_x - p.center_x) > lim
                    || abs(isl[i].center_z - p.center_z) > lim) ok = 0;
            }
            printf("    center=(-1000,-1000) found=%d", n);
            if (n > 0) printf(" top=(%d,%d) area=%" PRId64,
                              isl[0].center_x, isl[0].center_z, isl[0].area);
            printf("\n");
        }
        expect_true(ok, "all results near custom center", &fails);
        free(isl);
    }

    printf("\n== Custom biome target (cold ocean id 46) ==\n");
    {
        int biomes[1] = { 46 /* cold_ocean, present near seed 262 origin */ };
        SearchParams p;
        memset(&p, 0, sizeof(p));
        p.seed = 262;
        p.mc = gmif_mc_from_string("1.18");
        p.radius = 600;
        p.scale = 16;
        p.threads = 0;
        p.max_results = 3;
        p.biome_ids = biomes;
        p.biome_count = 1;
        MushroomIsland *isl = NULL;
        int n = 0;
        int rc = gmif_search(&p, &isl, &n, NULL, NULL, NULL, NULL);
        expect_true(rc == 0 && n > 0, "cold-ocean target finds regions", &fails);
        if (rc == 0 && n > 0) {
            int id = gmif_sample_biome(p.seed, p.mc, isl[0].center_x, isl[0].center_z);
            expect_true(id == 46, "largest cold-ocean region center is cold_ocean", &fails);
            printf("    top target region center=(%d,%d) area=%" PRId64 "\n",
                   isl[0].center_x, isl[0].center_z, isl[0].area);
        }
        free(isl);
    }

    printf("\n== Grid describe / overflow guards ==\n");
    {
        int sx = 0, sz = 0;
        int64_t ox = 0, oz = 0, cells = 0, mb = 0;
        expect_true(gmif_describe_grid(0, 0, 5000, 16, &sx, &sz, &ox, &oz,
                                       &cells, &mb) == 0,
                    "describe r=5000 scale16 ok", &fails);
        expect_true(cells > 0 && sx > 0, "r=5000 geometry positive", &fails);

        SearchParams big;
        memset(&big, 0, sizeof(big));
        big.seed = 262;
        big.mc = gmif_mc_from_string("1.18");
        big.radius = 30000000;
        big.scale = 16;
        big.use_tiles = 2;
        GmifSearchPlan plan;
        memset(&plan, 0, sizeof(plan));

        int maxr = gmif_max_radius(16);
        expect_true(maxr > 700000 && maxr < 1000000,
                    "native max radius at scale16 is >700k and <1M", &fails);
        printf("    max radius scale16 = %d\n", maxr);
        big.radius = maxr;
        int rcmax = gmif_plan_search(&big, &plan);
        printf("    plan at max: cells=%lld tile=%d mode=%d\n",
               (long long)plan.cells, plan.tile_size, plan.use_tiles);
        expect_true(rcmax == 0 && plan.use_tiles == 1
                    && plan.cells <= 10000000000LL,
                    "plan at native max radius is accepted", &fails);
        big.radius = maxr + 1;
        rcmax = gmif_plan_search(&big, &plan);
        expect_true(rcmax == GMIF_ERR_GRID_TOO_LARGE,
                    "native max radius + 1 is rejected", &fails);

        big.radius = 30000000;
        int rcbig = gmif_plan_search(&big, &plan);
        expect_true(rcbig == GMIF_ERR_GRID_TOO_LARGE,
                    "r=30M rejected with GRID_TOO_LARGE (not a crash)", &fails);
        printf("    r=30M: %s\n", gmif_last_error());

        big.radius = 5000;
        gmif_plan_search(&big, &plan);
        expect_true(plan.use_tiles == 0, "auto picks monolithic for r=5000", &fails);
        big.radius = 40000;
        gmif_plan_search(&big, &plan);
        expect_true(plan.use_tiles == 1, "auto picks tiled for r=40000", &fails);

        big.radius = 10000000;
        big.use_tiles = 0;
        rcbig = gmif_plan_search(&big, &plan);
        expect_true(rcbig == GMIF_ERR_GRID_TOO_LARGE,
                    "forced monolithic r=10M rejected", &fails);
    }

    printf("\n== Tiled vs monolithic regression ==\n");
    {
        SearchParams mono, tiled;
        memset(&mono, 0, sizeof(mono));
        memset(&tiled, 0, sizeof(tiled));
        mono.seed = 262;
        mono.mc = gmif_mc_from_string("1.18");
        mono.radius = 500;
        mono.scale = 16;
        mono.refine = 0;
        mono.threads = 0;
        mono.use_tiles = 0;
        tiled = mono;
        tiled.use_tiles = 1;
        tiled.tile_size = 32;

        MushroomIsland *a = NULL, *b = NULL;
        int na = 0, nb = 0;
        int ra = gmif_search(&mono, &a, &na, NULL, NULL, NULL, NULL);
        int rb = gmif_search(&tiled, &b, &nb, NULL, NULL, NULL, NULL);
        expect_true(ra == 0 && rb == 0, "mono and tiled both ok", &fails);
        if (ra == 0 && rb == 0) {
            expect_true(na == nb, "same region count (cross-tile merged)", &fails);
            if (na == nb && na > 0) {
                expect_true(a[0].area == b[0].area, "top area identical", &fails);
                expect_true(a[0].center_x == b[0].center_x
                            && a[0].center_z == b[0].center_z,
                            "top center identical", &fails);
                expect_true(a[0].min_x == b[0].min_x && a[0].max_x == b[0].max_x
                            && a[0].min_z == b[0].min_z && a[0].max_z == b[0].max_z,
                            "top bounds identical", &fails);
            }
        }
        free(a);
        free(b);
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
/* benchmark                                                           */
/* ------------------------------------------------------------------ */

static int run_benchmark(const SearchParams *base, const int *thread_list, int nthreads)
{
    GmifSearchPlan plan;
    int rc = gmif_plan_search(base, &plan);
    if (rc != GMIF_OK) {
        fprintf(stderr, "Plan failed: %s\n", gmif_last_error());
        return 1;
    }
    printf("Benchmark seed=%" PRId64 " version=%s center=(%d,%d) radius=%d scale=%d refine=%d\n",
           base->seed, gmif_mc_to_string(base->mc), base->center_x, base->center_z,
           base->radius, base->scale, base->refine);
    print_plan(&plan, base);
    printf("\n%-8s %10s %10s %10s %8s %10s\n",
           "threads", "time(s)", "found", "top area", "same", "peak MB");

    MushroomIsland *ref = NULL;
    int nref = 0;
    int failures = 0;
    for (int i = 0; i < nthreads; i++) {
        SearchParams p = *base;
        p.threads = thread_list[i];
        MushroomIsland *isl = NULL;
        int n = 0;
        double t0 = now_sec();
        rc = gmif_search(&p, &isl, &n, NULL, NULL, NULL, NULL);
        double t1 = now_sec();
        if (rc != GMIF_OK) {
            printf("%-8d FAILED rc=%d (%s)\n", p.threads, rc, gmif_last_error());
            failures++;
            free(isl);
            continue;
        }
        int same = 1;
        int isRef = (ref == NULL);
        int64_t top_area = n > 0 ? isl[0].area : 0;
        if (!ref) {
            ref = isl;
            nref = n;
            isl = NULL;
        } else {
            if (n != nref) same = 0;
            else {
                for (int k = 0; k < n; k++) {
                    if (ref[k].area != isl[k].area
                        || ref[k].samples != isl[k].samples
                        || ref[k].min_x != isl[k].min_x || ref[k].max_x != isl[k].max_x
                        || ref[k].min_z != isl[k].min_z || ref[k].max_z != isl[k].max_z
                        || ref[k].center_x != isl[k].center_x
                        || ref[k].center_z != isl[k].center_z) {
                        same = 0;
                    }
                }
            }
            if (!same) failures++;
        }
        printf("%-8d %10.3f %10d %10" PRId64 " %8s %10ld\n",
               p.threads, t1 - t0, n, top_area,
               isRef ? "ref" : (same ? "yes" : "NO"), peak_rss_mb());
        fflush(stdout);
        free(isl);
    }
    free(ref);
    if (failures) printf("\n%d benchmark run(s) failed or diverged.\n", failures);
    return failures ? 1 : 0;
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
    p.center_x = 0;
    p.center_z = 0;
    p.radius = 5000;
    p.min_area = 0;
    p.scale = 16;
    p.refine = 0;
    p.include_shore = 0;
    p.threads = 0;           /* auto (physical cores) */
    p.max_results = 10;
    p.use_tiles = 2;         /* auto */
    p.tile_size = 0;

    int biome_ids[256];
    int biome_count = 0;
    int have_seed = 0;
    int dump_region = 0;
    int dump_map = 0;
    int debug = 0;
    int describe = 0;
    int map_step = 16;
    int map_pad = 64;
    const char *csv_path = NULL;
    const char *json_path = NULL;
    int do_benchmark = 0;
    int bench_threads[16] = {1, 2, 4, 6, 8, 10, 12, 16, 0};
    int bench_n = 9;

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
        } else if (!strcmp(argv[i], "--max-radius")) {
            printf("%d\n", gmif_max_radius(16));
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
        } else if (!strcmp(argv[i], "--center-x") && i + 1 < argc) {
            p.center_x = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--center-z") && i + 1 < argc) {
            p.center_z = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--radius") && i + 1 < argc) {
            p.radius = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--min-area") && i + 1 < argc) {
            p.min_area = strtoll(argv[++i], NULL, 10);
        } else if (!strcmp(argv[i], "--max-results") && i + 1 < argc) {
            p.max_results = atoi(argv[++i]);
            if (p.max_results < 0) p.max_results = 0;
        } else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            p.scale = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--biomes") && i + 1 < argc) {
            char *s = argv[++i];
            biome_count = 0;
            char *tok = strtok(s, ",");
            while (tok && biome_count < 256) {
                biome_ids[biome_count++] = atoi(tok);
                tok = strtok(NULL, ",");
            }
            if (biome_count == 0) {
                fprintf(stderr, "--biomes needs at least one id\n");
                return 1;
            }
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
        } else if (!strcmp(argv[i], "--describe")) {
            describe = 1;
        } else if (!strcmp(argv[i], "--csv") && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (!strcmp(argv[i], "--json") && i + 1 < argc) {
            json_path = argv[++i];
        } else if (!strcmp(argv[i], "--threads") && i + 1 < argc) {
            p.threads = atoi(argv[++i]);
            if (p.threads < 0) p.threads = 0;
            if (p.threads > 256) p.threads = 256;
        } else if (!strcmp(argv[i], "--threads-list") && i + 1 < argc) {
            char *s = argv[++i];
            bench_n = 0;
            char *tok = strtok(s, ",");
            while (tok && bench_n < 16) {
                bench_threads[bench_n++] = atoi(tok);
                tok = strtok(NULL, ",");
            }
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

    if (biome_count > 0) {
        p.biome_ids = biome_ids;
        p.biome_count = biome_count;
    }

    if (!have_seed && !describe) {
        usage();
        return 1;
    }
    if (p.radius <= 0 || p.radius > 30000000) {
        fprintf(stderr, "Radius must be in 1..30000000 (half-size in blocks)\n");
        return 1;
    }
    if (p.min_area < 0) p.min_area = 0;

    GmifSearchPlan plan;
    int plan_rc = gmif_plan_search(&p, &plan);
    if (describe) {
        if (plan_rc != GMIF_OK) {
            fprintf(stderr, "Plan failed: %s\n", gmif_last_error());
            return 1;
        }
        print_plan(&plan, &p);
        return 0;
    }
    if (plan_rc != GMIF_OK) {
        fprintf(stderr, "Search plan rejected:\n%s\n", gmif_last_error());
        return 1;
    }
    if (debug) print_plan(&plan, &p);

    if (do_benchmark) {
        return run_benchmark(&p, bench_threads, bench_n);
    }

    fprintf(stderr,
        "Large Biome Finder\n"
        "  seed=%" PRId64 "  version=%s  center=(%d,%d)  radius=%d (half-size)\n"
        "  min_area=%" PRId64 "  scale=%d  refine=%d  shore=%d  biomes=%d\n\n",
        p.seed, gmif_mc_to_string(p.mc), p.center_x, p.center_z, p.radius,
        p.min_area, p.scale, p.refine, p.include_shore, p.biome_count);

    MushroomIsland *islands = NULL;
    int count = 0;
    double t0 = now_sec();
    int rc = gmif_search(&p, &islands, &count, on_progress, NULL, NULL, NULL);
    double t1 = now_sec();
    fprintf(stderr, "\n");

    if (rc != 0) {
        fprintf(stderr, "Search failed (rc=%d code=%d)\n", rc, gmif_last_error_code());
        fprintf(stderr, "%s\n",
                gmif_last_error()[0] ? gmif_last_error() : "(no detail)");
        return 1;
    }

    printf("Large Biome Finder v" GMIF_VERSION "\n\n");
    printf("Seed: %" PRId64 "\n", p.seed);
    printf("Version: %s\n", gmif_mc_to_string(p.mc));
    printf("Search center: (%d, %d)\n", p.center_x, p.center_z);
    printf("Search Radius (half-size): %d blocks\n", p.radius);
    printf("World box: [%lld, %lld] .. [%lld, %lld]\n",
           (long long)plan.world_x0, (long long)plan.world_z0,
           (long long)(plan.world_x0 + plan.sx * plan.scale - 1),
           (long long)(plan.world_z0 + plan.sz * plan.scale - 1));
    printf("Scan mode: %s (tile=%d, threads=%d)\n",
           plan.use_tiles ? "tiled" : "monolithic", plan.tile_size, plan.threads);
    printf("Time: %.3f s   Peak RAM: %ld MB\n", t1 - t0, peak_rss_mb());
    if (p.min_area > 0)
        printf("Min Area: %" PRId64 " blocks^2\n", p.min_area);
    else
        printf("Min Area: (optional, off)\n");
    if (p.max_results > 0)
        printf("Max results (top-N): %d\n", p.max_results);
    else
        printf("Max results (top-N): (all)\n");
    printf("Sample scale: %d%s\n", p.scale, p.refine ? " (refined to 4)" : "");
    if (p.biome_count > 0) {
        printf("Target biomes:");
        for (int i = 0; i < p.biome_count; i++) printf(" %d", p.biome_ids[i]);
        printf("\n");
    } else {
        printf("Target biomes: %d%s\n", 14, p.include_shore ? " (14,15 shore included)" : " (mushroom fields)");
    }
    printf("\nFound: %d\n\n", count);

    for (int i = 0; i < count; i++) {
        const MushroomIsland *isl = &islands[i];
        double area_km2 = (double)isl->area / 1000000.0;
        printf("#%d\n", i + 1);
        printf("  Center: %d, %d\n", isl->center_x, isl->center_z);
        printf("  Area (%s): %" PRId64 " blocks^2  (approx %.3f km^2)\n",
               precision_label(isl->area_precision), isl->area, area_km2);
        if (dump_region) {
            printf("  Region ID: %d\n", i + 1);
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
            printf("  Distance from center: %.1f\n", isl->distance);
        } else {
            printf("  Span: %d x %d (diag %.0f)\n",
                   isl->width, isl->height, isl->diagonal_span);
            printf("  Bounds: [%d, %d] .. [%d, %d]\n",
                   isl->min_x, isl->min_z, isl->max_x, isl->max_z);
            printf("  Distance from center: %.0f\n", isl->distance);
            printf("  Compactness: %.4f (perimeter approx %d)\n",
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

    if (csv_path) {
        FILE *f = fopen(csv_path, "w");
        if (!f) {
            fprintf(stderr, "Cannot write CSV: %s\n", csv_path);
        } else {
            fprintf(f, "seed,version,center_x,center_z,radius,rank,"
                       "center_island_x,center_island_z,"
                       "min_x,max_x,min_z,max_z,x_span,z_span,diagonal_span,"
                       "area_estimated,area_precision,samples,"
                       "compactness,perimeter,distance\n");
            for (int i = 0; i < count; i++) {
                const MushroomIsland *isl = &islands[i];
                fprintf(f, "%" PRId64 ",%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,"
                           "%" PRId64 ",%d,%" PRId64 ",%.4f,%d,%.2f\n",
                        p.seed, gmif_mc_to_string(p.mc), p.center_x, p.center_z,
                        p.radius, i + 1, isl->center_x, isl->center_z,
                        isl->min_x, isl->max_x, isl->min_z, isl->max_z,
                        isl->width, isl->height, isl->diagonal_span,
                        isl->area, isl->area_precision, isl->samples,
                        isl->compactness, isl->perimeter, isl->distance);
            }
            fclose(f);
            printf("CSV written: %s\n", csv_path);
        }
    }

    if (json_path) {
        FILE *f = fopen(json_path, "w");
        if (!f) {
            fprintf(stderr, "Cannot write JSON: %s\n", json_path);
        } else {
            fprintf(f, "{\n");
            fprintf(f, "  \"seed\": %" PRId64 ",\n", p.seed);
            fprintf(f, "  \"version\": \"%s\",\n", gmif_mc_to_string(p.mc));
            fprintf(f, "  \"centerX\": %d,\n", p.center_x);
            fprintf(f, "  \"centerZ\": %d,\n", p.center_z);
            fprintf(f, "  \"radius\": %d,\n", p.radius);
            fprintf(f, "  \"minArea\": %" PRId64 ",\n", p.min_area);
            fprintf(f, "  \"scale\": %d,\n", p.scale);
            fprintf(f, "  \"refine\": %s,\n", p.refine ? "true" : "false");
            fprintf(f, "  \"mode\": \"%s\",\n", plan.use_tiles ? "tiled" : "monolithic");
            fprintf(f, "  \"elapsedSeconds\": %.3f,\n", t1 - t0);
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
