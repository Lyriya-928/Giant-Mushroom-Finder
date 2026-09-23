package dev.sakuhime.mushroomfinder;

/** Single source of truth for the application version. Keep in sync with /VERSION. */
public final class AppInfo {
    public static final String NAME = "Giant Mushroom Island Finder";
    public static final String VERSION = "1.0.0";

    private AppInfo() {}

    public static String title() {
        return NAME + " v" + VERSION;
    }
}
