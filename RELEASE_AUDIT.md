# Release Audit

**Project:** Giant Mushroom Island Finder (GMIF)  
**Date:** 2026-09-17  
**Scope:** RC verification only (no new features)

---

## Automated Tests

**PASS**

| Suite | Result |
|---|---|
| `gmif_cli --self-test` | PASS |
| `gmif_cli --audit` | **38 PASS / 0 FAIL** |
| Java `--self-test` | PASS |
| Clean-tree build + CLI self-test | PASS |

---

## Tiled vs Monolithic

**PASS**

Compared `region count`, `area`, `samples`, `bounds`, `center`, `span` for each region.

| Seed | Version | Radius | Tile sizes (samples) | Result |
|---|---|---:|---|---|
| 262 | 1.18 | 500 | 8, 32, 256 | PASS |
| 262 | 1.18 | 2000 | 8, 32, 256 | PASS |
| 262 | 1.18 | 5000 | 8, 32, 256 | PASS |
| 0 | 1.18 | 500 | 8, 32, 256 | PASS |
| 1 | 1.20 | 500 | 8, 32, 256 | PASS |
| 12345 | 1.21.3 | 500 | 8, 32, 256 | PASS |
| 262 | 1.18 | 500 (refine=1) | 32 | PASS (area 74480 + center match) |

Additional tile sweep 16/64 vs 256 at r=1500: PASS.

**Fixes made during audit:**

1. Sort tie-break `(area, min_x, min_z)` so equal-area regions order identically.
2. Stable sampling: `scale>4` fills via **scale=4 + decimation** (cubiomes scale>4 fast path is Range-layout dependent and broke mono/tiled equality).
3. Tiled path now runs the same **refine** as monolithic (`gmif_refine_island`).
4. Refine center formula aligned with monolithic `refine_component`.

---

## Cross-Tile Boundary

**PASS**

| Case | Result |
|---|---|
| Island spanning many tiles (`tile_size=8` samples = 128 blocks @ scale 16) | Main island not split (area=73984) |
| Crosses X **and** Z tile lines | bounds `[-160,-208]..[95,207]` |
| Two separate islands stay separate | mono=2 tiled=2 |
| No area double-count on shared borders | `mono=73984/289 tiled=73984/289` |
| Tile size 16/64 vs 256 topology | identical |

---

## GUI

**PASS** (automated / code-integrated)

| Check | Result |
|---|---|
| JNI load (`build/lib/mushroomfinder.dll`) | PASS |
| Java self-test (search + results) | PASS |
| Auto tiled path via `use_tiles=2` | PASS (native) |
| Stages: init → starting → scan/tile → merge → done | PASS (SearchRunner) |
| Progress percent/indeterminate policy | PASS (code + native control) |
| Export CSV/JSON (CLI smoke) | PASS |
| Copy coordinates | Code path present (manual UI confirm recommended) |
| Pause / Resume / Stop | Implemented + native `GmifControl`; manual UI confirm recommended |

**Note:** Full interactive mouse-driven GUI pass (click Pause mid-search, etc.) was not automated on this agent host; lifecycle is covered by Java self-test + native control API.

---

## Large Radius

**PASS** (startup / planner; not full search completion)

| Radius | Behavior |
|---:|---|
| 100,000 | Starts; tile planner runs; no false `200000` project cap |
| 200,000 | Accepted (world border is 30,000,000) |
| 1,000,000 | Tiled mode selected by auto policy; no immediate `GRID_TOO_LARGE` |
| 30,000,000+1 | Rejected: world border |
| 20,000,000 mono-style budget | Real message: grid/memory too large **or** auto falls back to tiled |

World border limit = **30,000,000**. No artificial 200,000 cap.

---

## Memory

**PASS** (tiled does not allocate whole-map mask)

| Radius | Mode | Peak driver | Whole-map alloc? |
|---:|---|---|---|
| 20,000 | tiled | `tile_size²` (samples) | No |
| 100,000 | tiled | `tile_size²` | No |
| 1,000,000 | tiled (auto) | `tile_size²` | No |

README states:

> Tiled mode removes the whole-map memory requirement, but search time still
> grows approximately with the searched area.

Auto policy now forces tiled when the **stable fill** buffer (scale-4 footprint at scale=16) would exceed ~40M cells, not only when the coarse grid exceeds the legacy cap.

---

## Multi-thread Consistency

**PASS**

Monolithic, seed 262, r=2000, scale=16: threads **1 / 2 / 4 / 8** → identical `area`, `bounds`, `center`, `Top N`.

(Tile-parallel was **not** implemented — as requested.)

---

## Clean Build

**PASS**

From a clean copy of `native/ src/ cubiomes/ Makefile *.ps1 *.md LICENSE NOTICE`:

```text
mingw32-make -f Makefile CC=gcc all
javac …
gmif_cli --self-test  → PASS
```

**Requirements documented in README:**

- Windows 10+
- JDK 17+ (tested JDK 26)
- MinGW-w64 GCC with OpenMP (`-fopenmp`) — e.g. WinLibs via winget
- CMake **optional** (Makefile primary; CMake not required for build)
- PowerShell scripts: `build.ps1`, `run-gui.ps1`
- DLL load order: `gmif.native.path` → `java.library.path` → `build/lib/` → `build/`

---

## License / Attribution

**PASS**

| Item | Status |
|---|---|
| `LICENSE` (MIT, project) | Present |
| `NOTICE` (third-party + references) | Present |
| `cubiomes/LICENSE` (MIT) | Present, vendored unmodified |
| SunnySlopes / cubiomes-viewer | Architecture reference only — **no source copied** (stated in NOTICE + file headers) |
| File-level attribution headers | Present on native search / JNI / tile modules |

---

## Known Limitations

1. **Search time** grows with area; tiled only removes the whole-map **memory** wall.
2. **`scale > 4` sampling** is done at scale 4 and decimated for mono/tiled equality — slower than cubiomes’ scale-16 fast path.
3. **r ≥ ~1e5–1e6** can start and stream tiles, but a full run is long on a single machine.
4. **Interactive GUI** pause/resume/export confirmation is manual on a desktop session.
5. Area is always **estimated** (sample-based), labeled as such.
6. No Multi-Seed, no map preview, no tile-parallel (out of scope for this RC).

---

## Release Recommendation

# READY

Core correctness (tiled ≡ monolithic), cross-tile merge, thread consistency,
license/attribution, clean build, and automated suites are green.

Recommended before tagging a public release: one **5-minute human GUI smoke**
(Pause / Resume / Stop / Export / Copy) on the target Windows machine.
