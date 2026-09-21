#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
python3 - "$root" <<'PY'
import json, pathlib, sys
root = pathlib.Path(sys.argv[1])
expect = {
    "context-actions": ("Dolphin Context Actions", "Convert a file from the right-click menu."),
    "archive": ("Dolphin Archive", "Unpack or compress from the right-click menu."),
    "link": ("Dolphin Link", "Hardlink or symlink from the right-click menu."),
}
for slug, (name, summary) in expect.items():
    meta = json.loads((root / "packaging/pling" / slug / "metadata.json").read_text())
    desc = (root / "packaging/pling" / slug / "description.txt").read_text()
    first = desc.splitlines()[0]
    assert meta["product"]["name"] == name, meta["product"]["name"]
    assert meta["product"]["summary"] == summary
    assert meta["product"]["author"]["username"] == "brunner56"
    assert meta["product"]["author"]["name"] == "Boden Crouch"
    assert meta["product"]["category_id"] == 102
    assert meta["product"]["license"] == "MIT"
    assert first == summary, first
    assert len(first) <= 60
    assert "Wizard" not in desc and "wizard" not in desc
print("PASS: pling metadata")
PY
