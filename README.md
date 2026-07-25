# Dolphin Convert Actions

Smart context-menu media converter for the Dolphin file manager (KDE).

Right-click any media file in Dolphin and get only the conversion actions
that make sense — no clutter, no irrelevant options.

## How it works

The menu adapts to the file you right-click:

| File type | Menu shows |
|---|---|
| `.gif` | Convert to MP4, WebM, MKV · Extract Audio · Upload to Imgur · Audio conversion |
| `.mp4` / `.webm` / `.mkv` / `.avi` / `.mov` | Convert to GIF (with quality presets) · Re-encode to MP4, WebM, MKV · Extract Audio · Upload to Imgur |
| `.mp3` / `.ogg` / `.flac` / `.wav` / `.m4a` / `.opus` | Convert between formats (source format is omitted) |
| `.png` / `.jpg` / `.jpeg` | Upload to Imgur |

A dedicated **Audio Converter** submenu also appears for audio and video
files with all 7 formats available as one-click actions.

## Features

- **Two-directional menus** — only relevant actions shown per file type
- **GIF from video** — 4 quality presets (Small/Medium/Large/Source) using
  ffmpeg's two-pass palettegen method for clean colours
- **Video transcoding** — re-encode between MP4 (H.264+AAC), WebM (VP9+Opus),
  and MKV (H.264+AAC)
- **Audio conversion** — 7 formats: MP3 (V0), OGG (Q6), FLAC, WAV, M4A (AAC
  192k), Opus (128k), ALAC
- **Audio extraction** — pull audio track from video files as MP3
- **Imgur upload** — upload images/GIFs/videos to Imgur with automatic
  clipboard copy of the URL
- **Progress bars** — real-time progress via kdialog/qdbus, cancel any job
- **Batch processing** — select multiple files, process them all at once
- **Configurable defaults** — set your preferred audio format and GIF preset
- **Notifications** — desktop notification on completion (or errors)

## Installation

See [INSTALL.md](INSTALL.md) for every available method:

| Method | Command |
|---|---|
| pipx | `pipx install dolphin-convert-actions` |
| uvx | `uvx dolphin-convert-actions` |
| pip (user) | `pip install --user dolphin-convert-actions` |
| Debian/Ubuntu | `sudo apt install ./dolphin-convert-actions_1.0.0-1_all.deb` |
| Fedora/openSUSE | `sudo rpm -i dolphin-convert-actions-1.0.0-1.noarch.rpm` |
| Arch Linux | `makepkg -si` (from packaging/arch/) |
| Snap | `snap install dolphin-convert-actions` |
| Flatpak | `flatpak install io.github.brunner56.dolphin-convert-actions` |
| AppImage | Download and run from releases |
| From source | `make install` |

After installation, restart Dolphin (`killall dolphin`) to load the service
menus. The **Convert Actions** submenu will appear when you right-click any
supported media file.

## Quick start

1. Install (see above) and restart Dolphin
2. Right-click a `.gif` → **Convert Actions** → **Convert to MP4**
3. Right-click a `.mp4` → **Convert Actions** → **Convert to GIF…** →
   choose a preset
4. Right-click an `.mp3` → **Audio Converter** → **Convert to FLAC**
5. Right-click any image → **Convert Actions** → **Upload to Imgur**

## Configuration

Right-click any media file → **Convert Actions** → **Configure…** to open
the settings dialog where you can set:

- **Default audio format** — the format used when converting audio
- **Default GIF preset** — the GIF quality preset used for video-to-GIF
- **Imgur Client ID** — required for Imgur upload (register at
  https://api.imgur.com/oauth2/addclient)

Configuration is stored in `~/.config/dolphin-convert-actions/config.json`.

## Requirements

- **Python 3.10+**
- **ffmpeg** (≥ 4.4) with ffprobe
- **kdialog** (part of KDE)
- **libnotify** (for desktop notifications)

## Project structure

```
├── pyproject.toml              # Python package definition
├── Makefile                    # Build/install/uninstall
├── install.sh                  # Manual install script
├── debian/                     # Debian packaging
├── packaging/
│   ├── rpm/                    # RPM spec
│   ├── arch/                   # Arch Linux PKGBUILD
│   ├── snap/                   # Snapcraft yaml
│   ├── flatpak/                # Flatpak manifest
│   └── appimage/               # AppImage builder
├── servicemenus/
│   ├── dolphin-convert-actions.desktop    # Smart menu (all media)
│   └── dolphin-audio-converter.desktop    # Dedicated audio submenu
├── src/dolphin_convert_actions/
│   ├── cli.py                  # CLI entry point + smart dispatch
│   ├── ui.py                   # kdialog progress bars / dialogs
│   ├── config.py               # Config management
│   ├── converters/
│   │   ├── audio.py            # Audio transcoding (7 formats)
│   │   └── video.py            # GIF/MP4/WebM/MKV + audio extraction
│   └── uploaders.py            # Imgur upload
├── docs/
│   ├── CONFIGURATION.md        # Full config reference
│   └── DEVELOPMENT.md          # Contributor guide
└── man/
    └── dolphin-convert-actions.1
```

## Building packages

```bash
# Python wheel
pip install build
python -m build

# Debian .deb
dpkg-buildpackage -us -uc

# RPM
rpmbuild -ba packaging/rpm/dolphin-convert-actions.spec

# Arch Linux
cd packaging/arch && makepkg -si

# AppImage
appimage-builder --recipe packaging/appimage/AppImageBuilder.yml
```

## Licence

MIT
