# Development

## Setup

```bash
git clone https://github.com/bodencrouch/dolphin-context-actions.git
cd dolphin-context-actions
cmake -S . -B build/helper -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build/helper --parallel
DOLPHIN_CONTEXT_ACTIONS_HEADLESS=1 QT_QPA_PLATFORM=offscreen \
  ctest --test-dir build/helper --output-on-failure
cmake --install build/helper --prefix ~/.local
```

Never run the whole install as root/sudo/pkexec —
see [`scripts/check-privileged-pth.py`](../scripts/check-privileged-pth.py)
for the leftover hazard from old `pip install -e .` installs.
`make install`/`install.sh` already call `sudo` themselves for the
one step that needs it and refuse to run under `sudo`.

Makefile targets for development:

```bash
make install        # CMake helper + service menus + KIO plugin/KAuth helper
make install-menus  # Install just the .desktop files
make plugin-build   # Build the KIO plugin, KAuth helper, and probe
make install-plugin # Install the KIO plugin + KAuth helper to system paths
make uninstall      # Remove everything install added, including the KAuth helper
make doctor         # Scan for leftover root-owned .pth files from old pip installs
```

After any change to the `.desktop` files, restart Dolphin:

```bash
killall dolphin
```

After changing `kio-plugin/`, rebuild and reinstall it before restarting
Dolphin — this also re-installs the KAuth helper, so a plugin change and a
helper change are the same step:

```bash
make install-plugin
```

Reinstalling only replaces the files on disk; any Dolphin process already
running keeps the previously-loaded plugin in memory (a new `.so` on disk
doesn't get picked up by an existing process). `killall dolphin` after
`make install-plugin`, not just after `.desktop` changes.

## Store package and CI

- `ghns/install.sh` — installer bundled in the archive that Dolphin's
  *Download New Services…* dialog runs (user-scope, no root, no compiler).
- `scripts/build-ghns-package.sh` — builds that archive; ships
  `build/helper/helper/dolphin-context-actions` plus `assets/conversions.json`.
- `tests/test_ghns_package.sh` — installs the archive into a throwaway
  `$HOME` and checks the full install/run/uninstall cycle.
- `scripts/publish-to-pling.sh` — pushes a release payload to the
  store.kde.org product (see `packaging/pling/PUBLISHING.md`).
- `promo/generate.py` — renders the store preview images and demo GIF.
- `.github/workflows/` — CI (`ctest` for the helper, shellcheck, package test,
  KF6 plugin build in a Fedora container) and release-please releases.

## Project layout

```
helper/
├── main.cpp                 # CLI binary
├── cli.cpp                  # CLI parser + smart menu dispatch
├── config.cpp               # JSON config read/write
├── ui.cpp                   # kdialog/qdbus progress bar helpers
├── link_ops.cpp             # Link Shell Extension operations (hardlink/symlink/clone/copy)
├── archive_ops.cpp          # 7-Zip-style Archive menu (extract/compress/hash)
├── file_converter.cpp       # Document, data, and image conversion engines
├── file_converter_menus.cpp # Generate conversion service menus
├── uploaders.cpp            # Imgur upload
└── converters.cpp           # unique_output(), ffmpeg, audio, video

assets/
└── conversions.json         # Bundled conversion catalog

kio-plugin/
├── dolphinlinkfileitemaction.cpp        # Link context-menu plugin
├── dolphinlinkfileitemaction.json
├── dolphinarkfileitemaction.cpp         # Archive 7-Zip-style menu
├── dolphinarkfileitemaction.json
├── linkhelper.cpp                       # KAuth privileged helper (root-owned, D-Bus-activated)
└── io.github.bodencrouch.linkhelper.actions  # KAuth/polkit action policy

scripts/
└── check-privileged-pth.py  # `make doctor` -- leftover pip-install-as-root check
```

## Adding a new audio format

1. Add the preset to the audio table in `helper/converters.cpp`
2. Add a `[Desktop Action convertToXxx]` block to `servicemenus/dolphin-audio-converter.desktop`
3. Add the format to the smart menu choices in `helper/cli.cpp`

## How the smart menu works

When `--smart-menu` is called:

1. The MIME type of the first selected file is detected via `file --mime-type`
2. The file extension provides a secondary heuristic
3. Based on the type classification (gif/video/audio/image), a kdialog menu
   is built with only relevant actions
4. The user's choice dispatches to the appropriate converter

## Releasing

```bash
# Tag the release (versioning restarted at 0.1.0 -- see CHANGELOG.md)
git tag v0.1.0
git push --tags

# Build remaining package formats
dpkg-buildpackage -us -uc                # .deb
rpmbuild -ba packaging/rpm/dolphin-context-actions.spec  # .rpm

# Create a GitHub release with the built packages
```
