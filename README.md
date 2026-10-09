# ezExplorerFileDropMenu

A Windows Explorer shell extension that adds user-defined commands to the documented right-button drag-and-drop menu. Entries (name, icon, command line) are read from a JSON file, so any number of commands can be added without rebuilding.

> This is a right-drag command, not an ordinary right-click context-menu verb. It appears in Explorer's Move here / Copy here / Create shortcuts here menu when the destination is a folder.

## Architecture

A small 64-bit native COM DLL implements Microsoft's `IContextMenu` and `IShellExtInit` drag-and-drop handler contract. It extracts filesystem paths, reads `%LOCALAPPDATA%\ezExplorerFileDropMenu\menu.json` each time the menu is shown, and launches the selected entry's command line with `CreateProcessW`. No scripting runtime is loaded into `explorer.exe`.

The exact feasibility research, registry contract, interface choices, and limitations are in [docs/technical-design.md](docs/technical-design.md).

## Configuration

`menu.json` is an array of objects; each becomes one menu entry, in order. See [menu.sample.json](menu.sample.json).

```json
[
  {
    "name": "Create symbolic links here",
    "icon": "%SystemRoot%\\System32\\shell32.dll,29",
    "cmdline": "powershell -NoProfile -Command \"$f='{file}'; New-Item -ItemType SymbolicLink -Path (Join-Path '{targetDir}' (Split-Path -Leaf $f)) -Target $f\""
  }
]
```

- `name`: menu text. `cmdline`: command to run. Both are required.
- `icon`: optional `.ico` path, or `path,index` for an exe/dll. Environment variables are expanded.
- `{file}`: the command runs once per dragged item.
- `{files}`: the command runs once with all items, space-separated.
- `{targetDir}`: the drop folder (also the working directory).

Substituted paths are quoted automatically; do not add quotes around tokens. Commands are started directly, not through a shell, so use `cmd /c ...` for built-ins. Creating symlinks requires Developer Mode or elevation.

## Requirements

- 64-bit Windows 11; Windows 10 is expected to use the same documented handler contract but is not certified here.
- Visual Studio C++ x64 tools and CMake for building the native DLL.

## Build

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The build script configures a 64-bit Release DLL. It supports the older CMake shipped with Visual Studio 2017 as well as current CMake releases. The resulting distributable is written under `artifacts\release`. A compiler is needed only on the developer/build machine.

GitHub Actions builds every commit and pull request on `windows-latest`; the release directory is uploaded as a workflow artifact.

## Install and verify

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1 -ArtifactDirectory .\artifacts\release
powershell -ExecutionPolicy Bypass -File .\scripts\diagnostics.ps1
```

The per-user installer writes HKCU registration, validates the 64-bit architecture, copies the DLL below `%LOCALAPPDATA%\ezExplorerFileDropMenu`, seeds `menu.json` from the sample if none exists, and calls `SHChangeNotify`. It does not kill or restart Explorer.

Use the complete workflow in [docs/manual-testing.md](docs/manual-testing.md). Explorer integration is not claimed merely because the package builds.

## Diagnostics and logging

Diagnostics are read-only (`scripts\diagnostics.ps1`). Invalid `menu.json` content and process start failures are logged to `%LOCALAPPDATA%\ezExplorerFileDropMenu\logs\handler.log`. Output of the launched commands is not captured.

## Uninstall and upgrade

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1
```

Reinstall by running `install.ps1` again with a newly built artifact. Uninstallation removes only this project's HKCU values and files; your `menu.json` is kept.

## Known limitations and security

Only filesystem drag data represented as `CF_HDROP` is accepted. Namespace-only Shell items are rejected. Command lines of 32,767 characters or more (for example `{files}` with very many long paths) are refused and logged.

`menu.json` can run arbitrary commands as the current user; keep it writable only by you. The native handler runs in Explorer and must stay tiny: it performs no directory traversal or file operations. The native build must match Explorer's 64-bit process architecture.

## Testing and release

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The manual Explorer plan covers single, multiple, directory, mixed, volume, permissions, Unicode, long paths, cancellation, multiple entries, uninstall, and reinstall cases. Build artifacts should be tested on a clean Windows 11 account before release.
