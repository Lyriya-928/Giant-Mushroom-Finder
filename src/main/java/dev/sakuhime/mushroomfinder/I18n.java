package dev.sakuhime.mushroomfinder;

import java.util.Locale;

/** Simple bilingual UI strings (zh-CN / en). */
public final class I18n {

    public enum Lang {
        ZH, EN;

        public static Lang fromLocale() {
            Locale l = Locale.getDefault();
            String tag = l.getLanguage();
            return "zh".equals(tag) ? ZH : EN;
        }
    }

    private static Lang lang = Lang.fromLocale();

    private I18n() {}

    private static String t(String zh, String en) {
        return isZh() ? zh : en;
    }

    public static Lang get() {
        return lang;
    }

    public static void set(Lang l) {
        lang = (l == null) ? Lang.EN : l;
    }

    public static boolean isZh() {
        return lang == Lang.ZH;
    }

    public static String langDisplayName(Lang l) {
        return l == Lang.ZH ? "中文" : "English";
    }

    // ---- app ----
    public static String appTitle() {
        String base = t("大型群系查找器", "Large Biome Finder");
        return base + " v" + AppInfo.VERSION;
    }

    public static String appName() {
        return t("大型群系查找器", "Large Biome Finder");
    }

    public static String appSubtitle() {
        return t("寻找任意群系的大型连通区域 · Java 版种子搜索",
                "Find large connected regions of any biome in Java Edition seeds");
    }

    // ---- labels ----
    public static String seed() { return t("种子 Seed", "Seed"); }
    public static String version() { return t("版本", "Version"); }
    public static String center() { return t("搜索中心", "Search center"); }
    public static String centerX() { return "X"; }
    public static String centerZ() { return "Z"; }
    public static String radius() { return t("搜索半径 (格)", "Radius (blocks)"); }
    public static String radiusWithMax(int maxRadius) {
        return t("搜索半径 (最大 " + String.format(Locale.ROOT, "%,d", maxRadius) + ")",
                "Radius (max " + String.format(Locale.ROOT, "%,d", maxRadius) + ")");
    }
    public static String radiusUnit() { return t("格", "blocks"); }
    public static String errRadiusNativeLimit(int maxRadius, int scale, String raw) {
        return t("搜索半径超过当前构建的原生上限。\n"
                        + "scale " + scale + " 下最大半径：" + String.format(Locale.ROOT, "%,d", maxRadius)
                        + " 格（约 " + String.format(Locale.ROOT, "%,d", 10_000_000_000L) + " 个采样格）。\n"
                        + "当前输入：" + raw,
                "Search radius exceeds the native limit of this build.\n"
                        + "Maximum radius at scale " + scale + ": "
                        + String.format(Locale.ROOT, "%,d", maxRadius)
                        + " blocks (about " + String.format(Locale.ROOT, "%,d", 10_000_000_000L)
                        + " sample cells).\nYou entered: " + raw);
    }
    public static String tipRadiusMax(int maxRadius) {
        return t("半径 R 表示 [-R, R]。当前构建在 scale 16 下的最大半径是 "
                        + String.format(Locale.ROOT, "%,d", maxRadius) + " 格；更大的范围会先拒绝而不是耗尽内存。",
                "R means [-R, R]. This build supports up to "
                        + String.format(Locale.ROOT, "%,d", maxRadius)
                        + " blocks at scale 16; larger ranges are rejected before allocation.");
    }
    public static String targetBiome() { return t("目标群系", "Target biome"); }
    public static String targetMushroom() {
        return t("蘑菇岛 (14)", "Mushroom Fields (14)");
    }
    public static String targetMushroomShore() {
        return t("蘑菇岛 + 岸边 (14, 15)", "Mushroom Fields + Shore (14, 15)");
    }
    public static String targetCustom() { return t("自定义群系 ID…", "Custom biome IDs…"); }

