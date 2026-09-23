# Launch Giant Mushroom Island Finder GUI
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path','User')

$jar = Join-Path $root "build\GiantMushroomFinder.jar"
if (-not (Test-Path $jar)) {
    Write-Host "Jar not found. Run .\build.ps1 first." -ForegroundColor Yellow
    exit 1
}

# Prefer the freshest DLL. build\lib is updated by build.ps1;
# build\ root may hold a stale copy locked by another process.
$libDir = "build"
if (Test-Path "build\lib\mushroomfinder.dll") { $libDir = "build\lib" }

# Force load order: java.library.path first (build\lib), then override path.
& java '--enable-native-access=ALL-UNNAMED' `
       '-cp' "build\classes;build\GiantMushroomFinder.jar" `
       "-Djava.library.path=$libDir" `
       "-Dgmif.native.path=build\lib\mushroomfinder.dll" `
       'dev.sakuhime.mushroomfinder.Main'
