# Manual Explorer testing

These tests require 64-bit Windows 11, a built release artifact, and a disposable test directory. Explorer integration has not been claimed until these steps pass on the target machine.

1. Install with `powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1 -ArtifactDirectory .\artifacts\release`.
2. Create `source`, `destination`, and `outside` directories. Put a file in `source` and a nested directory in it.
3. Right-drag one file from `source` onto `destination`; choose **Create symbolic links here** (from the sample `menu.json`). Confirm a link named after the source exists in `destination` and points to the source.
4. Repeat with several files.
5. Repeat with one directory.
6. Repeat with a mixed file/directory selection.
7. Repeat on the same volume and across two volumes.
8. Repeat with spaces and non-ASCII names.
9. Repeat with a path supported by the system's long-path policy.
10. Start a drop and choose Cancel; confirm no link is created.
11. Edit `%LOCALAPPDATA%\ezExplorerFileDropMenu\menu.json` to add a second entry; confirm it appears on the next right-drag without restarting Explorer, with its icon. Confirm `{file}` runs once per item and `{files}` once for all items.
12. Break the JSON on purpose; confirm no entries appear and `logs\handler.log` explains why. Run `scripts\diagnostics.ps1`; confirm the CLSID and registration paths.
13. Run `powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1`; confirm the custom command is absent after Explorer refresh and no unrelated registry values were removed.
14. Reinstall and repeat step 3.

Record Windows build, Explorer presentation, source/destination volume, and whether the target is elevated. The command is intentionally separate from ordinary right-click context menus.