    /** Label for one of the common biome presets in the target combo. */
    public static String biomePresetLabel(String id) {
        if (id == null) return targetMushroom();
        return switch (id) {
            case "mushroom" -> targetMushroom();
            case "mushroom_shore" -> targetMushroomShore();
            case "plains" -> t("平原 Plains", "Plains");
            case "forest" -> t("森林 Forest", "Forest");
            case "desert" -> t("沙漠 Desert", "Desert");
            case "jungle" -> t("丛林 Jungle", "Jungle");
            case "savanna" -> t("热带草原 Savanna", "Savanna");
            case "badlands" -> t("恶地 Badlands", "Badlands");
            case "taiga" -> t("针叶林 Taiga", "Taiga");
            case "swamp" -> t("沼泽 Swamp", "Swamp");
            case "ocean" -> t("海洋 Ocean", "Ocean");
            case "cherry_grove" -> t("樱花树林 Cherry Grove (1.20+)",
                    "Cherry Grove (1.20+)");
            case "custom" -> targetCustom();
            default -> id;
        };
    }
    public static String customBiomeIds() { return t("群系 ID", "Biome IDs"); }
    public static String customBiomeHint() {
        return t("用逗号分隔，范围 0–255，例如 14,15",
                "Comma separated, 0–255, e.g. 14,15");
    }
    public static String options() { return t("搜索选项", "Search options"); }
    public static String minArea() { return t("最小面积", "Min area"); }
    public static String minAreaOff() { return t("0 = 不限制", "0 = no limit"); }
    public static String maxResults() { return t("结果数量", "Results"); }
    public static String threads() { return t("线程数", "Threads"); }
    public static String language() { return t("语言", "Language"); }
    public static String refine() {
        return t("精修候选区域（更准确）", "Refine candidates (more accurate)");
    }
    public static String includeShore() {
        return t("计入蘑菇岛岸边群系", "Include mushroom shore biome");
    }
    public static String excludeLushCaves() {
        return t("排除下方有繁茂洞穴的岛（1.18+）",
                "Drop islands above lush caves (1.18+)");
    }
    public static String scanMode() { return t("扫描模式", "Scan mode"); }
    public static String scanAuto() { return t("自动", "Auto"); }
    public static String scanFull() { return t("整区域", "Full grid"); }
    public static String scanTiled() { return t("分块流式", "Tiled"); }
    public static String tileSize() { return t("分块大小", "Tile size"); }
    public static String tileAuto() { return t("自动", "Auto"); }
    public static String recent() { return t("最近搜索", "Recent searches"); }
    public static String recentEmpty() { return t("暂无记录", "No recent searches"); }
    public static String clearRecent() { return t("清除", "Clear"); }
    public static String results() { return t("搜索结果", "Results"); }
    public static String copyFormatLabel() { return t("复制格式", "Copy format"); }
    public static String basicSettings() { return t("搜索", "Search"); }
    public static String advancedSettings() { return t("选项", "Options"); }

    public static String radiusHint500() { return "500"; }
    public static String radiusHint1000() { return "1000"; }
    public static String radiusHint2000() { return "2000"; }
    public static String radiusHint5000() { return "5000"; }

    public static String tipRadiusPresets() {
        return t("半径 R 表示 [-R, R]，不是直径。常用 500 / 1000 / 2000 / 5000。",
                "R means [-R, R] on each axis, not a diameter. Common: 500 / 1000 / 2000 / 5000.");
    }

