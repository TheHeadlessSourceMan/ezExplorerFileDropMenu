[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ArtifactDirectory)
$ErrorActionPreference = "Stop"
if (-not [Environment]::Is64BitOperatingSystem) { throw "64-bit Windows is required" }
$dll = Join-Path (Resolve-Path $ArtifactDirectory) "SymbolicLinkExplorerContextMenu.dll"
$worker = Join-Path (Resolve-Path $ArtifactDirectory) "worker.exe"
if (-not (Test-Path $dll) -or -not (Test-Path $worker)) { throw "Artifact directory must contain the native DLL and worker.exe" }
$install = Join-Path $env:LOCALAPPDATA "SymbolicLinkExplorerContextMenu"
New-Item $install -ItemType Directory -Force | Out-Null
New-Item (Join-Path $install "logs") -ItemType Directory -Force | Out-Null
Copy-Item $dll (Join-Path $install "SymbolicLinkExplorerContextMenu.dll") -Force
Copy-Item $worker (Join-Path $install "worker.exe") -Force
$clsid = "{6d4d8ef0-3e69-4f5d-8d5a-1f8cb0f2b9c4}"
$base = "HKCU:\Software\Classes\CLSID\$clsid"
New-Item $base -Force | Out-Null
Set-ItemProperty $base -Name '(default)' -Value 'Symbolic Link Explorer Context Menu'
$inproc = Join-Path $base 'InProcServer32'
New-Item $inproc -Force | Out-Null
Set-ItemProperty $inproc -Name '(default)' -Value (Join-Path $install 'SymbolicLinkExplorerContextMenu.dll')
Set-ItemProperty $inproc -Name ThreadingModel -Value Apartment
foreach ($kind in @('Directory','Folder')) {
    $key = "HKCU:\Software\Classes\$kind\shellex\DragDropHandlers\SymbolicLinkExplorerContextMenu"
    New-Item $key -Force | Out-Null
    Set-ItemProperty $key -Name '(default)' -Value $clsid
}
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ShellNotify { [DllImport("shell32.dll")] public static extern void SHChangeNotify(uint e, uint f, IntPtr a, IntPtr b); }
"@
[ShellNotify]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
Write-Host "Installed for the current user at $install"
