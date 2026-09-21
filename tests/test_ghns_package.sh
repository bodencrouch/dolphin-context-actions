#!/usr/bin/env bash
# End-to-end test of the "Download New Services…" packages, the way Dolphin's
# servicemenuinstaller runs them: extracted archive, install.sh --install as an
# unprivileged user, everything under $HOME. Run against a throwaway HOME so
# it is safe on developer machines and headless CI runners alike.
set -euo pipefail

export DOLPHIN_CONTEXT_ACTIONS_HEADLESS=1
export QT_QPA_PLATFORM=offscreen

repo="$(cd -- "$(dirname -- "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

step() { printf '\n== %s\n' "$*"; }

# extra_check is "conversions" for the convert package, empty otherwise.
exercise_product() {
    local product="$1"
    local app_slug="$2"
    local extra_check="${3:-}"
    local fake_home prefix tarball pkg_dir app_dir menu_dir launcher
    local found dup leftover listing count gen_count
    local -a desktops

    case "$product" in
        context-actions)
            desktops=(dolphin-context-actions.desktop dolphin-audio-converter.desktop)
            ;;
        archive)
            desktops=(dolphin-archive.desktop)
            ;;
        link)
            desktops=(dolphin-link-pick.desktop dolphin-link-drop.desktop)
            ;;
        *)
            echo "FAIL: unknown product $product"
            exit 1
            ;;
    esac

    fake_home="$work/home-$product"
    mkdir -p "$fake_home" "$work/dist"

    step "build $product"
    "$repo/scripts/build-ghns-package.sh" "$product" "$work/dist" >/dev/null

    prefix="dolphin-${app_slug#dolphin-}-servicemenu"
    tarball=""
    for f in "$work/dist/${prefix}-v"*.tar.gz; do
        [ -f "$f" ] || { echo "FAIL: no tarball for $product ($prefix)"; exit 1; }
        tarball="$f"
        break
    done
    [ -f "$tarball" ]

    step "extract $product like servicemenuinstaller does"
    mkdir -p "$fake_home/.local/share/servicemenu-download"
    tar -xzf "$tarball" -C "$fake_home/.local/share/servicemenu-download"
    pkg_dir=""
    for d in "$fake_home/.local/share/servicemenu-download/${prefix}-v"*; do
        [ -d "$d" ] || { echo "FAIL: extract dir missing for $product"; exit 1; }
        pkg_dir="$d"
        break
    done

    [ -f "$pkg_dir/install.sh" ] || { echo "FAIL: install.sh missing in $product tarball"; exit 1; }
    [ -x "$pkg_dir/dolphin-context-actions" ] || { echo "FAIL: helper missing in $product tarball"; exit 1; }
    [ ! -d "$pkg_dir/dolphin_context_actions" ] || { echo "FAIL: python package leaked into $product tarball"; exit 1; }
    [ "$(tr -d '\n' < "$pkg_dir/product.id")" = "$app_slug" ] \
        || { echo "FAIL: product.id want $app_slug got $(tr -d '\n' < "$pkg_dir/product.id" 2>/dev/null || true)"; exit 1; }

    if [ "$extra_check" = conversions ]; then
        [ -f "$pkg_dir/conversions.json" ] \
            || { echo "FAIL: conversions.json missing from $product tarball"; exit 1; }
    else
        [ ! -f "$pkg_dir/conversions.json" ] \
            || { echo "FAIL: conversions.json should not be in $product tarball"; exit 1; }
    fi

    for menu in "${desktops[@]}"; do
        [ -f "$pkg_dir/servicemenus/$menu" ] \
            || { echo "FAIL: $menu missing from $product tarball"; exit 1; }
    done
    found="$(find "$pkg_dir/servicemenus" -name '*.desktop' | wc -l)"
    [ "$found" -eq "${#desktops[@]}" ] \
        || { echo "FAIL: unexpected desktops in $product tarball (found $found, want ${#desktops[@]})"; exit 1; }

    run_installer() {
        HOME="$fake_home" XDG_DATA_HOME="$fake_home/.local/share" \
            bash "$pkg_dir/install.sh" "$@"
    }

    step "install $product with --install (primary servicemenuinstaller argument)"
    run_installer --install

    app_dir="$fake_home/.local/share/$app_slug"
    menu_dir="$fake_home/.local/share/kio/servicemenus"
    launcher="$app_dir/dolphin-context-actions"

    [ -x "$launcher" ] || { echo "FAIL: launcher missing/not executable"; exit 1; }
    [ -f "$app_dir/installed-files.txt" ] || { echo "FAIL: manifest missing"; exit 1; }

    step "static service menus for $product, Exec rewritten to absolute launcher"
    for menu in "${desktops[@]}"; do
        [ -x "$menu_dir/$menu" ] || { echo "FAIL: $menu missing/not executable"; exit 1; }
        grep -q "^Exec=\"$launcher\" " "$menu_dir/$menu" \
            || { echo "FAIL: $menu Exec not rewritten"; exit 1; }
        if grep -q "^Exec=dolphin-context-actions " "$menu_dir/$menu"; then
            echo "FAIL: $menu still has a PATH-relative Exec"; exit 1
        fi
    done

    if [ "$extra_check" = conversions ]; then
        step "the installed CLI runs end-to-end through the launcher"
        listing="$(HOME="$fake_home" "$launcher" --list-file-conversions)"
        echo "$listing" | grep -q "pdf-to-md" || { echo "FAIL: CLI listing wrong"; exit 1; }
        count="$(echo "$listing" | wc -l)"
        [ "$count" -ge 20 ] || { echo "FAIL: only $count conversions listed"; exit 1; }
        echo "   CLI lists $count conversions"

        step "generated Convert menus (if this machine has any tools)"
        gen_count="$(find "$menu_dir" -name 'dolphin-context-actions-convert-*.desktop' | wc -l)"
        echo "   $gen_count generated menu files"

        [ -L "$fake_home/.local/bin/dolphin-context-actions" ] \
            || { echo "FAIL: bin link missing for context-actions"; exit 1; }
    else
        step "no Convert menus generated for $product"
        gen_count="$(find "$menu_dir" -name 'dolphin-context-actions-convert-*.desktop' | wc -l)"
        [ "$gen_count" -eq 0 ] \
            || { echo "FAIL: $gen_count convert menus generated for $product"; exit 1; }
        [ ! -e "$fake_home/.local/bin/dolphin-context-actions" ] \
            || { echo "FAIL: $product stole ~/.local/bin/dolphin-context-actions"; exit 1; }
    fi

    step "reinstall $product is idempotent"
    run_installer --install
    [ -x "$launcher" ]
    dup="$(sort "$app_dir/installed-files.txt" | uniq -d | wc -l)"
    [ "$dup" -eq 0 ] || { echo "FAIL: manifest has duplicates after reinstall"; exit 1; }

    step "uninstall $product leaves that product's HOME entries clean"
    run_installer --uninstall
    [ ! -e "$app_dir" ] || { echo "FAIL: app dir survives uninstall"; exit 1; }
    leftover="$(find "$menu_dir" -name 'dolphin-*' 2>/dev/null | wc -l)"
    [ "$leftover" -eq 0 ] || { echo "FAIL: $leftover menu files survive uninstall"; exit 1; }
    [ ! -e "$fake_home/.local/bin/dolphin-context-actions" ] \
        || { echo "FAIL: bin link survives uninstall"; exit 1; }

    step "uninstall $product when nothing is installed exits 0 (servicemenuinstaller retries)"
    run_installer --deinstall

    step "no-argument call installs $product (older servicemenuinstaller fallback)"
    run_installer
    [ -x "$launcher" ]
    run_installer --uninstall
}

