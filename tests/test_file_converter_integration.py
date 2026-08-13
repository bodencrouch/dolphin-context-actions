from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def run_cli(*args: str) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["PYTHONPATH"] = str(ROOT / "src")
    return subprocess.run(
        [sys.executable, "-m", "dolphin_context_actions", *args],
        cwd=ROOT,
        env=env,
        capture_output=True,
        text=True,
        check=False,
    )


def test_lists_general_file_conversions_from_bundled_registry() -> None:
    result = run_cli("--list-file-conversions")

    assert result.returncode == 0, result.stderr
    assert "pdf-to-md\tTo Markdown" in result.stdout
    assert "csv-to-json\tTo JSON" in result.stdout
    assert "png-to-jpg\tTo JPEG" in result.stdout


def test_general_conversion_round_trip_uses_unified_cli(tmp_path: Path) -> None:
    source = tmp_path / "people.csv"
    source.write_text("name,age\nAda,36\nBob,40\n", encoding="utf-8")

    to_json = run_cli("--file-convert", "csv-to-json", str(source))
    assert to_json.returncode == 0, to_json.stderr
    json_path = tmp_path / "people.json"
    assert json_path.exists()

    source.unlink()
    to_csv = run_cli("--file-convert", "json-to-csv", str(json_path))
    assert to_csv.returncode == 0, to_csv.stderr
    assert "Ada,36" in (tmp_path / "people.csv").read_text(encoding="utf-8")


def test_general_conversion_failure_sets_nonzero_exit_status(tmp_path: Path) -> None:
    source = tmp_path / "people.csv"
    source.write_text("name\nAda\n", encoding="utf-8")

    result = run_cli("--file-convert", "missing-conversion", str(source))

    assert result.returncode != 0


def test_menu_install_stages_media_and_general_conversion_actions(tmp_path: Path) -> None:
    service_dir = tmp_path / "services"
    config_dir = tmp_path / "config"
    result = subprocess.run(
        [
            "make",
            "install-menus-only",
            f"SERVICEDIR={service_dir}",
            f"CONFIGDIR={config_dir}",
            f"CONVERTERBIN={tmp_path / 'bin' / 'dolphin-context-actions'}",
        ],
        cwd=ROOT,
        env={**os.environ, "PYTHONPATH": str(ROOT / "src")},
        capture_output=True,
        text=True,
        check=False,
    )

    assert result.returncode == 0, result.stderr
    assert (service_dir / "dolphin-context-actions.desktop").exists()
    generated = sorted(service_dir.glob("dolphin-context-actions-convert-*.desktop"))
    assert generated
    assert any("--file-convert csv-to-json" in path.read_text() for path in generated)
    assert (config_dir / "conversions.yaml").exists()
