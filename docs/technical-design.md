# Technical design

## Decision summary

The requested command belongs to Explorer's **right-button drag-and-drop shortcut menu**, not the ordinary file context menu. Microsoft documents this exact extension point as a **drag-and-drop handler**:

- Explorer creates the menu after a Shell object is dragged with the right mouse button.
- A drag-and-drop handler is an `IContextMenu` handler registered below `Directory\\shellex\\DragDropHandlers`.
- Explorer calls `IShellExtInit::Initialize` with the destination folder PIDL and the dragged `IDataObject`.
- The handler contributes a command through `IContextMenu::QueryContextMenu` and receives the selected command through `InvokeCommand`.

This is a supported way to append a command to the existing Move here / Copy here / Create shortcuts here / Cancel menu. It is not the same contract as a destination `IDropTarget`.

## Feasibility answers

1. **Menu owner:** Explorer's Shell drag-and-drop UI owns the right-drag menu. The documented extension surface is the drag-and-drop handler described in [Creating Shortcut Menu Handlers](https://learn.microsoft.com/en-us/windows/win32/shell/context-menu-handlers).
2. **Append support:** Yes. Microsoft explicitly says a drag-and-drop handler can add items to this shortcut menu.
3. **Interface and registration:** Implement `IContextMenu` and `IShellExtInit`; register the CLSID below `Directory\\shellex\\DragDropHandlers`. The handler name's default value is the CLSID.
4. **Closest alternative if unavailable:** An ordinary `Directory\\shell\\...` verb would be a normal context-menu command and would not receive the right-drag destination. This project does not substitute that behavior.
5. **pywin32 activation:** pywin32 supports Python COM classes and local servers, but the Shell extension contract is an in-process COM handler. A Python class registered as `LocalServer32` is not the documented activation model for this handler.
6. **In-process requirement:** Microsoft documents Shell extension handlers as in-process DLLs exporting `DllGetClassObject` and `DllCanUnloadNow`.
7. **Can pywin32 provide it safely:** Not as a supported pure-Python in-process DLL for Explorer. Loading a full Python runtime into Explorer would also violate the stability requirement.
8. **Windows 10/11:** The documented legacy drag-and-drop handler contract remains the relevant desktop Shell extension point. Windows 11's modern context-menu presentation does not turn this into a normal `shell` verb; the handler is still associated with the right-drag menu. Actual presentation must be verified on the target build.
9. **Registry keys:**
   - `HKCU\\Software\\Classes\\CLSID\\{CLSID}` default display name.
   - `HKCU\\Software\\Classes\\CLSID\\{CLSID}\\InProcServer32` default path to the native DLL and `ThreadingModel=Apartment`.
   - `HKCU\\Software\\Classes\\Directory\\shellex\\DragDropHandlers\\ezExplorerFileDropMenu` default `{CLSID}`.
   - `HKCU\\Software\\Classes\\Folder\\shellex\\DragDropHandlers\\ezExplorerFileDropMenu` default `{CLSID}` as a compatibility registration for Shell folder types.
10. **Registration scope:** HKCU's `Software\\Classes` merge is sufficient for a per-user installation. The DLL is 64-bit and is registered in the 64-bit user Shell view. No machine-wide elevation is required.

## Architecture

```text
Explorer.exe (64-bit)
  -> native COM DLL, Apartment, minimal IContextMenu/IShellExtInit
       -> extracts CF_HDROP paths and destination PIDL
       -> reads %LOCALAPPDATA%\ezExplorerFileDropMenu\menu.json on each menu display
       -> appends one menu item (name + icon) per entry
       -> on InvokeCommand expands tokens and starts the command with CreateProcessW
```

The native DLL never loads a scripting runtime and performs no file operations itself. `InvokeCommand` only expands the entry's command line and starts a child process, so work happens outside Explorer.

## COM interfaces

The native class implements:

- `IUnknown`: lifetime and interface discovery.
- `IShellExtInit`: receives the destination PIDL and dragged `IDataObject`.
- `IContextMenu`: contributes one command per `menu.json` entry and launches its command line.

