# 大型群系查找器（Large Biome Finder）

[English](README.md) | 简体中文

一个用于寻找 Minecraft Java Edition **任意群系大型连通区域** 的独立 Java 桌面工具。

仓库地址：<https://github.com/Lyriya-928/Large-Biome-Finder>

输入 Seed、Minecraft 版本、搜索中心与半径，选择常用群系或输入自定义
Cubiomes 群系 ID，即可查找面积最大的匹配区域，并查看坐标、估算面积、边界、
距离与紧凑度。搜索区域为矩形；本项目不是 Minecraft Mod。

> **本项目是独立桌面程序，不是 Minecraft Mod。**

## 功能

* Minecraft Java Edition Seed 搜索
* 任意群系 ID（0–255）的大型连通区域检测
* 常用群系预设：蘑菇岛、蘑菇岛+岸边、平原、森林、沙漠、丛林、热带草原、
  恶地、针叶林、沼泽、海洋、樱花树林（1.20+）
* 自定义群系 ID（逗号分隔、自动去重、范围校验）
* 可自定义搜索中心 X/Z 与半径
* 界面直接显示原生实际最大半径（scale 16 下 799,984 格）
* 按估算面积排序并显示 Top N
* 可选 16 → 4 精修
* 可选 **排除下方繁茂洞穴**（1.18+）：岛屿下方出现繁茂洞穴 (175) 时整岛丢弃
* 原生多线程采样（自动使用物理核心，可手动指定）
* 自动选择整区域扫描或分块流式扫描
* 按行流式合并跨 Tile 区域，内存有界
* 暂停 / 继续 / 停止，显示真实进度与剩余时间
* 最近搜索、设置持久化
* 坐标复制
* CSV / JSON 导出
* 中英文界面
* 轻量 Fluent 风格 Swing 界面
* CLI 与 Java Swing GUI

性能瓶颈、实测数据、线程扩展性、扫描阈值与上限分析详见
[PERFORMANCE.md](PERFORMANCE.md)。

## 工作原理

```text
Seed + 版本 + 搜索中心 + 目标群系
        ↓
Cubiomes 生物群系生成
        ↓
目标群系采样
        ↓
4 邻域连通区域分析
        ↓
按行流式合并跨 Tile 区域（DSU）
        ↓
可选的 scale 4 精修
        ↓
计算面积 / 边界 / 距离 / 紧凑度
        ↓
输出 Top N
```

在 MC 1.18+ 且 scale > 4 时，程序直接采样每个粗网格中心。这与旧版
“稳定 scale-4 降采样”保留的是同一个值，所以结果不变，但少生成 16 倍样本。
掩码填充按行带用 OpenMP 在多核上并行。

普通范围使用整区域扫描。超过 12,000,000 个粗采样格后自动切换为分块流式扫描。
合并时只保留当前 Tile 行和上一行的底部接触点，因此峰值内存由 Tile 大小和
网格宽度决定，而不是总 Tile 数或总半径（实测 6–30 MB）。搜索开始前，界面会
显示实际模式、预估峰值内存和原生最大半径。

### 排除下方繁茂洞穴（1.18+，仅蘑菇岛目标）

勾选“排除下方有繁茂洞穴的岛”后，每个蘑菇岛候选在输出前都会再做一次地下检查：
按 scale 4 重新生成岛屿覆盖范围的掩码，只对属于该岛的列向下采样完整的
地下高度（y = -64..319，每 4 格一个样本，正好是 1.18+ 群系噪声的垂直分辨率）。
只要任意一列出现繁茂洞穴（id 175），就**整岛丢弃**——不会拆分区域，也不会
裁剪结果。检查按行带流式处理（不会分配巨大的三维数组），支持暂停/停止，
并复用同一组工作线程。

该选项默认关闭，普通搜索完全不受影响；搜索其它群系时该选项不生效；1.18 以前
的版本没有地下三维群系数据，程序会明确提示不支持并拒绝，而不是用二维数据冒充。

搜索时间仍会随范围增大而增加。`线程数 = 自动` 使用物理核心数（在
6P+4E 的 i5-13490F 上为 10）；也可以手动填 1–256，结果完全一致。

## 搜索上限

* 整区域扫描：≤ 12,000,000 个粗采样格（预计约 60 MB 峰值）。
* 分块流式扫描：≤ 10,000,000,000 个粗采样格；自动增大 Tile 后 Tile 数
  仍须 ≤ 2,000,000。
* scale 16 下即 **最大半径 799,984 格**。GUI 从原生库读取该值并显示在半径
  标签中；输入更大值会得到明确提示并被拒绝。最大范围搜索可能持续数小时，
  程序会在开始前二次确认。
* 整数安全：采样格数与面积使用 `int64_t`；采样坐标和网格尺寸会检查 `int32`
  边界；Tile/区域数量有显式上限；超限请求在分配内存之前就被拒绝。
* MC 1.17 及更早版本仍需要完整 scale-4 图层窗口（没有 16× 捷径），但同样
  使用多线程和内存有界的流式合并。
* 排除繁茂洞穴仅支持 1.18+ 且目标包含蘑菇岛 (14)。它会在表面扫描之外再采样
  一遍地下列：参考机器上 seed 262、半径 10000 时，开启后从 0.84 s 增至
  1.50 s（10 线程）。

## 面积与精度

估算面积按照以下方式计算：

```text
面积 ≈ 目标群系采样点数量 × scale²
```

启用精修后，候选区域会使用稳定的 scale 4 采样重新计算面积和边界。
程序显示的面积是**估算值**，不是精确的方块数量。精修只改变统计值，
不改变“哪些连通区域被找到”。

