# Changelog

## Unreleased

- **Security: replaced pkexec-on-a-user-script elevation with a KAuth
  helper.** Dropping a hardlink/symlink into a directory you can't write to
  now prompts via polkit through a small, root-owned, D-Bus-activated
  helper (`kio-plugin/linkhelper.cpp`), not by elevating a script under
  `~/.local/bin` — pkexec performs no ownership check on its target, so the
  old approach was equivalent to granting root to anything that could write
  that file. Hardlink Clone / Symlink Clone / Smart Copy are not offered
  elevated, since they recurse over an arbitrarily large, user-controlled
  tree.
- **Fixed:** audio conversion silently used whichever `ffmpeg` was first on
  `PATH` even if it lacked the codec a format needed (e.g. OGG without
  `libvorbis`), dumping raw ffmpeg stderr instead of a clear error.
  `ffmpeg_tools.py` now resolves a binary against the encoders it actually
  reports.
- **Fixed:** converting a file could silently overwrite a same-named file
  already in the destination. Output paths now auto-rename on collision.
- **Fixed:** an invalid GIF preset crashed with an unhandled `KeyError`
  instead of a clean error dialog.
- **Fixed:** Configure's saved audio/GIF defaults were written but never
  read back by any conversion path.
- **Fixed:** a dead menu separator rendered as a blank, clickable row
  (kdialog has no separator concept).
- **Fixed:** `notify-send` blocked indefinitely with no notification daemon
  running, stalling the caller mid-operation; notifications are now
  fire-and-forget.
- **Fixed:** Link Properties and Enumerate Hardlinks mishandled broken
  symlinks and self-listing; picking a symlink stored its resolved target
  instead of the symlink itself.
- Added `scripts/check-privileged-pth.py` (`make doctor`) — detects
  root-owned `.pth` files pointing at user-writable directories, the class
  of local-privilege-escalation hazard a `sudo pip install -e .` / `pkexec
  pip install --user -e .` leaves behind. `install.sh` and `make
  install`/`make pip-install` now refuse to run as root themselves.
- Removed dead code: `link_ops.smart_move()` (unfinished, unreachable),
  `converters/audio.py:detect()`, `uploaders.py:is_gif()`.
- Removed `.commandcode/` and `.swival/` — unrelated AI-tool skill
  definitions that had been committed to the repository by mistake.
- Documented that Flatpak/Snap/AppImage cannot host the KAuth helper
  (sandboxing has no path to install a system D-Bus service or polkit
  action), and that the rpm/deb/Arch packages don't currently build
  `kio-plugin/` either — only `install.sh`/`make install` (source checkout)
  provide the Link Shell Extension features today.

## 0.1.0 (2026-07-29)

First release as **Dolphin Context Actions** (renamed from
`dolphin-convert-actions`, versioning restarts from 0.1.0).

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
- **Link Shell Extension integration** — stateful KIO plugin:
  - Pick Link Source appears at the context-menu root
  - Drop Link As and Cancel Link Creation replace it after a pick
  - Drop Link As contains hardlink, symlink, clone, smart copy, and inspection actions
  - Link Properties — view link information including reference counts,
    inodes, and sibling locations
  - Enumerate Hardlinks — find all hardlink siblings of a file
  - Auto Rename — automatic renaming when creating links in same directory
  - Persistent source storage — picked sources survive between sessions
- Packaging: Python wheel, Debian (.deb), RPM, Arch Linux (PKGBUILD), Snap,
  Flatpak, AppImage
- Install via pipx, uv, pip, or from source with `make install`
