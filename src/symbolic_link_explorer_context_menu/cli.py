"""Command-line diagnostics and worker dispatch."""
import argparse
import platform
import sys
from pathlib import Path

from . import __version__
from .registry import CLSID, Registration


def diagnostics() -> int:
    registration = Registration(str(Path(__file__).resolve().parent / "native" / "SymbolicLinkExplorerContextMenu.dll"))
    print(f"version: {__version__}")
    print(f"architecture: {platform.machine()}")
    print(f"python: {sys.version.split()[0]}")
    print(f"expected_clsid: {{{CLSID}}}")
    print(f"inproc_key: {registration.inproc_key}")
    for key in registration.drag_handler_keys:
        print(f"drag_handler_key: {key}")
    print(f"package: {Path(__file__).resolve().parent}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("diagnostics")
    args = parser.parse_args(argv)
    if args.command == "diagnostics":
        return diagnostics()
    return 2
