#!/usr/bin/env python3
"""Smoke tests for registry parsing and conversion matching."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from dolphin_context_actions import file_converter as converter

EXPECTED_IDS = {
    "pdf-to-md",
    "docx-to-pdf",
    "odt-to-pdf",
    "ods-to-pdf",
    "pptx-to-pdf",
    "xlsx-to-pdf",
    "rtf-to-pdf",
    "txt-to-pdf",
    "md-to-html",
    "html-to-md",
    "md-to-pdf",
    "yaml-to-json",
    "json-to-yaml",
    "csv-to-json",
    "json-to-csv",
    "tsv-to-csv",
    "toml-to-json",
    "json-to-toml",
    "png-to-jpg",
    "jpg-to-png",
    "webp-to-png",
    "webp-to-jpg",
    "png-to-webp",
    "heic-to-jpg",
}


class RegistryTests(unittest.TestCase):
    def test_parse_bundled_registry(self) -> None:
        data = converter.load_yaml(converter.BUNDLED_REGISTRY)
        conversions = converter.parse_registry(data)
        ids = {c.id for c in conversions}
        self.assertEqual(ids, EXPECTED_IDS)

    def test_pdf_match(self) -> None:
        data = converter.load_yaml(converter.BUNDLED_REGISTRY)
        conv = next(c for c in converter.parse_registry(data) if c.id == "pdf-to-md")
        with tempfile.NamedTemporaryFile(suffix=".pdf") as tmp:
            self.assertTrue(converter.matches_conversion(Path(tmp.name), conv))


if __name__ == "__main__":
    unittest.main()
