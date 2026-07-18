#!/usr/bin/env python3
"""Generate KDE service menus from the file conversion registry."""

from __future__ import annotations

import argparse
import importlib.util
import sys
from collections import defaultdict
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent

_converter_spec = importlib.util.spec_from_file_location(
    "dolphin_file_converter",
    SCRIPT_DIR / "dolphin-file-converter.py",
)
if _converter_spec is None or _converter_spec.loader is None:
    raise RuntimeError("Unable to load dolphin-file-converter.py")
converter = importlib.util.module_from_spec(_converter_spec)
sys.modules[_converter_spec.name] = converter
_converter_spec.loader.exec_module(converter)

MENU_PREFIX = "dolphin-file-converter"
SERVICEMENU_DIR = Path.home() / ".local" / "share" / "kio" / "servicemenus"
CONVERTER_BIN = Path.home() / ".local" / "bin" / "dolphin-file-converter"


def slug_for_extensions(extensions: tuple[str, ...]) -> str:
    return "-".join(ext.lstrip(".").lower() for ext in extensions)


def render_desktop(
    *,
    mimetypes: list[str],
    featured: list[converter.Conversion],
    converter_bin: Path,
) -> str:
    action_ids = [converter.desktop_action_id(conv.id) for conv in featured]
    action_ids.append("moreConversions")
    actions_line = ";".join(action_ids) + ";"

    lines = [
        "[Desktop Entry]",
        "Type=Service",
        f"MimeType={';'.join(mimetypes)};",
        f"Actions={actions_line}",
        "X-KDE-Submenu=Convert",
        "Icon=document-convert",
        "",
    ]

    for conv, action_id in zip(featured, action_ids[:-1], strict=True):
        lines.extend(
            [
                f"[Desktop Action {action_id}]",
                f"Name={conv.label}",
                f"Icon={conv.icon}",
                f'Exec="{converter_bin}" --convert {conv.id} %F',
                "",
            ]
        )

    lines.extend(
        [
            "[Desktop Action moreConversions]",
            "Name=More…",
            "Icon=view-list-details",
            f'Exec="{converter_bin}" --pick %F',
            "",
        ]
    )
    return "\n".join(lines)


def group_conversions(
    conversions: list[converter.Conversion],
) -> dict[tuple[str, ...], list[converter.Conversion]]:
    groups: dict[tuple[str, ...], list[converter.Conversion]] = defaultdict(list)
    for conv in conversions:
        groups[tuple(sorted(conv.source_extensions))].append(conv)
    return groups


def generate(
    *,
    output_dir: Path,
    converter_bin: Path,
    registry_path: Path | None = None,
) -> list[Path]:
    if registry_path is not None:
        original = converter.CONFIG_FILE
        converter.CONFIG_FILE = registry_path
        try:
            conversions = converter.load_conversions(available_only=True)
        finally:
            converter.CONFIG_FILE = original
    else:
        conversions = converter.load_conversions(available_only=True)

    output_dir.mkdir(parents=True, exist_ok=True)

    for old in output_dir.glob(f"{MENU_PREFIX}-*.desktop"):
        old.unlink()

    written: list[Path] = []
    for extensions, group in sorted(group_conversions(conversions).items()):
        slug = slug_for_extensions(extensions)
        mimetypes = sorted({mime for conv in group for mime in conv.source_mimetypes})
        if not mimetypes:
            mimetypes = ["application/octet-stream"]

        content = render_desktop(
            mimetypes=mimetypes,
            featured=[conv for conv in group if conv.featured],
            converter_bin=converter_bin,
        )
        target = output_dir / f"{MENU_PREFIX}-{slug}.desktop"
        target.write_text(content, encoding="utf-8")
        target.chmod(0o755)
        written.append(target)

    return written


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=SERVICEMENU_DIR,
        help="Directory for generated .desktop files",
    )
    parser.add_argument(
        "--converter-bin",
        type=Path,
        default=CONVERTER_BIN,
        help="Path to dolphin-file-converter executable",
    )
    parser.add_argument(
        "--registry",
        type=Path,
        default=None,
        help="Registry YAML path override",
    )
    args = parser.parse_args()

    written = generate(
        output_dir=args.output_dir,
        converter_bin=args.converter_bin,
        registry_path=args.registry,
    )

    if not written:
        print(
            "WARN: no service menus generated (no available conversions)",
            file=sys.stderr,
        )
        return 1

    for path in written:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
