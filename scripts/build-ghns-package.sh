#!/usr/bin/env bash
# Builds the archive uploaded to store.kde.org for Dolphin's
# "Download New Services…" dialog.
#
# The archive layout follows what Dolphin's servicemenuinstaller expects:
# an install.sh at the root (run with --install / --uninstall as the user),
# next to the payload it installs. See ghns/install.sh for the contract.
#
# Usage: scripts/build-ghns-package.sh [output-dir]   (default: dist/)
set -euo pipefail

repo="$(cd -- "$(dirname -- "$0")/.." && pwd)"
out_dir="${1:-$repo/dist}"

version="$(python3 -c "
import tomllib
with open('$repo/pyproject.toml', 'rb') as f:
    print(tomllib.load(f)['project']['version'])
")"

name="dolphin-context-actions-servicemenu-v${version}"
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
root="$stage/$name"
mkdir -p "$root"

# 1. The Python package, minus caches. conversions.yaml stays in as the
#    human-readable reference; conversions.json (generated below) is what the
#    no-PyYAML runtime actually loads.
mkdir -p "$root/dolphin_context_actions"
(cd "$repo/src/dolphin_context_actions" && find . -type f \
    ! -path '*/__pycache__/*' ! -name '*.pyc' -print0 \
    | tar --null -cf - --files-from=-) | tar -xf - -C "$root/dolphin_context_actions"

python3 - "$repo/src/dolphin_context_actions/conversions.yaml" \
          "$root/dolphin_context_actions/conversions.json" <<'PY'
import json, sys
import yaml
src, dst = sys.argv[1], sys.argv[2]
with open(src, encoding="utf-8") as f:
    data = yaml.safe_load(f)
with open(dst, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=1, sort_keys=True)
    f.write("\n")
PY

# 2. Service menus + installer + license.
mkdir -p "$root/servicemenus"
cp "$repo"/servicemenus/*.desktop "$root/servicemenus/"
cp "$repo/ghns/install.sh" "$root/install.sh"
chmod 0755 "$root/install.sh"
cp "$repo/LICENSE" "$root/LICENSE"
printf '%s\n' "$version" > "$root/VERSION"

cat > "$root/README" <<EOF
Dolphin Context Actions v${version}
https://github.com/bodencrouch/dolphin-context-actions

Installed automatically by Dolphin's "Download New Services..." dialog.
Manual install:   ./install.sh --install
Manual uninstall: ./install.sh --uninstall

Everything goes under your home directory only:
  ~/.local/share/dolphin-context-actions/   (the program)
  ~/.local/share/kio/servicemenus/          (the context menus)
  ~/.local/bin/dolphin-context-actions      (command-line launcher)
EOF

# 3. Sanity checks before anything is shipped.
bash -n "$root/install.sh"
if command -v shellcheck >/dev/null 2>&1; then
    shellcheck "$root/install.sh"
fi
# Service menus are Type=Service KDE files, which desktop-file-validate
# rejects wholesale -- validate structure with Python instead.
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
python3 -c "
import json, sys
data = json.load(open('$root/dolphin_context_actions/conversions.json'))
count = len(data['conversions'])
assert count > 0
print(f'ok: conversions.json ({count} conversions)')
"

# 4. Deterministic tarball.
mkdir -p "$out_dir"
epoch="${SOURCE_DATE_EPOCH:-$(git -C "$repo" log -1 --format=%ct 2>/dev/null || date +%s)}"
tar -C "$stage" \
    --sort=name --owner=0 --group=0 --numeric-owner \
    --mtime="@$epoch" \
    -czf "$out_dir/$name.tar.gz" "$name"

echo "Built: $out_dir/$name.tar.gz"
ls -l "$out_dir/$name.tar.gz"
