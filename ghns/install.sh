#!/usr/bin/env bash
# Installer for the Dolphin Context Actions service-menu package.
#
# Dolphin's "Download New Services…" dialog downloads this archive and runs
# this script through `servicemenuinstaller`, as the logged-in user, with no
# root and no way to ask questions. Everything here must therefore be
# user-scope, non-interactive, and safe to run twice.
#
# servicemenuinstaller calls:
#   install.sh --install      (some versions try --local-install or no args)
#   install.sh --uninstall    (some versions try --deinstall)
#
# What install does:
#   1. Copies the bundled Linux binary to  $XDG_DATA_HOME/dolphin-context-actions/
#   2. Links it from ~/.local/bin (best effort)
#   3. Installs the service menus to        $XDG_DATA_HOME/kio/servicemenus/
#      with Exec= rewritten to the absolute launcher path, because Dolphin
#      often runs without ~/.local/bin on PATH
#   4. Generates the per-filetype "Convert" menus for the tools present on
#      THIS machine (ffmpeg, libreoffice, pandoc, …) via the bundled binary
#   5. Records every installed file so uninstall removes exactly that
#
# No python3, pip, compilers, or sudo.
set -u

HERE="$(cd -- "$(dirname -- "$0")" && pwd)"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
APP_DIR="$DATA_HOME/dolphin-context-actions"
MENU_DIR="$DATA_HOME/kio/servicemenus"
BIN_DIR="$HOME/.local/bin"
LAUNCHER="$APP_DIR/dolphin-context-actions"
MANIFEST="$APP_DIR/installed-files.txt"
GENERATED_PREFIX="dolphin-context-actions-convert"

log() { printf '%s\n' "$*" >&2; }

fail() {
    log "ERROR: $*"
    exit 1
}

refresh_menus() {
    # Dolphin picks servicemenus up on the next context menu open; a sycoca
    # rebuild helps older setups. Never let it fail the install.
    if command -v kbuildsycoca6 >/dev/null 2>&1; then
        kbuildsycoca6 >/dev/null 2>&1 || true
    fi
}

notice() {
    # Fire and forget: notify-send can hang with no notification daemon.
    if command -v notify-send >/dev/null 2>&1; then
        notify-send -i document-convert -a "Context Actions" "$1" "$2" \
            >/dev/null 2>&1 &
    fi
}

do_install() {
    [ -x "$HERE/dolphin-context-actions" ] || fail "Package payload missing next to install.sh."
    [ -d "$HERE/servicemenus" ] || fail "Service menus missing next to install.sh."

    # Re-install cleanly over any previous version.
    [ -f "$MANIFEST" ] && do_uninstall quiet

    mkdir -p "$APP_DIR" "$MENU_DIR" || fail "Cannot create $APP_DIR."
    : > "$MANIFEST".tmp

    # 1. The Linux helper binary -- not compiled here, not pip-installed.
    cp "$HERE/dolphin-context-actions" "$LAUNCHER" || fail "Copy failed."
    chmod 0755 "$LAUNCHER"
    printf '%s\n' "$LAUNCHER" >> "$MANIFEST".tmp

    for extra in conversions.yaml conversions.json; do
        if [ -f "$HERE/$extra" ]; then
            cp "$HERE/$extra" "$APP_DIR/$extra" || fail "Copy failed."
            printf '%s\n' "$APP_DIR/$extra" >> "$MANIFEST".tmp
        fi
    done

    # Best-effort ~/.local/bin link: nice for terminals, and the optional
    # Link Shell Extension KIO plugin looks the command up there. Never
    # overwrite something that isn't ours (e.g. a cargo install).
    if mkdir -p "$BIN_DIR" 2>/dev/null; then
        if [ ! -e "$BIN_DIR/dolphin-context-actions" ] || [ -L "$BIN_DIR/dolphin-context-actions" ]; then
            ln -sfn "$LAUNCHER" "$BIN_DIR/dolphin-context-actions" \
                && printf '%s\n' "$BIN_DIR/dolphin-context-actions" >> "$MANIFEST".tmp
        fi
    fi

    # 2. Static service menus, Exec rewritten to the absolute launcher.
    for menu in "$HERE"/servicemenus/*.desktop; do
        [ -e "$menu" ] || continue
        target="$MENU_DIR/$(basename "$menu")"
        sed "s|^Exec=dolphin-context-actions |Exec=\"$LAUNCHER\" |" "$menu" > "$target" \
            || fail "Cannot write $target."
        chmod 0755 "$target"   # KDE only runs executable service menus
        printf '%s\n' "$target" >> "$MANIFEST".tmp
    done

    # 3. Per-filetype Convert menus, generated against the tools installed on
    #    this machine. Not fatal: the static menus above already work.
    if ! "$LAUNCHER" --generate-menus --output-dir "$MENU_DIR" --converter-bin "$LAUNCHER" \
        >> "$MANIFEST".tmp 2>/dev/null; then
        log "Note: no Convert menus generated (no supported conversion tools found yet)."
        log "They appear automatically after: install.sh --install with the tools present."
    fi

    mv "$MANIFEST".tmp "$MANIFEST"
    refresh_menus
    notice "Context Actions installed" "Right-click a file in Dolphin to convert it."
    log "Installed. Right-click a media or document file in Dolphin."
    return 0
}

do_uninstall() {
    quiet="${1:-}"
    if [ -f "$MANIFEST" ]; then
        while IFS= read -r file; do
            case "$file" in
                "$APP_DIR"/*|"$MENU_DIR/$GENERATED_PREFIX"*|"$MENU_DIR"/dolphin-*.desktop|"$BIN_DIR"/dolphin-context-actions)
                    rm -f -- "$file" ;;
            esac
        done < "$MANIFEST"
        rm -f -- "$MANIFEST"
    fi
    # Belt and braces for older manifests: generated menus carry our prefix.
    rm -f -- "$MENU_DIR/$GENERATED_PREFIX"-*.desktop 2>/dev/null
    # Only remove the bin link if it points at us.
    if [ -L "$BIN_DIR/dolphin-context-actions" ]; then
        case "$(readlink "$BIN_DIR/dolphin-context-actions")" in
            "$APP_DIR"/*) rm -f -- "$BIN_DIR/dolphin-context-actions" ;;
        esac
    fi
    rm -rf -- "$APP_DIR"
    refresh_menus
    if [ "$quiet" != quiet ]; then
        notice "Context Actions removed" "The context menu entries are gone."
        log "Uninstalled."
    fi
    return 0
}

case "${1:-<none>}" in
    --install|--local-install|"<none>")
        do_install ;;
    --uninstall|--deinstall)
        do_uninstall ;;
    *)
        log "Usage: $0 [--install|--uninstall]"
        exit 1 ;;
esac
