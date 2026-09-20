from __future__ import annotations

from unittest import mock

from dolphin_context_actions import file_converter, ui


def test_kdialog_does_not_exec_when_headless(monkeypatch) -> None:
    monkeypatch.setenv("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1")
    with mock.patch("dolphin_context_actions.ui.subprocess.run") as run:
        result = ui.kdialog("--title", "Dolphin Context Actions", "--error", "nope")
    run.assert_not_called()
    assert result.returncode == 1


def test_notify_does_not_exec_when_headless(monkeypatch) -> None:
    monkeypatch.setenv("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1")
    with mock.patch("dolphin_context_actions.ui.subprocess.Popen") as popen:
        ui.notify("title", "msg")
    popen.assert_not_called()


def test_unknown_conversion_error_stays_headless(monkeypatch) -> None:
    monkeypatch.setenv("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1")
    with mock.patch("dolphin_context_actions.ui.subprocess.run") as run:
        code = file_converter.run_convert("missing-conversion", [], overwrite=False)
    run.assert_not_called()
    assert code == 1


def test_gui_blocked_explicit_flag(monkeypatch) -> None:
    monkeypatch.delenv("PYTEST_CURRENT_TEST", raising=False)
    monkeypatch.delenv("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", raising=False)
    assert not ui.gui_blocked()
    monkeypatch.setenv("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1")
    assert ui.gui_blocked()
