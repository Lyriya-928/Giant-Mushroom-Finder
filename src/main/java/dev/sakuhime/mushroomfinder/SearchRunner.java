package dev.sakuhime.mushroomfinder;

import javax.swing.SwingUtilities;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Runs the blocking native search on a background thread and exposes
 * pause / resume / stop to the UI thread.
 *
 * Lifecycle pattern inspired by SunnySlopes FortressFinderGUI.
 * No source code copied.
 */
public final class SearchRunner {

    /** Progress stage codes used by the GUI (in addition to native stages). */
    public static final String STAGE_INIT = "init";
    public static final String STAGE_STARTING = "starting";
    public static final String STAGE_SCAN = "scan";
    public static final String STAGE_TILE = "tile";
    public static final String STAGE_MERGE = "merge";
    public static final String STAGE_CONNECT = "connect";
    public static final String STAGE_REFINE = "refine";
    public static final String STAGE_DONE = "done";
    public static final String STAGE_PAUSED = "paused";
    public static final String STAGE_STOPPING = "stopping";

    public interface Listener {
        void onProgress(int percent, int found, String stage);
        void onCompleted(List<SearchResult> results);
        void onCancelled();
        void onError(String message);
    }

    private enum State { IDLE, RUNNING, PAUSED, STOPPING, DONE, CANCELLED, ERROR }

    private final AtomicReference<State> state = new AtomicReference<>(State.IDLE);
    private final List<Listener> listeners = new CopyOnWriteArrayList<>();
    private final AtomicBoolean stopRequested = new AtomicBoolean(false);
    private Thread worker;
    private long controlPtr;
    private javax.swing.Timer progressTimer;
    private int lastPercent = 0;
    private String lastStage = STAGE_INIT;
    private volatile boolean tiled = false;
    private volatile long startedAtNanos = 0L;
    private volatile long finishedAtNanos = 0L;

    public boolean isTiled() {
        return tiled;
    }

    /** Wall time of the last finished search in milliseconds (0 if none). */
    public long lastDurationMs() {
        long a = startedAtNanos;
        long b = finishedAtNanos;
        if (a == 0 || b < a) return 0L;
        return (b - a) / 1_000_000L;
    }

    public void addListener(Listener l) {
        listeners.add(l);
    }

    public void removeListener(Listener l) {
        listeners.remove(l);
    }

    public synchronized boolean isRunning() {
        State s = state.get();
        return s == State.RUNNING || s == State.PAUSED || s == State.STOPPING;
    }

    public synchronized boolean isPaused() {
        return state.get() == State.PAUSED;
    }

    public synchronized boolean isStopping() {
        return state.get() == State.STOPPING;
    }

    public synchronized void start(SearchSettings settings) {
        if (isRunning()) return;
        stopRequested.set(false);
        state.set(State.RUNNING);
        lastPercent = 0;
        lastStage = STAGE_INIT;
        tiled = false;
        startedAtNanos = System.nanoTime();
        finishedAtNanos = 0L;

        /* Immediate feedback on the EDT before any heavy work. */
        fireProgress(-1, 0, STAGE_INIT);
        fireProgress(-1, 0, STAGE_STARTING);

        controlPtr = NativeBridge.nativeCreateControl();

        worker = new Thread(() -> runSearch(settings), "gmif-search");
        worker.setDaemon(true);
        worker.start();

        progressTimer = new javax.swing.Timer(120, e -> pollProgress());
        progressTimer.start();
    }

    public synchronized void pause() {
        if (state.get() != State.RUNNING) return;
        NativeBridge.nativePause(controlPtr);
        state.set(State.PAUSED);
        fireProgress(lastPercent, -1, STAGE_PAUSED);
    }

    public synchronized void resume() {
        if (state.get() != State.PAUSED) return;
        NativeBridge.nativeResume(controlPtr);
        state.set(State.RUNNING);
        fireProgress(lastPercent, -1, lastStage);
    }

    public synchronized void stop() {
        State s = state.get();
        if (s != State.RUNNING && s != State.PAUSED) return;
        stopRequested.set(true);
        state.set(State.STOPPING);
        if (controlPtr != 0) {
            NativeBridge.nativeStop(controlPtr);
            NativeBridge.nativeResume(controlPtr); /* wake if paused */
        }
        fireProgress(-1, -1, STAGE_STOPPING);
    }

