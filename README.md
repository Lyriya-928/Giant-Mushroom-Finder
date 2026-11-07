# Large Biome Finder

English | [简体中文](README.zh-CN.md)

A standalone Java desktop application for finding large connected regions of
**any biome** in Minecraft Java Edition.

Repository: <https://github.com/Lyriya-928/Large-Biome-Finder>

Enter a seed, Minecraft version, search centre and radius, then choose a common
biome preset or enter custom Cubiomes biome ids. The app reports the largest
matching regions with coordinates, estimated area, bounds, distance and
compactness. Search areas are rectangles; this is not a Minecraft mod.

> **This is a standalone application, not a Minecraft mod.**

## Features

* Minecraft Java Edition seed searching
* Large connected-region detection for any biome id (0–255)
* Common biome presets: Mushroom Fields, Mushroom + Shore, Plains, Forest,
  Desert, Jungle, Savanna, Badlands, Taiga, Swamp, Ocean, Cherry Grove (1.20+)
* Custom biome id input (comma separated, deduplicated, validated)
* Configurable search centre X/Z and radius
* Real native maximum radius shown in the GUI (799,984 blocks at scale 16)
* Top N results sorted by estimated area
* Optional scale 16 → 4 refinement
* Parallel native sampling using all physical cores (manual override available)
* Automatic full-grid vs tiled streaming scan
* Row-streamed cross-tile merging with bounded memory
* Pause / Resume / Stop with real progress and ETA
* Coordinate copying
* CSV / JSON export
* Recent searches and persistent settings
* Chinese and English interfaces
* Lightweight Fluent-style Swing UI
* CLI and Java Swing GUI

See [PERFORMANCE.md](PERFORMANCE.md) for measured bottlenecks, benchmark data,
thread scaling, the scan-mode threshold and the limit analysis.

## How It Works

```text
Seed + Version + Search centre + Biome target
      ↓
Cubiomes biome generation
      ↓
Target biome sampling
      ↓
4-neighbor connected components
      ↓
Row-streamed tile merging with DSU
      ↓
Optional scale-4 refinement
      ↓
Area / Bounds / Distance / Compactness
      ↓
Top N results
```

For MC 1.18+ and scale > 4 the sampler evaluates the centre sample of each
coarse cell directly. This is exactly the value the previous "stable scale-4
decimation" kept, so results are unchanged, but 16× fewer biome samples are
generated. Mask filling runs in parallel row bands over all physical cores.

Normal areas use the full-grid path. Above 12,000,000 coarse sample cells the
application switches automatically to tiled streaming. Only the current tile
row and the previous row's bottom contacts are kept for merging, so peak
memory depends on tile size and grid width rather than the total radius
(observed 6–30 MB). The scan mode, estimated peak memory and native maximum
radius are shown in the GUI before a search.

Search time still increases approximately with the searched area. `Threads =
Auto` uses the number of physical cores (10 on a 6P+4E i5-13490F); manual
1–256 is available and does not change results.

## Limits

* Full-grid path: ≤ 12,000,000 coarse sample cells (about 60 MB predicted).
* Tiled path: ≤ 10,000,000,000 coarse sample cells, with ≤ 2,000,000 tiles
  after automatic tile-size growth.
* At scale 16 that is a **maximum radius of 799,984 blocks**. The GUI reads this
  value from the native library, displays it in the radius label, and rejects
  larger input with a specific message. A maximum-size search is a multi-hour
  operation; the GUI asks for confirmation for very large plans.
* Integer safety: cell counts and areas use `int64_t`; sample coordinates and
  grid dimensions are checked against `int32` limits; tile/region counts have
  explicit caps; unsupported requests are rejected before allocation.
* MC ≤ 1.17 still needs the full scale-4 layer window (no 16× shortcut), but it
  is parallel and uses the same bounded tiled merge.

## Search Accuracy

Estimated area is calculated from sampled target-biome points:

```text
area ≈ positive samples × scale²
```

Optional refinement uses stable scale-4 sampling for more accurate area and
boundaries. The reported area is an estimate, not an exact block count.
Every result is the largest matching connected component; refinement does not
change which regions are found, only their reported statistics.

## Download

