#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

SERVICEDIR="${HOME}/.local/share/kio/servicemenus"
CONFIGDIR="${HOME}/.config/dolphin-convert-actions"

is_installed() {
    pip show dolphin-convert-actions >/dev/null 2>&1
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
        "servicemenus/dolphin-convert-actions.desktop"
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

    rm -f \
        "${SERVICEDIR}/dolphin-link-extension.desktop" \
        "${SERVICEDIR}/dolphin-link-extension-bg.desktop"
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

echo "=== Dolphin Convert Actions Installer ==="
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