    // ---- tooltips ----
    public static String tipSeed() {
        return t("Minecraft Java 版世界种子（整数，可为负）",
                "Minecraft Java world seed (integer, may be negative)");
    }
    public static String tipVersion() {
        return t("生物群系生成算法版本；请与游戏实际版本一致",
                "Biome generation version; must match the game version");
    }
    public static String tipCenter() {
        return t("搜索区域中心的世界坐标。范围会以此点为中心向四周扩展。",
                "World coordinates of the search centre. The area expands around it.");
    }
    public static String tipRadius() {
        return t("以搜索中心为原点的半边长。1000 表示 2000×2000 的区域。",
                "Half-size around the search centre. 1000 means a 2000×2000 area.");
    }
    public static String tipMinArea() {
        return t("可选。0 = 不过滤，返回范围内面积最大的若干个区域。",
                "Optional. 0 = no filter; return the largest regions in range.");
    }
    public static String tipMaxResults() {
        return t("按面积从大到小保留的结果条数", "How many regions to keep, largest first");
    }
    public static String tipRefine() {
        return t("对粗采样候选再按 scale 4 重算面积与边界，更准但稍慢",
                "Recompute candidates at scale 4 for better area/bounds (slower)");
    }
    public static String tipShore() {
        return t("将 mushroom_field_shore 也视为区域的一部分",
                "Treat mushroom_field_shore as part of the region");
    }
    public static String tipLushCaves() {
        return t("对每个蘑菇岛按其覆盖范围向下检查 scale 4 的三维群系"
                        + "（y=-64..319，每 4 格采样）；只要下方出现繁茂洞穴 (175)"
                        + "就整岛丢弃，不会拆分或裁剪岛屿。需要 1.18+，且仅对目标包含"
                        + "蘑菇岛 (14) 的搜索生效。",
                "Checks the 3D biomes under each mushroom island at scale 4 "
                        + "(y=-64..319, every 4 blocks). An island is dropped as a "
                        + "whole when lush caves (175) appear below it; it is never "
                        + "split or trimmed. Requires 1.18+ and a target set that "
                        + "contains mushroom fields (14).");
    }
    public static String errLushNeeds118() {
        return t("「排除下方繁茂洞穴」需要 Minecraft 1.18 或更高版本。\n"
                        + "旧版本没有地下三维群系数据，无法进行该检查。\n"
                        + "请切换版本，或取消勾选该选项。",
                "Excluding islands above lush caves needs Minecraft 1.18 or newer.\n"
                        + "Underground 3D biome generation does not exist before 1.18.\n"
                        + "Switch the version or clear the option.");
    }
    public static String lushOnlyMushroom() {
        return t("该选项只对目标包含蘑菇岛 (14) 的搜索生效。",
                "This option only applies when the target set contains mushroom fields (14).");
    }
    public static String tipThreads() {
        return t("并行采样线程数。自动 = 物理核心数；不改变搜索结果。",
                "Parallel sampling threads. Auto = physical cores. Results stay identical.");
    }
    public static String tipScanMode() {
        return t("自动：普通范围用整区域扫描，超大范围自动切换为分块流式扫描。",
                "Auto uses the full grid for normal areas and switches to tiled streaming for very large ones.");
    }
    public static String tipTileSize() {
        return t("分块边长，单位是采样点而不是方块；256 在 scale 16 下覆盖 4096 格。",
                "Tile side in samples, not blocks; 256 covers 4096 blocks at scale 16.");
    }
    public static String tipBiome() {
        return t("默认搜索蘑菇岛。自定义模式可输入任意 cubiomes 群系 ID。",
                "Searches Mushroom Fields by default. Custom mode accepts any cubiomes biome id.");
    }
    public static String tipSearch() { return t("开始搜索", "Start search"); }
    public static String tipPause() { return t("暂停 / 继续当前搜索", "Pause / resume the current search"); }
    public static String tipStop() { return t("停止当前搜索", "Stop the current search"); }
    public static String tipCopy() {
        return t("复制选中结果的中心坐标（未选中则复制第一条）",
                "Copy the selected row's centre coordinates (or the first result)");
    }
    public static String tipExportCsv() { return t("将结果表导出为 CSV 文件", "Export results to a CSV file"); }
    public static String tipExportJson() { return t("将结果导出为 JSON 文件", "Export results to a JSON file"); }
    public static String tipCopyFormat() {
        return t("复制到剪贴板时使用的坐标格式", "Clipboard format for copied coordinates");
    }
    public static String tipLanguage() { return t("切换界面语言", "Switch UI language"); }
    public static String tipTable() {
        return t("点击表头排序；双击行可复制该岛中心坐标。面积为采样估算值。",
                "Click a header to sort; double-click a row to copy its centre. Area is a sample estimate.");
    }
    public static String tipRecent() {
        return t("选择一条最近搜索以恢复种子、版本、中心和范围。",
                "Pick a recent search to restore its seed, version, centre and radius.");
    }

    // ---- buttons ----
    public static String search() { return t("开始搜索", "Search"); }
    public static String pause() { return t("暂停", "Pause"); }
    public static String resume() { return t("继续", "Resume"); }
    public static String stop() { return t("停止", "Stop"); }
    public static String copyCoords() { return t("复制坐标", "Copy coords"); }
    public static String exportCsv() { return t("导出 CSV", "Export CSV"); }
    public static String exportJson() { return t("导出 JSON", "Export JSON"); }

