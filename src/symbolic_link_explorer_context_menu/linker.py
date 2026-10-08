"""Business operation for creating links from a worker process."""
import logging
from pathlib import Path
from typing import Callable

from .models import LinkRequest, LinkResult

LinkFunction = Callable[[Path, Path], None]


def create_links(request: LinkRequest, link_function: LinkFunction) -> tuple[LinkResult, ...]:
    """Create one link named after each source in the destination directory."""
    results: list[LinkResult] = []
    for source in request.sources:
        link_path = request.destination / source.name
        try:
            link_function(source, link_path)
        except (OSError, RuntimeError, ValueError) as error:
            logging.getLogger(__name__).exception("Unable to link %s", source)
            results.append(LinkResult(source, link_path, str(error)))
        else:
            results.append(LinkResult(source, link_path))
    return tuple(results)