exercise_product context-actions dolphin-context-actions conversions
exercise_product archive dolphin-archive
exercise_product link dolphin-link

# Shared HOME: uninstalling one product must not remove another product's menus.
step "shared HOME: context-actions then archive; uninstall archive only"
shared_home="$work/home-shared"
mkdir -p "$shared_home"

extract_product() {
    local product="$1"
    local app_slug="$2"
    local prefix tarball pkg_dir

    "$repo/scripts/build-ghns-package.sh" "$product" "$work/dist" >/dev/null
    prefix="dolphin-${app_slug#dolphin-}-servicemenu"
    tarball=""
    for f in "$work/dist/${prefix}-v"*.tar.gz; do
        [ -f "$f" ] || { echo "FAIL: no tarball for shared-HOME $product"; exit 1; }
        tarball="$f"
        break
    done
    mkdir -p "$shared_home/.local/share/servicemenu-download"
    tar -xzf "$tarball" -C "$shared_home/.local/share/servicemenu-download"
    pkg_dir=""
    for d in "$shared_home/.local/share/servicemenu-download/${prefix}-v"*; do
        [ -d "$d" ] || { echo "FAIL: extract dir missing for shared-HOME $product"; exit 1; }
        pkg_dir="$d"
        break
    done
    printf '%s' "$pkg_dir"
}

