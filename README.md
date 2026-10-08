# Symbolic Link Explorer Context Menu

A Windows Explorer shell extension that adds **Create symbolic links here** to the documented right-button drag-and-drop menu. It creates filesystem symbolic links for all dragged files and directories by calling the existing `osTools.ln()` function.

> This is a right-drag command, not an ordinary right-click context-menu verb. It appears in Explorer's Move here / Copy here / Create shortcuts here menu when the destination is a folder.

## Architecture

A small 64-bit native COM DLL implements Microsoft's `IContextMenu` and `IShellExtInit` drag-and-drop handler contract. It extracts filesystem paths and launches an isolated Python worker. The worker calls `osTools.ln()` once per source and logs results outside Explorer. Python is deliberately not loaded into `explorer.exe`.

The exact feasibility research, registry contract, interface choices, and limitations are in [docs/technical-design.md](docs/technical-design.md).

## Requirements

- 64-bit Windows 11; Windows 10 is expected to use the same documented handler contract but is not certified here.
- 64-bit CPython 3.11 or newer for development and worker packaging.
- Visual Studio C++ x64 tools and CMake for building the native DLL.
- A checkout of `osTools`, available to the worker build, because the operation uses its `ln()` implementation.

## Developer setup

From a Visual Studio x64 developer PowerShell:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install --upgrade pip setuptools wheel
.\.venv\Scripts\python.exe -m pip install -e .
```

For local development, make the sibling `osTools` checkout importable, for example by setting `PYTHONPATH=D:\git` in the build shell. Do not use an activated virtual environment or checkout-relative path for an installed artifact.

## Build

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The build script configures a 64-bit Release DLL and packages the Python worker. It supports the older CMake shipped with Visual Studio 2017 as well as current CMake releases. The resulting distributable is written under `artifacts\release`. A compiler is needed only on the developer/build machine.

GitHub Actions builds and tests every commit and pull request on `windows-latest`; the release directory is uploaded as a workflow artifact.

## Install and verify

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1 -ArtifactDirectory .\artifacts\release
python -m symbolic_link_explorer_context_menu diagnostics
```

The per-user installer writes HKCU registration, validates the 64-bit architecture, copies artifacts below `%LOCALAPPDATA%\SymbolicLinkExplorerContextMenu`, and calls `SHChangeNotify`. It does not kill or restart Explorer.

Use the complete workflow in [docs/manual-testing.md](docs/manual-testing.md). Explorer integration is not claimed merely because the package builds.

## Diagnostics and logging

Diagnostics are read-only:

```powershell
python -m symbolic_link_explorer_context_menu diagnostics
```

Worker logs are stored at `%LOCALAPPDATA%\SymbolicLinkExplorerContextMenu\logs\worker.log`. Failures are recorded per dragged item. A collision with an existing non-symbolic-link path is reported and does not overwrite it.

## Uninstall and upgrade

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1
```

Reinstall by running `install.ps1` again with a newly built artifact. Uninstallation removes only this project's HKCU values and files.

## Known limitations and security

Only filesystem drag data represented as `CF_HDROP` is accepted. Namespace-only Shell items are rejected. The handler supports multiple files, directories, mixed selections, spaces, Unicode, and system-supported long paths. Link names are the source leaf names, so two sources with the same leaf name may collide.

The native handler runs in Explorer and must stay tiny. It performs no directory traversal, link creation, or Python import. The worker uses a structured UTF-8 JSON request instead of shell-constructed path arguments. The native build must match Explorer's 64-bit process architecture.

## Testing and release

```powershell
$env:PYTHONPATH = 'src'
python -m unittest discover -s tests -v
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

The manual Explorer plan covers single, multiple, directory, mixed, volume, permissions, Unicode, long paths, cancellation, uninstall, and reinstall cases. Build artifacts should be tested on a clean Windows 11 account before release.
