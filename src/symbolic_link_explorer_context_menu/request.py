"""Safe JSON request serialization used across the native/Python boundary."""
import json
from pathlib import Path
from typing import TextIO

from .models import LinkRequest


def write_request(request: LinkRequest, path: Path) -> None:
    """Write one UTF-8 JSON request atomically enough for a new worker."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(request.to_json_object(), stream, ensure_ascii=False, separators=(",", ":"))
        stream.write("\n")


def read_request(stream: TextIO) -> LinkRequest:
    """Decode and validate a request from an open text stream."""
    return LinkRequest.from_json_object(json.load(stream))


def read_request_file(path: Path) -> LinkRequest:
    with path.open("r", encoding="utf-8") as stream:
        return read_request(stream)