run_shared_installer() {
    local pkg_dir="$1"
    shift
    HOME="$shared_home" XDG_DATA_HOME="$shared_home/.local/share" \
        bash "$pkg_dir/install.sh" "$@"
}

ctx_pkg="$(extract_product context-actions dolphin-context-actions)"
run_shared_installer "$ctx_pkg" --install

arc_pkg="$(extract_product archive dolphin-archive)"
run_shared_installer "$arc_pkg" --install

shared_menu_dir="$shared_home/.local/share/kio/servicemenus"
shared_ctx_dir="$shared_home/.local/share/dolphin-context-actions"
shared_arc_dir="$shared_home/.local/share/dolphin-archive"

[ -x "$shared_menu_dir/dolphin-context-actions.desktop" ] \
    || { echo "FAIL: shared HOME missing dolphin-context-actions.desktop before archive uninstall"; exit 1; }
[ -x "$shared_menu_dir/dolphin-audio-converter.desktop" ] \
    || { echo "FAIL: shared HOME missing dolphin-audio-converter.desktop before archive uninstall"; exit 1; }
[ -x "$shared_menu_dir/dolphin-archive.desktop" ] \
    || { echo "FAIL: shared HOME missing dolphin-archive.desktop before archive uninstall"; exit 1; }

ctx_gen_count="$(find "$shared_menu_dir" -name 'dolphin-context-actions-convert-*.desktop' | wc -l)"
echo "   shared HOME has $ctx_gen_count generated Convert menus before archive uninstall"
if [ "$ctx_gen_count" -gt 0 ]; then
    sample_gen="$(find "$shared_menu_dir" -name 'dolphin-context-actions-convert-*.desktop' | head -n 1)"
    [ -f "$sample_gen" ] \
        || { echo "FAIL: sample convert menu missing before archive uninstall"; exit 1; }
fi

run_shared_installer "$arc_pkg" --uninstall

[ -d "$shared_ctx_dir" ] \
    || { echo "FAIL: context-actions app dir removed when uninstalling archive"; exit 1; }
[ -x "$shared_menu_dir/dolphin-context-actions.desktop" ] \
    || { echo "FAIL: dolphin-context-actions.desktop removed when uninstalling archive"; exit 1; }
[ -x "$shared_menu_dir/dolphin-audio-converter.desktop" ] \
    || { echo "FAIL: dolphin-audio-converter.desktop removed when uninstalling archive"; exit 1; }
if [ "$ctx_gen_count" -gt 0 ]; then
    after_gen_count="$(find "$shared_menu_dir" -name 'dolphin-context-actions-convert-*.desktop' | wc -l)"
    [ "$after_gen_count" -eq "$ctx_gen_count" ] \
        || { echo "FAIL: convert menus changed ($ctx_gen_count -> $after_gen_count) when uninstalling archive"; exit 1; }
fi
[ ! -e "$shared_menu_dir/dolphin-archive.desktop" ] \
    || { echo "FAIL: dolphin-archive.desktop survives archive uninstall in shared HOME"; exit 1; }
[ ! -e "$shared_arc_dir" ] \
    || { echo "FAIL: dolphin-archive app dir survives archive uninstall in shared HOME"; exit 1; }

echo
echo "PASS: GHNS package installs, runs, and uninstalls cleanly."
