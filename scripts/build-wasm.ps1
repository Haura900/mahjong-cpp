param(
    [string]$BuildDirectory = "build-wasm",
    [string]$BoostIncludeDirectory = "C:\vcpkg\installed\x64-windows\include"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repoRoot $BuildDirectory

emcmake cmake -S $repoRoot -B $buildPath `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUILD_SERVER=OFF `
    -DBUILD_TEST=OFF `
    -DBUILD_SAMPLES=OFF `
    -DBUILD_TOOLS=OFF `
    -DBUILD_WASM=ON `
    "-DBOOST_INCLUDE_DIR=$BoostIncludeDirectory"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $buildPath --config Release --target mahjong-wasm
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Get-ChildItem -LiteralPath $buildPath -Filter "mahjong*" |
    Select-Object Name, Length
