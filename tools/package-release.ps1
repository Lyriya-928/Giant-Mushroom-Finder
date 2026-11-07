# Package Windows release assets and release notes for a version tag.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\package-release.ps1 -Tag v1.1.0
#
# Requires a completed build (run .\build.ps1 or .\tools\ci-windows.ps1 first).
# Outputs, all under build\release\:
#   LargeBiomeFinder-<version>-windows-x64.zip   everything below, zipped
#   assets.txt                                   asset paths for `gh release create`
#   release-notes.md                             release body
#   SHA256SUMS.txt                               checksums
param(
    [Parameter(Mandatory = $true)][string]$Tag
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$version = (Get-Content -Raw VERSION).Trim()
$tagVersion = $Tag.TrimStart('v')
if ($tagVersion -ne $version) {
    throw "Tag '$Tag' does not match VERSION '$version'; bump VERSION first."
}

$jar = "build\LargeBiomeFinder-$version.jar"
foreach ($path in @($jar, 'build\gmif_cli.exe', 'build\gmif.dll')) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing build output: $path (run .\build.ps1 first)"
    }
}

$dist = "build\release"
if (Test-Path -LiteralPath $dist) { Remove-Item -LiteralPath $dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dist | Out-Null

$stageName = "LargeBiomeFinder-$version-windows-x64"
$stage = Join-Path $dist $stageName
New-Item -ItemType Directory -Force -Path $stage | Out-Null
foreach ($file in @($jar, 'build\gmif_cli.exe', 'build\gmif.dll')) {
    Copy-Item -LiteralPath $file -Destination $stage -Force
}
foreach ($doc in @('README.md', 'README.zh-CN.md', 'PERFORMANCE.md', 'LICENSE', 'NOTICE')) {
    if (Test-Path -LiteralPath $doc) { Copy-Item -LiteralPath $doc -Destination $stage -Force }
}

$zip = Join-Path $dist "$stageName.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force

$assets = @($zip, $jar, 'build\gmif_cli.exe', 'build\gmif.dll')

$hashLines = foreach ($path in $assets) {
    $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    "{0}  {1}" -f $hash, (Split-Path -Leaf $path)
}
$hashLines | Set-Content -Path (Join-Path $dist 'SHA256SUMS.txt') -Encoding ASCII

# Publish the checksum file alongside the binaries it describes.
$assets += Join-Path $dist 'SHA256SUMS.txt'
$assets | Set-Content -Path (Join-Path $dist 'assets.txt') -Encoding ASCII

function Last-Match([string]$path, [string]$pattern, [string]$fallback) {
    if (Test-Path -LiteralPath $path) {
        $hit = Select-String -LiteralPath $path -Pattern $pattern | Select-Object -Last 1
        if ($hit) { return $hit.Line.Trim() }
    }
    return $fallback
}

$audit = Last-Match 'build\ci-audit.log' 'RELEASE AUDIT:' 'not recorded in this build'
$selfTest = Last-Match 'build\ci-selftest.log' 'SELF-TEST PASS' 'not recorded in this build'
$maxRadius = '799984'
if (Test-Path -LiteralPath 'build\gmif_cli.exe') {
    $reported = (& .\build\gmif_cli.exe --max-radius | Select-Object -Last 1)
    if ($reported) { $maxRadius = $reported.Trim() }
}

$notes = @"
Windows x64 build of **Large Biome Finder $version**, produced by GitHub Actions
on a `windows-latest` runner with Temurin JDK 21 and MinGW-w64 GCC.

### Download

| File | Purpose |
| --- | --- |
| ``$stageName.zip`` | JAR, CLI and DLL together, plus docs |
| ``LargeBiomeFinder-$version.jar`` | GUI / CLI JAR (DLL embedded, extracted on first run) |
| ``gmif_cli.exe`` | Standalone native CLI |
| ``gmif.dll`` | JNI library, for callers that want it directly |

Verify with ``SHA256SUMS.txt`` (``Get-FileHash -Algorithm SHA256``).

### Run

``````powershell
java -jar LargeBiomeFinder-$version.jar
gmif_cli.exe --help
gmif_cli.exe --max-radius
``````

Native maximum radius at scale 16: **$maxRadius** blocks.

### Verified in this build

* Native audit: $audit
* Java self-test: $selfTest
* Same build/test commands as the Windows CI workflow (``tools/ci-windows.ps1``)

See ``PERFORMANCE.md`` in the repository for measured bottlenecks and scan-mode details.
"@

# UTF-8 without BOM: a BOM would leak into the rendered release body.
[System.IO.File]::WriteAllText(
    (Join-Path $dist 'release-notes.md'),
    $notes,
    (New-Object System.Text.UTF8Encoding($false)))

Write-Host "Packaged $Tag"
Get-ChildItem -LiteralPath $dist -File | ForEach-Object {
    "  {0}  ({1:N0} bytes)" -f $_.Name, $_.Length
}
