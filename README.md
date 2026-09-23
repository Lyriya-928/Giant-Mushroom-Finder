# Giant Mushroom Island Finder (GMIF)

**Version: 1.0.0**

面向 **Minecraft Java Edition** 的巨大蘑菇岛（Mushroom Fields）Seed Finder。

独立 Java 桌面程序，不是 Minecraft Mod。

输入 Seed 和版本，扫描指定范围内的 Mushroom Fields，按面积、跨度、紧凑度等指标找出大型蘑菇岛。

---

## 功能（当前阶段）

- 输入 Seed
- 选择 Minecraft Java Edition 版本
- 设置搜索半径（半边长，单位方块）
- 设置最小蘑菇岛面积（估算值）
- 可选：候选区域精修（scale 16 → 4）
- 采样线程数（1/2/4/8/自动）
- 中英文界面切换
- 各参数悬停说明
- 开始 / 暂停 / 继续 / 停止搜索
- 查看进度与结果（中心坐标、面积、跨度、对角线、距离、紧凑度）
- 复制坐标（`X Z` / `/tp` / 标签格式；双击行）
- 导出 CSV / JSON

**暂不包含 Multi-Seed Finder**（已从近期计划移除）。地图预览计划复用现有 mask/region 数据，不重新生成世界。

---

## 架构

```text
Java Swing GUI
      │
      ▼
JNI Bridge
      │
      ▼
Native search core (C)
      │
      ▼
Cubiomes (biome generation)
```

| 层 | 职责 |
|---|---|
| Java GUI | 参数输入、任务生命周期、结果展示 |
| JNI | 参数传递、pause/resume/stop、结果回传 |
| Native | 粗采样扫描、连通区域分析、面积/跨度/紧凑度 |
| Cubiomes | Minecraft biome / world generation |

---

## 构建（Windows）

依赖：

- MinGW-w64 GCC
- JDK 17+
- make（mingw32-make）

```powershell
# 1) 环境（PowerShell）
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path','User')
$env:JAVA_HOME = "C:\Program Files\Java\jdk-26.0.1"

# 2) 构建 CLI + DLL + JAR
.\build.ps1

# 3) 无头自测
.\build\gmif_cli.exe --seed 262 --version 1.18 --radius 500 --min-area 1000 --refine
java '-cp' 'build\classes' '-Djava.library.path=build' `
     'dev.sakuhime.mushroomfinder.Main' '--self-test'

# 4) 启动 GUI
.\run-gui.ps1
```

或使用 Makefile：

```powershell
mingw32-make -f Makefile CC=gcc all
mingw32-make -f Makefile CC=gcc native
mingw32-make -f Makefile CC=gcc jar
```

产物：

| 文件 | 说明 |
|---|---|
| `build/gmif_cli.exe` | 命令行验证工具 |
| `build/mushroomfinder.dll` | JNI 原生库 |
| `build/GiantMushroomFinder.jar` | GUI 程序 |

---

## CLI 示例

```text
gmif_cli --seed 262 --version 1.18 --radius 2000 --min-area 5000 --scale 16 --refine

Giant Mushroom Island Finder

Seed: 262
Version: 1.18
Radius: 2000
...

Found: N

#1
  X: ...
  Z: ...
  Area: ... blocks² (estimated)
  Span: W x H
  Distance: ...
  Compactness: ...
```

---

## 搜索说明

1. **粗扫描**：按 `scale`（默认 16 blocks）对 ±radius 区域采样 Mushroom Fields。
2. **连通区域**：4-neighbor Connected Component Analysis，分离独立岛屿。
3. **分块流式（Tiled）**：大范围自动按 Tile 扫描并 **DSU 合并**跨边界 Region；Tile 缓冲用完即释放。
4. **精修（可选）**：对候选 bbox 用 scale 4 重算面积与边界。
5. **指标**：
   - `area` = 正样本数 × scale²（估算值）
   - `width/height` = 包围盒跨度
   - `distance` = 中心到 (0,0) 的欧氏距离
   - `compactness` ≈ 4πA / P²
6. **排序**：按面积降序（平局按 min_x/min_z，保证 mono/tiled 顺序一致）。

面积是基于采样的估算，不是精确 block 计数。

### Tile Size 单位

**Tile Size = samples（采样点），不是方块。**

```text
tile-size = 256  →  256 × 256 samples
scale = 16       →  每边覆盖 256 × 16 = 4096 格方块
```

### 内存与时间

> **Tiled mode removes the whole-map memory requirement, but search time still
> grows approximately with the searched area.**

单个 Tile 内存由 `Tile Size` 决定，与 Radius 无关；总耗时仍近似正比于搜索面积。

`scale > 4` 时内部使用稳定的 scale=4 采样再降采样，以保证 Monolithic 与 Tiled 结果一致
（Cubiomes 在 scale>4 的快速路径与 Range 布局相关，不能用于精确等价）。

## 验证

```powershell
.\build\gmif_cli.exe --self-test
.\build\gmif_cli.exe --audit          # Tiled vs Mono / 跨 Tile / 线程一致性
```

---

## Credits

This project uses and/or references the following open-source projects:

### Cubiomes

https://github.com/Cubitect/cubiomes

MIT License. Used for Minecraft Java Edition biome and world generation.

### FortressFinderGUI

https://github.com/SunnySlopes/FortressFinderGUI

Used as a reference for the Java + JNI + native search architecture,
including search lifecycle and native worker integration.
No source code copied.

### RiverFinderGUI

https://github.com/SunnySlopes/RiverFinderGUI

Used as a reference for biome-region search architecture.
No source code copied.

### SlimeFinderGUI

https://github.com/SunnySlopes/SlimeFinderGUI

Used as a reference for Java/native search organization.
No source code copied.

### Cubiomes Viewer

https://github.com/Cubitect/cubiomes-viewer

Used as a reference for Minecraft biome visualization and
seed-finding workflows. Recommended for manual cross-checking.

---

## License

本项目代码采用 MIT License（见 `LICENSE`）。

第三方组件见 `NOTICE`：

- **Cubiomes**（MIT，`cubiomes/`，未修改）
- SunnySlopes / Cubitect 项目仅作架构参考，**无源码复制**

## Credits

This project uses and/or references the following open-source projects:

### Cubiomes

https://github.com/Cubitect/cubiomes

MIT License. Used for Minecraft Java Edition biome and world generation
(vendored in `cubiomes/`, unmodified).

### FortressFinderGUI / RiverFinderGUI / SlimeFinderGUI / Cubiomes Viewer

See `NOTICE` for reference-only attribution (architecture inspiration,
no source code copied).
