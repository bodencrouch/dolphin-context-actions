# KDE File Converter

Right-click a file in Dolphin, pick **Actions → Convert**, and get a sensible list of format conversions for that file type.

This is a small utility for KDE Plasma that adds context-menu conversions using the standard [Dolphin service menu](https://develop.kde.org/docs/apps/dolphin/service-menus/) mechanism. No Dolphin plugins, no daemon, no background service — just a YAML registry, a Python backend, and generated `.desktop` files.

## What it does

When you select a supported file, Dolphin shows a **Convert** submenu. Each file type gets one featured action in the submenu; everything else is under **More…**.

### Office → PDF (LibreOffice)

DOCX, ODT, ODS, PPTX, XLSX, RTF, TXT → PDF

### Text & markup (Pandoc)

MD → HTML (featured), MD → PDF, HTML → MD

### Data formats

| Source | Featured | Via More… |
|--------|----------|-----------|
| PDF | To Markdown | — |
| YAML / YML | To JSON | — |
| CSV | To JSON | — |
| TOML | To JSON | — |
| JSON | — | To YAML, To CSV, To TOML |
| TSV | — | To CSV |

JSON → CSV requires a top-level array of objects (`[{...}, {...}]`).

### Images (ImageMagick)

PNG → JPEG (featured), WebP → PNG (featured), plus JPG ↔ PNG, WebP → JPEG, PNG → WebP, HEIC → JPEG via **More…**

Output files are written next to the source file with the new extension (`report.pdf` → `report.md`). Existing outputs are skipped unless you pass `--overwrite` on the command line.

## Requirements

- KDE Plasma 6 (or recent Plasma 5 with the `~/.local/share/kio/servicemenus/` path)
- Python 3.11+
- `kdialog` and `notify-send` (part of a normal KDE install)

Optional dependencies depend on which conversions you want:

| Conversion | Needs |
|------------|-------|
| PDF → Markdown | [PyMuPDF](https://pypi.org/project/PyMuPDF/) |
| Office → PDF | LibreOffice (`libreoffice` or `soffice`) |
| Markdown / HTML | [Pandoc](https://pandoc.org/) |
| YAML ↔ JSON | [PyYAML](https://pypi.org/project/PyYAML/) |
| TOML ↔ JSON | stdlib `tomllib` (read) + [tomli-w](https://pypi.org/project/tomli-w/) (write) |
| CSV / TSV | Python stdlib (no extra packages) |
| Images | [ImageMagick](https://imagemagick.org/) (`magick` or `convert`) |

Install Python deps: `pip install -r requirements.txt`

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
