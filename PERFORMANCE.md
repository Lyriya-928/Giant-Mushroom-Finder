# Performance & scan strategy (v1.1.0)

**中文摘要**：v1.1.0 在上一轮多线程/采样优化的基础上，加入了常用与自定义群系
目标、产品更名为 Large Biome Finder，并把分块合并改成“按行流式”以去掉随
Tile 数增长的全量边界数组。分块搜索上限从 20 亿粗采样格提高到 100 亿，
scale 16 下原生最大半径从约 357,760 格提高到 **799,984 格**；GUI 直接显示
这个由原生库计算出的值。普通范围仍走整区域扫描，超过 1200 万粗采样格才
自动切换为分块流式扫描。下文给出可复现的实测数据与边界测试。

## Test environment

- Windows 11, Intel Core i5-13490F (6 P-cores + 4 E-cores, 16 logical CPUs), 38 GB free RAM.
- MinGW-w64 GCC 16.1.0, `-O3 -fopenmp`, JDK 26.0.1.
- Seed 262, Minecraft 1.18, scale 16, no refine, top-N = all, `gmif_cli.exe`.
- Wall-clock times from the CLI; peak memory from `PROCESS_MEMORY_COUNTERS`.
  This desktop was not idle, so single-thread numbers vary by roughly ±15%.
  The tables use a representative run and state the range where it matters.
- CPU utilisation from `typeperf "\Process(gmif_cli)\% Processor Time"`; on a
  16-logical-CPU machine 1600 % means all logical CPUs busy.

## Baseline (before the optimization work)

`--threads 1/2/4/8` produced **identical** results and nearly identical times:
the worker count was parsed in Java, passed through JNI and stored in
`SearchParams.threads`, but no native loop ever read it. The measured
`--benchmark` times at r=5000 were 47.8 / 50.2 / 38.3 / 39.2 s (variance from
the machine, not from threading).

The dominant cost was `gmif_fill_mask_range`:

- for scale > 4 it generated a **scale-4 window 16× larger** than the coarse
  grid and then kept only the centre sample of every 4×4 group, discarding
  15/16 of the work, serially;
- the monolithic path also allocated an unused Cubiomes cache;
- `extract_components` kept a full `int` label grid and then rescanned the
  whole grid once per component to count its perimeter
  (O(cells × components));
- progress stayed at 0 % until the whole fill finished, so long searches looked
  frozen and pause/stop had poor latency.

| case | time | peak RAM |
|---|---:|---:|
| r=5000, mono, 1 thread | 35.6 s (bench_mem), 36.0 s (CLI) | 29 MB |
| r=5000, tiled 256, 1 thread | 39.5 s | 9 MB |
| r=8000, auto (mono), 1 thread | 91.5 s | 67 MB |

## What changed in v1.1.0

1. **Direct centre sampling for 1.18+.** For MC ≥ 1.18 and scale ≥ 4, a coarse
   cell *c* is evaluated as
   `sampleBiomeNoise((c*scale/4 + scale/8), 15, ...)` in 4-block units. This is
   exactly the value the old stable scale-4 decimation kept, so the mask is
   bit-for-bit identical while 16× fewer noise samples are evaluated.
   MC ≤ 1.17 still needs the full scale-4 layer window and keeps the old
   decimation path, but it is now parallel too.
2. **OpenMP row bands.** Both fast and cached paths fill the mask in row bands
   with `schedule(dynamic,1)`. The band size is chosen from the worker count
   (enough bands to balance 6 P-cores + 4 E-cores) and, for the cached path,
   capped at ~2 MB per worker. Each band generates its own true-coordinate
   window, so every thread writes the same values as before.
3. **O(cells) CCA.** A visited bitset plus a growable DFS stack replaces the
   label grid; samples and perimeter edges are counted in the same traversal.
   The per-component full-grid rescans are gone.
4. **Real scan progress and cancellation.** The fill reports 0–60 % in
   mono mode and per-tile progress in tiled mode; pause/stop are polled every
   band. The GUI shows percent, found count and ETA.
5. **Automatic threads = physical cores.** Windows
   `GetLogicalProcessorInformationEx(RelationProcessorCore)` returns 10 on this
   machine, so Auto = 10 instead of 16 logical CPUs. Manual 1…256 is still
   honoured; both give identical results.
6. **Tiled path uses the same parallel fill** and streams border contacts one
   tile row at a time. Only the current row and the previous row's bottom
   contacts are kept, so memory does not grow with the number of tiles.
7. **Auto scan selection.** Monolithic while the coarse grid is ≤ 12 M cells,
   tiled otherwise. The threshold is justified below; memory estimates and the
   resolved mode are shown in the GUI before a search starts.
8. **Higher, safe ceiling.** The tiled cap is now 10 × 10⁹ coarse cells
   (was 2 × 10⁹). The native planner derives the maximum radius from that cap
   and rejects larger input before allocation. At scale 16 the accepted radius
   is 799,984 blocks. Tile/region counters, int32 sample coordinates and int64
   cell/area arithmetic are all checked; fragmented targets are rejected above
   4 M connected regions instead of overflowing an `int`.