    private synchronized void pollProgress() {
        long ptr = controlPtr;
        if (ptr == 0) return;
        if (state.get() == State.STOPPING) {
            fireProgress(-1, -1, STAGE_STOPPING);
            return;
        }
        int[] out = new int[4];
        try {
            NativeBridge.nativePollProgress(ptr, out);
        } catch (Throwable ignored) {
            return;
        }
        tiled = out[3] == 1;
        String stage = switch (out[2]) {
            case 1 -> STAGE_CONNECT;
            case 2 -> STAGE_REFINE;
            case 3 -> STAGE_DONE;
            case 4 -> STAGE_TILE;
            case 5 -> STAGE_MERGE;
            default -> STAGE_SCAN;
        };
        int pct = out[0];
        int found = out[1];
        /* percent < 0 means "unknown / indeterminate" for the UI. */
        if (pct > 0 || stage.equals(STAGE_CONNECT)
                || stage.equals(STAGE_REFINE) || stage.equals(STAGE_DONE)) {
            lastPercent = Math.max(pct, 0);
        }
        lastStage = stage;
        if (state.get() == State.PAUSED) {
            fireProgress(lastPercent, found, STAGE_PAUSED);
        } else {
            fireProgress(pct, found, stage);
        }
    }

    private static String safeLastError() {
        try {
            int code = NativeBridge.nativeGetLastErrorCode();
            String msg = NativeBridge.nativeGetLastError();
            return I18n.errNative(code, msg);
        } catch (UnsatisfiedLinkError e) {
            return I18n.isZh()
                    ? ("原生库版本过期（缺少 " + e.getMessage()
                            + "）。请执行 .\\build.ps1 后重启程序。")
                    : ("Native library is out of date (missing " + e.getMessage()
                            + "). Rebuild with .\\build.ps1 and restart the app.");
        } catch (Throwable t) {
            String m = t.getMessage();
            return (m == null || m.isBlank()) ? "Native search failed." : "Native search failed: " + m;
        }
    }

    private void runSearch(SearchSettings s) {
        try {
            double[] flat;
            try {
                flat = NativeBridge.nativeSearch(
                        s.seed, s.version, s.centerX, s.centerZ, s.radius, s.minArea,
                        s.scale, s.refine, s.includeShore, controlPtr,
                        s.threads, s.maxResults, s.biomes);
            } catch (UnsatisfiedLinkError e) {
                finishError("Native library is out of date (missing " + e.getMessage()
                        + "). Rebuild with .\\build.ps1 and restart the app.");
                return;
            }

            if (stopRequested.get() || flat == null) {
                if (stopRequested.get()) {
                    finishCancel();
                } else {
                    String msg = safeLastError();
                    finishError(msg);
                }
                return;
            }

            int n = flat.length / NativeBridge.RESULT_STRIDE;
            List<SearchResult> results = new ArrayList<>(n);
            for (int i = 0; i < n; i++) {
                results.add(SearchResult.fromFlat(flat, i * NativeBridge.RESULT_STRIDE));
            }
            state.set(State.DONE);
            stopTimer();
            finishedAtNanos = System.nanoTime();
            fireProgress(100, results.size(), STAGE_DONE);
            fireCompleted(results);
        } catch (Throwable t) {
            if (stopRequested.get()) {
                finishCancel();
            } else if (t instanceof UnsatisfiedLinkError) {
                finishError("Native library is out of date (missing "
                        + t.getMessage() + "). Rebuild with .\\build.ps1 and restart the app.");
            } else {
                finishError(String.valueOf(t.getMessage()));
            }
        } finally {
            /* Free the control block on the EDT after any already-queued
             * progress poll events have run. Capture the pointer so a new
             * search started in the meantime cannot be freed by mistake. */
            stopTimer();
            scheduleDestroyControl();
        }
    }

    private void scheduleDestroyControl() {
        final long ptr;
        synchronized (this) {
            ptr = controlPtr;
        }
        if (ptr == 0) return;
        SwingUtilities.invokeLater(() -> {
            synchronized (SearchRunner.this) {
                if (controlPtr != ptr) return;
                NativeBridge.nativeDestroyControl(ptr);
                controlPtr = 0;
            }
        });
    }

    private void finishCancel() {
        state.set(State.CANCELLED);
        stopTimer();
        finishedAtNanos = System.nanoTime();
        fireCancelled();
    }

    private void finishError(String msg) {
        state.set(State.ERROR);
        stopTimer();
        finishedAtNanos = System.nanoTime();
        fireError(msg);
    }

    private synchronized void stopTimer() {
        if (progressTimer != null) {
            progressTimer.stop();
            progressTimer = null;
        }
    }

    private void fireProgress(int pct, int found, String stage) {
        SwingUtilities.invokeLater(() -> {
            for (Listener l : listeners) l.onProgress(pct, found, stage);
        });
    }

    private void fireCompleted(List<SearchResult> results) {
        SwingUtilities.invokeLater(() -> {
            for (Listener l : listeners) l.onCompleted(results);
        });
    }

    private void fireCancelled() {
        SwingUtilities.invokeLater(() -> {
            for (Listener l : listeners) l.onCancelled();
        });
    }

    private void fireError(String msg) {
        SwingUtilities.invokeLater(() -> {
            for (Listener l : listeners) l.onError(msg);
        });
    }
}
