/*
 * Large Biome Finder — lush caves exclusion (Minecraft 1.18+)
 *
 * The surface search decides where an island is.  This module answers a
 * second, independent question for every candidate: do lush caves generate
 * in the ground below that island?  If they do, the island is dropped as a
 * whole — the search never removes individual samples and never splits a
 * region, because the island shape comes from the 2D surface pass and only
 * the keep/drop decision is made here.
 *
 * Implementation notes:
 *   - Cubiomes is asked for a scale-4 (4x4 blocks) target mask of the island
 *     bounding box.  Only columns where the mask is set (i.e. that belong to
 *     the island) are tested, so a bounding box that also covers a strait or
 *     a neighbouring island does not poison the result.
 *   - Each column is sampled with the 1.18+ 3D biome noise at scale 4 over
 *     block y = -64..319 in 4-block steps (96 samples).  That is exactly the
 *     vertical resolution Minecraft evaluates the underground biome grid at,
 *     so no cave cell can hide between two samples.
 *   - Work is streamed in row bands; a band is at most GMIF_LUSH_BAND_CELLS
 *     mask cells and one column buffer is 96 ints (384 B) per thread.  A full
 *     island volume is never materialised as a 3D array.
 *   - The band loop honours pause/stop via the shared GmifControl, and the
 *     column loop is parallelised with OpenMP over band rows.
 *
 * Correctness of the vertical range: above the terrain surface the 1.18+
 * depth noise keeps returning the surface biome (sampling y=320 over a
 * mushroom island still yields mushroom_fields), while deeper samples walk
 * through the underground biomes.  Testing the full column therefore cannot
 * create a false "lush caves" from air above the island, and it cannot miss a
 * cave below it.
 */
#include "mushroom_finder.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "generator.h"
#include "util.h"

/** Mask cells kept in memory per streaming band (also bounds stop latency). */
#define GMIF_LUSH_BAND_CELLS 65536

/** Number of 4-block vertical samples in one column (-64..319). */
#define GMIF_LUSH_SY (GMIF_LUSH_Y4_MAX - GMIF_LUSH_Y4_MIN + 1)

static int64_t lush_floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) q--;
    return q;
}

static int lush_should_stop(GmifControl *control, CancelFn cancel, void *ud)
{
    if (control) {
        if (control->stop) return 1;
        if (control->pause && gmif_control_wait_if_paused(control)) return 1;
    }
    if (cancel && cancel(ud)) return 1;
    return 0;
}

int gmif_supports_lush_caves(int mc)
{
    return mc >= MC_1_18;
}

int gmif_targets_mushroom(const SearchParams *p)
{
    if (!p) return 1;
    if (p->biome_ids && p->biome_count > 0) {
        for (int i = 0; i < p->biome_count; i++) {
            if (p->biome_ids[i] == mushroom_fields) return 1;
        }
        return 0;
    }
    /* Historical default target is mushroom_fields (+ shore optionally). */
    return 1;
}

