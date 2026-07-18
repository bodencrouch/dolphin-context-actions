# Adding conversions

The conversion catalog lives in a YAML file. After you edit it, run `./install.sh --apply` to regenerate Dolphin's context menus.

Your live copy is at:

```
~/.config/dolphin-file-converter/conversions.yaml
```

The repository ships a default at `config/conversions.yaml`.

## Registry format

Each conversion is one entry under `conversions:`:

```yaml
conversions:
  - id: pdf-to-md              # unique ID, used on the command line
    label: To Markdown         # text shown in the context menu
    source_extensions: [.pdf]  # file extensions this applies to
    source_mimetypes: [application/pdf]  # Dolphin mimetypes
    target_extension: .md      # output extension (written beside the source file)
    featured: true             # show directly in the Convert submenu
    requires_commands: []      # at least one must exist on PATH (empty = no check)
    requires_packages: [python3-PyMuPDF]  # Python imports that must succeed
    engine: pymupdf_text         # which backend function runs the conversion
    icon: text-markdown          # freedesktop icon name for the menu entry
```

### Field notes

**`id`** — Stable identifier. Use lowercase with hyphens. Referenced by `--convert id`.

**`label`** — Short menu text. "To PDF" reads better in a submenu than "Convert to PDF".

**`source_extensions`** — Include the dot. Matching is case-insensitive.

**`source_mimetypes`** — Dolphin uses these to decide when the menu appears. If you're unsure, run `mimetype -b yourfile.ext` in a terminal. You can list several mimetypes for formats that vary by system (YAML is a good example).

**`featured`** — `true` puts the action in the native submenu. `false` hides it there but keeps it in the **More…** picker. Useful for conversions you want available but not cluttering the menu.

**`requires_commands`** — If non-empty, at least one command must be found on `$PATH`. For LibreOffice, list both `libreoffice` and `soffice` since distros differ.

**`requires_packages`** — Python import names checked at menu generation time. Supported aliases: `python3-PyMuPDF`, `pymupdf`, `PyYAML`, `tomli-w`.

**`engine`** — See below. This is the only field that ties a registry entry to code.

## Built-in engines

| Engine | What it does | Typical use |
|--------|--------------|-------------|
| `pymupdf_text` | Extracts plain text from PDF pages via PyMuPDF | PDF → Markdown |
| `libreoffice_headless` | Runs `libreoffice --headless --convert-to …` | Office docs → PDF |
| `yaml_json` | Parses YAML or JSON and writes the other format | YAML ↔ JSON |
| `pandoc` | Runs `pandoc source -o target` | Markdown ↔ HTML, MD → PDF |
| `csv_json` | CSV/TSV ↔ JSON via stdlib `csv` | CSV → JSON, JSON → CSV, TSV → CSV |
| `toml_json` | TOML ↔ JSON via `tomllib` and `tomli_w` | TOML ↔ JSON |
| `imagemagick` | Runs `magick` or `convert` for raster swaps | PNG/JPEG/WebP/HEIC |

### Engine notes

**`csv_json`** — JSON → CSV only accepts a top-level JSON array where every item is an object with the same keys. Other shapes raise a clear error.

**`imagemagick`** — HEIC support depends on your ImageMagick build (libheif delegate). If conversion fails, the error dialog shows ImageMagick's message.

Adding a new engine means editing `scripts/dolphin-file-converter.py` — add a function and register it in the `ENGINES` dict. Keep engines small and focused.

## Worked example: Markdown to PDF

`md-to-pdf` is already in the bundled registry. To add a similar conversion yourself:

1. Install pandoc and make sure `pandoc` is on your PATH.
2. Add a registry entry (the `pandoc` engine already exists):

```yaml
  - id: md-to-pdf
    label: To PDF
    source_extensions: [.md]
    source_mimetypes: [text/markdown]
    target_extension: .pdf
    featured: false
    requires_commands: [pandoc]
    engine: pandoc
    icon: application-pdf
```

3. Re-install:

```bash
./install.sh --apply
```

Right-click a `.md` file → **Convert → More… → To PDF (.md)**.

## Menu generation behavior

The generator groups conversions by source extension set and writes one service menu file per group. For each group:

- Featured conversions become direct submenu actions.
- **More…** is always present and lists every available conversion for the selected files.

If LibreOffice isn't installed, DOCX conversions won't appear at all — the generator skips entries whose dependencies aren't met.

On Plasma 6, generated `.desktop` files must be executable. `install.sh` handles that.

## Tips

- Restart Dolphin after regenerating menus if changes don't show up immediately.
- Test from the terminal first: `dolphin-file-converter --convert your-id ~/testfile.ext`
- Multi-select works: each applicable file in the selection is converted individually.
- Output goes beside the source file. There's no "Save as" dialog.
