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

Write-Host "`n== Native JNI DLL ==" -ForegroundColor Cyan
mingw32-make -f Makefile CC=gcc native
if ($LASTEXITCODE -ne 0) { throw "DLL build failed" }

# Always refresh build\lib\mushroomfinder.dll (preferred load path).
New-Item -ItemType Directory -Force -Path build\lib | Out-Null
$dllSrc = "build\mushroomfinder.dll"
if (Test-Path $dllSrc) {
    Copy-Item $dllSrc "build\lib\mushroomfinder.dll" -Force
} else {
    # If build\mushroomfinder.dll is locked, link to lib path directly.
    gcc -shared -o "build\lib\mushroomfinder.dll" `
        "build\mushroom_finder.o" "build\mushroom_finder_JNI.o" "build\libcubiomes.a" `
        -lm -fopenmp
    if ($LASTEXITCODE -ne 0) { throw "DLL link to build\lib failed" }
}
Write-Host "DLL -> build\lib\mushroomfinder.dll"

Write-Host "`n== Java ==" -ForegroundColor Cyan
$sources = Get-ChildItem "src\main\java\dev\sakuhime\mushroomfinder\*.java" |
           ForEach-Object { $_.FullName }
javac -encoding UTF-8 -d build\classes $sources
if ($LASTEXITCODE -ne 0) { throw "javac failed" }

jar cfe build\GiantMushroomFinder.jar dev.sakuhime.mushroomfinder.Main -C build\classes .
if ($LASTEXITCODE -ne 0) { throw "jar failed" }

Write-Host "`n== Smoke: CLI seed 262 ==" -ForegroundColor Cyan
& .\build\gmif_cli.exe --seed 262 --version 1.18 --radius 500 --min-area 1000 --refine
if ($LASTEXITCODE -ne 0) { throw "CLI smoke failed" }

Write-Host "`n== Smoke: Java --self-test ==" -ForegroundColor Cyan
& java '-cp' 'build\classes' '-Djava.library.path=build' `
      'dev.sakuhime.mushroomfinder.Main' '--self-test'
if ($LASTEXITCODE -ne 0) { throw "Java self-test failed" }

Write-Host "`nBuild OK" -ForegroundColor Green
Write-Host "  build\gmif_cli.exe"
Write-Host "  build\mushroomfinder.dll"
Write-Host "  build\GiantMushroomFinder.jar"
Write-Host "`nRun GUI:  .\run-gui.ps1"