9. **Biome presets + custom ids.** The GUI keeps the common mushroom target and
   adds plains/forest/desert/jungle/savanna/badlands/taiga/swamp/ocean/cherry
   grove presets plus validated custom ids. The native search core already works
   on arbitrary target ids, so no new search path is involved.

## After v1.1.0

### Time and memory (same seed/version/scale)

| case | threads | time | peak RAM | speed-up vs baseline |
|---|---:|---:|---:|---:|
| r=5000 mono | 1 | 2.65 s | 6 MB | 13.6× |
| r=5000 mono | 10 (Auto) | 0.42–0.49 s | 6 MB | ≈ 72–85× |
| r=5000 mono | 16 | 0.34–0.44 s | 6 MB | ≈ 82–105× |
| r=5000 tiled 256 | 10 | 0.46 s | 6 MB | — |
| r=8000 mono | 1 | 6.7–8.2 s | 6–7 MB | ≈ 11–14× |
| r=8000 mono | 10 | 1.03–1.17 s | 7 MB | ≈ 78–89× |
| r=15000 mono | 1 | 30.7 s | 9 MB | — |
| r=15000 mono | 10 | 3.98 s | 9 MB | — |
| r=15000 mono | 16 | 3.38 s | 9 MB | — |
| r=30000 tiled 256 | 1 | 166.8 s | 6 MB | — |
| r=30000 tiled 256 | 10 | 16.1–21.9 s | 6 MB | — |
| r=30000 tiled 256 | 16 | 23.3 s | 6 MB | — |

The r=5000 full-grid speed-up is 13.6× from the 16× algorithm change alone
(the 1.18 fast path); adding 10 physical workers brings it to ≈ 80×. Memory
falls from 29 MB to 6 MB at r=5000 and from 67 MB to 7 MB at r=8000.

### Thread scaling and consistency

Audit runs every search with 1, 2, 4, 8, 10 and 16 threads for monolithic and
tiled modes. All returned **identical** islands (area, samples, bounds, centre,
perimeter, precision, distance). The table below is the r=5000 mono timing
from `--benchmark` (one run; expect ±10 %):

| threads | 1 | 2 | 4 | 8 | 10 | 12 | 16 |
|---|---:|---:|---:|---:|---:|---:|---:|
| time (s) | 2.70 | 1.60 | 0.88 | 0.62 | 0.45 | 0.39 | 0.35–0.44 |

Scaling is bounded by the 6 P + 4 E topology. 16 logical threads use SMT on the
P-cores; they are sometimes ~10 % faster and sometimes slower than 10 physical
cores. Auto therefore chooses 10 (physical cores) and leaves the headroom for
the UI; manual 12/16 is available.

For the r=30000 tiled search the Windows process CPU counter read
584–813 % (1600 % = all logical CPUs), i.e. ~6–8 logical cores busy. The rest
is the 6 P + 4 E asymmetry plus the serial per-tile CCA/border/merge phases.
This is a large change from the baseline (one core, ~100 %).

### Scan-mode threshold

- The tiled path is not slower than the full-grid path in practice (r=5000:
  2.59 s vs 2.70 s at 1 thread), so the choice is about memory and stability,
  not speed.
- Measured monolithic peak: 6 MB at 0.39 M cells, 7 MB at 1 M cells, 9 MB at
  3.5 M cells. The CCA rewrite removed the label grid and the component array
  is now growable, so ≈ 4 B/cell + one fill band per worker is a safe model.
- 12 M cells predicts ≈ 50–60 MB peak — still comfortable, but the point where
  a very long full-grid search starts to hold a large mask and a large
  component array in memory. At scale 16 that is radius ≈ 27,700 blocks; at
  scale 4 ≈ 6,900 blocks. Everything above streams in 256-sample tiles
  (128 at scale 1), keeping peak memory at 6–30 MB.
- Hard limits: tiled work > 10 × 10⁹ coarse cells or > 2 M tiles after tile
  growth is rejected with a concrete error. The native maximum radius is
  computed from the cell cap (`gmif_max_radius`), shown by the GUI, and checked
  in the planner before any allocation. At scale 16 it is **799,984** blocks.

### Large-radius and row-streaming memory

- Before the row-streaming change, all tile border contacts were kept until the
  end: for a broad target (ocean/plains) that grows as ~4 × cells / tile cells
  and reached hundreds of MB at the old 2 × 10⁹ ceiling. Raising the ceiling
  without this change would have been unsafe.
- The new merge keeps one row of contacts plus the previous row's bottom
  contacts. For the 10 × 10⁹-cell ceiling at tile 256 that is roughly 4 MB of
  border buffers per row, independent of the ~152 k tiles.
