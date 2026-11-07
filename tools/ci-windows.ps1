# Windows CI: build, native audit and Java self-test.
# Run locally with:  powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\ci-windows.ps1
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
            [Environment]::GetEnvironmentVariable('Path', 'User') + ';' +
            $env:Path

New-Item -ItemType Directory -Force -Path build | Out-Null
$failed = $false
$status = "PASSED"
$maxRadius = "unknown"
$javaExe = 'java'
if ($env:JAVA_HOME -and (Test-Path (Join-Path $env:JAVA_HOME 'bin\java.exe'))) {
    $javaExe = Join-Path $env:JAVA_HOME 'bin\java.exe'
}

function Tail-Log([string]$path, [int]$lines = 14) {
    if (Test-Path -LiteralPath $path) {
        return ((Get-Content -LiteralPath $path -Tail $lines) -join "`n")
    }
    return "(log not produced)"
}

try {
    Write-Host "== Build ==" -ForegroundColor Cyan
    & .\build.ps1 | Tee-Object -FilePath build\ci-build.log
    if ($LASTEXITCODE -ne 0) { throw "build.ps1 failed (exit $LASTEXITCODE)" }

    Write-Host "`n== Native audit ==" -ForegroundColor Cyan
    & .\build\gmif_cli.exe --audit | Tee-Object -FilePath build\ci-audit.log
    if ($LASTEXITCODE -ne 0) { throw "native audit failed (exit $LASTEXITCODE)" }

    Write-Host "`n== Java self-test ==" -ForegroundColor Cyan
    & $javaExe '--enable-native-access=ALL-UNNAMED' `
          '-jar' 'build\LargeBiomeFinder-1.2.0.jar' '--self-test' |
        Tee-Object -FilePath build\ci-selftest.log
    if ($LASTEXITCODE -ne 0) { throw "Java self-test failed (exit $LASTEXITCODE)" }

    $maxRadius = (& .\build\gmif_cli.exe --max-radius | Select-Object -Last 1)
    if ($LASTEXITCODE -ne 0 -or [int]$maxRadius -lt 700000) {
        throw "native max radius check failed: $maxRadius"
    }
} catch {
    $failed = $true
    $status = "FAILED"
    Write-Host "CI failure: $_" -ForegroundColor Red
} finally {
    $summary = @"
## Large Biome Finder Windows CI: $status

- Build: build\LargeBiomeFinder-1.2.0.jar
- Native max radius (scale 16): $maxRadius
- Native audit: see ci-audit.log
- Java self-test: see ci-selftest.log

### Build tail
$(Tail-Log 'build\ci-build.log')

### Audit tail
$(Tail-Log 'build\ci-audit.log')

### Self-test tail
$(Tail-Log 'build\ci-selftest.log')
"@
    if ($env:GITHUB_STEP_SUMMARY) {
        $summary | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Encoding utf8 -Append
    }
    Write-Host $summary
}

if ($failed) { exit 1 }
Write-Host "CI PASS" -ForegroundColor Green
