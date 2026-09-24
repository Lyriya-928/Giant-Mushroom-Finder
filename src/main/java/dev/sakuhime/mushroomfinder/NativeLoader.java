package dev.sakuhime.mushroomfinder;

import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.net.URL;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Loads the bundled native library (gmif).
 *
 * Release strategy: the Windows x64 DLL is packaged as a JAR resource at
 * {@code /native/windows-x86_64/gmif.dll}. On first run it is extracted
 * <b>next to the JAR</b> (not %TEMP%) as {@code gmif.dll}. Subsequent runs
 * compare SHA-256 and only rewrite when the resource content changed.
 *
 * Dependency note (Windows x64 MinGW-w64 UCRT build): imports are only
 * KERNEL32.dll and api-ms-win-crt-*.dll — no libgcc / libstdc++ / libwinpthread.
 */
final class NativeLoader {

    /** Canonical extracted file name next to the JAR. */
    static final String LIB_FILE_NAME = "gmif.dll";

    private NativeLoader() {}

    static void load() throws IOException {
        load(null);
    }

    /** @param forcedResourcePath optional override of the classpath resource. */
    static void load(String forcedResourcePath) throws IOException {
        String osName = System.getProperty("os.name", "?");
        String osArch = System.getProperty("os.arch", "?");
        String javaVer = System.getProperty("java.version", "?");

        // 1) Dev / explicit override (absolute path to a DLL).
        String override = System.getProperty("gmif.native.path", "").trim();
        if (!override.isEmpty()) {
            Path p = Path.of(override).toAbsolutePath().normalize();
            if (!Files.isRegularFile(p)) {
                throw fail(javaVer, osName, osArch, p,
                        "gmif.native.path points to a missing file", null);
            }
            try {
                System.load(p.toString());
                return;
            } catch (UnsatisfiedLinkError e) {
                throw fail(javaVer, osName, osArch, p,
                        "System.load failed for gmif.native.path", e);
            }
        }

        String resourcePath = forcedResourcePath != null
                ? forcedResourcePath
                : resourceForPlatform(osName, osArch);
        if (resourcePath == null) {
            throw fail(javaVer, osName, osArch, null,
                    "unsupported platform (need Windows x86_64 / amd64). "
                            + "No bundled native library for this OS/arch.", null);
        }

        // 2) Locate JAR directory (preferred extract target).
        Path jarDir = locateJarDirectory();
        Path target = (jarDir != null ? jarDir : Path.of(".").toAbsolutePath().normalize())
                .resolve(LIB_FILE_NAME);

        byte[] resourceBytes = readResource(resourcePath);
        if (resourceBytes == null) {
            throw fail(javaVer, osName, osArch, target,
                    "JAR resource missing: " + resourcePath
                            + " (rebuild with build.ps1 so the DLL is embedded)", null);
        }

        List<String> notes = new ArrayList<>();
        try {
            ensureExtracted(target, resourceBytes, notes);
        } catch (IOException e) {
            throw fail(javaVer, osName, osArch, target,
                    "failed to extract native library next to the JAR: " + e.getMessage(), e);
        }

        try {
            System.load(target.toAbsolutePath().normalize().toString());
        } catch (UnsatisfiedLinkError e) {
            throw fail(javaVer, osName, osArch, target,
                    "System.load failed after extract. "
                            + "If a dependency DLL is missing, place it next to gmif.dll. "
                            + (notes.isEmpty() ? "" : ("Notes: " + String.join("; ", notes))),
                    e);
        }
    }

    /**
     * Resource path inside the JAR for the current platform.
     * Windows x86_64 → /native/windows-x86_64/gmif.dll
     */
    static String resourceForPlatform(String osName, String osArch) {
        String os = osName == null ? "" : osName.toLowerCase(Locale.ROOT);
        String arch = normalizeArch(osArch);
        if (os.contains("win") && ("x86_64".equals(arch) || "amd64".equals(arch))) {
            return "/native/windows-x86_64/gmif.dll";
        }
        return null;
    }

    private static String normalizeArch(String arch) {
        if (arch == null) return "?";
        String a = arch.toLowerCase(Locale.ROOT);
        if (a.equals("amd64") || a.equals("x86_64") || a.equals("x64")) return "x86_64";
        return a;
    }

