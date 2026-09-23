package dev.sakuhime.mushroomfinder;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Headless verification used by --self-test. Runs one search against a
 * known seed and checks that a mushroom island near origin is found.
 */
final class SelfTest {
    private SelfTest() {}

    static int run() {
        if (!NativeBridge.isLoaded()) {
            System.err.println("FAIL: native library not loaded: "
                    + NativeBridge.getLoadError());
            return 2;
        }
        System.out.println("Native library loaded.");
        System.out.println("Versions: " + String.join(", ", NativeBridge.nativeKnownVersions()));

        System.out.println("== Radius validation ==");
        expect(RadiusValidator.parse("63").error == RadiusValidator.Error.TOO_SMALL, "63 rejected");
        expect(RadiusValidator.parse("64").ok(), "64 accepted");
        expect(RadiusValidator.parse("200000").ok(), "200000 accepted (no project cap)");
        expect(RadiusValidator.parse("1000000").ok(), "1_000_000 accepted");
        expect(RadiusValidator.parse("30000000").ok(), "30_000_000 accepted (world border)");
        expect(RadiusValidator.parse("30000001").error == RadiusValidator.Error.BEYOND_WORLD_BORDER,
                "30_000_001 rejected (world border)");
        expect(RadiusValidator.parse("abc").error == RadiusValidator.Error.NOT_INTEGER, "abc rejected");
        expect(RadiusValidator.parse("12.5").error == RadiusValidator.Error.NOT_INTEGER, "12.5 rejected");
        expect(RadiusValidator.parse("100000").isLarge(), "100000 large warning");
        expect(RadiusValidator.parse("1000000").isHuge(), "1000000 huge warning");
        expect(RadiusValidator.parse("1000").ok() && !RadiusValidator.parse("1000").isLarge(), "1000 normal");
        long samples = RadiusValidator.parse("1000").coarseSamplePoints(16);
        expect(samples == 126L * 126L, "coarse samples r=1000 scale16 = 126²");

        System.out.println("== Grid / large-radius error reasons ==");
        long[] g5k = NativeBridge.nativeDescribeGrid(5000, 16);
        expect(g5k != null && g5k[3] > 0, "describe 5000 ok");
        long[] g100k = NativeBridge.nativeDescribeGrid(100000, 16);
        expect(g100k != null && g100k[3] > 0, "describe 100000 ok");
        long[] g10m = NativeBridge.nativeDescribeGrid(10_000_000, 16);
        expect(g10m == null, "describe 10M fails (grid too large)");
        expect(NativeBridge.nativeGetLastErrorCode() == NativeBridge.ERR_GRID_TOO_LARGE,
                "10M code is GRID_TOO_LARGE");
        String e10 = NativeBridge.nativeGetLastError();
        expect(e10 != null && !e10.contains("version string"),
                "10M message does not say version string");
        System.out.println("    10M: " + e10);

        SearchRunner runner = new SearchRunner();
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<List<SearchResult>> resultsRef = new AtomicReference<>();
        AtomicReference<String> errRef = new AtomicReference<>();
        final int[] maxPct = {0};
        final int[] progressTicks = {0};

        runner.addListener(new SearchRunner.Listener() {
            @Override public void onProgress(int percent, int found, String stage) {
                progressTicks[0]++;
                if (percent > maxPct[0]) maxPct[0] = percent;
                System.out.printf("progress %d%% found=%d stage=%s%n", percent, found, stage);
            }
            @Override public void onCompleted(List<SearchResult> results) {
                resultsRef.set(results);
                done.countDown();
            }
            @Override public void onCancelled() {
                errRef.set("cancelled");
                done.countDown();
            }
            @Override public void onError(String message) {
                errRef.set(message);
                done.countDown();
            }
        });

        SearchSettings settings = new SearchSettings(
                262L, "1.18", 500, 0L, 16, true, false, 1, 10);
        runner.start(settings);

        try {
            if (!done.await(60, TimeUnit.SECONDS)) {
                System.err.println("FAIL: timeout");
                runner.stop();
                return 3;
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return 4;
        }

        List<SearchResult> results = resultsRef.get();
        if (errRef.get() != null) {
            System.err.println("FAIL: " + errRef.get());
            return 5;
        }
        if (results == null || results.isEmpty()) {
            System.err.println("FAIL: no islands found for seed 262");
            return 6;
        }

        boolean nearOrigin = false;
        for (SearchResult r : results) {
            System.out.printf(
                    "  island center=(%d,%d) area=%d [%s] span=%dx%d diag=%.0f dist=%.0f%n",
                    r.centerX, r.centerZ, r.area, r.areaLabel(),
                    r.width, r.height, r.diagonalSpan, r.distance);
            if (Math.abs(r.centerX) < 200 && Math.abs(r.centerZ) < 200 && r.area > 10000) {
                nearOrigin = true;
            }
        }
        if (!nearOrigin) {
            System.err.println("FAIL: expected a large island near origin for seed 262");
            return 7;
        }
        if (maxPct[0] < 50) {
            System.err.println("FAIL: progress never advanced (max=" + maxPct[0] + "%)");
            return 8;
        }
        System.out.println("PASS (max progress " + maxPct[0] + "%, ticks=" + progressTicks[0] + ")");
        return 0;
    }

    private static void expect(boolean cond, String name) {
        System.out.println((cond ? "  [PASS] " : "  [FAIL] ") + name);
    }
}
