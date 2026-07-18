#!/usr/bin/env python3
"""Engine tests for dolphin-file-converter."""

from __future__ import annotations

import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "scripts" / "dolphin-file-converter.py"

spec = importlib.util.spec_from_file_location("converter", SCRIPT)
assert spec and spec.loader
converter = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = converter
spec.loader.exec_module(converter)


class EngineTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)

    def tearDown(self) -> None:
        self.tempdir.cleanup()

    def test_csv_json_round_trip(self) -> None:
        source = self.root / "sample.csv"
        target = self.root / "sample.json"
        source.write_text("name,age\nAda,36\nBob,40\n", encoding="utf-8")
        converter.convert_csv_json(source, target)
        data = json.loads(target.read_text(encoding="utf-8"))
        self.assertEqual(len(data), 2)
        self.assertEqual(data[0]["name"], "Ada")

        csv_out = self.root / "sample-out.csv"
        converter.convert_csv_json(target, csv_out)
        self.assertIn("Ada", csv_out.read_text(encoding="utf-8"))

    def test_csv_json_rejects_non_array_json(self) -> None:
        source = self.root / "bad.json"
        target = self.root / "bad.csv"
        source.write_text('{"name": "Ada"}', encoding="utf-8")
        with self.assertRaises(RuntimeError):
            converter.convert_csv_json(source, target)

    def test_tsv_to_csv(self) -> None:
        source = self.root / "sample.tsv"
        target = self.root / "sample.csv"
        source.write_text("name\tage\nAda\t36\n", encoding="utf-8")
        converter.convert_csv_json(source, target)
        self.assertIn("Ada,36", target.read_text(encoding="utf-8"))

    def test_toml_json_to_json(self) -> None:
        source = self.root / "sample.toml"
        target = self.root / "sample.json"
        source.write_text('title = "Hello"\ncount = 2\n', encoding="utf-8")
        converter.convert_toml_json(source, target)
        data = json.loads(target.read_text(encoding="utf-8"))
        self.assertEqual(data["title"], "Hello")
        self.assertEqual(data["count"], 2)

    @unittest.skipUnless(
        __import__("importlib").util.find_spec("tomli_w") is not None,
        "tomli_w not installed",
    )
    def test_toml_json_json_to_toml(self) -> None:
        source = self.root / "sample.json"
        target = self.root / "sample.toml"
        source.write_text('{"title": "Hello", "count": 2}', encoding="utf-8")
        converter.convert_toml_json(source, target)
        self.assertIn("title", target.read_text(encoding="utf-8"))

    @unittest.skipUnless(shutil.which("pandoc"), "pandoc not installed")
    def test_pandoc_md_to_html(self) -> None:
        source = self.root / "sample.md"
        target = self.root / "sample.html"
        source.write_text("# Hello\n", encoding="utf-8")
        converter.convert_pandoc(source, target)
        self.assertIn("Hello", target.read_text(encoding="utf-8"))

    @unittest.skipUnless(
        shutil.which("magick") or shutil.which("convert"),
        "ImageMagick not installed",
    )
    def test_imagemagick_png_to_jpg(self) -> None:
        source = self.root / "pixel.png"
        target = self.root / "pixel.jpg"
        cmd = converter.find_imagemagick()
        assert cmd is not None
        create = subprocess.run(
            [*cmd, "-size", "1x1", "xc:red", str(source)],
            capture_output=True,
            text=True,
        )
        if create.returncode != 0:
            self.skipTest("ImageMagick could not create a test PNG")
        converter.convert_imagemagick(source, target)
        self.assertTrue(target.exists())
        self.assertGreater(target.stat().st_size, 0)

    def test_imagemagick_missing_binary(self) -> None:
        original = converter.find_imagemagick
        converter.find_imagemagick = lambda: None  # type: ignore[method-assign]
        try:
            source = self.root / "pixel.png"
            target = self.root / "pixel.jpg"
            source.write_bytes(b"not-a-real-image")
            with self.assertRaises(RuntimeError):
                converter.convert_imagemagick(source, target)
        finally:
            converter.find_imagemagick = original  # type: ignore[method-assign]


if __name__ == "__main__":
    unittest.main()
