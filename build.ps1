# Giant Mushroom Island Finder — build script (Windows PowerShell)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path','User') + ';' +
            $env:Path

function Test-Jdk($path) {
    return ($path -and (Test-Path (Join-Path $path 'include\jni.h')))
}

# Prefer an explicit JAVA_HOME, then the JDK that owns javac on PATH, then
# common install locations. Never assume a specific machine's JDK path.
if (-not (Test-Jdk $env:JAVA_HOME)) {
    $javac = Get-Command javac -ErrorAction SilentlyContinue
    if ($javac) {
        $fromJavac = Split-Path -Parent (Split-Path -Parent $javac.Source)
        if (Test-Jdk $fromJavac) { $env:JAVA_HOME = $fromJavac }
    }
}
if (-not (Test-Jdk $env:JAVA_HOME)) {
    $candidates = Get-ChildItem "C:\Program Files\Java" -Directory -ErrorAction SilentlyContinue |
                  Sort-Object Name -Descending |
                  ForEach-Object { $_.FullName }
    foreach ($c in $candidates) {
        if (Test-Jdk $c) { $env:JAVA_HOME = $c; break }
    }
}
if (-not (Test-Jdk $env:JAVA_HOME)) {
    throw "No JDK with include\jni.h found. Set JAVA_HOME to your JDK."
}

Write-Host "JAVA_HOME=$env:JAVA_HOME"
$JDK_BIN = Join-Path $env:JAVA_HOME 'bin'
$JAVAC = Join-Path $JDK_BIN 'javac.exe'
$JAR   = Join-Path $JDK_BIN 'jar.exe'
$JAVA  = Join-Path $JDK_BIN 'java.exe'
foreach ($exe in @($JAVAC, $JAR, $JAVA)) {
    if (-not (Test-Path $exe)) { throw "Missing JDK tool: $exe" }
}
$gcc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $gcc) { throw "gcc not found on PATH (install WinLibs via winget)" }
Write-Host "gcc=$($gcc.Source)"
$make = Get-Command mingw32-make -ErrorAction SilentlyContinue
if (-not $make) { $make = Get-Command make -ErrorAction SilentlyContinue }
if (-not $make) { throw "make / mingw32-make not found on PATH" }
Write-Host "make=$($make.Source)"

New-Item -ItemType Directory -Force -Path build, build\classes | Out-Null
Get-ChildItem "build\*Finder-*.jar", "build\*MushroomFinder-*.jar" -ErrorAction SilentlyContinue |
    Remove-Item -Force -ErrorAction SilentlyContinue

Write-Host "`n== Native CLI ==" -ForegroundColor Cyan
& $make.Source -f Makefile CC=gcc all
if ($LASTEXITCODE -ne 0) { throw "CLI build failed" }

Write-Host "`n== Native JNI DLL (gmif.dll) ==" -ForegroundColor Cyan
& $make.Source -f Makefile CC=gcc "JAVA_HOME=$($env:JAVA_HOME)" native
if ($LASTEXITCODE -ne 0) { throw "DLL build failed" }

# Embed copy for JAR packaging
$srcDir = "src\main\resources\native\windows-x86_64"
New-Item -ItemType Directory -Force -Path $srcDir | Out-Null
Copy-Item "build\gmif.dll" "$srcDir\gmif.dll" -Force
Write-Host "DLL -> $srcDir\gmif.dll"

Write-Host "`n== Java ==" -ForegroundColor Cyan
$sources = Get-ChildItem "src\main\java\dev\sakuhime\mushroomfinder\*.java" |
           ForEach-Object { $_.FullName }
& $JAVAC -encoding UTF-8 -d build\classes $sources
if ($LASTEXITCODE -ne 0) { throw "javac failed" }

& $JAR cfe build\LargeBiomeFinder-1.2.0.jar dev.sakuhime.mushroomfinder.Main `
    -C build\classes . -C src\main\resources .
if ($LASTEXITCODE -ne 0) { throw "jar failed" }

Write-Host "`n== Smoke: CLI seed 262 ==" -ForegroundColor Cyan
& .\build\gmif_cli.exe --seed 262 --version 1.18 --radius 500 --min-area 1000 --refine
if ($LASTEXITCODE -ne 0) { throw "CLI smoke failed" }

Write-Host "`n== Smoke: scan plan ==" -ForegroundColor Cyan
& .\build\gmif_cli.exe --seed 262 --version 1.18 --radius 5000 --describe
if ($LASTEXITCODE -ne 0) { throw "Scan plan smoke failed" }
& .\build\gmif_cli.exe --max-radius
if ($LASTEXITCODE -ne 0) { throw "Max radius smoke failed" }

Write-Host "`n== Smoke: Java --self-test (fat JAR) ==" -ForegroundColor Cyan
& $JAVA '--enable-native-access=ALL-UNNAMED' `
      '-jar' 'build\LargeBiomeFinder-1.2.0.jar' '--self-test'
if ($LASTEXITCODE -ne 0) { throw "Java self-test failed" }

Write-Host "`nBuild OK" -ForegroundColor Green
Write-Host "  build\gmif_cli.exe"
Write-Host "  build\gmif.dll  (also embedded in JAR)"
Write-Host "  build\LargeBiomeFinder-1.2.0.jar"
Write-Host "`nRun GUI:  java -jar build\LargeBiomeFinder-1.2.0.jar"
