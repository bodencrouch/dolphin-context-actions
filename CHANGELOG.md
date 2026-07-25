# Changelog

## 1.0.0 (2026-07-25)

- Smart two-directional context menus for Dolphin — shows only relevant
  conversion actions based on file MIME type
- GIF creation from video with 4 quality presets (Small 480px, Medium 800px,
  Large 1280px, Source size) using ffmpeg two-pass palettegen
- Video transcoding to MP4 (H.264+AAC), WebM (VP9+Opus), and MKV
  (H.264+AAC) with progress bars
- Audio extraction from video files to MP3
- Audio conversion to 7 formats: MP3 (V0), OGG (Q6), FLAC, WAV, M4A (AAC
  192k), Opus (128k), ALAC
- Dedicated Audio Converter submenu with all 7 formats as one-click actions
- Imgur upload for images/GIFs/videos with clipboard copy of the URL
- kdialog progress bars with real-time ffmpeg progress and cancel support
- Configurable defaults (audio format, GIF preset, Imgur Client ID) via
  kdialog configure dialog
- Desktop notifications on job completion
- Batch processing of multiple selected files
- Packaging: Python wheel, Debian (.deb), RPM, Arch Linux (PKGBUILD), Snap,
  Flatpak, AppImage
- Install via pipx, uv, pip, or from source with `make install`