- Measured: seed 262, MC 1.18, ocean target id 0, r=30000 (14.07 M cells),
  tile 256, 10 threads: **7.7 s wall time, 7 MB peak**, 3 result rows.
  At the 10¹⁰-cell ceiling the old all-tile border arrays would hold roughly
  1.2 GB for a fully covered target; the row buffers stay in the low MB range.
- A maximum-size search (r=799,984, ~10¹⁰ cells) is a multi-hour operation.
  The GUI requires an explicit confirmation for very large plans; the cap is a
  safety boundary, not a performance promise.

## Correctness verification

`gmif_cli --audit` (49 checks) and the Java `--self-test` pass on the built
artifacts. The audit includes:

- **Fast fill == stable decimation**, cell by cell, for MC 1.18 / 1.19.4 /
  1.21.3 at scales 4, 16 and 64, including negative and odd windows.
- **Monolithic vs tiled** equality for seeds/versions/scales, tile sizes
  7, 8, 13, 16, 32, 33, 64, 127 and 256, custom centres and custom biome ids.
- **Thread consistency** 1…16 for both modes.
- **Cross-tile merge**: a main island spanning many tile borders is not split;
  separate islands stay separate; no area double counting.
- **Custom centre**: negative coordinates, results inside the requested box,
  distance measured from that centre.
- **Custom biome targets**: cold-ocean id 46 and a broad ocean id 0 target
  (many tile rows/borders) match between modes; the reported centre really
  samples as cold ocean; `include_shore` equals `{14,15}`.
- **Auto mode**: r=5000 plans monolithic, r=30000 plans tiled, and auto /
  forced mono / forced tiled produce the same 45 islands at r=15000.
- **Native ceiling**: the maximum scale-16 radius plans a tiled scan under the
  10¹⁰-cell cap, and max + 1 is rejected before allocation.
- **Top-N after global merge** matches the global sort.
- Java self-test: JNI signatures, plan/mode resolution, max-radius/preset
  checks, real search, custom centre/biome, progress reaching 100 %, and
  settings-independent result parsing.

GUI checks (Swing `printAll` captures and reflection-driven clicks) verified
the results table, language switch without losing results, common and custom
biome targets, displayed max radius, and pause/resume/stop on a 30 k-radius
tiled search.

## GitHub Actions

`.github/workflows/windows-ci.yml` runs on `windows-latest` for pushes and pull
requests. It installs JDK 21 and MinGW-w64, then runs
`tools/ci-windows.ps1`, which performs the same `build.ps1` build, native
`--audit`, Java `--self-test`, and max-radius check. Logs and a result summary
are written to the job summary and uploaded as a workflow artifact. The PR
checks therefore show the actual build and test outcome, not just a green
syntax check.

## Reproduce

```powershell
# build
.\build.ps1

# audit + self-test
.\build\gmif_cli.exe --audit
java --enable-native-access=ALL-UNNAMED -jar .\build\LargeBiomeFinder-1.1.0.jar --self-test

# native maximum radius for scale 16
.\build\gmif_cli.exe --max-radius

# thread/time/memory comparison (1..16 threads + auto)
.\build\gmif_cli.exe --seed 262 --version 1.18 --radius 5000 --benchmark

# scan plan, custom centre and biome presets
.\build\gmif_cli.exe --seed 262 --version 1.18 --radius 5000 --describe
.\build\gmif_cli.exe --seed 262 --version 1.18 --center-x -1000 --center-z -1000 --radius 1200
.\build\gmif_cli.exe --seed 262 --version 1.18 --radius 2000 --biomes 1
.\build\gmif_cli.exe --seed 262 --version 1.18 --radius 600 --biomes 46
.\build\gmif_cli.exe --seed 262 --version 1.18 --radius 30000 --biomes 0 --tiles --tile-size 256
```

## Remaining limitations

- The UI theme is a painted Fluent-style light theme (rounded cards, Fluent
  colours/typography, custom progress/buttons). Real DWM Mica/Acrylic blur is
  not enabled because mixing a transparent window with opaque Swing children
  is fragile across Windows versions; the app deliberately stays a normal
  Swing window.
- Search areas are rectangles only. Biome targets are cubiomes biome ids
  (0–255); there is no shape predicate or "any biome" mode.
- Area is an estimate from sampled cells (`samples × scale²`); refine at scale 4
  improves it, scale 1 is available via the CLI but is much slower.
- For MC ≤ 1.17 the full scale-4 layer window is still required, so the 16×
  shortcut does not apply; the parallel bands and CCA rewrite still help.
- Thread scaling is bounded by the CPU topology; 16 logical CPUs are not 16
  independent cores. Auto uses physical cores and manual override remains.
- The 10¹⁰-cell ceiling is a hard safety bound, not a promise of interactive
  speed: a maximum-radius search can take hours and there is no resume-from-disk.
- Settings are stored under `%APPDATA%\LargeBiomeFinder\settings.properties`.
  The previous `GiantMushroomFinder` file is read once for migration. If the
  location is not writable the app keeps working without persistence.