    // ---- copy formats ----
    public static String fmtPlain() { return "X Z"; }
    public static String fmtMcTp() { return "/tp @s X ~ Z"; }
    public static String fmtLabeled() { return "X: x  Z: z"; }

    public static String[] copyFormats() {
        return new String[] { fmtPlain(), fmtMcTp(), fmtLabeled() };
    }

    // ---- table columns ----
    public static String[] tableColumns() {
        if (isZh()) {
            return new String[] {
                    "#", "中心 X", "中心 Z", "面积", "≈ km²",
                    "跨度", "距离", "紧凑度", "精度"
            };
        }
        return new String[] {
                "#", "X", "Z", "Area", "≈ km²",
                "Span", "Dist.", "Compact", "Accuracy"
        };
    }

    // ---- status ----
    public static String ready() { return t("就绪。", "Ready."); }
    public static String searching() { return t("搜索中…", "Searching…"); }
    public static String initializing() { return t("正在初始化搜索…", "Initializing search…"); }
    public static String startingThreads(int n) {
        return t("正在启动 " + n + " 个采样线程…",
                "Starting " + n + " sampling thread(s)…");
    }
    public static String scanStarted() { return t("扫描中…", "Scanning…"); }
    public static String stopping() { return t("正在停止…", "Stopping…"); }
    public static String paused() { return t("已暂停", "Paused"); }
    public static String stopped() { return t("搜索已停止。", "Search stopped."); }
    public static String doneFound(int n) {
        return t("完成。找到 " + n + " 个区域。", "Done. Found " + n + " region(s).");
    }
    public static String doneFoundTimed(int n, String time) {
        return t("完成。找到 " + n + " 个区域，用时 " + time + "。",
                "Done. Found " + n + " region(s) in " + time + ".");
    }
    public static String resultsFound(int n) {
        return t(n + " 条结果", n + " result(s)");
    }
    public static String resultsEmpty() {
        return t("还没有结果。设置种子与范围后开始搜索。",
                "No results yet. Set a seed and area, then start a search.");
    }
    public static String resultsNone() {
        return t("此范围内没有符合条件的区域。可以增大范围或降低最小面积。",
                "No regions matched. Try a larger radius or a lower minimum area.");
    }
    public static String searchingBox(String box) {
        return t("搜索中 · " + box, "Searching · " + box);
    }
    public static String progressText(int percent, int found, String stage) {
        return t(stage + " — 已找到 " + found, stage + " — found " + found);
    }
    public static String elapsed(String time) { return t("已用 " + time, "Elapsed " + time); }
    public static String eta(String time) { return t("预计剩余 " + time, "ETA " + time); }
    public static String elapsedEta(String elapsed, String eta) {
        return t("已用 " + elapsed + " · 预计剩余 " + eta,
                "Elapsed " + elapsed + " · ETA " + eta);
    }
    public static String elapsedOnly(String elapsed) {
        return t("已用 " + elapsed, "Elapsed " + elapsed);
    }
    public static String searchingPercent(int pct) {
        return t("搜索中… " + pct + "%", "Searching… " + pct + "%");
    }

    // ---- plan preview ----
    public static String planMonolithic() { return t("整区域扫描", "Full grid"); }
    public static String planTiled() { return t("分块流式", "Tiled"); }
    public static String planNone() { return t("等待有效参数", "Waiting for valid input"); }
    public static String planSummary(String mode, String grid, double mb, long samples) {
        if (isZh()) {
            return mode + " · " + grid + " · " + String.format(Locale.ROOT, "%,d", samples)
                    + " 采样 · 约 " + String.format(Locale.ROOT, "%.1f", mb) + " MB";
        }
        return mode + " · " + grid + " · " + String.format(Locale.ROOT, "%,d", samples)
                + " samples · " + String.format(Locale.ROOT, "%.1f", mb) + " MB";
    }
    public static String planInvalid(String reason) {
        return t("参数无效：" + reason, "Invalid input: " + reason);
    }
    public static String planWillStream() {
        return t("范围较大，将使用分块流式扫描以控制内存占用。",
                "Large area: tiled streaming will keep memory bounded.");
    }
    public static String planWillRefine() {
        return t("精修已开启：候选区域会按 scale 4 重算，耗时略增。",
                "Refine is on: candidates are recomputed at scale 4 (a little slower).");
    }

