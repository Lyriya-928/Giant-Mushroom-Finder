package dev.sakuhime.mushroomfinder;

/** One discovered mushroom-fields island. */
public final class SearchResult {
    public final int centerX;
    public final int centerZ;
    public final int minX;
    public final int maxX;
    public final int minZ;
    public final int maxZ;
    public final int width;
    public final int height;
    public final long area;
    public final long samples;
    public final double distance;
    public final double compactness;
    public final int perimeter;
    public final double diagonalSpan;
    /** 0=coarse estimate, 1=refined scale4, 2=scale1 */
    public final int areaPrecision;

    public SearchResult(int centerX, int centerZ,
                        int minX, int maxX, int minZ, int maxZ,
                        int width, int height,
                        long area, long samples,
                        double distance, double compactness, int perimeter,
                        double diagonalSpan, int areaPrecision) {
        this.centerX = centerX;
        this.centerZ = centerZ;
        this.minX = minX;
        this.maxX = maxX;
        this.minZ = minZ;
        this.maxZ = maxZ;
        this.width = width;
        this.height = height;
        this.area = area;
        this.samples = samples;
        this.distance = distance;
        this.compactness = compactness;
        this.perimeter = perimeter;
        this.diagonalSpan = diagonalSpan;
        this.areaPrecision = areaPrecision;
    }

    public static SearchResult fromFlat(double[] flat, int offset) {
        return new SearchResult(
                (int) flat[offset + 0],
                (int) flat[offset + 1],
                (int) flat[offset + 2],
                (int) flat[offset + 3],
                (int) flat[offset + 4],
                (int) flat[offset + 5],
                (int) flat[offset + 6],
                (int) flat[offset + 7],
                (long) flat[offset + 8],
                (long) flat[offset + 9],
                flat[offset + 10],
                flat[offset + 11],
                (int) flat[offset + 12],
                flat[offset + 13],
                (int) flat[offset + 14]);
    }

    public String coordinateText() {
        return centerX + " " + centerZ;
    }

    public String areaLabel() {
        return switch (areaPrecision) {
            case 2 -> "scale1 (high precision)";
            case 1 -> "refined-scale4 (estimated)";
            default -> "coarse-estimate";
        };
    }

    /** 1 block² = 1 m² in Minecraft. */
    public double areaKm2() {
        return area / 1_000_000.0;
    }

    public String areaBlocksFormatted() {
        return String.format("%,d", area);
    }

    public String areaKm2Formatted() {
        return String.format("%.3f", areaKm2());
    }

    public String areaDisplay() {
        return areaBlocksFormatted() + " blocks² (≈ " + areaKm2Formatted() + " km²)";
    }
}
