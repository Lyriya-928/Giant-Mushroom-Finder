# Giant Mushroom Island Finder

一个用于寻找 Minecraft Java Edition **蘑菇岛（Mushroom Fields）** 的独立 Java 桌面工具。

输入 Seed、Minecraft 版本和搜索半径，即可扫描指定范围，寻找大型蘑菇岛，并查看坐标、面积、边界和其他统计信息。

> **本项目是独立桌面程序，不是 Minecraft Mod。**

## 功能

* Minecraft Java Edition Seed 搜索
* Mushroom Fields 区域检测
* 可自定义搜索半径
* 按估算面积排序并显示 Top N
* 可选 16 → 4 精修
* 多线程原生搜索
* 支持大范围搜索的分块流式扫描
* 跨 Tile 区域自动合并
* 暂停 / 继续 / 停止
* 坐标复制
* CSV / JSON 导出
* 中英文界面
* CLI 与 Java Swing GUI

## 工作原理

搜索流程：

```text
Seed + Minecraft 版本
        ↓
Cubiomes 生物群系生成
        ↓
Mushroom Fields 采样
        ↓
4 邻域连通区域分析
        ↓
使用 DSU 合并跨 Tile 区域
        ↓
可选的 scale 4 精修
        ↓
计算面积 / 边界 / 距离 / 紧凑度
        ↓
输出 Top N
```

在大范围搜索时，程序使用分块流式扫描，而不是一次性为整个搜索范围分配内存。

每个 Tile 处理完成后即可释放其缓冲区。

跨越 Tile 边界的蘑菇岛会通过并查集进行全局合并。

因此，峰值内存主要由 Tile 大小决定，而不是整个搜索半径决定。

但是，搜索时间仍会随着搜索范围扩大而增加。

## 面积与精度

默认搜索使用较粗的采样尺度。

估算面积按照以下方式计算：

```text
面积 ≈ Mushroom Fields 采样点数量 × scale²
```

启用精修后，候选区域会使用稳定的 scale 4 采样重新计算面积和边界。

因此，程序显示的面积是**估算值**，不是精确的方块数量。

## Windows 构建

### 环境要求

* Windows
* JDK 17 或更高版本
* MinGW-w64 GCC
* OpenMP
* `make` / `mingw32-make`

使用以下命令构建：

```powershell
.\build.ps1
```

或者：

```powershell
mingw32-make -f Makefile CC=gcc all
```

启动 GUI：

```powershell
.\run-gui.ps1
```

构建完成后主要产物：

```text
build/
├─ gmif_cli.exe
├─ mushroomfinder.dll
└─ GiantMushroomFinder.jar
```

## CLI 示例

```text
gmif_cli --seed 262 --version 1.18 --radius 2000 --min-area 5000 --refine
```

示例输出：

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

## 验证

运行内置测试：

```powershell
.\build\gmif_cli.exe --self-test
.\build\gmif_cli.exe --audit
```

审计包括：

* 单体扫描与分块扫描一致性
* 跨 Tile 区域合并
* 精修结果一致性
* 多线程结果一致性
* 结果排序一致性

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
