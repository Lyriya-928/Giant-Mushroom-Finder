# Giant Mushroom Island Finder

A standalone Java desktop application for finding large **Mushroom Fields** in Minecraft Java Edition.

Enter a seed, Minecraft version, and search radius to find large Mushroom Fields regions and view their coordinates, area, bounds, and other statistics.

> **This is a standalone application, not a Minecraft mod.**

## Features

* Minecraft Java Edition seed searching
* Mushroom Fields region detection
* Configurable search radius
* Top N results sorted by estimated area
* Optional scale 16 → 4 refinement
* Multi-threaded native search
* Tiled streaming search for large search areas
* Cross-tile region merging
* Pause / Resume / Stop
* Coordinate copying
* CSV / JSON export
* Chinese and English interfaces
* CLI and Java Swing GUI

## How It Works

The search pipeline is:

```text
Seed + Version
      ↓
Cubiomes biome generation
      ↓
Mushroom Fields sampling
      ↓
4-neighbor connected components
      ↓
Tiled region merging with DSU
      ↓
Optional scale-4 refinement
      ↓
Area / Bounds / Distance / Compactness
      ↓
Top N results
```

For large search radii, the application uses tiled streaming instead of allocating the entire search area at once.

Each tile is processed and released after use. Regions crossing tile boundaries are merged globally with a Disjoint Set Union structure.

This keeps peak memory usage dependent mainly on tile size rather than the total search radius.

Search time still increases approximately with the searched area.

## Search Accuracy

The default scan uses a coarse sampling scale.

Estimated area is calculated from sampled Mushroom Fields points:

```text
area ≈ positive samples × scale²
```

Optional refinement uses stable scale-4 sampling for more accurate area and boundaries.

The reported area is an estimate, not an exact block count.

## Building on Windows

### Requirements

* Windows
* JDK 17 or newer
* MinGW-w64 GCC
* OpenMP
* `make` / `mingw32-make`

Build with:

```powershell
.\build.ps1
```

Or:

```powershell
mingw32-make -f Makefile CC=gcc all
```

Start the GUI with:

```powershell
.\run-gui.ps1
```

The build produces:

```text
build/
├─ gmif_cli.exe
├─ mushroomfinder.dll
└─ GiantMushroomFinder.jar
```

## CLI Example

```text
gmif_cli --seed 262 --version 1.18 --radius 2000 --min-area 5000 --refine
```

Example output:

```text
Found: N

#1
  X: ...
  Z: ...
  Area: ... blocks² (estimated)
  Span: W x H
  Distance: ...
  Compactness: ...
```

## Validation

Run the built-in tests:

```powershell
.\build\gmif_cli.exe --self-test
.\build\gmif_cli.exe --audit
```

The audit covers:

* Monolithic vs. tiled search consistency
* Cross-tile region merging
* Refinement consistency
* Multi-thread consistency
* Result ordering

## Credits

### Cubiomes

Used for Minecraft Java Edition biome and world generation.

Cubiomes is distributed under the MIT License.

### SunnySlopes Projects

The following projects were used as architectural references:

* FortressFinderGUI
* RiverFinderGUI
* SlimeFinderGUI

They were used as references for Java/JNI/native search architecture and workflow design. No source code was copied.

### Cubiomes Viewer

Used as a reference for Minecraft biome visualization and seed-finding workflows.

See `NOTICE` for third-party attribution details.

## License

This project is licensed under the MIT License.

Third-party components and attribution information are documented in `NOTICE`.
