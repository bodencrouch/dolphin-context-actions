# KDE File Converter

Right-click a file in Dolphin, pick **Actions → Convert**, and get a sensible list of format conversions for that file type.

This is a small utility for KDE Plasma that adds context-menu conversions using the standard [Dolphin service menu](https://develop.kde.org/docs/apps/dolphin/service-menus/) mechanism. No Dolphin plugins, no daemon, no background service — just a YAML registry, a Python backend, and generated `.desktop` files.

## What it does

When you select a supported file, Dolphin shows a **Convert** submenu:

| File type | Featured action | Also available via **More…** |
|-----------|-----------------|------------------------------|
| PDF | To Markdown | — |
| DOCX | To PDF | — |
| YAML / YML | To JSON | — |
| JSON | — | To YAML |

Output files are written next to the source file with the new extension (`report.pdf` → `report.md`). Existing outputs are skipped unless you pass `--overwrite` on the command line.

Featured conversions show up directly in the submenu. Everything else (and anything you add later) is reachable through **More…**, which opens a picker dialog.

## Requirements

- KDE Plasma 6 (or recent Plasma 5 with the `~/.local/share/kio/servicemenus/` path)
- Python 3.10+
- `kdialog` and `notify-send` (part of a normal KDE install)

Optional dependencies depend on which conversions you want:

| Conversion | Needs |
|------------|-------|
| PDF → Markdown | [PyMuPDF](https://pypi.org/project/PyMuPDF/) (`python3-PyMuPDF` on Fedora, or `pip install pymupdf`) |
| DOCX → PDF | LibreOffice (`libreoffice` or `soffice` on PATH, or the Flatpak) |
| YAML ↔ JSON | [PyYAML](https://pypi.org/project/PyYAML/) |

Conversions whose dependencies are missing are simply omitted from the generated menus.

## Install

```bash
git clone https://github.com/oldrepublicwizard/kde-file-converter.git
cd kde-file-converter
./install.sh --apply
```

Then restart Dolphin (or open a new window).

The installer places:

- `~/.local/bin/dolphin-file-converter` — backend script
- `~/.config/dolphin-file-converter/conversions.yaml` — your editable registry copy
- `~/.local/share/kio/servicemenus/dolphin-file-converter-*.desktop` — generated menus

To refresh menus after editing the registry:

```bash
./install.sh --apply
```

## Command-line usage

The same backend powers the context menu:

```bash
# Run a specific conversion
dolphin-file-converter --convert pdf-to-md ~/Documents/paper.pdf

# Open the picker for a selection
dolphin-file-converter --pick ~/Documents/config.yaml ~/Documents/data.json

# Overwrite existing output
dolphin-file-converter --convert yaml-to-json --overwrite ~/project/settings.yaml
```

## Adding conversions

Edit `~/.config/dolphin-file-converter/conversions.yaml` (or the bundled [`config/conversions.yaml`](config/conversions.yaml) before install), then re-run `./install.sh --apply`.

See [docs/adding-conversions.md](docs/adding-conversions.md) for the registry format, available engines, and a worked example.

## How it works

```
conversions.yaml  →  install.sh  →  generate servicemenus  →  Dolphin context menu
                              ↘
                                dolphin-file-converter  →  conversion engine
```

1. **Registry** — YAML list of conversions (source type, target extension, engine, dependencies).
2. **Generator** — reads the registry, checks what's available on your system, writes one `.desktop` file per source file type.
3. **Backend** — runs the actual conversion when you pick an action.

This follows the same pattern as other KDE service menus (Ark's extract actions, community audio converters, etc.). The submenu only appears for file types that have at least one available conversion.

## Uninstall

```bash
rm -f ~/.local/bin/dolphin-file-converter
rm -f ~/.local/share/kio/servicemenus/dolphin-file-converter-*.desktop
rm -rf ~/.config/dolphin-file-converter   # optional, removes your registry copy
```

## License

MIT — see [LICENSE](LICENSE).
