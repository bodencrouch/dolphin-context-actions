#!/usr/bin/env python3
"""KDE/Dolphin registry-driven file converter backend."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

APP_NAME = "File Converter"
PROJECT_ROOT = Path(__file__).resolve().parent.parent

CONFIG_DIR = Path.home() / ".config" / "dolphin-file-converter"
CONFIG_FILE = CONFIG_DIR / "conversions.yaml"
SYSTEM_REGISTRY = Path("/usr/share/kde-file-converter/conversions.yaml")
BUNDLED_REGISTRY = PROJECT_ROOT / "config" / "conversions.yaml"


@dataclass(frozen=True)
class Conversion:
    id: str
    label: str
    source_extensions: tuple[str, ...]
    source_mimetypes: tuple[str, ...]
    target_extension: str
    featured: bool
    requires_commands: tuple[str, ...]
    requires_packages: tuple[str, ...]
    engine: str
    icon: str = "document-convert"


def load_yaml(path: Path) -> dict:
    try:
        import yaml
    except ImportError as exc:
        raise RuntimeError(
            "PyYAML is required. Install with: pip install PyYAML"
        ) from exc
    return yaml.safe_load(path.read_text(encoding="utf-8")) or {}


def registry_path() -> Path:
    for candidate in (CONFIG_FILE, SYSTEM_REGISTRY, BUNDLED_REGISTRY):
        if candidate.exists():
            return candidate
    raise FileNotFoundError(
        "Conversion registry not found. Expected one of:\n"
        f"  {CONFIG_FILE}\n"
        f"  {SYSTEM_REGISTRY}\n"
        f"  {BUNDLED_REGISTRY}"
    )


def parse_registry(data: dict) -> list[Conversion]:
    conversions: list[Conversion] = []
    for entry in data.get("conversions", []):
        conversions.append(
            Conversion(
                id=str(entry["id"]),
                label=str(entry["label"]),
                source_extensions=tuple(
                    ext.lower() if ext.startswith(".") else f".{ext.lower()}"
                    for ext in entry.get("source_extensions", [])
                ),
                source_mimetypes=tuple(entry.get("source_mimetypes", [])),
                target_extension=str(entry["target_extension"]),
                featured=bool(entry.get("featured", False)),
                requires_commands=tuple(entry.get("requires_commands", [])),
                requires_packages=tuple(entry.get("requires_packages", [])),
                engine=str(entry["engine"]),
                icon=str(entry.get("icon", "document-convert")),
            )
        )
    return conversions


def command_available(name: str) -> bool:
    return shutil.which(name) is not None


def package_available(name: str) -> bool:
    if name in {"python3-PyMuPDF", "pymupdf", "PyMuPDF"}:
        try:
            import pymupdf  # noqa: F401
            return True
        except ImportError:
            return False
    if name == "PyYAML":
        try:
            import yaml  # noqa: F401
            return True
        except ImportError:
            return False
    return False


def conversion_available(conv: Conversion) -> bool:
    if conv.requires_commands and not any(
        command_available(cmd) for cmd in conv.requires_commands
    ):
        return False
    if conv.requires_packages and not all(
        package_available(pkg) for pkg in conv.requires_packages
    ):
        return False
    return True


def load_conversions(*, available_only: bool = False) -> list[Conversion]:
    conversions = parse_registry(load_yaml(registry_path()))
    if available_only:
        return [c for c in conversions if conversion_available(c)]
    return conversions


def kdialog(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["kdialog", *args],
        capture_output=True,
        text=True,
        env=os.environ.copy(),
    )


def notify(title: str, message: str, icon: str = "document-convert") -> None:
    subprocess.run(
        ["notify-send", "-i", icon, "-a", APP_NAME, title, message],
        capture_output=True,
    )


def kdialog_error(message: str) -> None:
    kdialog("--title", APP_NAME, "--error", message)


def matches_conversion(path: Path, conv: Conversion) -> bool:
    if not path.is_file():
        return False
    return path.suffix.lower() in conv.source_extensions


def applicable_conversions(
    paths: list[Path], conversions: list[Conversion]
) -> list[Conversion]:
    return [
        conv
        for conv in conversions
        if any(matches_conversion(path, conv) for path in paths)
    ]


def output_path_for(source: Path, conv: Conversion) -> Path:
    ext = conv.target_extension
    if not ext.startswith("."):
        ext = f".{ext}"
    return source.with_suffix(ext)


def find_libreoffice() -> list[str] | None:
    for cmd in ("libreoffice", "soffice"):
        if command_available(cmd):
            return [cmd]
    flatpak = shutil.which("flatpak")
    if flatpak:
        return [
            flatpak,
            "run",
            "--command=libreoffice",
            "org.libreoffice.LibreOffice",
        ]
    return None


def convert_pymupdf_text(source: Path, target: Path) -> None:
    import pymupdf

    parts: list[str] = []
    with pymupdf.open(source) as doc:
        for page in doc:
            parts.append(page.get_text())
    target.write_text(
        "\n\n".join(part.strip() for part in parts if part.strip()),
        encoding="utf-8",
    )


def convert_libreoffice_headless(source: Path, target: Path) -> None:
    cmd_prefix = find_libreoffice()
    if not cmd_prefix:
        raise RuntimeError("LibreOffice is not installed")

    outdir = source.parent
    cmd = [
        *cmd_prefix,
        "--headless",
        "--convert-to",
        target.suffix.lstrip("."),
        "--outdir",
        str(outdir),
        str(source),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        stderr = (result.stderr or result.stdout or "LibreOffice conversion failed").strip()
        raise RuntimeError(stderr[:500])

    produced = outdir / f"{source.stem}{target.suffix}"
    if produced != target and produced.exists():
        produced.replace(target)


def convert_yaml_json(source: Path, target: Path) -> None:
    import yaml

    text = source.read_text(encoding="utf-8")
    if source.suffix.lower() == ".json":
        data = json.loads(text)
        target.write_text(
            yaml.safe_dump(data, sort_keys=False, allow_unicode=True),
            encoding="utf-8",
        )
    else:
        data = yaml.safe_load(text)
        target.write_text(
            json.dumps(data, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )


ENGINES = {
    "pymupdf_text": convert_pymupdf_text,
    "libreoffice_headless": convert_libreoffice_headless,
    "yaml_json": convert_yaml_json,
}


def run_conversion(
    source: Path, conv: Conversion, *, overwrite: bool = False
) -> Path:
    if not matches_conversion(source, conv):
        raise ValueError(f"{source.name} is not supported for {conv.id}")

    target = output_path_for(source, conv)
    if target.exists() and not overwrite:
        raise FileExistsError(f"Output already exists: {target.name}")

    engine = ENGINES.get(conv.engine)
    if engine is None:
        raise RuntimeError(f"Unknown engine: {conv.engine}")

    engine(source, target)
    return target


def picker_label(conv: Conversion, paths: list[Path]) -> str:
    exts = ", ".join(
        sorted({p.suffix.lower() for p in paths if matches_conversion(p, conv)})
    )
    if exts:
        return f"{conv.label} ({exts})"
    return conv.label


def run_pick(paths: list[Path], *, overwrite: bool = False) -> int:
    conversions = applicable_conversions(
        paths, load_conversions(available_only=True)
    )
    if not conversions:
        kdialog_error("No conversions are available for the selected file(s).")
        return 1

    menu_args = ["--title", APP_NAME, "--menu", "Select a conversion:"]
    for conv in conversions:
        menu_args.extend([conv.id, picker_label(conv, paths)])

    result = kdialog(*menu_args)
    if result.returncode != 0:
        return result.returncode

    chosen_id = result.stdout.strip()
    conv = next((c for c in conversions if c.id == chosen_id), None)
    if conv is None:
        kdialog_error("Invalid conversion selection.")
        return 1

    return run_convert(chosen_id, paths, overwrite=overwrite)


def run_convert(conversion_id: str, paths: list[Path], *, overwrite: bool = False) -> int:
    conversions = load_conversions(available_only=True)
    conv = next((c for c in conversions if c.id == conversion_id), None)
    if conv is None:
        kdialog_error(f"Unknown or unavailable conversion: {conversion_id}")
        return 1

    targets = [path for path in paths if matches_conversion(path, conv)]
    if not targets:
        kdialog_error(f"No selected files support {conv.label}.")
        return 1

    converted: list[Path] = []
    skipped: list[str] = []
    failed: list[str] = []

    for source in targets:
        try:
            converted.append(run_conversion(source, conv, overwrite=overwrite))
        except FileExistsError as exc:
            skipped.append(str(exc))
        except Exception as exc:
            failed.append(f"{source.name}: {exc}")

    if converted:
        if len(converted) == 1:
            notify("Conversion complete", f"Created {converted[0].name}", conv.icon)
        else:
            notify(
                "Conversions complete",
                f"Created {len(converted)} files",
                conv.icon,
            )

    if skipped and not converted:
        kdialog_error("\n".join(skipped))
        return 1

    if failed:
        kdialog_error("\n".join(failed))
        return 1

    return 0


def desktop_action_id(conversion_id: str) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9-]", "", conversion_id.replace("_", "-"))
    if not cleaned:
        raise ValueError(f"Invalid conversion id: {conversion_id}")
    if cleaned[0].isdigit():
        cleaned = f"convert{cleaned}"
    return cleaned


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--convert", metavar="ID", help="Run a specific conversion")
    parser.add_argument("--pick", action="store_true", help="Show conversion picker")
    parser.add_argument("--overwrite", action="store_true", help="Replace existing outputs")
    parser.add_argument("files", nargs="*", help="Selected file paths")
    args = parser.parse_args(argv)

    paths = [Path(raw) for raw in args.files if raw]
    if not paths:
        kdialog_error("No files were selected.")
        return 1

    if args.pick:
        return run_pick(paths, overwrite=args.overwrite)
    if args.convert:
        return run_convert(args.convert, paths, overwrite=args.overwrite)

    parser.print_help()
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