    // ---- errors / dialogs ----
    public static String errRadiusRange(int min, int max, String raw) {
        return t("搜索半径需在 " + min + "–" + max + " 格之间。\n当前输入：" + raw,
                "Search radius must be between " + min + " and " + max + " blocks.\nYou entered: " + raw);
    }
    public static String errRadiusInteger(String raw) {
        return t("搜索半径必须是整数。\n当前输入：" + raw,
                "Search radius must be an integer.\nYou entered: " + raw);
    }
    public static String errRadiusTooSmall(int min, String raw) {
        return t("搜索半径不能小于 " + min + " 格。\n当前输入：" + raw,
                "Search radius must be at least " + min + " blocks.\nYou entered: " + raw);
    }
    public static String errRadiusWorldBorder(int max, String raw) {
        return t("搜索半径超过 Minecraft 世界边界。\n最大半径：" + max
                        + " 格（世界边界 ±" + max + "）。\n当前输入：" + raw,
                "Search radius exceeds the Minecraft world border.\nMax radius: "
                        + max + " blocks (border ±" + max + ").\nYou entered: " + raw);
    }
    public static String errCenterInteger(String raw) {
        return t("搜索中心坐标必须是整数。\n当前输入：" + raw,
                "Search centre coordinates must be integers.\nYou entered: " + raw);
    }
    public static String errCenterBorder(int max, String raw) {
        return t("搜索中心超出世界边界（±" + max + "）。\n当前输入：" + raw,
                "Search centre is outside the world border (±" + max + ").\nYou entered: " + raw);
    }
    public static String errBoxBorder(int radius, int cx, int cz) {
        return t("搜索区域超出世界边界。\n中心 (" + cx + ", " + cz + ")，半径 " + radius
                        + "。请减小半径或移动中心。",
                "The search box exceeds the world border.\nCentre (" + cx + ", " + cz
                        + "), radius " + radius + ". Reduce the radius or move the centre.");
    }
    public static String errBiomes(String raw) {
        return t("群系 ID 无效。请输入 0–255 之间的整数，用逗号分隔。\n当前输入：" + raw,
                "Invalid biome ids. Enter integers 0–255 separated by commas.\nYou entered: " + raw);
    }
    public static String errThreads(String raw) {
        return t("线程数无效。请输入 1–256 的整数，或选择自动。\n当前输入：" + raw,
                "Invalid thread count. Enter 1–256 or choose Auto.\nYou entered: " + raw);
    }
    public static String errTileSize(String raw) {
        return t("分块大小无效。请输入 64–2048 的整数，或选择自动。\n当前输入：" + raw,
                "Invalid tile size. Enter 64–2048 or choose Auto.\nYou entered: " + raw);
    }
    public static String warnLargeRadius(int radius) {
        return t("搜索范围很大（半径 " + radius + " 格），可能需要较长时间。",
                "Large search range (radius " + radius + "); this may take a while.");
    }
    public static String warnHugeRadius(long samples) {
        return t("范围非常大，粗采样点约 " + String.format("%,d", samples)
                        + " 个，搜索可能需要很长时间。",
                "Very large range — about " + String.format("%,d", samples)
                        + " coarse samples. This may take a very long time.");
    }
    public static String confirmLargeTitle() { return t("确认开始搜索", "Confirm search"); }
    public static String confirmLargeBody(String plan, String warning) {
        return t("即将开始一次较大的搜索：\n\n" + plan + "\n\n" + warning
                        + "\n\n可以随时暂停或停止。是否继续？",
                "You are about to start a large search:\n\n" + plan + "\n\n" + warning
                        + "\n\nYou can pause or stop at any time. Continue?");
    }
    public static String searchBoxLabel(int radius) {
        return searchBoxLabel(0, 0, radius);
    }
    public static String searchBoxLabel(int cx, int cz, int radius) {
        String c = "(" + String.format("%,d", cx) + ", " + String.format("%,d", cz) + ")";
        if (isZh()) {
            return "中心 " + c + " · 范围 ±" + String.format("%,d", radius);
        }
        return "centre " + c + " · ±" + String.format("%,d", radius) + " blocks";
    }
    public static String coarseSamples(long n) {
        return t("粗采样点约 " + String.format("%,d", n),
                "≈ " + String.format("%,d", n) + " coarse samples");
    }
    public static String threadsEditableTip() { return tipThreads(); }
    public static String tileSizeHelp() { return tipTileSize(); }

