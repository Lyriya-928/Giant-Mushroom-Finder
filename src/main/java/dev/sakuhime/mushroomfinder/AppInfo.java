package dev.sakuhime.mushroomfinder;

/** Single source of truth for the application version. Keep in sync with /VERSION. */
public final class AppInfo {
    public static final String NAME = "Large Biome Finder";
    public static final String VERSION = "1.1.0";

    private AppInfo() {}

    public static String title() {
        return NAME + " v" + VERSION;
    }
}
