package dev.sakuhime.mushroomfinder;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;

/**
 * Loads the native library (mushroomfinder).
 *
 * Search order is deliberately explicit so a stale build\mushroomfinder.dll
 * cannot shadow a newer build\lib\mushroomfinder.dll (see UnsatisfiedLinkError
 * on nativeGetLastErrorCode when an old DLL is loaded).
 */
final class NativeLoader {
    private NativeLoader() {}

    static void load() throws IOException {
        String os = System.getProperty("os.name", "").toLowerCase();
        String libName;
        String resource;
        if (os.contains("win")) {
            libName = "mushroomfinder.dll";
            resource = "/native/mushroomfinder.dll";
        } else if (os.contains("mac")) {
            libName = "libmushroomfinder.dylib";
            resource = "/native/libmushroomfinder.dylib";
        } else {
            libName = "libmushroomfinder.so";
            resource = "/native/libmushroomfinder.so";
        }

        List<String> tried = new ArrayList<>();
        UnsatisfiedLinkError lastLink = null;

        // 1) Explicit override
        String override = System.getProperty("gmif.native.path", "");
        if (!override.isBlank()) {
            Path p = Path.of(override).toAbsolutePath();
            tried.add(p.toString());
            if (Files.isRegularFile(p)) {
                System.load(p.toString());
                return;
            }
        }

        // 2) java.library.path (run-gui.ps1 sets this to build\lib)
        try {
            System.loadLibrary("mushroomfinder");
            return;
        } catch (UnsatisfiedLinkError e) {
            lastLink = e;
            tried.add("System.loadLibrary(mushroomfinder) via java.library.path="
                    + System.getProperty("java.library.path", ""));
        }

        // 3) Preferred local paths — newest layout FIRST
        String[] rel = {
                "build/lib/" + libName,
                "build/" + libName,
                libName,
                "build/native/" + libName,
        };
        for (String r : rel) {
            Path p = Path.of(r).toAbsolutePath();
            tried.add(p.toString());
            if (Files.isRegularFile(p)) {
                try {
                    System.load(p.toString());
                    return;
                } catch (UnsatisfiedLinkError e) {
                    lastLink = e;
                    // try next candidate
                }
            }
        }

        // 4) Extract from classpath resource (fat jar)
        try (InputStream in = NativeLoader.class.getResourceAsStream(resource)) {
            if (in != null) {
                Path tmp = Files.createTempFile("gmif-native", extOf(libName));
                tmp.toFile().deleteOnExit();
                Files.copy(in, tmp, StandardCopyOption.REPLACE_EXISTING);
                System.load(tmp.toString());
                return;
            }
        }

        throw new IOException(
                "Native library mushroomfinder not loaded.\n"
                + "Tried:\n  " + String.join("\n  ", tried)
                + (lastLink != null ? "\nLast link error: " + lastLink.getMessage() : "")
                + "\nRun .\\build.ps1 and start via .\\run-gui.ps1");
    }

    private static String extOf(String name) {
        int i = name.lastIndexOf('.');
        return i >= 0 ? name.substring(i) : ".bin";
    }
}
