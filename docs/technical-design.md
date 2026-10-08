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
   - `HKCU\\Software\\Classes\\Directory\\shellex\\DragDropHandlers\\SymbolicLinkExplorerContextMenu` default `{CLSID}`.
   - `HKCU\\Software\\Classes\\Folder\\shellex\\DragDropHandlers\\SymbolicLinkExplorerContextMenu` default `{CLSID}` as a compatibility registration for Shell folder types.
10. **Registration scope:** HKCU's `Software\\Classes` merge is sufficient for a per-user installation. The DLL is 64-bit and is registered in the 64-bit user Shell view. No machine-wide elevation is required.

## Architecture

```text
Explorer.exe (64-bit)
  -> native COM DLL, Apartment, minimal IContextMenu/IShellExtInit
       -> extracts CF_HDROP paths and destination PIDL
       -> appends "Create symbolic links here"
       -> on InvokeCommand starts worker with a UTF-16 JSON request

isolated Python worker process
  -> validates paths and destination
  -> calls osTools.ln(source, destination / source.name)
  -> writes diagnostics/logs outside Explorer
```

The native DLL never imports Python and never performs link creation. `InvokeCommand` only serializes the already-captured paths and starts the worker, so failures are contained outside Explorer and the Shell callback remains lightweight.

## COM interfaces

The native class implements:

- `IUnknown`: lifetime and interface discovery.
- `IShellExtInit`: receives the destination PIDL and dragged `IDataObject`.
- `IContextMenu`: contributes one command and starts the worker.

`IDataObject` is queried for `CF_HDROP` with `TYMED_HGLOBAL`. This project intentionally supports filesystem items represented by `CF_HDROP`: files, directories, mixed selections, spaces, Unicode, and multiple items. Namespace-only Shell items without filesystem paths are rejected with a diagnostic rather than guessed. `ReleaseStgMedium` releases the acquired storage medium.

`IPersistFile` is not implemented because Microsoft assigns it to drop handlers; it is not required by the documented drag-and-drop handler contract.

## Action semantics

For each dragged source `S`, the worker calls the existing `osTools.ln(S, D / S.name)`, where `D` is the destination directory supplied by Explorer. Existing non-symlink destinations fail rather than being overwritten. Existing symlinks follow `osTools.ln()` behavior. The operation is best-effort per item and records every success/failure in the log. No files are moved or copied.

The worker receives a JSON file in a per-user application data directory rather than untrusted command-line path concatenation. The native shim passes only a fixed worker executable path and a generated request filename using `CreateProcessW` with a correctly quoted command line.

## Registration and notification

The installer writes only the keys owned by this application under `HKCU\\Software\\Classes`, records the installed DLL and worker paths, and removes only those owned values during uninstall. It calls `SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr)` after registration changes. Explorer is not killed or restarted by normal installation.

The 64-bit DLL must be loaded by 64-bit Explorer. A 32-bit build would be invisible to the 64-bit Shell view and is not supported by this first release.

## Threading and containment

The CLSID is registered with `ThreadingModel=Apartment`, matching Microsoft's Shell extension registration guidance. COM methods return HRESULTs and contain native exceptions. `QueryContextMenu` does no filesystem traversal. `InvokeCommand` copies the captured values, starts the worker, and returns. The worker catches per-item failures and logs to `%LOCALAPPDATA%\\SymbolicLinkExplorerContextMenu\\logs`.

## Installation lifecycle

`install.ps1` validates 64-bit Windows and Python/dependencies, builds or locates the native DLL and worker, copies production files below `%LOCALAPPDATA%\\SymbolicLinkExplorerContextMenu`, writes HKCU registration, calls Shell notification, and prints the registry and artifact paths. It is idempotent. `uninstall.ps1` removes the owned registration and installation directory after a best-effort Shell notification. `diagnostics.ps1` and `python -m symbolic_link_explorer_context_menu diagnostics` are read-only.

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
