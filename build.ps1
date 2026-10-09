[CmdletBinding()]
param()
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build"
$artifact = Join-Path $root "artifacts\release"
$cmake = (Get-Command cmake -ErrorAction Stop).Source
$native = Join-Path $root "native"
New-Item $build -ItemType Directory -Force | Out-Null
Push-Location $build
try {
    & $cmake $native -A x64
    $configureExitCode = $LASTEXITCODE
    if ($configureExitCode -ne 0) { throw "CMake configure failed" }
    & $cmake --build . --config Release
    $buildExitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
if ($buildExitCode -ne 0) { throw "Native build failed" }
Remove-Item $artifact -Recurse -Force -ErrorAction SilentlyContinue
New-Item $artifact -ItemType Directory -Force | Out-Null
Copy-Item (Join-Path $build "Release\ezExplorerFileDropMenu.dll") $artifact
Copy-Item (Join-Path $root "menu.sample.json") $artifact
Write-Host "Release artifact: $artifact"
