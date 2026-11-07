package dev.sakuhime.mushroomfinder;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Properties;

/**
 * Small persistent preference store for the desktop app.
 *
 * Stored under %APPDATA%/LargeBiomeFinder/settings.properties (or the
 * user home when APPDATA is unavailable). The previous
 * GiantMushroomFinder settings file is read once for migration. The format is
 * a plain properties file so users can inspect or delete it safely. Nothing is
 * written to the repository or to a shared location.
 */
public final class UserPrefs {

    private static final String RECENT_KEY = "recent.";
    private static final int RECENT_MAX = 8;

    public long seed = 262L;
    public String version = "1.21.3";
    public int centerX = 0;
    public int centerZ = 0;
    public int radius = 1000;
    public long minArea = 0L;
    public int maxResults = 10;
    public int threads = 0;            // 0 = auto
    public int scanMode = 2;           // 0 full, 1 tiled, 2 auto
    public int tileSize = 0;           // 0 = auto
    public int targetMode = 0;         // legacy: 0 mushroom, 1 mushroom+shore, 2 custom
    public String targetPreset = "mushroom"; // stable id, see BiomePreset
    public String customBiomes = "14";
    public boolean refine = false;
    public boolean excludeLushCaves = false;
    public String language = "";       // "" = follow system

    private final List<String> recent = new ArrayList<>();
    private Path file;

    private UserPrefs() {}

    public static UserPrefs load() {
        UserPrefs p = new UserPrefs();
        p.file = locate();
        if (p.file == null) return p;
        Path source = p.file;
        if (!Files.isRegularFile(source)) {
            Path legacy = locateLegacy();
            if (legacy != null && Files.isRegularFile(legacy)) source = legacy;
            else return p;
        }
        Properties props = new Properties();
        try (var in = Files.newBufferedReader(source, StandardCharsets.UTF_8)) {
            props.load(in);
        } catch (IOException | RuntimeException ignored) {
            return p;
        }
        p.seed = parseLong(props.getProperty("seed"), p.seed);
        p.version = props.getProperty("version", p.version);
        p.centerX = parseInt(props.getProperty("centerX"), p.centerX);
        p.centerZ = parseInt(props.getProperty("centerZ"), p.centerZ);
        p.radius = parseInt(props.getProperty("radius"), p.radius);
        p.minArea = parseLong(props.getProperty("minArea"), p.minArea);
        p.maxResults = parseInt(props.getProperty("maxResults"), p.maxResults);
        p.threads = parseInt(props.getProperty("threads"), p.threads);
        p.scanMode = parseInt(props.getProperty("scanMode"), p.scanMode);
        p.tileSize = parseInt(props.getProperty("tileSize"), p.tileSize);
        p.targetMode = parseInt(props.getProperty("targetMode"), p.targetMode);
        p.targetPreset = props.getProperty("targetPreset",
                p.targetMode == 1 ? "mushroom_shore"
                        : (p.targetMode == 2 ? BiomePreset.CUSTOM_ID : "mushroom"));
        p.customBiomes = props.getProperty("customBiomes", p.customBiomes);
        p.refine = Boolean.parseBoolean(props.getProperty("refine", "false"));
        p.excludeLushCaves = Boolean.parseBoolean(
                props.getProperty("excludeLushCaves", "false"));
        p.language = props.getProperty("language", p.language);
        for (int i = 0; i < RECENT_MAX; i++) {
            String v = props.getProperty(RECENT_KEY + i);
            if (v != null && !v.isBlank()) p.recent.add(v);
        }
        return p;
    }

    /** New settings location; legacy location is read once for migration. */
    private static Path locate() {
        try {
            String appData = System.getenv("APPDATA");
            Path base = (appData != null && !appData.isBlank())
                    ? Path.of(appData)
                    : Path.of(System.getProperty("user.home", "."));
            return base.resolve("LargeBiomeFinder").resolve("settings.properties");
        } catch (RuntimeException e) {
            return null;
        }
    }

    private static Path locateLegacy() {
        try {
            String appData = System.getenv("APPDATA");
            Path base = (appData != null && !appData.isBlank())
                    ? Path.of(appData)
                    : Path.of(System.getProperty("user.home", "."));
            return base.resolve("GiantMushroomFinder").resolve("settings.properties");
        } catch (RuntimeException e) {
            return null;
        }
    }

    private static int parseInt(String s, int fallback) {
        try { return Integer.parseInt(s.trim()); } catch (Exception e) { return fallback; }
    }

    private static long parseLong(String s, long fallback) {
        try { return Long.parseLong(s.trim()); } catch (Exception e) { return fallback; }
    }

    public List<String> recent() {
        return List.copyOf(recent);
    }

    /** Most recent first; duplicates are moved to the front. */
    public void pushRecent(String entry) {
        if (entry == null || entry.isBlank()) return;
        recent.remove(entry);
        recent.add(0, entry);
        while (recent.size() > RECENT_MAX) recent.remove(recent.size() - 1);
    }

    public void clearRecent() {
        recent.clear();
    }

    public void save() {
        if (file == null) return;
        Properties props = new Properties();
        props.setProperty("seed", Long.toString(seed));
        props.setProperty("version", version == null ? "" : version);
        props.setProperty("centerX", Integer.toString(centerX));
        props.setProperty("centerZ", Integer.toString(centerZ));
        props.setProperty("radius", Integer.toString(radius));
        props.setProperty("minArea", Long.toString(minArea));
        props.setProperty("maxResults", Integer.toString(maxResults));
        props.setProperty("threads", Integer.toString(threads));
        props.setProperty("scanMode", Integer.toString(scanMode));
        props.setProperty("tileSize", Integer.toString(tileSize));
        props.setProperty("targetMode", Integer.toString(targetMode));
        props.setProperty("targetPreset", targetPreset == null ? "mushroom" : targetPreset);
        props.setProperty("customBiomes", customBiomes == null ? "" : customBiomes);
        props.setProperty("refine", Boolean.toString(refine));
        props.setProperty("excludeLushCaves", Boolean.toString(excludeLushCaves));
        props.setProperty("language", language == null ? "" : language);
        for (int i = 0; i < recent.size(); i++) {
            props.setProperty(RECENT_KEY + i, recent.get(i));
        }
        try {
            Path parent = file.getParent();
            if (parent != null) Files.createDirectories(parent);
            Path tmp = file.resolveSibling(file.getFileName() + ".tmp");
            try (var out = Files.newBufferedWriter(tmp, StandardCharsets.UTF_8)) {
                props.store(out, "Large Biome Finder settings");
            }
            try {
                Files.move(tmp, file, java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            } catch (IOException e) {
                Files.deleteIfExists(tmp);
                throw e;
            }
        } catch (IOException | RuntimeException ignored) {
            // Preferences are best-effort; never break a search over them.
        }
    }
}
