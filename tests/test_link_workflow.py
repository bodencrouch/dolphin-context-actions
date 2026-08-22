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
        self.assertFalse(link_ops.source_manager.has_sources())

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

    def test_drop_renames_instead_of_failing_when_the_name_is_taken(self):
        source_dir = self.root / "source"
        target = self.root / "target"
        source_dir.mkdir()
        target.mkdir()
        source = source_dir / "report.tar.gz"
        source.write_text("linked content")
        (target / "report.tar.gz").write_text("something else")

        for expected in ("report.tar - Hardlink.gz", "report.tar - Hardlink (2).gz"):
            link_ops.pick_link_source([str(source)])
            self.assertTrue(link_ops.drop_hardlink(str(target)))
            self.assertEqual(source.stat().st_ino, (target / expected).stat().st_ino)

        for expected in ("report.tar - Symlink.gz", "report.tar - Symlink (2).gz"):
            link_ops.pick_link_source([str(source)])
            self.assertTrue(link_ops.drop_symlink(str(target)))
            self.assertTrue((target / expected).is_symlink())

        # The original the user already had is never touched.
        self.assertEqual("something else", (target / "report.tar.gz").read_text())

    def test_drop_rename_keeps_whole_directory_name(self):
        source = self.root / "source" / "v1.2"
        target = self.root / "target"
        source.mkdir(parents=True)
        target.mkdir()
        (target / "v1.2").mkdir()

        link_ops.pick_link_source([str(source)])
        self.assertTrue(link_ops.drop_symlink(str(target)))
        self.assertTrue((target / "v1.2 - Symlink").is_symlink())

    def test_drop_rename_steps_past_a_dangling_symlink(self):
        source_dir = self.root / "source"
        target = self.root / "target"
        source_dir.mkdir()
        target.mkdir()
        source = source_dir / "note.txt"
        source.write_text("linked content")
        # exists() is False for a broken symlink, so a rename that only checked
        # exists() would pick this name and then fail with EEXIST.
        os.symlink("nowhere", target / "note.txt")

        link_ops.pick_link_source([str(source)])
        self.assertTrue(link_ops.drop_hardlink(str(target)))
        self.assertEqual(source.stat().st_ino, (target / "note - Hardlink.txt").stat().st_ino)

    def test_clone_and_smart_copy_rename_instead_of_failing(self):
        source = self.root / "source" / "project"
        target = self.root / "target"
        source.mkdir(parents=True)
        target.mkdir()
        (source / "file.txt").write_text("clone me")
        (target / "project").mkdir()

        cases = [
            ("hardlink-clone", "project - Hardlink Clone"),
            ("symlink-clone", "project - Symlink Clone"),
            ("smart-copy", "project - Smart Copy"),
        ]
        for drop_type, expected in cases:
            for name in (expected, f"{expected} (2)"):
                link_ops.pick_link_source([str(source)])
                self.assertTrue(link_ops.drop_as(str(target), drop_type), drop_type)
                self.assertTrue((target / name / "file.txt").exists(), name)

        # The folder that was already there keeps its own (empty) contents.
        self.assertEqual([], list((target / "project").iterdir()))

    def test_clone_operations_rename_when_called_directly(self):
        """The rename lives in the operation, so the CLI flags get it too."""
        source = self.root / "source" / "project"
        target = self.root / "target"
        source.mkdir(parents=True)
        target.mkdir()
        (source / "file.txt").write_text("clone me")
        (target / "project").mkdir()

        self.assertTrue(link_ops.hardlink_clone(str(source), str(target / "project")))
        self.assertTrue((target / "project - Hardlink Clone" / "file.txt").exists())
        self.assertTrue(link_ops.symlink_clone(str(source), str(target / "project")))
        self.assertTrue((target / "project - Symlink Clone" / "file.txt").is_symlink())

    def test_cloning_a_folder_into_itself_terminates(self):
        source = self.root / "project"
        source.mkdir()
        (source / "file.txt").write_text("clone me")

        link_ops.pick_link_source([str(source)])
        self.assertTrue(link_ops.drop_as(str(source), "hardlink-clone"))
        clone = source / "project"
        self.assertTrue((clone / "file.txt").exists())
        self.assertFalse((clone / "project").exists())

    def test_drop_clone_uses_destination_folder_and_clears_pick(self):
        source = self.root / "source"
        target = self.root / "target"
        source.mkdir()
        target.mkdir()
        (source / "file.txt").write_text("clone me")
        link_ops.pick_link_source([str(source)])

        self.assertTrue(link_ops.drop_as(str(target), "symlink-clone"))
        self.assertTrue((target / "source" / "file.txt").is_symlink())
        self.assertFalse(link_ops.source_manager.has_sources())


if __name__ == "__main__":
    unittest.main()
