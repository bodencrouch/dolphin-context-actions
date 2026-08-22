#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:?build directory required}"
probe="${build_dir}/bin/link_menu_probe"
plugin_dir="${build_dir}/bin"
test_home="$(mktemp -d)"

cleanup() {
    case "$test_home" in
        /tmp/tmp.*) rm -rf "$test_home" ;;
    esac
}
trap cleanup EXIT

mkdir -p "${test_home}/work"
printf 'menu state\n' > "${test_home}/work/source.txt"

run_probe() {
    HOME="$test_home" \
        QT_PLUGIN_PATH="$plugin_dir" \
        QT_QPA_PLATFORM=offscreen \
        "$probe" "$@"
}

idle_output="$(run_probe "${test_home}/work/source.txt")"
grep -Fx 'Pick Link Source' <<<"$idle_output"
if grep -Fx 'Drop Link As' <<<"$idle_output"; then exit 1; fi
if grep -Fx 'Cancel Link Creation' <<<"$idle_output"; then exit 1; fi

admin_idle_output="$(run_probe --admin "${test_home}/work/source.txt")"
grep -Fx 'Pick Link Source' <<<"$admin_idle_output"
if grep -Fx 'Drop Link As' <<<"$admin_idle_output"; then exit 1; fi
if grep -Fx 'Cancel Link Creation' <<<"$admin_idle_output"; then exit 1; fi

run_probe "${test_home}/work/source.txt" 'Pick Link Source' >/dev/null
picked_output="$(run_probe "${test_home}/work")"
grep -Fx 'Drop Link As' <<<"$picked_output"
grep -Fx 'Cancel Link Creation' <<<"$picked_output"
if grep -Fx 'Pick Link Source' <<<"$picked_output"; then exit 1; fi
grep -Fx '  Drop Hardlink' <<<"$picked_output"
grep -Fx '  Drop Symlink' <<<"$picked_output"
grep -Fx '  Hardlink Clone' <<<"$picked_output"
grep -Fx '  Symlink Clone' <<<"$picked_output"
grep -Fx '  Smart Copy' <<<"$picked_output"
grep -Fx '  Enumerate Hardlinks' <<<"$picked_output"
grep -Fx '  Link Properties' <<<"$picked_output"

run_probe "${test_home}/work" 'Cancel Link Creation' >/dev/null
grep -Fx 'Pick Link Source' < <(run_probe "${test_home}/work/source.txt")
