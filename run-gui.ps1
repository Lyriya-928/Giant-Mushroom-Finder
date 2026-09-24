# Launch Giant Mushroom Island Finder GUI from the fat JAR.
# The JAR embeds gmif.dll and extracts it next to the JAR on first run.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path','User')

$jar = Join-Path $root "build\GiantMushroomFinder-1.0.0.jar"
if (-not (Test-Path $jar)) {
    $found = Get-ChildItem "build\GiantMushroomFinder*.jar" -ErrorAction SilentlyContinue |
             Select-Object -First 1 -ExpandProperty FullName
    if ($found) { $jar = $found }
}
if (-not (Test-Path $jar)) {
    Write-Host "Jar not found. Run .\build.ps1 first." -ForegroundColor Yellow
    exit 1
}

& java '--enable-native-access=ALL-UNNAMED' '-jar' $jar
