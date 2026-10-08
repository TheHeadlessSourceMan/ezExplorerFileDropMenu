import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock

from symbolic_link_explorer_context_menu.linker import create_links
from symbolic_link_explorer_context_menu.models import LinkRequest
from symbolic_link_explorer_context_menu.registry import HANDLER_NAME, Registration
from symbolic_link_explorer_context_menu.request import read_request_file, write_request


class CoreTests(unittest.TestCase):
    def test_request_round_trip_preserves_unicode_and_spaces(self):
        request = LinkRequest((Path("C:/one file/é.txt"), Path("C:/two")), Path("D:/destination folder"), 8)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "request.json"
            write_request(request, path)
            self.assertEqual(request, read_request_file(path))
            self.assertIn("é", path.read_text(encoding="utf-8"))

    def test_multiple_sources_are_named_in_destination(self):
        request = LinkRequest((Path("C:/a.txt"), Path("C:/folder")), Path("D:/target"))
        link = Mock()
        results = create_links(request, link)
        self.assertEqual(2, link.call_count)
        link.assert_any_call(Path("C:/a.txt"), Path("D:/target/a.txt"))
        link.assert_any_call(Path("C:/folder"), Path("D:/target/folder"))
        self.assertTrue(all(item.error is None for item in results))

    def test_one_failure_does_not_hide_other_items(self):
        request = LinkRequest((Path("C:/bad"), Path("C:/good")), Path("D:/target"))
        def link(source, destination):
            if source.name == "bad":
                raise OSError("collision")
        results = create_links(request, link)
        self.assertEqual("collision", results[0].error)
        self.assertIsNone(results[1].error)

    def test_registry_keys_are_stable(self):
        registration = Registration("C:/installed/handler.dll")
        self.assertIn(HANDLER_NAME, registration.drag_handler_keys[0])
        self.assertTrue(registration.inproc_key.endswith("InProcServer32"))

    def test_json_is_structured_not_shell_quoted(self):
        request = LinkRequest((Path('C:/a"b.txt'),), Path("D:/target"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "request.json"
            write_request(request, path)
            self.assertEqual(str(Path('C:/a"b.txt')), json.loads(path.read_text(encoding="utf-8"))["sources"][0])


if __name__ == "__main__":
    unittest.main()
