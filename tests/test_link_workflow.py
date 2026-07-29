import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from dolphin_context_actions import cli, link_ops, ui


class LinkWorkflowTest(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)
        self.old_state_file = link_ops._PICKED_SOURCES_FILE
        link_ops._PICKED_SOURCES_FILE = self.root / "picked_sources.json"
        link_ops.source_manager._loaded = False
        link_ops.source_manager._sources = []
        self.ui_patches = [
            mock.patch.object(ui, name)
            for name in ("notify", "error_dialog", "info_dialog", "menu_dialog")
        ]
        self.ui_mocks = [patch.start() for patch in self.ui_patches]

    def tearDown(self):
        for patch in self.ui_patches:
            patch.stop()
        link_ops._PICKED_SOURCES_FILE = self.old_state_file
        link_ops.source_manager._loaded = False
        link_ops.source_manager._sources = []
        self.tempdir.cleanup()

    def test_pick_then_drop_without_menu_dialog(self):
        source = self.root / "source.txt"
        hardlink_dir = self.root / "hardlinks"
        symlink_dir = self.root / "symlinks"
        source.write_text("linked content")
        hardlink_dir.mkdir()
        symlink_dir.mkdir()

        cli.handle_pick_link_source([str(source)])
        self.ui_mocks[3].assert_not_called()
        self.assertTrue(link_ops.drop_hardlink(str(hardlink_dir)))
        self.assertEqual(source.stat().st_ino, (hardlink_dir / source.name).stat().st_ino)
        self.assertFalse(link_ops.has_picked_sources())

        cli.handle_pick_link_source([str(source)])
        self.assertTrue(link_ops.drop_symlink(str(symlink_dir)))
        link = symlink_dir / source.name
        self.assertTrue(link.is_symlink())
        self.assertFalse(os.readlink(link).startswith("/"))

    def test_failed_drop_keeps_source_for_retry(self):
        source = self.root / "source.txt"
        target = self.root / "target"
        source.write_text("linked content")
        target.mkdir()
        link_ops.pick_link_source([str(source)])

        with mock.patch.object(os, "link", side_effect=OSError("test failure")):
            self.assertFalse(link_ops.drop_hardlink(str(target)))

        self.assertEqual(link_ops.source_manager.get(), [str(source.resolve())])

    def test_clone_and_smart_copy_preserve_source_and_links(self):
        source = self.root / "source"
        nested = source / "nested"
        nested.mkdir(parents=True)
        original = nested / "original.txt"
        sibling = nested / "sibling.txt"
        symlink = source / "original-link"
        original.write_text("source stays intact")
        os.link(original, sibling)
        os.symlink("nested/original.txt", symlink)
        original_inode = original.stat().st_ino

        hardlink_clone = self.root / "hardlink-clone"
        self.assertTrue(link_ops.hardlink_clone(str(source), str(hardlink_clone)))
        self.assertEqual(original_inode, (hardlink_clone / "nested" / "original.txt").stat().st_ino)
        self.assertEqual(os.readlink(symlink), os.readlink(hardlink_clone / "original-link"))

        symlink_clone = self.root / "symlink-clone"
        self.assertTrue(link_ops.symlink_clone(str(source), str(symlink_clone)))
        self.assertTrue((symlink_clone / "nested" / "original.txt").is_symlink())
        self.assertEqual(original.resolve(), (symlink_clone / "nested" / "original.txt").resolve())

        copies = self.root / "copies"
        copies.mkdir()
        self.assertTrue(link_ops.smart_copy(str(source), str(copies)))
        copied = copies / source.name
        self.assertEqual(
            (copied / "nested" / "original.txt").stat().st_ino,
            (copied / "nested" / "sibling.txt").stat().st_ino,
        )
        self.assertNotEqual(original_inode, (copied / "nested" / "original.txt").stat().st_ino)
        self.assertEqual("nested/original.txt", os.readlink(copied / "original-link"))

        self.assertEqual("source stays intact", original.read_text())
        self.assertEqual(original_inode, original.stat().st_ino)

    def test_drop_clone_uses_destination_folder_and_clears_pick(self):
        source = self.root / "source"
        target = self.root / "target"
        source.mkdir()
        target.mkdir()
        (source / "file.txt").write_text("clone me")
        link_ops.pick_link_source([str(source)])

        self.assertTrue(link_ops.drop_as(str(target), "symlink-clone"))
        self.assertTrue((target / "source" / "file.txt").is_symlink())
        self.assertFalse(link_ops.has_picked_sources())


if __name__ == "__main__":
    unittest.main()
