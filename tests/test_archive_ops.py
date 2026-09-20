#!/usr/bin/env python3
"""Naming and 7z-backed behaviour for the Archive context menu."""

from __future__ import annotations

import shutil
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from dolphin_context_actions import archive_ops as ark


class NamingTests(unittest.TestCase):
    def test_needs_extract_matches_7zip_exclude_list(self) -> None:
        self.assertFalse(ark.needs_extract("notes.txt"))
        self.assertFalse(ark.needs_extract("clip.mp4"))
        self.assertFalse(ark.needs_extract("Photo.JPG"))
        self.assertTrue(ark.needs_extract("payload.zip"))
        self.assertTrue(ark.needs_extract("payload.7z"))
        self.assertTrue(ark.needs_extract("payload.tar.gz"))
        self.assertTrue(ark.needs_extract("README"))
        self.assertTrue(ark.needs_extract("weird.notalist"))

    def test_extract_subfolder_strips_last_extension(self) -> None:
        self.assertEqual(ark.extract_subfolder_name("photos.zip"), "photos")
        self.assertEqual(ark.extract_subfolder_name("photos.tar.gz"), "photos.tar")
        self.assertEqual(ark.extract_subfolder_name("noext"), "noext~")
        self.assertEqual(ark.extract_subfolder_name("archive.7z.001"), "archive")
        self.assertEqual(ark.extract_subfolder_name("vol.part1.rar"), "vol")
        self.assertEqual(ark.extract_subfolder_name("vol.part01.rar"), "vol")
        self.assertEqual(ark.extract_subfolder_name("vol.part001.rar"), "vol")

    def test_create_archive_name_single_file(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            txt = root / "letter.txt"
            txt.write_text("hi", encoding="utf-8")
            tgz = root / "bundle.tar.gz"
            tgz.write_text("x", encoding="utf-8")
            self.assertEqual(ark.create_archive_name([txt]), "letter")
            self.assertEqual(ark.create_archive_name([tgz]), "bundle.tar.gz")

    def test_create_archive_name_directory_keeps_dots(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            folder = Path(raw) / "v1.2"
            folder.mkdir()
            self.assertEqual(ark.create_archive_name([folder]), "v1.2")

    def test_create_archive_name_multiple_uses_parent_folder(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            parent = Path(raw) / "Project"
            parent.mkdir()
            a = parent / "a.txt"
            b = parent / "b.txt"
            a.write_text("a", encoding="utf-8")
            b.write_text("b", encoding="utf-8")
            self.assertEqual(ark.create_archive_name([a, b]), "Project")

    def test_create_archive_name_numbers_when_7z_already_selected(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            archive = Path(raw) / "letter.7z"
            archive.write_bytes(b"PK")
            self.assertEqual(ark.create_archive_name([archive]), "letter_2")

    def test_hash_sidecar_keeps_filename(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            txt = Path(raw) / "letter.txt"
            txt.write_text("hi", encoding="utf-8")
            self.assertEqual(ark.create_archive_name([txt], is_hash=True), "letter.txt")

    def test_quoted_reduced_escapes_ampersand_and_shortens(self) -> None:
        self.assertEqual(ark.quoted_reduced("A & B"), '"A && B"')
        long = "a" * 80
        reduced = ark.quoted_reduced(long)
        self.assertTrue(reduced.startswith('"'))
        self.assertIn(" ... ", reduced)
        self.assertEqual(len(reduced), 2 + 32 + 5 + 32)

    def test_selection_wants_extract(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            archive = root / "a.zip"
            text = root / "a.txt"
            folder = root / "dir"
            archive.write_bytes(b"PK\x05\x06" + b"\x00" * 18)
            text.write_text("x", encoding="utf-8")
            folder.mkdir()
            self.assertTrue(ark.selection_wants_extract([archive]))
            self.assertFalse(ark.selection_wants_extract([text]))
            self.assertFalse(ark.selection_wants_extract([archive, text]))
            self.assertFalse(ark.selection_wants_extract([folder]))


@unittest.skipUnless(shutil.which("7z") or shutil.which("7za"), "7z not installed")
class SevenZipOperationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)

    def tearDown(self) -> None:
        self.tempdir.cleanup()

    def _zip_with_folder(self, name: str) -> Path:
        archive = self.root / name
        inner = self.root / "payload"
        inner.mkdir()
        (inner / "readme.txt").write_text("hello", encoding="utf-8")
        with zipfile.ZipFile(archive, "w") as zf:
            zf.write(inner / "readme.txt", arcname="payload/readme.txt")
        shutil.rmtree(inner)
        return archive

    def test_extract_here_keeps_archive_layout(self) -> None:
        archive = self._zip_with_folder("payload.zip")
        with mock.patch.object(ark.ui, "notify"):
            ark.extract_here([archive])
        self.assertTrue((self.root / "payload" / "readme.txt").is_file())
        self.assertEqual((self.root / "payload" / "readme.txt").read_text(), "hello")

    def test_extract_to_uses_spe_so_root_folder_is_not_duplicated(self) -> None:
        archive = self._zip_with_folder("payload.zip")
        with mock.patch.object(ark.ui, "notify"):
            ark.extract_to([archive])
        self.assertTrue((self.root / "payload" / "readme.txt").is_file())
        self.assertFalse((self.root / "payload" / "payload" / "readme.txt").exists())

    def test_compress_to_zip_round_trip(self) -> None:
        source = self.root / "letter.txt"
        source.write_text("hi", encoding="utf-8")
        with mock.patch.object(ark.ui, "notify"):
            ark.compress_to_zip([source])
        created = self.root / "letter.zip"
        self.assertTrue(created.is_file())
        with zipfile.ZipFile(created) as zf:
            self.assertEqual(zf.read("letter.txt"), b"hi")

    def test_hash_sha256_mentions_the_file(self) -> None:
        source = self.root / "letter.txt"
        source.write_text("hello\n", encoding="utf-8")
        with mock.patch.object(ark, "_show_text") as shown:
            ark.hash_files([source], "sha256")
        shown.assert_called_once()
        title, body = shown.call_args.args
        self.assertIn("SHA256", title)
        self.assertIn("letter.txt", body)


if __name__ == "__main__":
    unittest.main()