Prebuilt Windows binaries are published on the
[Releases](https://github.com/Lyriya-928/Large-Biome-Finder/releases) page:

* `LargeBiomeFinder-<version>-windows-x64.zip` — JAR, CLI, DLL and docs
* `LargeBiomeFinder-<version>.jar` — GUI / CLI JAR with the native DLL embedded
* `gmif_cli.exe` — standalone native CLI
* `gmif.dll` — JNI library for embedding
* `SHA256SUMS.txt` — checksums for all assets

```powershell
java -jar LargeBiomeFinder-1.1.0.jar
```

The JAR extracts its embedded DLL on first run, so no separate install step is
needed. Building from source is described below.

## Building on Windows

### Requirements

* Windows
* JDK 17 or newer
* MinGW-w64 GCC with OpenMP
* `make` / `mingw32-make`
* PowerShell (for the build script)

Build, package and smoke-test:

```powershell
.\build.ps1
```

Or build the native CLI manually:

```powershell
mingw32-make -f Makefile CC=gcc all
```

Start the GUI:

```powershell
.\run-gui.ps1
```

Build outputs:

```text
build/
├─ gmif_cli.exe
├─ gmif.dll          (also embedded in the JAR)
└─ LargeBiomeFinder-1.1.0.jar
```

## CLI examples

```text
gmif_cli --seed 262 --version 1.18 --radius 2000 --min-area 5000 --refine
gmif_cli --seed 262 --version 1.18 --radius 2000 --biomes 1
gmif_cli --seed 262 --version 1.18 --center-x -1000 --center-z -1000 --radius 1200
gmif_cli --seed 262 --version 1.18 --radius 600 --biomes 46
gmif_cli --seed 262 --version 1.18 --radius 5000 --describe
gmif_cli --seed 262 --version 1.18 --radius 5000 --benchmark
gmif_cli --max-radius
```

`--threads 0` means automatic (physical cores). `--tiles` / `--no-tiles`
force a scan mode; `--tile-size` is in samples, not blocks. `--max-radius`
prints the native maximum for scale 16.

Common biome ids: `14` Mushroom Fields, `15` Mushroom Shore, `1` Plains,
`4` Forest, `2` Desert, `21` Jungle, `35` Savanna, `37` Badlands, `5` Taiga,
`6` Swamp, `0` Ocean, `185` Cherry Grove (1.20+).

## Validation

```powershell
.\build\gmif_cli.exe --audit
.\build\gmif_cli.exe --self-test
java --enable-native-access=ALL-UNNAMED -jar .\build\LargeBiomeFinder-1.1.0.jar --self-test
```

The audit covers:

* Fast centre sampling vs. the original stable scale-4 mask, cell by cell
* Monolithic vs. tiled search consistency (odd tile sizes, negative centres)
* Broad targets (ocean/plains) over many tile rows and full tile borders
* Cross-tile region merging and no area double counting
* Refinement consistency
* Thread consistency 1…16 for both scan modes
* Custom centre bounds and distance
* Custom/common biome targets and legacy shore equivalence
* Automatic mode selection near the threshold
* Native maximum radius accepted, max + 1 rejected
* Top-N ordering after the global merge

GitHub Actions runs the same build, native audit and Java self-test on a
Windows runner for pushes and pull requests. See [PERFORMANCE.md](PERFORMANCE.md)
for the full measured results.

## Releasing

Pushing a `v*` tag triggers the
[Release workflow](.github/workflows/release.yml): it rebuilds on a Windows
runner, re-runs the native audit and Java self-test, then publishes the JAR,
CLI, DLL, ZIP and `SHA256SUMS.txt` to GitHub Releases.

```powershell
# 1. bump VERSION (for example 1.1.0), commit and merge to main
# 2. tag and push
git tag -a v1.1.0 -m "Large Biome Finder 1.1.0"
git push origin v1.1.0
```

The tag must match `VERSION`; otherwise the workflow fails at packaging and
publishes nothing. To dry-run the packaging locally, use
`.\tools\package-release.ps1 -Tag v1.1.0` (outputs under `build\release\`).

## Credits

### Cubiomes

Used for Minecraft Java Edition biome and world generation.

Cubiomes is distributed under the MIT License.

### SunnySlopes Projects

The following projects were used as architectural references:

* FortressFinderGUI
* RiverFinderGUI
* SlimeFinderGUI

They were used as references for Java/JNI/native search architecture and
workflow design. No source code was copied.

### Cubiomes Viewer

Used as a reference for Minecraft biome visualization and seed-finding
workflows.

See `NOTICE` for third-party attribution details.

## License

This project is licensed under the MIT License.