## 下载

Windows 预编译版本发布在
[Releases](https://github.com/Lyriya-928/Large-Biome-Finder/releases) 页面：

* `LargeBiomeFinder-<version>-windows-x64.zip` — JAR、CLI、DLL 及文档
* `LargeBiomeFinder-<version>.jar` — 内置原生 DLL 的 GUI / CLI JAR
* `gmif_cli.exe` — 独立原生命令行工具
* `gmif.dll` — 供其他程序调用的 JNI 库
* `SHA256SUMS.txt` — 所有产物的校验和

```powershell
java -jar LargeBiomeFinder-1.2.0.jar
```

JAR 首次运行时会自动释放内置 DLL，无需额外安装。从源码构建见下文。

## Windows 构建

### 环境要求

* Windows
* JDK 17 或更高版本
* MinGW-w64 GCC（带 OpenMP）
* `make` / `mingw32-make`
* PowerShell

一键构建、打包并冒烟测试：

```powershell
.\build.ps1
```

也可以只构建原生 CLI：

```powershell
mingw32-make -f Makefile CC=gcc all
```

启动 GUI：

```powershell
.\run-gui.ps1
```

构建产物：

```text
build/
├─ gmif_cli.exe
├─ gmif.dll          （同时嵌入 JAR）
└─ LargeBiomeFinder-1.2.0.jar
```

## CLI 示例

```text
gmif_cli --seed 262 --version 1.18 --radius 2000 --min-area 5000 --refine
gmif_cli --seed 262 --version 1.18 --radius 2000 --biomes 1
gmif_cli --seed 262 --version 1.18 --center-x -1000 --center-z -1000 --radius 1200
gmif_cli --seed 262 --version 1.18 --radius 600 --biomes 46
gmif_cli --seed 262 --version 1.18 --radius 5000 --describe
gmif_cli --seed 262 --version 1.18 --radius 5000 --benchmark
gmif_cli --seed 262 --version 1.18 --radius 5000 --exclude-lush-caves --refine
gmif_cli --seed 262 --version 1.18 --radius 5000 --lush-report
gmif_cli --max-radius
```

`--threads 0` 表示自动（物理核心）。`--tiles` / `--no-tiles` 强制扫描模式；
`--tile-size` 的单位是采样点，不是方块。`--max-radius` 输出 scale 16 下的
原生最大半径。

常用群系 ID：`14` 蘑菇岛、`15` 蘑菇岛岸边、`1` 平原、`4` 森林、`2` 沙漠、
`21` 丛林、`35` 热带草原、`37` 恶地、`5` 针叶林、`6` 沼泽、`0` 海洋、
`185` 樱花树林（1.20+）。

## 验证

```powershell
.\build\gmif_cli.exe --audit
.\build\gmif_cli.exe --self-test
java --enable-native-access=ALL-UNNAMED -jar .\build\LargeBiomeFinder-1.2.0.jar --self-test
```

审计包括：

* 快速中心采样与旧版稳定 scale-4 掩码逐格一致性
* 整区域扫描与分块扫描一致性（奇数 Tile、负坐标中心）
* 海洋/平原等大范围目标跨多行 Tile、完整 Tile 边界的流式合并
* 跨 Tile 区域合并、面积不重复计数
* 精修结果一致性
* 两种扫描模式下 1…16 线程结果一致性
* 自定义搜索中心的范围与距离
* 常用/自定义群系目标，以及 `include_shore` 与 `{14,15}` 的等价性
* 自动模式切换阈值
* 原生最大半径可规划、最大半径 +1 被拒绝
* 全局合并后的 Top-N 排序
* 排除繁茂洞穴：干净岛屿原样保留、含繁茂洞穴的岛屿整岛丢弃、分块/精修/线程
  变体结果一致、其它群系目标不受影响、1.17 及更早版本被明确拒绝

GitHub Actions 在 Windows 运行器上对推送和 PR 执行相同的构建、原生 audit
与 Java self-test。完整实测数据与上限依据见 [PERFORMANCE.md](PERFORMANCE.md)。

## 发布

推送 `v*` 标签即触发 [Release 工作流](.github/workflows/release.yml)：
在 Windows 运行器上重新构建、跑原生 audit 与 Java self-test，然后把 JAR、
CLI、DLL、压缩包和 `SHA256SUMS.txt` 发布到 GitHub Releases。

```powershell
# 1. 修改 VERSION（例如 1.2.0），提交并合入 main
# 2. 打标签并推送
git tag -a v1.2.0 -m "Large Biome Finder 1.2.0"
git push origin v1.2.0
```

标签必须与 `VERSION` 文件一致，否则工作流在打包阶段直接失败，不会发布
版本不匹配的产物。本地可用 `.\tools\package-release.ps1 -Tag v1.2.0`
预演打包（产物在 `build\release\`）。

## 致谢

### Cubiomes

用于 Minecraft Java Edition 生物群系和世界生成。

Cubiomes 使用 MIT License。

### SunnySlopes 项目

以下项目作为架构设计参考：

* FortressFinderGUI
* RiverFinderGUI
* SlimeFinderGUI

主要参考 Java / JNI / Native 搜索架构和任务流程设计，未复制其源代码。

### Cubiomes Viewer

用于参考 Minecraft 生物群系可视化和 Seed 搜索工作流。

第三方项目的具体版权和致谢信息请参阅 `NOTICE`。

## 许可证

本项目采用 MIT License。

第三方组件及其致谢信息记录在 `NOTICE` 中。
