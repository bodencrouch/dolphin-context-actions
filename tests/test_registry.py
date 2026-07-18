#!/usr/bin/env python3
"""Smoke tests for registry parsing and conversion matching."""

from __future__ import annotations

import importlib.util
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


class RegistryTests(unittest.TestCase):
    def test_parse_bundled_registry(self) -> None:
        data = converter.load_yaml(ROOT / "config" / "conversions.yaml")
        conversions = converter.parse_registry(data)
        ids = {c.id for c in conversions}
        self.assertIn("pdf-to-md", ids)
        self.assertIn("yaml-to-json", ids)

    def test_pdf_match(self) -> None:
        data = converter.load_yaml(ROOT / "config" / "conversions.yaml")
        conv = next(c for c in converter.parse_registry(data) if c.id == "pdf-to-md")
        with tempfile.NamedTemporaryFile(suffix=".pdf") as tmp:
            self.assertTrue(converter.matches_conversion(Path(tmp.name), conv))


if __name__ == "__main__":
    unittest.main()
