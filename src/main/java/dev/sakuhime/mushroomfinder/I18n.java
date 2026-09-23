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
        String base = isZh() ? "巨大蘑菇岛查找器" : "Giant Mushroom Island Finder";
        return base + " v" + AppInfo.VERSION;
    }

    // ---- labels ----
    public static String seed() { return isZh() ? "种子 Seed:" : "Seed:"; }
    public static String version() { return isZh() ? "版本 Version:" : "Version:"; }
    public static String radius() {
        return isZh() ? "地图半径 / Search Radius:" : "Search Radius:";
    }
    public static String errRadiusRange(int min, int max, String raw) {
        return isZh()
                ? ("地图半径需在 " + min + "–" + max + " 格之间。\n当前输入：" + raw)
                : ("Search radius must be between " + min + " and " + max + " blocks.\nYou entered: " + raw);
    }
    public static String errRadiusInteger(String raw) {
        return isZh()
                ? ("地图半径必须是整数。\n当前输入：" + raw)
                : ("Search radius must be an integer.\nYou entered: " + raw);
    }
    public static String errRadiusTooSmall(int min, String raw) {
        return isZh()
                ? ("地图半径不能小于 " + min + " 格。\n当前输入：" + raw)
                : ("Search radius must be at least " + min + " blocks.\nYou entered: " + raw);
    }
    public static String errRadiusWorldBorder(int max, String raw) {
        return isZh()
                ? ("地图半径超过 Minecraft 世界边界。\n最大半径：" + max
                        + " 格（世界边界 ±" + max + "）。\n当前输入：" + raw)
                : ("Search radius exceeds the Minecraft world border.\nMax radius: "
                        + max + " blocks (border ±" + max + ").\nYou entered: " + raw);
    }
    public static String warnLargeRadius(int radius) {
        return isZh()
                ? ("搜索范围很大（半径 " + radius + " 格），可能需要较长时间。")
                : ("Large search range (radius " + radius + "); this may take a while.");
    }
    public static String warnHugeRadius(long samples) {
        return isZh()
                ? ("范围非常大，粗采样点约 " + String.format("%,d", samples)
                        + " 个，搜索可能需要很长时间。")
                : ("Very large range — about " + String.format("%,d", samples)
                        + " coarse samples. Search may take a very long time.");
    }
    public static String searchBoxLabel(int radius) {
        return isZh()
                ? ("搜索范围：[-" + String.format("%,d", radius) + ", "
                        + String.format("%,d", radius) + "]")
                : ("Search box: [-" + String.format("%,d", radius) + ", "
                        + String.format("%,d", radius) + "]");
    }
    public static String initializing() {
        return isZh() ? "正在初始化搜索…" : "Initializing search…";
    }
    public static String startingThreads(int n) {
        return isZh()
                ? ("正在启动 " + n + " 个搜索线程…")
                : ("Starting " + n + " search thread(s)…");
    }
    public static String scanStarted() {
        return isZh() ? "扫描中… 已启动" : "Scanning… started";
    }
    public static String scanNoPercentYet() {
        return isZh() ? "扫描中…" : "Scanning…";
    }
    public static String stopping() {
        return isZh() ? "正在停止…" : "Stopping…";
    }
    public static String coarseSamples(long n) {
        return isZh()
                ? ("粗采样点约 " + String.format("%,d", n))
                : ("≈ " + String.format("%,d", n) + " coarse samples");
    }
    public static String threadsEditableTip() {
        return isZh()
                ? "并行采样线程数。可选 Auto / 1 / 2 / 4 / 8 / 16 / 32，或输入整数。"
                : "Parallel sampling threads. Pick Auto / 1 / 2 / 4 / 8 / 16 / 32 or type an integer.";
    }

    public static String tileSizeHelp() {
        return isZh()
                ? "Tile Size 单位是采样点（samples），不是方块。\n"
                  + "256 = 256×256 samples。\n"
                  + "在 scale=16 时，每边覆盖 256×16 = 4096 格方块。"
                : "Tile Size is in samples, not blocks.\n"
                  + "256 = 256×256 samples.\n"
                  + "At scale=16 each side covers 256×16 = 4096 blocks.";
    }

    public static String errNative(int code, String nativeMsg) {
        if (isZh()) {
            if (code == NativeBridge.ERR_GRID_TOO_LARGE) {
                return "搜索网格过大，当前实现无法在合理内存内完成。\n\n"
                        + (nativeMsg == null ? "" : nativeMsg + "\n\n")
                        + "建议：把「地图半径」调小后重试。\n"
                        + "当前构建在 scale=16 下半径大约不宜超过 12 万格。";
            }
            if (code == NativeBridge.ERR_ALLOC) {
                return "内存分配失败。\n\n"
                        + (nativeMsg == null ? "" : nativeMsg + "\n\n")
                        + "请减小搜索半径，或关闭其他占内存的程序后重试。";
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
            return nativeMsg != null && !nativeMsg.isBlank()
                    ? nativeMsg
                    : ("搜索失败（代码 " + code + "）");
        }
        return nativeMsg != null && !nativeMsg.isBlank()
                ? ("[code " + code + "]\n" + nativeMsg)
                : ("Search failed (code " + code + ")");
    }
    public static String minArea() {
        return isZh() ? "最小面积 (可选):" : "Min area (optional):";
    }
    public static String maxResults() {
        return isZh() ? "结果数量:" : "Results:";
    }
    public static String threads() { return isZh() ? "线程数:" : "Threads:"; }
    public static String language() { return isZh() ? "语言:" : "Language:"; }
    public static String refine() {
        return isZh() ? "精修候选区域 (更准)" : "Refine candidates (more accurate)";
    }
    public static String includeShore() {
        return isZh() ? "计入岸边生物群系" : "Include shore biome";
    }
    public static String results() {
        return isZh() ? "结果" : "Results";
    }
    public static String basicSettings() {
        return isZh() ? "基本设置" : "Basic";
    }
    public static String advancedSettings() {
        return isZh() ? "高级筛选" : "Advanced";
    }
    public static String radiusHint500() { return "500"; }
    public static String radiusHint1000() { return "1000"; }
    public static String radiusHint2000() { return "2000"; }
    public static String radiusHint5000() { return "5000"; }
    public static String tipRadiusPresets() {
        return isZh()
                ? "常用范围：500 / 1000 / 2000 / 5000。半径 R 表示 [-R, R]，不是直径。"
                : "Common: 500 / 1000 / 2000 / 5000. R means [-R, R], not diameter.";
    }

    // ---- buttons ----
    public static String search() { return isZh() ? "搜索" : "Search"; }
    public static String pause() { return isZh() ? "暂停" : "Pause"; }
    public static String resume() { return isZh() ? "继续" : "Resume"; }
    public static String stop() { return isZh() ? "停止" : "Stop"; }
    public static String copyCoords() {
        return isZh() ? "复制坐标" : "Copy coords";
    }
    public static String exportCsv() { return isZh() ? "导出 CSV" : "Export CSV"; }
    public static String exportJson() {
        return isZh() ? "导出 JSON" : "Export JSON";
    }

    // ---- tooltips ----
    public static String tipSeed() {
        return isZh()
                ? "Minecraft Java 版世界种子（整数，可为负）"
                : "Minecraft Java world seed (integer, may be negative)";
    }
    public static String tipVersion() {
        return isZh()
                ? "生物群系生成算法版本；请与游戏实际版本一致"
                : "Biome generation version; must match the game version";
    }
    public static String tipRadius() {
        return isZh()
                ? "以 (0,0) 为中心的搜索半边长。1000 表示 [-1000,1000]×[-1000,1000]，不是直径。"
                : "Half-size around (0,0). 1000 means [-1000,1000] on X and Z — not a diameter.";
    }
    public static String tipMinArea() {
        return isZh()
                ? "可选。0 或留空 = 不过滤，直接返回范围内面积最大的若干座岛。"
                : "Optional. 0 or empty = no filter; return the largest islands in range.";
    }
    public static String tipMaxResults() {
        return isZh()
                ? "按面积从大到小保留的结果条数"
                : "How many islands to keep, largest area first";
    }
    public static String tipRefine() {
        return isZh()
                ? "对粗采样候选再按 scale 4 重算面积与边界，更准但稍慢"
                : "Recompute candidates at scale 4 for better area/bounds (slower)";
    }
    public static String tipShore() {
        return isZh()
                ? "将 mushroom_field_shore 也视为岛屿一部分"
                : "Treat mushroom_field_shore as part of the island";
    }
    public static String tipThreads() {
        return isZh()
                ? "并行采样线程数；Auto 使用全部 CPU 核心。不改变搜索结果。"
                : "Parallel sampling threads. Auto uses all CPU cores. Results stay identical.";
    }
    public static String tipSearch() {
        return isZh() ? "开始搜索" : "Start search";
    }
    public static String tipPause() {
        return isZh() ? "暂停 / 继续当前搜索" : "Pause / resume the current search";
    }
    public static String tipStop() {
        return isZh() ? "停止当前搜索" : "Stop the current search";
    }
    public static String tipCopy() {
        return isZh()
                ? "复制选中结果的中心坐标（未选中则复制第一条）"
                : "Copy center coordinates of the selected row (or the first result)";
    }
    public static String tipExportCsv() {
        return isZh() ? "将结果表导出为 CSV 文件" : "Export results to a CSV file";
    }
    public static String tipExportJson() {
        return isZh() ? "将结果导出为 JSON 文件" : "Export results to a JSON file";
    }
    public static String tipCopyFormat() {
        return isZh()
                ? "复制到剪贴板时使用的坐标格式"
                : "Clipboard format for copied coordinates";
    }
    public static String tipLanguage() {
        return isZh() ? "切换界面语言" : "Switch UI language";
    }
    public static String tipTable() {
        return isZh()
                ? "双击行可复制该岛中心坐标。面积为采样估算值。"
                : "Double-click a row to copy its center. Area is a sample-based estimate.";
    }

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
                    "#", "中心 X", "中心 Z", "面积 blocks²", "≈ km²", "跨度 W×H", "距离"
            };
        }
        return new String[] {
                "#", "Center X", "Center Z", "Area blocks²", "≈ km²", "Span W×H", "Distance"
        };
    }

    // ---- status / dialogs ----
    public static String ready() {
        return isZh() ? "就绪。" : "Ready.";
    }
    public static String searching() {
        return isZh() ? "搜索中…" : "Searching…";
    }
    public static String doneFound(int n) {
        return isZh() ? ("完成。找到 " + n + " 座岛屿。") : ("Done. Found " + n + " island(s).");
    }
    public static String stopped() {
        return isZh() ? "搜索已停止。" : "Search stopped.";
    }
    public static String paused() {
        return isZh() ? "已暂停" : "paused";
    }
    public static String stageScan() { return isZh() ? "扫描" : "scan"; }
    public static String stageTile() {
        return isZh() ? "分块扫描" : "tile scan";
    }
    public static String stageMerge() {
        return isZh() ? "区域合并" : "merge regions";
    }
    public static String stageConnect() { return isZh() ? "连通分析" : "connect"; }
    public static String stageRefine() { return isZh() ? "精修" : "refine"; }
    public static String stageDone() { return isZh() ? "完成" : "done"; }
    public static String stageInit() { return isZh() ? "初始化" : "init"; }

    public static String progressText(int percent, int found, String stage) {
        if (found >= 0) {
            return isZh()
                    ? (stage + " — 已找到 " + found)
                    : (stage + " — found " + found);
        }
        return stage;
    }

    public static String nativeMissing(String err) {
        return isZh()
                ? ("原生库未加载：" + err)
                : ("Native library not loaded: " + err);
    }

    public static String invalidSeed(String s) {
        return isZh() ? ("无效种子：" + s) : ("Invalid seed: " + s);
    }
    public static String errTitle() {
        return isZh() ? "错误" : "Error";
    }
    public static String inputErrorTitle() {
        return isZh() ? "输入错误" : "Input error";
    }
    public static String exportTitle() {
        return isZh() ? "导出" : "Export";
    }
    public static String nothingToExport() {
        return isZh() ? "没有可导出的结果。" : "No results to export.";
    }
    public static String exportOk(String path) {
        return isZh() ? ("已导出：" + path) : ("Exported: " + path);
    }
    public static String exportFailedTitle() {
        return isZh() ? "导出失败" : "Export failed";
    }
    public static String copied(String text) {
        return isZh() ? ("已复制：" + text) : ("Copied: " + text);
    }
    public static String nothingToCopy() {
        return isZh() ? "没有可复制的结果。" : "Nothing to copy.";
    }
    public static String auto() {
        return isZh() ? "自动" : "Auto";
    }
}
