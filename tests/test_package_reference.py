from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "package_reference.py"
SPEC = importlib.util.spec_from_file_location("cx_package_reference", MODULE_PATH)
assert SPEC and SPEC.loader
reference_package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(reference_package)


class PackageReferenceTests(unittest.TestCase):
    def test_release_reference_pins_every_guide_to_its_tag(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "reference.html"
            source.write_text(
                '<!doctype html><html><head><style>body{color:white}</style></head>'
                '<body><section class="hero"><h1>Language & Runtime Reference</h1></section>'
                '<a href="index.html">Docs home</a><a href="syntax.html">Syntax</a>'
                '<a href="engine-settings.html">Settings</a><a href="world-and-entities.html">World</a>'
                '<script>const entries = [];</script></body></html>',
                encoding="utf-8",
            )
            output = reference_package.build_reference(
                source, root / "dist", "0.10.14-preview.6", "v0.10.14-preview.6", "Decitle/CX"
            )
            data = output.read_text(encoding="utf-8")
            self.assertEqual(output.name, "0_CX-Docs-v0.10.14-preview.6.html")
            self.assertIn("https://github.com/Decitle/CX/tree/v0.10.14-preview.6/docs", data)
            self.assertIn("https://github.com/Decitle/CX/blob/v0.10.14-preview.6/docs/syntax.md", data)
            self.assertIn("https://github.com/Decitle/CX/blob/v0.10.14-preview.6/docs/engine-settings.md", data)
            self.assertIn("https://github.com/Decitle/CX/blob/v0.10.14-preview.6/docs/world-and-entities.md", data)
            self.assertIn("Version-matched reference", data)
            self.assertIn("<style>", data)
            self.assertIn("const entries", data)
            self.assertNotIn('href="syntax.html"', data)
            self.assertNotIn('href="index.html"', data)

    def test_rejects_non_version_tag_and_wrong_reference(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "reference.html"
            source.write_text("<html><body>not the reference</body></html>", encoding="utf-8")
            with self.assertRaises(ValueError):
                reference_package.build_reference(source, root / "dist", "x", "not-a-tag", "Decitle/CX")
            with self.assertRaises(RuntimeError):
                reference_package.build_reference(source, root / "dist", "0.10.14", "v0.10.14", "Decitle/CX")


if __name__ == "__main__":
    unittest.main()