    public static String errNative(int code, String nativeMsg) {
        if (isZh()) {
            if (code == NativeBridge.ERR_GRID_TOO_LARGE) {
                return "搜索范围过大，当前配置无法在合理内存内完成。\n\n"
                        + (nativeMsg == null ? "" : nativeMsg + "\n\n")
                        + "建议：减小半径、移动搜索中心，或改用更大的采样 scale。\n"
                        + "分块模式仍然无法完成时，程序会明确拒绝而不是耗尽内存。";
            }
            if (code == NativeBridge.ERR_ALLOC) {
                return "内存分配失败。\n\n"
                        + (nativeMsg == null ? "" : nativeMsg + "\n\n")
                        + "请减小搜索半径，或关闭其他占用内存的程序后重试。";
            }
            if (code == NativeBridge.ERR_OVERFLOW) {
                return "坐标/网格计算溢出。\n\n" + (nativeMsg == null ? "" : nativeMsg);
            }
            if (code == NativeBridge.ERR_VERSION) {
                return "Minecraft 版本不受支持。\n\n" + (nativeMsg == null ? "" : nativeMsg);
            }
            if (code == NativeBridge.ERR_BIOME_GEN) {
                return "生物群系采样失败。\n\n" + (nativeMsg == null ? "" : nativeMsg);
            }
            if (code == NativeBridge.ERR_CANCELLED) {
                return "搜索已取消。";
            }
            return nativeMsg != null && !nativeMsg.isBlank()
                    ? nativeMsg
                    : ("搜索失败（代码 " + code + "）");
        }
        if (code == NativeBridge.ERR_GRID_TOO_LARGE) {
            return "Search area is too large for this configuration.\n\n"
                    + (nativeMsg == null ? "" : nativeMsg + "\n\n")
                    + "Reduce the radius or use a coarser sample scale.";
        }
        return nativeMsg != null && !nativeMsg.isBlank()
                ? ("[code " + code + "]\n" + nativeMsg)
                : ("Search failed (code " + code + ")");
    }

    public static String stageScan() { return t("扫描", "scan"); }
    public static String stageTile() { return t("分块扫描", "tile scan"); }
    public static String stageMerge() { return t("区域合并", "merge"); }
    public static String stageConnect() { return t("连通分析", "connect"); }
    public static String stageRefine() { return t("精修", "refine"); }
    public static String stageDone() { return t("完成", "done"); }
    public static String stageInit() { return t("初始化", "init"); }

    public static String progressLine(int percent, int found, String stage) {
        return t(stage + " · " + Math.max(0, percent) + "% · 已找到 " + Math.max(0, found),
                stage + " · " + Math.max(0, percent) + "% · found " + Math.max(0, found));
    }

    public static String nativeMissing(String err) {
        return t("原生库未加载：" + err, "Native library not loaded: " + err);
    }
    public static String invalidSeed(String s) {
        return t("无效种子：" + s, "Invalid seed: " + s);
    }
    public static String errTitle() { return t("错误", "Error"); }
    public static String inputErrorTitle() { return t("输入错误", "Input error"); }
    public static String exportTitle() { return t("导出", "Export"); }
    public static String nothingToExport() { return t("没有可导出的结果。", "No results to export."); }
    public static String exportOk(String path) { return t("已导出：" + path, "Exported: " + path); }
    public static String exportFailedTitle() { return t("导出失败", "Export failed"); }
    public static String copied(String text) { return t("已复制：" + text, "Copied: " + text); }
    public static String nothingToCopy() { return t("没有可复制的结果。", "Nothing to copy."); }
    public static String auto() { return t("自动", "Auto"); }
    public static String autoWith(int n) { return t("自动 (" + n + ")", "Auto (" + n + ")"); }
    public static String yes() { return t("是", "Yes"); }
    public static String no() { return t("否", "No"); }
}
