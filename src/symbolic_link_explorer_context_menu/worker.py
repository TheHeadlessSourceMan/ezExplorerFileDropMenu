"""Isolated worker entry point; Explorer never imports this module."""
import argparse
import logging
import sys
from pathlib import Path

from osTools.ln import ln

from symbolic_link_explorer_context_menu.linker import create_links
from symbolic_link_explorer_context_menu.request import read_request_file


def configure_logging(log_path: Path) -> None:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    logging.basicConfig(filename=log_path, encoding="utf-8", level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")


def run(request_path: Path, log_path: Path) -> int:
    configure_logging(log_path)
    try:
        request = read_request_file(request_path)
        results = create_links(request, ln)
    except (OSError, ValueError, TypeError, ImportError) as error:
        logging.exception("Request failed: %s", error)
        return 1
    failures = sum(result.error is not None for result in results)
    logging.info("Processed %d source(s), %d failure(s)", len(results), failures)
    return 1 if failures else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Process a symbolic-link drag request")
    parser.add_argument("request", type=Path)
    parser.add_argument("--log", type=Path, required=True)
    args = parser.parse_args(argv)
    return run(args.request, args.log)


if __name__ == "__main__":
    sys.exit(main())
