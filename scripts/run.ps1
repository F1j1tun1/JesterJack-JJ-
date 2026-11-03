param([switch]$Release)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $projectRoot
try {
    cmake --preset desktop
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    $buildConfig = if ($Release) { 'Release' } else { 'Debug' }
    cmake --build build/desktop --config $buildConfig --parallel 1
    if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
    ctest --test-dir build/desktop -C $buildConfig --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Game rules tests failed.' }
    $gamePath = Join-Path $projectRoot "build\desktop\$buildConfig\blackjack_forge.exe"
    if (-not (Test-Path -LiteralPath $gamePath)) {
        $gamePath = Join-Path $projectRoot 'build\desktop\blackjack_forge.exe'
    }
    & $gamePath
    if ($LASTEXITCODE -ne 0) { throw 'Game exited with an error.' }
} finally {
    Pop-Location
}
