# Development Notes — Giant Mushroom Island Finder

## 本地环境（本机已验证）

| 工具 | 路径 / 版本 |
|---|---|
| JDK | `C:\Program Files\Java\jdk-26.0.1`（Java 26） |
| GCC | WinLibs MinGW-w64 UCRT（winget `BrechtSanders.WinLibs.POSIX.UCRT`） |
| CMake | 4.4.3（winget） |
| make | `mingw32-make` |
| Cubiomes | `cubiomes/`（MIT，shallow clone） |
| 代理 | GitHub 走 `http://127.0.0.1:7890` |

PowerShell 刷新 PATH：

```powershell
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path','User')
```

## 关键实现决策

### Range 坐标

Cubiomes `Range.x/z` 是**缩放坐标**，不是世界 block 坐标。

| scale | r.x 含义 | 含义 |
|---|---|---|
| 1 | world blocks | 1:1 |
| 4 | biome coords | 世界 = r.x * 4 |
| 16 | chunk-like | 世界 = r.x * 16 |

扫描时 `r.x = -half`，`world_x0 = -half * scale`。

### scale=1 缓冲

`allocCache()` 对 1.18+ 的 scale=1 会额外分配 voronoi 源缓冲。
不要用裸 `malloc(sx*sz)` 代替。

### 分带扫描

scale>1 时按 Z 分带调用 `genBiomes`，便于检查 pause/stop。
scale=1 必须整块生成（voronoi 源依赖完整范围）。

### Pause / Stop

`GmifControl` 为跨线程原子标志：

- Java UI 线程调用 `nativePause/Resume/Stop`
- 搜索线程在扫描/连通/精修循环中 `should_stop()` 检查
- pause 时在 native 侧 sleep 轮询，不占用 Java 线程池

### JNI 进度

不使用 JNI 回调（避免重入）。
进度写入 `GmifControl.percent/found/stage`，
Java `javax.swing.Timer` 每 150ms 调用 `nativePollProgress`。

## 验证结果（更新 2026-09-17 P2.5–P3.5）

### 回归自测

```powershell
.\build\gmif_cli.exe --self-test
```

全部 PASS：版本解析、seed262 原点 biome、scale4 vs scale16+refine 面积/中心一致、
中心点 getBiomeAt 交叉校验、radius 为半径、负坐标路径、ASCII map。

### ASCII 交叉验证（seed 262 / 1.18 / r=500）

主岛 bounds `[-160,-200]..[99,211]`，center `(-32,4)`，area≈74480。
ASCII map 显示连续蘑菇区域，与 Cubiomes `getBiomeAt` 一致。

### 多线程 Benchmark（seed 262 / 1.18 / r=3000 / scale=16）

| Threads | Time | Found | Top area |
|--------:|-----:|------:|---------:|
| 1 | 0.400 s | 4 | 74240 |
| 2 | 0.213 s | 4 | 74240 |
| 4 | 0.160 s | 4 | 74240 |
| 8 | 0.092 s | 4 | 74240 |

多线程**不改变**结果（Found / Top area 一致）。OpenMP 并行仅用于 Z 分带采样；CCA 仍单线程。

### 导出

```powershell
.\build\gmif_cli.exe --seed 262 --version 1.18 --radius 500 --min-area 1000 `
  --refine --csv out.csv --json out.json --threads 4
```

GUI 提供 Export CSV / Export JSON 与 Copy coordinates。

### GUI 使用模式（当前）

**基本设置**
- Seed / Version
- 地图半径 Search Radius（预设 500 / 1000 / 2000 / 5000，可自定义）
- 结果数量 Results（默认 10，按面积从大到小）

**高级筛选（可选）**
- 最小面积 Min area（默认 0 = 不过滤）
- Refine / Shore / Threads / 语言 / 复制格式

半径 R 表示 **[-R, R]**，不是直径。未设 Min area 时返回范围内面积最大的 N 座岛。
面积同时显示 `blocks²` 与 `≈ km²`（1 block² = 1 m²）。

**不做**：自动扩大半径找岛；Multi-Seed Finder。

```powershell
.\build\gmif_cli.exe --seed 262 --version 1.18 --radius 1000 --max-results 10 --refine
```

## 下一步

- [ ] 地图预览（复用现有 mask/region，不重新世界生成）
- [ ] 通用 BiomeSearchCondition（蘑菇岛流程稳定后）

## 目录结构

```text
Giant Mushroom Island Finder/
├── native/
│   ├── mushroom_finder.h      # 公共 API + GmifControl
│   ├── mushroom_finder.c      # 扫描 / CCA / 精修 / 指标 / dump / OpenMP
│   ├── mushroom_finder_JNI.c  # JNI 导出
│   └── mushroom_cli.c         # CLI + self-test + dump + csv/json + benchmark
├── src/main/java/dev/sakuhime/mushroomfinder/
│   ├── Main.java
│   ├── MainFrame.java         # GUI + export + threads
│   ├── NativeBridge.java
│   ├── NativeLoader.java
│   ├── SearchRunner.java
│   ├── SearchSettings.java
│   ├── SearchResult.java
│   └── SelfTest.java
├── cubiomes/
├── build/
├── Makefile                   # OpenMP 已启用
├── build.ps1
├── run-gui.ps1
├── README.md
└── DEVELOPMENT.md
```
