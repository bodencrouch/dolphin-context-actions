#!/usr/bin/env bash
set -euo pipefail

SERVICEDIR="${HOME}/.local/share/kio/servicemenus"
CONFIGDIR="${HOME}/.config/dolphin-convert-actions"

echo "Installing Python package..."
pip install --user -e .

echo "Installing service menus..."
mkdir -p "$SERVICEDIR" "$CONFIGDIR"
cp servicemenus/dolphin-convert-actions.desktop "$SERVICEDIR/"
cp servicemenus/dolphin-audio-converter.desktop "$SERVICEDIR/"
cp servicemenus/dolphin-link-extension.desktop "$SERVICEDIR/"
cp servicemenus/dolphin-link-extension-bg.desktop "$SERVICEDIR/"

echo ""
echo "✓ Installed. Restart Dolphin (killall dolphin) to reload service menus."