int gmif_island_has_lush_caves(const void *generator, const SearchParams *params,
                               const MushroomIsland *isl,
                               GmifControl *control, CancelFn cancel,
                               void *userdata)
{
    const Generator *g = (const Generator *)generator;
    if (!g || !params || !isl) {
        return gmif_set_error(GMIF_ERR_INVALID_ARG,
                              "invalid arguments to the lush caves check");
    }
    if (g->mc < MC_1_18) {
        return gmif_set_error(GMIF_ERR_VERSION,
            "the lush caves check requires Minecraft 1.18 or newer (got %s).\n"
            "  Underground biome generation does not exist before 1.18, so the\n"
            "  2D biome data of older versions cannot be used as a substitute.",
            mc2str(g->mc));
    }
    if (g->dim != DIM_OVERWORLD) return 0;

    int64_t bx0 = isl->min_x, bx1 = isl->max_x;
    int64_t bz0 = isl->min_z, bz1 = isl->max_z;
    if (bx0 < -30000000) bx0 = -30000000;
    if (bz0 < -30000000) bz0 = -30000000;
    if (bx1 > 30000000) bx1 = 30000000;
    if (bz1 > 30000000) bz1 = 30000000;
    if (bx1 < bx0 || bz1 < bz0) return 0;

    /* Island bounding box in scale-4 (4x4 block) cells. */
    int64_t x4 = lush_floor_div(bx0, GMIF_LUSH_SAMPLE_SCALE);
    int64_t z4 = lush_floor_div(bz0, GMIF_LUSH_SAMPLE_SCALE);
    int64_t w = lush_floor_div(bx1, GMIF_LUSH_SAMPLE_SCALE) - x4 + 1;
    int64_t h = lush_floor_div(bz1, GMIF_LUSH_SAMPLE_SCALE) - z4 + 1;
    if (w <= 0 || h <= 0) return 0;
    if (w > INT_MAX || h > INT_MAX
        || x4 < INT_MIN || x4 > INT_MAX
        || z4 < INT_MIN || z4 > INT_MAX) {
        return gmif_set_error(GMIF_ERR_OVERFLOW,
            "lush caves check: island footprint overflows 32-bit sample coordinates");
    }

    int threads = params->threads > 0 ? params->threads : gmif_default_threads();
    if (threads > 256) threads = 256;
    if (threads < 1) threads = 1;

    /* Stream the footprint in row bands.  band rows x w cells stays bounded
     * even for very wide islands, and one row never exceeds w cells. */
    int band = 1;
    if (w < GMIF_LUSH_BAND_CELLS) band = (int)(GMIF_LUSH_BAND_CELLS / w);
    if (band < 1) band = 1;
    if ((int64_t)band > h) band = (int)h;

    unsigned char *mask = (unsigned char *)malloc((size_t)w * (size_t)band);
    if (!mask) {
        return gmif_set_error(GMIF_ERR_ALLOC,
            "allocation failed: lush caves mask (%lld x %d cells)",
            (long long)w, band);
    }

    volatile int found = 0;
    volatile int gen_error = 0;
    volatile int aborted = 0;
    int rc = GMIF_OK;

    for (int64_t z0 = 0; z0 < h; z0 += band) {
        if (found || aborted) break;
        if (lush_should_stop(control, cancel, userdata)) {
            rc = GMIF_ERR_CANCELLED;
            break;
        }
        int br = (int)((h - z0 < (int64_t)band) ? (h - z0) : (int64_t)band);

        /* Same target mask as the search, at 4-block resolution. */
        rc = gmif_fill_mask_range(g, params, GMIF_LUSH_SAMPLE_SCALE,
                                  x4, z4 + z0, (int)w, br, mask,
                                  control, -1, -1);
        if (rc != GMIF_OK) break;

#ifdef _OPENMP
        #pragma omp parallel for schedule(dynamic, 1) num_threads(threads)
#endif
        for (int r = 0; r < br; r++) {
            if (found || gen_error || aborted) continue;
            if (lush_should_stop(control, cancel, userdata)) {
                aborted = 1;
                continue;
            }
            const unsigned char *row = mask + (size_t)r * (size_t)w;
            int col[GMIF_LUSH_SY];
            for (int64_t x = 0; x < w; x++) {
                if (!row[x]) continue;
                if (found) break;
                Range rg;
                rg.scale = GMIF_LUSH_SAMPLE_SCALE;
                rg.x = (int)(x4 + x);
                rg.z = (int)(z4 + z0 + r);
                rg.sx = 1;
                rg.sz = 1;
                rg.y = GMIF_LUSH_Y4_MIN;
                rg.sy = GMIF_LUSH_SY;
                if (genBiomes(g, col, rg) != 0) {
                    gen_error = 1;
                    break;
                }
                for (int k = 0; k < GMIF_LUSH_SY; k++) {
                    if (col[k] == GMIF_BIOME_LUSH_CAVES) {
                        found = 1;
                        break;
                    }
                }
            }
        }
        if (gen_error) {
            rc = gmif_set_error(GMIF_ERR_BIOME_GEN,
                                "lush caves check: biome generation failed");
            break;
        }
        if (aborted) {
            rc = GMIF_ERR_CANCELLED;
            break;
        }
    }

    free(mask);
    if (rc != GMIF_OK) return rc;
    return found ? 1 : 0;
}
