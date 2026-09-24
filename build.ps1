# Giant Mushroom Island Finder — build script (Windows PowerShell)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path','User')

if (-not $env:JAVA_HOME) {
    $candidates = @(
        "C:\Program Files\Java\jdk-26.0.1",
        "C:\Program Files\Java\jdk-21"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $env:JAVA_HOME = $c; break }
    }
}
if (-not $env:JAVA_HOME) {
    throw "JAVA_HOME not set and no JDK found"
}

Write-Host "JAVA_HOME=$env:JAVA_HOME"
$gcc = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $gcc) { throw "gcc not found on PATH (install WinLibs via winget)" }
Write-Host "gcc=$($gcc.Source)"

New-Item -ItemType Directory -Force -Path build, build\classes | Out-Null

Write-Host "`n== Native CLI ==" -ForegroundColor Cyan
mingw32-make -f Makefile CC=gcc all
if ($LASTEXITCODE -ne 0) { throw "CLI build failed" }

Write-Host "`n== Native JNI DLL (gmif.dll) ==" -ForegroundColor Cyan
mingw32-make -f Makefile CC=gcc native
if ($LASTEXITCODE -ne 0) { throw "DLL build failed" }

# Embed copy for JAR packaging
$srcDir = "src\main\resources\native\windows-x86_64"
New-Item -ItemType Directory -Force -Path $srcDir | Out-Null
Copy-Item "build\gmif.dll" "$srcDir\gmif.dll" -Force
Write-Host "DLL -> $srcDir\gmif.dll"

Write-Host "`n== Java ==" -ForegroundColor Cyan
$sources = Get-ChildItem "src\main\java\dev\sakuhime\mushroomfinder\*.java" |
           ForEach-Object { $_.FullName }
javac -encoding UTF-8 -d build\classes $sources
if ($LASTEXITCODE -ne 0) { throw "javac failed" }

jar cfe build\GiantMushroomFinder-1.0.0.jar dev.sakuhime.mushroomfinder.Main `
    -C build\classes . -C src\main\resources .
if ($LASTEXITCODE -ne 0) { throw "jar failed" }

Write-Host "`n== Smoke: CLI seed 262 ==" -ForegroundColor Cyan
& .\build\gmif_cli.exe --seed 262 --version 1.18 --radius 500 --min-area 1000 --refine
if ($LASTEXITCODE -ne 0) { throw "CLI smoke failed" }

Write-Host "`n== Smoke: Java --self-test (fat JAR) ==" -ForegroundColor Cyan
& java '--enable-native-access=ALL-UNNAMED' `
      '-jar' 'build\GiantMushroomFinder-1.0.0.jar' '--self-test'
if ($LASTEXITCODE -ne 0) { throw "Java self-test failed" }

Write-Host "`nBuild OK" -ForegroundColor Green
Write-Host "  build\gmif_cli.exe"
Write-Host "  build\gmif.dll  (also embedded in JAR)"
Write-Host "  build\GiantMushroomFinder-1.0.0.jar"
Write-Host "`nRun GUI:  java -jar build\GiantMushroomFinder-1.0.0.jar"
