#!/usr/bin/env bash
set -euo pipefail
bin="${1:?helper binary}"
export DOLPHIN_CONTEXT_ACTIONS_HEADLESS=1
export QT_QPA_PLATFORM=offscreen

listing="$("$bin" --list-file-conversions)"
echo "$listing" | grep -q $'pdf-to-md\tTo Markdown'
echo "$listing" | grep -q $'csv-to-json\tTo JSON'
echo "$listing" | grep -q $'png-to-jpg\tTo JPEG'

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
printf 'name,age\nAda,36\nBob,40\n' > "$work/people.csv"
"$bin" --file-convert csv-to-json "$work/people.csv"
test -f "$work/people.json"
rm -f "$work/people.csv"
"$bin" --file-convert json-to-csv "$work/people.json"
grep -q 'Ada,36' "$work/people.csv"

if "$bin" --file-convert missing-conversion "$work/people.csv"; then
    echo "FAIL: missing conversion exited 0"
    exit 1
fi

root="$(cd "$(dirname "$0")/.." && pwd)"
grep -q 'return QStringLiteral("dolphin-context-actions");' \
    "$root/kio-plugin/dolphinlinkfileitemaction.cpp"
grep -q 'return QStringLiteral("dolphin-context-actions");' \
    "$root/kio-plugin/dolphinarkfileitemaction.cpp"
grep -q 'X-KDE-Submenu=Archive' "$root/servicemenus/dolphin-archive.desktop"
grep -q 'Exec=dolphin-context-actions --archive extract-here' "$root/servicemenus/dolphin-archive.desktop"
grep -q 'Exec=dolphin-context-actions --archive compress-to-7z' "$root/servicemenus/dolphin-archive.desktop"
grep -q 'X-KDE-Submenu=Link' "$root/servicemenus/dolphin-link-pick.desktop"
grep -q 'Exec=dolphin-context-actions --pick-link-source' "$root/servicemenus/dolphin-link-pick.desktop"
grep -q 'Exec=dolphin-context-actions --drop-hardlink --target-dir' "$root/servicemenus/dolphin-link-drop.desktop"
echo "PASS: helper CLI"
