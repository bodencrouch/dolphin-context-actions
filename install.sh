#!/usr/bin/env bash
# Install KDE File Converter into the current user's home directory.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT_SRC="$ROOT/scripts/dolphin-file-converter.py"
GENERATOR_SRC="$ROOT/scripts/generate-file-converter-servicemenus.py"
REGISTRY_SRC="$ROOT/config/conversions.yaml"
BIN_DST="${HOME}/.local/bin/dolphin-file-converter"
CONFIG_DIR="${HOME}/.config/dolphin-file-converter"
CONFIG_DST="${CONFIG_DIR}/conversions.yaml"
SERVICEMENU_DIR="${HOME}/.local/share/kio/servicemenus"
DRY_RUN=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [--apply] [--dry-run]

Installs KDE File Converter for the current user:

  ~/.local/bin/dolphin-file-converter
  ~/.config/dolphin-file-converter/conversions.yaml
  ~/.local/share/kio/servicemenus/dolphin-file-converter-*.desktop

Dependencies (per conversion, checked at install time):
  PDF      python3-PyMuPDF
  DOCX     libreoffice or soffice
  YAML/JSON PyYAML
EOF
}

log() { printf 'INFO %s\n' "$*"; }
warn() { printf 'WARN %s\n' "$*" >&2; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --apply) DRY_RUN=0 ;;
    --dry-run) DRY_RUN=1 ;;
    -h | --help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 1 ;;
  esac
  shift
done

for required in "$SCRIPT_SRC" "$GENERATOR_SRC" "$REGISTRY_SRC"; do
  [[ -f "$required" ]] || { echo "Missing $required" >&2; exit 1; }
done

if [[ "$DRY_RUN" -eq 1 ]]; then
  log "(dry-run) install scripts and registry"
  log "(dry-run) generate service menus in $SERVICEMENU_DIR"
  exit 0
fi

mkdir -p "$(dirname "$BIN_DST")" "$CONFIG_DIR" "$SERVICEMENU_DIR"
install -m755 "$SCRIPT_SRC" "$BIN_DST"
install -m644 "$REGISTRY_SRC" "$CONFIG_DST"
log "installed $BIN_DST"
log "installed $CONFIG_DST"

python3 "$GENERATOR_SRC" \
  --output-dir "$SERVICEMENU_DIR" \
  --converter-bin "$BIN_DST" \
  --registry "$CONFIG_DST"

if ! python3 -c "import pymupdf" 2>/dev/null; then
  warn "PyMuPDF not available — PDF conversions will be omitted from menus"
fi

if ! command -v libreoffice >/dev/null 2>&1 && ! command -v soffice >/dev/null 2>&1; then
  warn "LibreOffice CLI not found — DOCX conversions will be omitted from menus"
fi

if ! python3 -c "import yaml" 2>/dev/null; then
  warn "PyYAML not available — YAML/JSON conversions will be omitted from menus"
fi

log "Install complete. Restart Dolphin if the Convert menu does not appear."
