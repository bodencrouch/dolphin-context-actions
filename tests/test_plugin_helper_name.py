import re
import unittest
from pathlib import Path

import tomllib

REPO_ROOT = Path(__file__).resolve().parent.parent
PLUGIN_SOURCE = REPO_ROOT / "kio-plugin" / "dolphinlinkfileitemaction.cpp"
PYPROJECT = REPO_ROOT / "pyproject.toml"


class PluginHelperNameTest(unittest.TestCase):
    """The KIO plugin launches the CLI by name, so a rename must not drift.

    When the console script was renamed the installed plugin kept calling the old
    name, so every "Drop Link As" action silently did nothing.
    """

    def test_helper_name_matches_console_script(self):
        source = PLUGIN_SOURCE.read_text()
        match = re.search(
            r'helperName\(\)\s*\{\s*return QStringLiteral\("([^"]+)"\);', source
        )
        self.assertIsNotNone(match, "helperName() literal not found in plugin source")

        with PYPROJECT.open("rb") as handle:
            scripts = tomllib.load(handle)["project"]["scripts"]

        self.assertIn(match.group(1), scripts)


if __name__ == "__main__":
    unittest.main()
