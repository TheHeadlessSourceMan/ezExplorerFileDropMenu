$ErrorActionPreference = "Stop"
$install = Join-Path $env:LOCALAPPDATA "ezExplorerFileDropMenu"
Write-Host "architecture: $([Environment]::Is64BitOperatingSystem)"
Write-Host "install: $install"
Write-Host "dll: $(Test-Path (Join-Path $install 'ezExplorerFileDropMenu.dll'))"
Write-Host "menu.json: $(Test-Path (Join-Path $install 'menu.json'))"
reg.exe query "HKCU\Software\Classes\CLSID\{6d4d8ef0-3e69-4f5d-8d5a-1f8cb0f2b9c4}\InProcServer32"
foreach ($kind in @('Directory','Folder','Directory\Background','Drive')) {
    reg.exe query "HKCU\Software\Classes\$kind\shellex\DragDropHandlers\ezExplorerFileDropMenu"
}
