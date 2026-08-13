#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# `pip install --user -e .` writes a .pth file pointing back at this checkout.
# Run as root (sudo/pkexec on the whole script, not just the plugin install
# step below) that .pth file ends up root-owned while still pointing at a
# directory this user can write to -- any root Python process then has a
# user-writable directory on its import path. Hit this exact class of bug
# twice already; refusing outright is cheaper than relying on cleanup after
# the fact. `sudo` is still used, but only for the one step that needs it.
if [ "$(id -u)" -eq 0 ]; then
    echo "Do not run this installer as root (or via sudo/pkexec)." >&2
    echo "It calls sudo itself for the one step that needs it." >&2
    exit 1
fi

SERVICEDIR="${HOME}/.local/share/kio/servicemenus"
CONFIGDIR="${HOME}/.config/dolphin-context-actions"
REGISTRY_SRC="src/dolphin_context_actions/conversions.yaml"
REGISTRY_DST="${CONFIGDIR}/conversions.yaml"

is_installed() {
    pip show dolphin-context-actions >/dev/null 2>&1
}

install_python_package() {
    if is_installed; then
        echo "Python package already installed. Updating..."
        pip install --user -e . --quiet
    else
        echo "Installing Python package..."
        pip install --user -e .
    fi
}

install_service_menus() {
    mkdir -p "$SERVICEDIR" "$CONFIGDIR"

    local desktop_files=(
        "servicemenus/dolphin-context-actions.desktop"
        "servicemenus/dolphin-audio-converter.desktop"
    )

    for f in "${desktop_files[@]}"; do
        local basename
        basename="$(basename "$f")"
        local dest="${SERVICEDIR}/${basename}"

        if [ -f "$dest" ]; then
            if cmp -s "$f" "$dest"; then
                echo "  ${basename} already up to date."
            else
                echo "  Updating ${basename}..."
            fi
            cp "$f" "$dest"
            chmod +x "$dest"
        else
            echo "  Installing ${basename}..."
            cp "$f" "$dest"
            chmod +x "$dest"
        fi
    done

    if [ ! -f "$REGISTRY_DST" ]; then
        cp "$REGISTRY_SRC" "$REGISTRY_DST"
    fi

    python3 -m dolphin_context_actions.file_converter_menus \
        --output-dir "$SERVICEDIR" \
        --converter-bin "${HOME}/.local/bin/dolphin-context-actions" \
        --registry "$REGISTRY_DST"

    # Menus installed under former project names still call the old executable,
    # so they show up in Dolphin and silently do nothing when clicked.
    rm -f \
        "${SERVICEDIR}/dolphin-link-extension.desktop" \
        "${SERVICEDIR}/dolphin-link-extension-bg.desktop" \
        "${SERVICEDIR}/dolphin-convert-actions.desktop"
    rm -f "${SERVICEDIR}"/dolphin-file-converter-*.desktop
}

install_link_plugin() {
    cmake \
        -S kio-plugin \
        -B build/kio-plugin \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=ON \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build/kio-plugin --parallel
    sudo cmake --install build/kio-plugin
}

echo "=== Dolphin Context Actions Installer ==="
echo ""

install_python_package
echo ""

install_service_menus
echo ""

install_link_plugin
echo ""

echo "=== Installation complete ==="
echo "  Service menus: ${SERVICEDIR}/"
echo "  Link plugin:   /usr/lib64/qt6/plugins/kf6/kfileitemaction/"
echo "  Config dir:    ${CONFIGDIR}/"
echo "  Bin dir:       ${HOME}/.local/bin/"
echo ""
echo "Restart Dolphin (killall dolphin) to reload service menus."
echo ""
echo "If you ever install this package's Python component as root (sudo/pkexec"
echo "pip install), run scripts/check-privileged-pth.py --fix afterward."
