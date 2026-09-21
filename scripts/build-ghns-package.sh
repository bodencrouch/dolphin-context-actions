#!/usr/bin/env bash
# Builds the archives uploaded to store.kde.org for Dolphin's
# "Download New Services…" dialog.
#
# The archive layout follows what Dolphin's servicemenuinstaller expects:
# an install.sh at the root (run with --install / --uninstall as the user),
# next to the payload it installs. See ghns/install.sh for the contract.
#
# Usage: scripts/build-ghns-package.sh <product> [output-dir]
#   product is context-actions, archive, or link (default output-dir: dist/)
set -euo pipefail

repo="$(cd -- "$(dirname -- "$0")/.." && pwd)"

# Old CI passed an output directory as $1 (dist, ./dist, /tmp/out). Treat a
# path containing / or . — and any other non-product token, so that
# `scripts/build-ghns-package.sh dist` still works — as out_dir with product
# context-actions.
case "${1:-}" in
    context-actions|archive|link) ;;
    "")
        ;;
    *)
        set -- context-actions "$1"
        ;;
esac
product="${1:?product: context-actions, archive, or link}"
out_dir="${2:-$repo/dist}"

case "$product" in
    context-actions|archive|link) ;;
    *)
        echo "product: context-actions, archive, or link (got $product)" >&2
        exit 1
        ;;
esac

version="$(sed -n 's/.*project(dolphin-context-actions VERSION \([0-9.]*\).*/\1/p' "$repo/CMakeLists.txt" | head -n1)"
[ -n "$version" ] || { echo "Could not read version from CMakeLists.txt" >&2; exit 1; }

case "$product" in
    context-actions)
        product_id="dolphin-context-actions"
        store_name="Dolphin Context Actions"
        name="dolphin-context-actions-servicemenu-v${version}"
        ;;
    archive)
        product_id="dolphin-archive"
        store_name="Dolphin Archive"
        name="dolphin-archive-servicemenu-v${version}"
        ;;
    link)
        product_id="dolphin-link"
        store_name="Dolphin Link"
        name="dolphin-link-servicemenu-v${version}"
        ;;
esac

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
root="$stage/$name"
mkdir -p "$root/servicemenus"

# 1. Release binary. The store archive cannot compile C++ on the user's
#    machine, so the Linux helper ships prebuilt.
cmake -S "$repo" -B "$repo/build/helper" -DCMAKE_BUILD_TYPE=Release
cmake --build "$repo/build/helper" --parallel --target dolphin-context-actions
cp "$repo/build/helper/helper/dolphin-context-actions" "$root/dolphin-context-actions"
chmod 0755 "$root/dolphin-context-actions"

# 2. Registry: JSON (the C++ helper embeds the same catalog via Qt resources).
#    Convert menus are generated from this file at install time; archive/link
#    omit it so they do not install Convert entries.
if [ "$product" = context-actions ]; then
    cp "$repo/assets/conversions.json" "$root/conversions.json"
fi

# 3. Service menus + installer + license + product.id.
case "$product" in
    context-actions)
        cp "$repo/servicemenus/dolphin-context-actions.desktop" \
           "$repo/servicemenus/dolphin-audio-converter.desktop" \
           "$root/servicemenus/"
        ;;
    archive)
        cp "$repo/servicemenus/dolphin-archive.desktop" "$root/servicemenus/"
        ;;
    link)
        cp "$repo/servicemenus/dolphin-link-pick.desktop" \
           "$repo/servicemenus/dolphin-link-drop.desktop" \
           "$root/servicemenus/"
        ;;
esac
cp "$repo/ghns/install.sh" "$root/install.sh"
chmod 0755 "$root/install.sh"
cp "$repo/LICENSE" "$root/LICENSE"
printf '%s\n' "$version" > "$root/VERSION"
printf '%s\n' "$product_id" > "$root/product.id"

readme_bin_line=""
if [ "$product" = context-actions ]; then
    readme_bin_line="
  ~/.local/bin/dolphin-context-actions      (command-line launcher)"
fi
cat > "$root/README" <<EOF
${store_name} v${version}
https://github.com/bodencrouch/dolphin-context-actions

Installed automatically by Dolphin's "Download New Services..." dialog.
Manual install:   ./install.sh --install
Manual uninstall: ./install.sh --uninstall

Everything goes under your home directory only:
  ~/.local/share/${product_id}/   (the program)
  ~/.local/share/kio/servicemenus/          (the context menus)${readme_bin_line}
EOF

# 4. Sanity checks before anything is shipped.
bash -n "$root/install.sh"
if command -v shellcheck >/dev/null 2>&1; then
    shellcheck "$root/install.sh"
fi
# Service menus are Type=Service KDE files, which desktop-file-validate
# rejects wholesale -- validate structure with Python when it is present.
if command -v python3 >/dev/null 2>&1; then
    python3 - "$root"/servicemenus/*.desktop <<'PY'
import configparser, sys
for path in sys.argv[1:]:
    cp = configparser.RawConfigParser()
    cp.optionxform = str
    with open(path, encoding="utf-8") as f:
        cp.read_file(f)
    entry = cp["Desktop Entry"]
    assert entry["Type"] == "Service", path
    actions = [a for a in entry["Actions"].split(";") if a]
    for action in actions:
        section = cp[f"Desktop Action {action}"]
        assert section.get("Name"), f"{path}: {action} has no Name"
        assert section.get("Exec") or action == "configure", f"{path}: {action} has no Exec"
    print(f"ok: {path} ({len(actions)} actions)")
PY
else
    echo "skip: python3 not found; desktop-file configparser check omitted"
fi

listing="$("$root/dolphin-context-actions" --list-file-conversions)"
count="$(printf '%s\n' "$listing" | wc -l)"
printf '%s\n' "$listing" | grep -q "pdf-to-md"
[ "$count" -ge 20 ]
echo "ok: bundled binary lists ${count} conversions"

if [ -f "$root/conversions.json" ]; then
    python3 -c "
import json
data = json.load(open('$root/conversions.json'))
count = len(data['conversions'])
assert count > 0
print(f'ok: conversions.json ({count} conversions)')
"
fi

# 5. Deterministic tarball.
mkdir -p "$out_dir"
epoch="${SOURCE_DATE_EPOCH:-$(git -C "$repo" log -1 --format=%ct 2>/dev/null || date +%s)}"
tar -C "$stage" \
    --sort=name --owner=0 --group=0 --numeric-owner \
    --mtime="@$epoch" \
    -czf "$out_dir/$name.tar.gz" "$name"

echo "Built: $out_dir/$name.tar.gz"
ls -l "$out_dir/$name.tar.gz"
