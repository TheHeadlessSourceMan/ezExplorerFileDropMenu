$ErrorActionPreference = "Stop"
$clsid = "{6d4d8ef0-3e69-4f5d-8d5a-1f8cb0f2b9c4}"
foreach ($kind in @('Directory','Folder')) {
    Remove-Item "HKCU:\Software\Classes\$kind\shellex\DragDropHandlers\SymbolicLinkExplorerContextMenu" -Recurse -Force -ErrorAction SilentlyContinue
}
Remove-Item "HKCU:\Software\Classes\CLSID\$clsid" -Recurse -Force -ErrorAction SilentlyContinue
$install = Join-Path $env:LOCALAPPDATA "SymbolicLinkExplorerContextMenu"
Remove-Item $install -Recurse -Force -ErrorAction SilentlyContinue
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ShellNotify { [DllImport("shell32.dll")] public static extern void SHChangeNotify(uint e, uint f, IntPtr a, IntPtr b); }
"@
[ShellNotify]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
Write-Host "Uninstalled the current-user registration and files"
