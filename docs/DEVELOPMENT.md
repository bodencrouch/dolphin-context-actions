# Development

## Setup

```bash
git clone https://github.com/brunner56/dolphin-convert-actions.git
cd dolphin-convert-actions
pip install -e .
```

Makefile targets for development:

```bash
make install        # Install package + service menus
make install-menus  # Install just the .desktop files
make plugin-build   # Build the KIO link action and its probe
make install-plugin # Install the KIO plugin under Qt's plugin directory
make uninstall      # Remove package + service menus
```

After any change to the `.desktop` files, restart Dolphin:

```bash
killall dolphin
```

After changing `kio-plugin/`, rebuild and reinstall it before restarting
Dolphin:

```bash
make install-plugin
```

## Project layout

```
src/dolphin_convert_actions/
├── __init__.py              # Package marker
├── __main__.py              # `python -m dolphin_convert_actions` support
├── cli.py                   # CLI parser + smart menu dispatch
├── config.py                # JSON config read/write
├── ui.py                    # kdialog/qdbus progress bar helpers
├── converters/
│   ├── audio.py             # Audio transcoding (7 formats)
│   └── video.py             # GIF/MP4/WebM/MKV + audio extraction
└── uploaders.py             # Imgur upload
```

## Adding a new audio format

1. Add the preset to `AUDIO_PRESETS` in `src/dolphin_convert_actions/converters/audio.py`
2. Add a `[Desktop Action convertToXxx]` block to `servicemenus/dolphin-audio-converter.desktop`
3. Add the format to the smart menu choices in `cli.py` (both `is_gif` and
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
# Tag the release
git tag v1.0.0
git push --tags

# Build all package formats
python -m build                          # wheel + sdist
dpkg-buildpackage -us -uc                # .deb
rpmbuild -ba packaging/rpm/dolphin-convert-actions.spec  # .rpm

# Create a GitHub release with the built packages
```
