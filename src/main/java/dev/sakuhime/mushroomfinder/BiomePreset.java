package dev.sakuhime.mushroomfinder;

import java.util.List;

/**
 * Common biome targets for the GUI, plus a custom-id entry.
 *
 * The ids are Cubiomes biome ids and are deliberately kept as a tiny data
 * table: adding another common biome does not require touching the search
 * core. Custom ids are validated in the same 0..255 range.
 */
public record BiomePreset(String id, int[] ids) {

    public static final String CUSTOM_ID = "custom";

    private static final List<BiomePreset> ALL = List.of(
            new BiomePreset("mushroom", new int[] {14}),
            new BiomePreset("mushroom_shore", new int[] {14, 15}),
            new BiomePreset("plains", new int[] {1}),
            new BiomePreset("forest", new int[] {4}),
            new BiomePreset("desert", new int[] {2}),
            new BiomePreset("jungle", new int[] {21}),
            new BiomePreset("savanna", new int[] {35}),
            new BiomePreset("badlands", new int[] {37}),
            new BiomePreset("taiga", new int[] {5}),
            new BiomePreset("swamp", new int[] {6}),
            new BiomePreset("ocean", new int[] {0}),
            new BiomePreset("cherry_grove", new int[] {185}),
            new BiomePreset(CUSTOM_ID, null)
    );

    public BiomePreset {
        ids = ids == null ? null : ids.clone();
    }

    @Override
    public int[] ids() {
        return ids == null ? null : ids.clone();
    }

    public boolean isCustom() {
        return ids == null;
    }

    public String label() {
        return I18n.biomePresetLabel(id);
    }

    public static List<BiomePreset> all() {
        return ALL;
    }

    public static BiomePreset byId(String id) {
        if (id != null) {
            for (BiomePreset p : ALL) {
                if (p.id.equals(id)) return p;
            }
        }
        return ALL.get(0);
    }

    public static int indexOfId(String id) {
        for (int i = 0; i < ALL.size(); i++) {
            if (ALL.get(i).id.equals(id)) return i;
        }
        return -1;
    }

    /** Old settings stored 0=mushroom, 1=mushroom+shore, 2=custom. */
    public static int indexFromLegacyMode(int mode) {
        return switch (mode) {
            case 1 -> indexOfId("mushroom_shore");
            case 2 -> indexOfId(CUSTOM_ID);
            default -> indexOfId("mushroom");
        };
    }

    /** Legacy value written alongside targetPreset for older builds. */
    public static int legacyModeFor(String id) {
        if ("mushroom_shore".equals(id)) return 1;
        if (CUSTOM_ID.equals(id)) return 2;
        return 0;
    }
}
