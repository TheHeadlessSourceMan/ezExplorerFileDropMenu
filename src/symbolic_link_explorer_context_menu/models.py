"""Typed request and result models for the isolated worker."""
from dataclasses import dataclass
from pathlib import Path
from typing import Any


@dataclass(frozen=True)
class LinkRequest:
    """A filesystem-only right-drag request."""

    sources: tuple[Path, ...]
    destination: Path
    key_state: int = 0

    @classmethod
    def from_json_object(cls, value: Any) -> "LinkRequest":
        if not isinstance(value, dict):
            raise ValueError("request must be a JSON object")
        raw_sources = value.get("sources")
        destination = value.get("destination")
        if not isinstance(raw_sources, list) or not raw_sources:
            raise ValueError("sources must be a non-empty list")
        if not all(isinstance(item, str) and item for item in raw_sources):
            raise ValueError("sources must contain non-empty strings")
        if not isinstance(destination, str) or not destination:
            raise ValueError("destination must be a non-empty string")
        key_state = value.get("key_state", 0)
        if not isinstance(key_state, int):
            raise ValueError("key_state must be an integer")
        return cls(tuple(Path(item) for item in raw_sources), Path(destination), key_state)

    def to_json_object(self) -> dict[str, object]:
        return {
            "sources": [str(item) for item in self.sources],
            "destination": str(self.destination),
            "key_state": self.key_state,
        }


@dataclass(frozen=True)
class LinkResult:
    source: Path
    link: Path
    error: str | None = None
