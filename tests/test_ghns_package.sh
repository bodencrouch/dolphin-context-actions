#!/usr/bin/env bash
# End-to-end test of the "Download New Services…" package, the way Dolphin's
# servicemenuinstaller runs it: extracted archive, install.sh --install as an
# unprivileged user, everything under $HOME. Run against a throwaway HOME so
# it is safe on developer machines and headless CI runners alike.
set -euo pipefail

repo="$(cd -- "$(dirname -- "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fake_home="$work/home"
mkdir -p "$fake_home"

step() { printf '\n== %s\n' "$*"; }

step "build the package"
"$repo/scripts/build-ghns-package.sh" "$work/dist" >/dev/null
tarball="$(echo "$work"/dist/dolphin-context-actions-servicemenu-v*.tar.gz)"
[ -f "$tarball" ]

step "extract like servicemenuinstaller does"
mkdir -p "$fake_home/.local/share/servicemenu-download"
tar -xzf "$tarball" -C "$fake_home/.local/share/servicemenu-download"
pkg_dir="$(echo "$fake_home"/.local/share/servicemenu-download/dolphin-context-actions-servicemenu-v*)"
[ -f "$pkg_dir/install.sh" ]

run_installer() {
    HOME="$fake_home" XDG_DATA_HOME="$fake_home/.local/share" \
        bash "$pkg_dir/install.sh" "$@"
}

step "install with --install (primary servicemenuinstaller argument)"
run_installer --install

app_dir="$fake_home/.local/share/dolphin-context-actions"
menu_dir="$fake_home/.local/share/kio/servicemenus"
launcher="$app_dir/dolphin-context-actions"

[ -x "$launcher" ] || { echo "FAIL: launcher missing/not executable"; exit 1; }
[ -f "$app_dir/installed-files.txt" ] || { echo "FAIL: manifest missing"; exit 1; }
[ -f "$app_dir/dolphin_context_actions/conversions.json" ] \
    || { echo "FAIL: JSON registry missing"; exit 1; }

step "static service menus installed, Exec rewritten to absolute launcher"
for menu in dolphin-context-actions.desktop dolphin-audio-converter.desktop; do
    [ -x "$menu_dir/$menu" ] || { echo "FAIL: $menu missing/not executable"; exit 1; }
    grep -q "^Exec=\"$launcher\" " "$menu_dir/$menu" \
        || { echo "FAIL: $menu Exec not rewritten"; exit 1; }
    if grep -q "^Exec=dolphin-context-actions " "$menu_dir/$menu"; then
        echo "FAIL: $menu still has a PATH-relative Exec"; exit 1
    fi
done

step "the installed CLI runs end-to-end through the launcher"
listing="$(HOME="$fake_home" "$launcher" --list-file-conversions)"
echo "$listing" | grep -q "pdf-to-md" || { echo "FAIL: CLI listing wrong"; exit 1; }
count="$(echo "$listing" | wc -l)"
[ "$count" -ge 20 ] || { echo "FAIL: only $count conversions listed"; exit 1; }
echo "   CLI lists $count conversions"

step "the vendored runtime works without PyYAML on sys.path"
HOME="$fake_home" python3 - "$app_dir" <<'PY'
import sys, types

app_dir = sys.argv[1]
# Simulate a machine with no PyYAML: poison the import before any code runs.
sys.modules["yaml"] = None  # import yaml -> ImportError
sys.path.insert(0, app_dir)
from dolphin_context_actions import file_converter
conversions = file_converter.load_conversions(available_only=False)
assert len(conversions) >= 20, len(conversions)
print(f"   registry loads {len(conversions)} conversions with PyYAML blocked")
PY

step "generated Convert menus (if this machine has any tools)"
gen_count="$(find "$menu_dir" -name 'dolphin-context-actions-convert-*.desktop' | wc -l)"
echo "   $gen_count generated menu files"

step "reinstall is idempotent"
run_installer --install
[ -x "$launcher" ]
dup="$(sort "$app_dir/installed-files.txt" | uniq -d | wc -l)"
[ "$dup" -eq 0 ] || { echo "FAIL: manifest has duplicates after reinstall"; exit 1; }

step "uninstall with --uninstall leaves HOME clean"
run_installer --uninstall
[ ! -e "$app_dir" ] || { echo "FAIL: app dir survives uninstall"; exit 1; }
leftover="$(find "$menu_dir" -name 'dolphin-*' 2>/dev/null | wc -l)"
[ "$leftover" -eq 0 ] || { echo "FAIL: $leftover menu files survive uninstall"; exit 1; }
[ ! -e "$fake_home/.local/bin/dolphin-context-actions" ] \
    || { echo "FAIL: bin link survives uninstall"; exit 1; }

step "uninstall when nothing is installed exits 0 (servicemenuinstaller retries)"
run_installer --deinstall

step "no-argument call installs (older servicemenuinstaller fallback)"
run_installer
[ -x "$launcher" ]
run_installer --uninstall

echo
echo "PASS: GHNS package installs, runs, and uninstalls cleanly."
