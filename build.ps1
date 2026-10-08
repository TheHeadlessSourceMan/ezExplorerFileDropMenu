[CmdletBinding()]
param(
    [string]$Python = "python",
    [string]$OsToolsPath = "D:\git\osTools"
)
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
if (-not (Test-Path (Join-Path $OsToolsPath "ln.py"))) { throw "osTools ln.py not found: $OsToolsPath" }
Remove-Item $artifact -Recurse -Force -ErrorAction SilentlyContinue
New-Item $artifact -ItemType Directory -Force | Out-Null
Copy-Item (Join-Path $build "Release\SymbolicLinkExplorerContextMenu.dll") $artifact
& $Python -m pip install pyinstaller
if ($LASTEXITCODE -ne 0) { throw "PyInstaller installation failed" }
$env:PYTHONPATH = "$root\src;$([System.IO.Path]::GetDirectoryName($OsToolsPath))"
& $Python -m PyInstaller --noconfirm --clean --onefile --name worker --distpath $artifact (Join-Path $root "src\symbolic_link_explorer_context_menu\worker.py")
if ($LASTEXITCODE -ne 0) { throw "Worker packaging failed" }
Write-Host "Release artifact: $artifact"
