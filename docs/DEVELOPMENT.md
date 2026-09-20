# Development

## Setup

```bash
git clone https://github.com/bodencrouch/dolphin-context-actions.git
cd dolphin-context-actions
cargo test
cargo install --path . --root ~/.local --force
```

Never run the whole install as root/sudo/pkexec —
see [`scripts/check-privileged-pth.py`](../scripts/check-privileged-pth.py)
for the leftover hazard from old `pip install -e .` installs.
`make install`/`install.sh` already call `sudo` themselves for the
one step that needs it and refuse to run under `sudo`.

Makefile targets for development:

```bash
make install        # cargo install + service menus + KIO plugin/KAuth helper
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
  *Download New Services…* dialog runs (user-scope, no root, no cargo).
- `scripts/build-ghns-package.sh` — builds that archive; ships
  `target/release/dolphin-context-actions` plus `assets/conversions.yaml`.
- `tests/test_ghns_package.sh` — installs the archive into a throwaway
  `$HOME` and checks the full install/run/uninstall cycle.
- `scripts/publish-to-pling.sh` — pushes a release payload to the
  store.kde.org product (see `packaging/pling/PUBLISHING.md`).
- `promo/generate.py` — renders the store preview images and demo GIF.
- `.github/workflows/` — CI (`cargo test`, shellcheck, package test,
  KF6 plugin build in a Fedora container) and release-please releases.

## Project layout

```
src/
├── main.rs                  # CLI binary
├── lib.rs                   # crate root
├── cli.rs                   # CLI parser + smart menu dispatch
├── config.rs                # JSON config read/write
├── ui.rs                    # kdialog/qdbus progress bar helpers
├── link_ops.rs              # Link Shell Extension operations (hardlink/symlink/clone/copy)
├── archive_ops.rs           # 7-Zip-style Archive menu (extract/compress/hash)
├── file_converter.rs        # Document, data, and image conversion engines
├── file_converter_menus.rs  # Generate conversion service menus
├── uploaders.rs             # Imgur upload
├── bin/
│   └── generate_menus.rs    # Menu-generation helper
└── converters/
    ├── mod.rs               # unique_output() -- auto-rename on collision
    ├── ffmpeg_tools.rs      # Resolves an ffmpeg binary against actual encoder support
    ├── audio.rs             # Audio transcoding (7 formats)
    └── video.rs             # GIF/MP4/WebM/MKV + audio extraction

assets/
└── conversions.yaml         # Bundled conversion catalog

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

1. Add the preset to `preset()` and `AUDIO_FORMATS` in `src/converters/audio.rs`
2. Add a `[Desktop Action convertToXxx]` block to `servicemenus/dolphin-audio-converter.desktop`
3. Add the format to the smart menu choices in `src/cli.rs` (both `is_gif` and
   `is_audio` sections)

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
