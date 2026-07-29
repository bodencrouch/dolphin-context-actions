# Dolphin Context Actions

Adds media conversion and file-linking actions to Dolphin's right-click menu.

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

### Create links from Dolphin

The context menu follows the stateful flow from [Link Shell Extension](https://schinagl.priv.at/nt/hardlinkshellext/linkshellextension.html):

- Before picking, **Pick Link Source** appears at the context-menu root.
- After picking, **Drop Link As** and **Cancel Link Creation** replace it.
- **Drop Link As** expands to hardlink, symlink, clone, copy, and inspection actions.
- Successful drops return the menu to **Pick Link Source**.
- Failed drops keep the source so you can retry.

## Installation

See [INSTALL.md](INSTALL.md) for every available method:

| Method | Command |
|---|---|
| pipx | `pipx install dolphin-context-actions` |
| uvx | `uvx dolphin-context-actions` |
| pip (user) | `pip install --user dolphin-context-actions` |
| Debian/Ubuntu | `sudo apt install ./dolphin-context-actions_0.1.0-1_all.deb` |
| Fedora/openSUSE | `sudo rpm -i dolphin-context-actions-0.1.0-1.noarch.rpm` |
| Arch Linux | `makepkg -si` (from packaging/arch/) |
| Snap | `snap install dolphin-context-actions` |
| Flatpak | `flatpak install io.github.bodencrouch.dolphin-context-actions` |
| AppImage | Download and run from releases |
| From source | `make install` |

Use the source install to enable the root **Pick Link Source** action.

After installation, restart Dolphin (`killall dolphin`) to load the service
menus. The **Context Actions** submenu will appear when you right-click any
supported media file.

## Quick start

1. Install (see above) and restart Dolphin
2. Right-click a `.gif` → **Context Actions** → **Convert to MP4**
3. Right-click a `.mp4` → **Context Actions** → **Convert to GIF…** →
   choose a preset
4. Right-click an `.mp3` → **Audio Converter** → **Convert to FLAC**
5. Right-click any image → **Context Actions** → **Upload to Imgur**

### Create a link

1. Right-click a file or folder, then choose **Pick Link Source**.
2. Right-click the destination folder or its background.
3. Open **Drop Link As**.
4. Choose **Drop Hardlink**, **Drop Symlink**, or another drop action.

## Configuration

Right-click any media file → **Context Actions** → **Configure…** to open
the settings dialog where you can set:

- **Default audio format** — the format used when converting audio
- **Default GIF preset** — the GIF quality preset used for video-to-GIF
- **Imgur Client ID** — required for Imgur upload (register at
  https://api.imgur.com/oauth2/addclient)

Configuration is stored in `~/.config/dolphin-context-actions/config.json`.

## Requirements

- **Python 3.10+**
- **ffmpeg** (≥ 4.4) with ffprobe (for media conversion only)
- **kdialog** (part of KDE)
- **libnotify** (for desktop notifications)
- **KIO 6** (for the root context-menu plugin)

## Limitations

### Link Shell Extension on Linux

The Link Shell Extension features work on most modern Linux filesystems (ext4, btrfs, xfs, etc.) but have some limitations compared to the Windows version:

- **No Junctions** — NTFS Junctions are Windows-specific and cannot be created on Linux
- **No Volume Mountpoints** — Windows Volume Mountpoints are not supported on Linux
- **Directory Hardlinks** — Unix filesystems do not allow them. Use **Drop Symlink**.
- **Smart Move** — Dolphin does not expose file moves to context-menu plugins.
- **Reparse Points** — NTFS-specific reparse point operations are not applicable on Linux
- **Backup Mode** — The Windows version's backup mode with elevated privileges is not implemented

The drop menu supports hardlinks, symlinks, clones, smart copy, link properties, and local hardlink enumeration.

## Project structure

```
├── pyproject.toml              # Python package definition
├── Makefile                    # Build/install/uninstall
├── install.sh                  # Manual install script
├── kio-plugin/                 # Stateful root context-menu plugin
├── debian/                     # Debian packaging
├── packaging/
│   ├── rpm/                    # RPM spec
│   ├── arch/                   # Arch Linux PKGBUILD
│   ├── snap/                   # Snapcraft yaml
│   ├── flatpak/                # Flatpak manifest
│   └── appimage/               # AppImage builder
├── servicemenus/
│   ├── dolphin-context-actions.desktop         # Smart menu (all media)
│   └── dolphin-audio-converter.desktop         # Dedicated audio submenu
├── src/dolphin_context_actions/
│   ├── cli.py                  # CLI entry point + smart dispatch
│   ├── ui.py                   # kdialog progress bars / dialogs
│   ├── config.py               # Config management
│   ├── link_ops.py             # Link Shell Extension operations
│   ├── converters/
│   │   ├── audio.py            # Audio transcoding (7 formats)
│   │   └── video.py            # GIF/MP4/WebM/MKV + audio extraction
│   └── uploaders.py            # Imgur upload
├── docs/
│   ├── CONFIGURATION.md        # Full config reference
│   └── DEVELOPMENT.md          # Contributor guide
└── man/
    └── dolphin-context-actions.1
```

## Building packages

```bash
# Python wheel
pip install build
python -m build

# Debian .deb
dpkg-buildpackage -us -uc

# RPM
rpmbuild -ba packaging/rpm/dolphin-context-actions.spec

# Arch Linux
cd packaging/arch && makepkg -si

# AppImage
appimage-builder --recipe packaging/appimage/AppImageBuilder.yml
```

## Licence

MIT