`IDataObject` is queried for `CF_HDROP` with `TYMED_HGLOBAL`. This project intentionally supports filesystem items represented by `CF_HDROP`: files, directories, mixed selections, spaces, Unicode, and multiple items. Namespace-only Shell items without filesystem paths are rejected with a diagnostic rather than guessed. `ReleaseStgMedium` releases the acquired storage medium.

`IPersistFile` is not implemented because Microsoft assigns it to drop handlers; it is not required by the documented drag-and-drop handler contract.

## Action semantics

`menu.json` is an array of `{"name", "icon", "cmdline"}` objects; each becomes a menu entry in array order. `icon` is optional and may be an `.ico` path or `path,index` for an exe/dll; environment variables are expanded in `icon` and `cmdline`.

- `{file}` in `cmdline`: the command is started once per dragged item.
- `{files}`: the command is started once with all items, separated by spaces.
- `{targetDir}`: the drop destination folder.

Each substituted path is quoted per `CommandLineToArgvW` rules, so do not add quotes around a token; if a token is already inside quotes in the template, it is inserted escaped without extra quotes. Commands run through `CreateProcessW` (no implicit shell; use `cmd /c` for built-ins) with `targetDir` as working directory and no console window. Command lines of 32,767 characters or more are refused. Failures to parse the file or start a process are logged to `logs\handler.log`.

The configuration is an arbitrary-command launcher, so it lives in the per-user profile and should only be writable by that user.

## Registration and notification

The installer writes only the keys owned by this application under `HKCU\\Software\\Classes`, records the installed DLL and worker paths, and removes only those owned values during uninstall. It calls `SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr)` after registration changes. Explorer is not killed or restarted by normal installation.

The 64-bit DLL must be loaded by 64-bit Explorer. A 32-bit build would be invisible to the 64-bit Shell view and is not supported by this first release.

## Threading and containment

The CLSID is registered with `ThreadingModel=Apartment`, matching Microsoft's Shell extension registration guidance. COM methods return HRESULTs and contain native exceptions. `QueryContextMenu` does no filesystem traversal. `QueryContextMenu` reads only the small `menu.json`. `InvokeCommand` starts the child processes and returns; failures are logged to `%LOCALAPPDATA%\\ezExplorerFileDropMenu\\logs`.

## Installation lifecycle

`install.ps1` validates 64-bit Windows, copies the DLL below `%LOCALAPPDATA%\\ezExplorerFileDropMenu`, seeds `menu.json` from `menu.sample.json` only if absent, writes HKCU registration, and calls Shell notification. It is idempotent. `uninstall.ps1` removes the owned registration and installed files but keeps the user's `menu.json`. `diagnostics.ps1` is read-only.

## Alternatives considered

- **`Directory\\shell` or `*\\shell`:** ordinary context-menu verbs; they do not receive the right-drag destination and would implement the wrong UX.
- **`DropHandler` / `IDropTarget`:** makes a destination object handle drops and negotiates `DROPEFFECT`; it does not append a command to Explorer's existing right-drag menu.
- **`DragDropHandlers` with `IDropTarget`:** wrong interface. Microsoft documents this registration as an `IContextMenu` drag-and-drop handler.
- **Pure pywin32 in Explorer:** rejected because the supported pywin32 server path is out-of-process and loading Python into Explorer is unnecessarily fragile.
- **Replacing Explorer's drop target or injecting UI:** unsupported and unsafe.

## Sources

- [Creating Shortcut Menu Handlers](https://learn.microsoft.com/en-us/windows/win32/shell/context-menu-handlers)
- [Creating Shell Extension Handlers](https://learn.microsoft.com/en-us/windows/win32/shell/handlers)
- [IDropTarget](https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nn-oleidl-idroptarget)
- [IDataObject](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nn-objidl-idataobject)
- [IPersistFile](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nn-objidl-ipersistfile)
- [IShellExtInit](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ishellextinit)
- [pywin32 `localserver.py`](https://github.com/mhammond/pywin32/blob/main/com/win32com/server/localserver.py)

## Verification status

The Shell contract and pywin32 activation boundary were researched from the sources above. Explorer integration is not claimed until the built DLL is installed and manually exercised on Windows 11 using the procedure in `docs/manual-testing.md`.