    /** Extract if missing or SHA-256 differs; leave untouched when identical. */
    static void ensureExtracted(Path target, byte[] resourceBytes, List<String> notes)
            throws IOException {
        Path abs = target.toAbsolutePath().normalize();
        Path parent = abs.getParent();
        if (parent != null) {
            Files.createDirectories(parent);
        }

        if (Files.isRegularFile(abs)) {
            byte[] existing = Files.readAllBytes(abs);
            if (sha256(existing).equals(sha256(resourceBytes))) {
                if (notes != null) notes.add("gmif.dll up to date (SHA-256 match)");
                return;
            }
            if (notes != null) notes.add("gmif.dll updated (SHA-256 differed)");
        } else {
            if (notes != null) notes.add("gmif.dll extracted from JAR");
        }

        // Write via temp file in the same directory, then atomic-ish move.
        Path tmp = abs.resolveSibling(abs.getFileName() + ".tmp");
        Files.write(tmp, resourceBytes);
        try {
            Files.move(tmp, abs, StandardCopyOption.REPLACE_EXISTING);
        } catch (IOException e) {
            // Another process may have extracted it; compare again.
            try {
                if (Files.isRegularFile(abs)
                        && sha256(Files.readAllBytes(abs)).equals(sha256(resourceBytes))) {
                    Files.deleteIfExists(tmp);
                    return;
                }
            } catch (IOException ignored) {
            }
            Files.deleteIfExists(tmp);
            throw e;
        }
    }

    /** Directory containing the running JAR, or null if not running from a JAR. */
    static Path locateJarDirectory() {
        try {
            URL loc = NativeLoader.class.getProtectionDomain()
                    .getCodeSource().getLocation();
            if (loc == null) return null;
            URI uri = loc.toURI();
            Path p = Path.of(uri);
            if (Files.isRegularFile(p) && p.getFileName().toString().toLowerCase(Locale.ROOT)
                    .endsWith(".jar")) {
                return p.toAbsolutePath().normalize().getParent();
            }
            // Running from classes directory (dev).
            return null;
        } catch (Exception e) {
            return null;
        }
    }

    private static byte[] readResource(String resourcePath) {
        try (InputStream in = NativeLoader.class.getResourceAsStream(resourcePath)) {
            if (in == null) return null;
            return in.readAllBytes();
        } catch (IOException e) {
            return null;
        }
    }

    static String sha256(byte[] data) {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] d = md.digest(data);
            StringBuilder sb = new StringBuilder(d.length * 2);
            for (byte b : d) sb.append(String.format("%02x", b));
            return sb.toString();
        } catch (NoSuchAlgorithmException e) {
            return "";
        }
    }

    private static IOException fail(String javaVer, String osName, String osArch,
                                    Path path, String message, Throwable cause) {
        StringBuilder sb = new StringBuilder();
        sb.append("GMIF native library failed to load.\n");
        sb.append("  reason: ").append(message).append('\n');
        sb.append("  Java: ").append(javaVer).append('\n');
        sb.append("  OS: ").append(osName)
                .append("  arch: ").append(osArch).append('\n');
        sb.append("  DLL path: ")
                .append(path == null ? "(none)" : path.toAbsolutePath().normalize())
                .append('\n');
        sb.append("  Library file: ").append(LIB_FILE_NAME).append('\n');
        if (cause != null) {
            sb.append("  cause: ").append(cause.getClass().getName())
                    .append(": ").append(cause.getMessage()).append('\n');
            Throwable c = cause.getCause();
            if (c != null) {
                sb.append("  root: ").append(c.getClass().getName())
                        .append(": ").append(c.getMessage()).append('\n');
            }
        }
        IOException ex = new IOException(sb.toString().trim());
        if (cause != null) ex.initCause(cause);
        return ex;
    }

    /** Test helper: list files currently next to the JAR (or cwd). */
    static Path extractTargetDir() {
        Path jarDir = locateJarDirectory();
        return jarDir != null ? jarDir : Path.of(".").toAbsolutePath().normalize();
    }

    static boolean deleteExtracted() throws IOException {
        Path t = extractTargetDir().resolve(LIB_FILE_NAME);
        return Files.deleteIfExists(t);
    }
}
