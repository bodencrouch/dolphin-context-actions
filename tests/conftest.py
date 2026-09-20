"""Keep every test from opening kdialog / notify-send on the developer desktop."""

from __future__ import annotations

import os


def pytest_configure() -> None:
    os.environ["DOLPHIN_CONTEXT_ACTIONS_HEADLESS"] = "1"
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
