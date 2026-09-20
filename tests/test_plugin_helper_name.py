import re
import unittest
from pathlib import Path

try:
    import tomllib
except ModuleNotFoundError:  # Python < 3.11
    import tomli as tomllib

REPO_ROOT = Path(__file__).resolve().parent.parent
PLUGIN_SOURCES = (
    REPO_ROOT / "kio-plugin" / "dolphinlinkfileitemaction.cpp",
    REPO_ROOT / "kio-plugin" / "dolphinarkfileitemaction.cpp",
)
PYPROJECT = REPO_ROOT / "pyproject.toml"


class PluginHelperNameTest(unittest.TestCase):
    """The KIO plugins launch the CLI by name, so a rename must not drift.

    When the console script was renamed the installed plugin kept calling the old
    name, so every "Drop Link As" action silently did nothing.
    """

    def test_helper_name_matches_console_script(self):
        with PYPROJECT.open("rb") as handle:
            scripts = tomllib.load(handle)["project"]["scripts"]

        for source_path in PLUGIN_SOURCES:
            with self.subTest(plugin=source_path.name):
                source = source_path.read_text()
                match = re.search(
                    r'helperName\(\)\s*\{\s*return QStringLiteral\("([^"]+)"\);',
                    source,
                )
                self.assertIsNotNone(
                    match, f"helperName() literal not found in {source_path.name}"
                )
                self.assertIn(match.group(1), scripts)


if __name__ == "__main__":
    unittest.main()
