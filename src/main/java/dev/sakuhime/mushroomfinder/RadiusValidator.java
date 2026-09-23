package dev.sakuhime.mushroomfinder;

/**
 * Parses and validates user-facing search radius (half-size in blocks).
 *
 * Minecraft Java world border is ±30,000,000 on X/Z.
 */
public final class RadiusValidator {

    public static final int MIN_RADIUS = 64;
    /** Minecraft Java Edition world border (blocks). */
    public static final int MAX_RADIUS = 30_000_000;
    /** Above this, show a “very large range” warning. */
    public static final int LARGE_RADIUS = 100_000;
    /** Above this, show estimated coarse sample count warning. */
    public static final int HUGE_RADIUS = 1_000_000;

    public enum Error {
        NONE,
        NOT_INTEGER,
        TOO_SMALL,
        BEYOND_WORLD_BORDER
    }

    public static final class Result {
        public final int radius;
        public final Error error;
        public final String raw;

        Result(int radius, Error error, String raw) {
            this.radius = radius;
            this.error = error;
            this.raw = raw;
        }

        public boolean ok() {
            return error == Error.NONE;
        }

        public boolean isLarge() {
            return ok() && radius >= LARGE_RADIUS;
        }

        public boolean isHuge() {
            return ok() && radius >= HUGE_RADIUS;
        }

        /** Coarse samples at scale 16 over [-R,R]² (one sample ≈ 16×16 blocks). */
        public long coarseSamplePoints(int scale) {
            if (!ok() || scale <= 0) return 0;
            long half = (radius + scale - 1) / scale;
            long n = half * 2;
            return n * n;
        }
    }

    private RadiusValidator() {}

    public static Result parse(String rawInput) {
        String raw = rawInput == null ? "" : rawInput.trim();
        if (raw.isEmpty() || !raw.matches("[+-]?\\d+")) {
            return new Result(-1, Error.NOT_INTEGER, raw);
        }
        long radius;
        try {
            radius = Long.parseLong(raw);
        } catch (NumberFormatException ex) {
            return new Result(-1, Error.NOT_INTEGER, raw);
        }
        if (radius < MIN_RADIUS) {
            return new Result((int) Math.min(radius, Integer.MAX_VALUE),
                    Error.TOO_SMALL, raw);
        }
        if (radius > MAX_RADIUS) {
            return new Result(MAX_RADIUS, Error.BEYOND_WORLD_BORDER, raw);
        }
        return new Result((int) radius, Error.NONE, raw);
    }
}
