"""Pure registry model used by installers and tests."""
from dataclasses import dataclass
from uuid import UUID

CLSID = UUID("6d4d8ef0-3e69-4f5d-8d5a-1f8cb0f2b9c4")
HANDLER_NAME = "SymbolicLinkExplorerContextMenu"


@dataclass(frozen=True)
class Registration:
    dll_path: str

    @property
    def clsid_key(self) -> str:
        return rf"HKCU\Software\Classes\CLSID\{{{CLSID}}}"

    @property
    def inproc_key(self) -> str:
        return self.clsid_key + r"\InProcServer32"

    @property
    def drag_handler_keys(self) -> tuple[str, ...]:
        return tuple(
            rf"HKCU\Software\Classes\{kind}\shellex\DragDropHandlers\{HANDLER_NAME}"
            for kind in ("Directory", "Folder")
        )
