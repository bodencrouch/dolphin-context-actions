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
        data = converter.load_registry_file(converter.BUNDLED_REGISTRY)
        conversions = converter.parse_registry(data)
        ids = {c.id for c in conversions}
        self.assertEqual(ids, EXPECTED_IDS)

    def test_json_registry_matches_yaml(self) -> None:
        """The GHNS package ships the registry converted to JSON; the two
        formats must parse to identical conversions."""
        import json

        yaml_data = converter.load_registry_file(converter.BUNDLED_REGISTRY)
        with tempfile.TemporaryDirectory() as tmp:
            json_path = Path(tmp) / "conversions.json"
            json_path.write_text(json.dumps(yaml_data), encoding="utf-8")
            json_conversions = converter.parse_registry(
                converter.load_registry_file(json_path)
            )
        self.assertEqual(
            converter.parse_registry(yaml_data), json_conversions
        )

    def test_json_sibling_is_used_when_pyyaml_is_missing(self) -> None:
        """Simulates the Download-New-Services install, where PyYAML is not
        available and a pre-converted .json sits next to the .yaml name."""
        import builtins
        import json
        from unittest import mock

        yaml_data = converter.load_registry_file(converter.BUNDLED_REGISTRY)
        real_import = builtins.__import__

        def no_yaml(name, *args, **kwargs):
            if name == "yaml":
                raise ImportError("PyYAML absent in GHNS install")
            return real_import(name, *args, **kwargs)

        with tempfile.TemporaryDirectory() as tmp:
            missing_yaml = Path(tmp) / "conversions.yaml"
            (Path(tmp) / "conversions.json").write_text(
                json.dumps(yaml_data), encoding="utf-8"
            )
            with mock.patch.object(builtins, "__import__", side_effect=no_yaml):
                data = converter.load_registry_file(missing_yaml)
        self.assertEqual(
            converter.parse_registry(yaml_data), converter.parse_registry(data)
        )

    def test_pdf_match(self) -> None:
        data = converter.load_registry_file(converter.BUNDLED_REGISTRY)
        conv = next(c for c in converter.parse_registry(data) if c.id == "pdf-to-md")
        with tempfile.NamedTemporaryFile(suffix=".pdf") as tmp:
            self.assertTrue(converter.matches_conversion(Path(tmp.name), conv))


if __name__ == "__main__":
    unittest.main()
