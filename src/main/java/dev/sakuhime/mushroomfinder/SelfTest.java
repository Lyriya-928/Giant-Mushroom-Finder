package dev.sakuhime.mushroomfinder;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Headless verification used by --self-test. Exercises the JNI boundary,
 * radius validation, plan/mode selection and a few real searches (including
 * a custom centre and a custom biome target).
 */
final class SelfTest {
    private SelfTest() {}

    private static int failures = 0;

    private static final class Outcome {
        List<SearchResult> results;
        String error;
        int maxPercent;
        int ticks;
    }

    static int run() {
        if (!NativeBridge.isLoaded()) {
            System.err.println("FAIL: native library not loaded: "
                    + NativeBridge.getLoadError());
            return 2;
        }
        System.out.println("Native library loaded (v" + AppInfo.VERSION + ").");
        System.out.println("Physical-core default threads: " + NativeBridge.defaultThreads());
        System.out.println("Versions: " + String.join(", ", NativeBridge.nativeKnownVersions()));

        System.out.println("== Radius validation ==");
        expect(RadiusValidator.parse("63").error == RadiusValidator.Error.TOO_SMALL, "63 rejected");
        expect(RadiusValidator.parse("64").ok(), "64 accepted");
        expect(RadiusValidator.parse("200000").ok(), "200000 accepted");
        expect(RadiusValidator.parse("1000000").ok(), "1_000_000 accepted");
        expect(RadiusValidator.parse("30000000").ok(), "30_000_000 accepted (world border)");
        expect(RadiusValidator.parse("30000001").error == RadiusValidator.Error.BEYOND_WORLD_BORDER,
                "30_000_001 rejected (world border)");
        expect(RadiusValidator.parse("abc").error == RadiusValidator.Error.NOT_INTEGER, "abc rejected");
        expect(RadiusValidator.parse("12.5").error == RadiusValidator.Error.NOT_INTEGER, "12.5 rejected");
        expect(RadiusValidator.parse("100000").isLarge(), "100000 large warning");
        expect(RadiusValidator.parse("1000000").isHuge(), "1000000 huge warning");
        expect(RadiusValidator.parse("1000").coarseSamplePoints(16) == 126L * 126L,
                "coarse samples r=1000 scale16 = 126²");

        System.out.println("== Search plans / error reasons ==");
        NativeBridge.SearchPlan small = NativeBridge.plan("1.18", 0, 0, 5000, 16, 2, 0, 0);
        expect(small != null, "plan r=5000 ok");
        if (small != null) {
            expect(!small.tiled(), "r=5000 auto -> monolithic");
            expect(small.cells() == 626L * 626L, "r=5000 grid = 626²");
            System.out.printf("    mono plan: %s, ~%.1f MB, ~%,d samples%n",
                    small.gridText(), small.estMb(), small.estSamples());
        }
        NativeBridge.SearchPlan big = NativeBridge.plan("1.18", 0, 0, 40000, 16, 2, 0, 0);
        expect(big != null && big.tiled(), "r=40000 auto -> tiled");
        if (big != null) {
            System.out.printf("    tiled plan: %s, tile=%d, ~%.1f MB%n",
                    big.gridText(), big.tileSize(), big.estMb());
        }
        expect(NativeBridge.plan("1.18", 0, 0, 30_000_000, 16, 2, 0, 0) == null,
                "r=30M rejected by plan");
        expect(NativeBridge.nativeGetLastErrorCode() == NativeBridge.ERR_GRID_TOO_LARGE,
                "r=30M code is GRID_TOO_LARGE");
        expect(NativeBridge.plan("1.18", 0, 0, 10_000_000, 16, 0, 0, 0) == null,
                "forced monolithic r=10M rejected");
        long[] g = NativeBridge.nativeDescribeGrid(0, 0, 5000, 16);
        expect(g != null && g[2] > 0, "describeGrid r=5000 ok");

        System.out.println("== Native limit / biome presets ==");
        int max16 = NativeBridge.maxRadius(16);
        expect(max16 > 700000 && max16 < 1000000,
                "native max radius at scale16 is " + max16);
        NativeBridge.SearchPlan maxPlan = NativeBridge.plan("1.18", 0, 0, max16, 16, 2, 0, 0);
        expect(maxPlan != null && maxPlan.tiled(),
                "native max radius plans a tiled scan");
        expect(NativeBridge.plan("1.18", 0, 0, max16 + 1, 16, 2, 0, 0) == null,
                "native max radius + 1 is rejected");
        expect(BiomePreset.byId("plains").ids()[0] == 1, "plains preset id=1");
        expect(BiomePreset.byId("ocean").ids()[0] == 0, "ocean preset id=0");
        expect(BiomePreset.byId("cherry_grove").ids()[0] == 185,
                "cherry grove preset id=185");
        expect(BiomePreset.byId(BiomePreset.CUSTOM_ID).isCustom(),
                "custom preset has no fixed ids");

        System.out.println("== Real search: seed 262 / 1.18 / r=500 refine ==");
        Outcome o1 = runSearch(new SearchSettings(
                262L, "1.18", 0, 0, 500, 0L, 16, true, false, 0, 10, 2, 0, null));
        if (o1.error != null) {
            expect(false, "search error: " + o1.error);
        } else {
            expect(o1.results != null && !o1.results.isEmpty(), "results found");
            if (o1.results != null) {
                SearchResult top = o1.results.get(0);
                expect(Math.abs(top.centerX) < 200 && Math.abs(top.centerZ) < 200,
                        "largest island near origin");
                expect(top.area > 10000, "largest island area > 10k");
                expect(top.areaPrecision == 1, "refined precision reported");
                System.out.printf("    top center=(%d,%d) area=%d span=%dx%d dist=%.0f%n",
                        top.centerX, top.centerZ, top.area, top.width, top.height,
                        top.distance);
            }
            expect(o1.maxPercent >= 50, "progress advanced (max=" + o1.maxPercent + "%)");
        }

        System.out.println("== Real search: custom centre (-1000,-1000) ==");
        Outcome o2 = runSearch(new SearchSettings(
                262L, "1.18", -1000, -1000, 1200, 0L, 16, false, false, 0, 10, 2, 0, null));
        if (o2.error != null) {
            expect(false, "search error: " + o2.error);
        } else {
            expect(o2.results != null && !o2.results.isEmpty(), "custom-centre results");
            if (o2.results != null && !o2.results.isEmpty()) {
                SearchResult r = o2.results.get(0);
                boolean inBox = Math.abs(r.centerX + 1000) <= 1200 + 80
                        && Math.abs(r.centerZ + 1000) <= 1200 + 80;
                expect(inBox, "result stays inside the custom box");
                double expected = Math.hypot(r.centerX + 1000.0, r.centerZ + 1000.0);
                expect(Math.abs(r.distance - expected) < 40,
                        "distance is measured from the custom centre");
            }
        }

        System.out.println("== Real search: custom biome target (cold ocean 46) ==");
        Outcome o3 = runSearch(new SearchSettings(
                262L, "1.18", 0, 0, 600, 0L, 16, false, false, 0, 5, 2, 0,
                new int[] {46}));
        if (o3.error != null) {
            expect(false, "search error: " + o3.error);
        } else {
            expect(o3.results != null && !o3.results.isEmpty(), "custom biome results");
            if (o3.results != null && !o3.results.isEmpty()) {
                SearchResult r = o3.results.get(0);
                int id = NativeBridge.nativeSampleBiome(262L, "1.18", r.centerX, r.centerZ);
                expect(id == 46, "largest cold-ocean region centre has biome 46 (got " + id + ")");
                System.out.printf("    top target region center=(%d,%d) area=%d%n",
                        r.centerX, r.centerZ, r.area);
            }
        }

        System.out.println("== Lush caves exclusion (1.18+) ==");
        expect(NativeBridge.supportsLushCaves("1.18"), "1.18 supports 3D biomes");
        expect(NativeBridge.supportsLushCaves("1.21.3"), "1.21.3 supports 3D biomes");
        expect(!NativeBridge.supportsLushCaves("1.16.5"), "1.16.5 has no 3D biomes");
        expect(NativeBridge.plan("1.16.5", 0, 0, 500, 16, 2, 0, 0, true) == null,
                "1.16.5 + exclusion is rejected by the plan");
        expect(NativeBridge.nativeGetLastErrorCode() == NativeBridge.ERR_VERSION,
                "rejection code is ERR_VERSION");
        expect(NativeBridge.plan("1.16.5", 0, 0, 500, 16, 2, 0, 0, false) != null,
                "1.16.5 without exclusion still plans");

        Outcome lushOff = runSearch(new SearchSettings(
                262L, "1.18", 0, 0, 5000, 0L, 16, false, false, false, 0, 0, 2, 0, null));
        Outcome lushOn = runSearch(new SearchSettings(
                262L, "1.18", 0, 0, 5000, 0L, 16, false, false, true, 0, 0, 2, 0, null));
        if (lushOff.error != null || lushOn.error != null) {
            expect(false, "lush search error: " + lushOff.error + " / " + lushOn.error);
        } else {
            int no = lushOff.results.size();
            int nn = lushOn.results.size();
            expect(no == 7, "seed 262 r=5000 finds 7 islands without the filter (got " + no + ")");
            expect(nn == 4, "the filter drops the 3 islands above lush caves (got " + nn + ")");
            boolean subset = true;
            for (SearchResult kept : lushOn.results) {
                boolean found = false;
                for (SearchResult ref : lushOff.results) {
                    if (kept.centerX == ref.centerX && kept.centerZ == ref.centerZ
                            && kept.area == ref.area) {
                        found = true;
                        break;
                    }
                }
                if (!found) subset = false;
            }
            expect(subset, "every kept island is unchanged (no split, no trimming)");
        }

        System.out.println("== Lush exclusion leaves other targets alone ==");
        int[] oceanTarget = {46};
        Outcome ocOff = runSearch(new SearchSettings(
                262L, "1.18", 0, 0, 600, 0L, 16, false, false, false, 0, 0, 2, 0,
                oceanTarget));
        Outcome ocOn = runSearch(new SearchSettings(
                262L, "1.18", 0, 0, 600, 0L, 16, false, false, true, 0, 0, 2, 0,
                oceanTarget));
        if (ocOff.error != null || ocOn.error != null) {
            expect(false, "ocean search error: " + ocOff.error + " / " + ocOn.error);
        } else {
            expect(ocOff.results.size() == ocOn.results.size(),
                    "cold-ocean target returns identical results (" + ocOn.results.size() + ")");
            boolean same = true;
            for (int i = 0; i < ocOff.results.size(); i++) {
                SearchResult a = ocOff.results.get(i);
                SearchResult b = ocOn.results.get(i);
                if (a.centerX != b.centerX || a.centerZ != b.centerZ || a.area != b.area) {
                    same = false;
                }
            }
            expect(same, "non-mushroom results are byte-identical");
        }

        System.out.println("========================================");
        if (failures == 0) {
            System.out.println("SELF-TEST PASS");
            return 0;
        }
        System.out.println("SELF-TEST FAIL (" + failures + " failure(s))");
        return 1;
    }

    private static Outcome runSearch(SearchSettings settings) {
        Outcome out = new Outcome();
        SearchRunner runner = new SearchRunner();
        CountDownLatch done = new CountDownLatch(1);
        AtomicReference<List<SearchResult>> resultsRef = new AtomicReference<>();
        AtomicReference<String> errRef = new AtomicReference<>();

        runner.addListener(new SearchRunner.Listener() {
            @Override public void onProgress(int percent, int found, String stage) {
                out.ticks++;
                if (percent > out.maxPercent) out.maxPercent = percent;
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

        runner.start(settings);
        try {
            if (!done.await(180, TimeUnit.SECONDS)) {
                runner.stop();
                out.error = "timeout";
                return out;
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            out.error = "interrupted";
            return out;
        }
        out.error = errRef.get();
        out.results = resultsRef.get() == null ? List.of() : resultsRef.get();
        return out;
    }

    private static void expect(boolean cond, String name) {
        System.out.println((cond ? "  [PASS] " : "  [FAIL] ") + name);
        if (!cond) failures++;
    }
}
