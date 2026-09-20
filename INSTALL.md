# Installation

## Quick reference

| Method | Command |
|---|---|
| From source | `make install` (needs rustc/cargo) |
| GHNS | Download New Services (ships a prebuilt helper) |
| Debian/Ubuntu | `sudo apt install ./dolphin-context-actions_0.1.0-1_all.deb` |
| Fedora | `sudo dnf install dolphin-context-actions-0.1.0-1.noarch.rpm` |
| openSUSE | `sudo zypper install dolphin-context-actions-0.1.0-1.noarch.rpm` |
| Arch Linux | `makepkg -si` (from `packaging/arch/`) |
| Snap | `snap install dolphin-context-actions` |
| Flatpak | `flatpak install --from https://flathub.org/…` |
| AppImage | Download from Releases, `chmod +x`, run |

pipx, uvx, and `pip install` are gone on this branch.

After any method, restart Dolphin to load the menus:

```bash
killall dolphin
```

---

## From Dolphin (GHNS)

Open **Settings → Configure Dolphin → Context Menu → Download New
Services…**, search for **Dolphin Context Actions**, and install. That
package ships a prebuilt helper. No rustc, no cargo.

## From source (make)

```bash
git clone https://github.com/bodencrouch/dolphin-context-actions.git
cd dolphin-context-actions
make install
```

This runs `cargo install --path . --root ~/.local --force`, installs the
media service menus, and builds `kio-plugin/` — the context-menu plugins
(Pick Link Source / Drop Link As, and the Archive submenu) and the KAuth
privileged-link helper — then installs both to system paths. Only the
final `cmake --install` step uses `sudo`; `make install` refuses to run
under `sudo` itself (see
[`scripts/check-privileged-pth.py`](scripts/check-privileged-pth.py) for
the leftover pip-as-root check).

GHNS ships a prebuilt helper and the conversion menus. Debian/Ubuntu,
Fedora, openSUSE, Arch, Snap, Flatpak, and AppImage still package the old
Python CLI and service menus only. None of them currently build
`kio-plugin/`, so **Pick Link Source** / **Drop Link As** (the whole Link
Shell Extension feature set, not just the elevated case) and the
**Archive** submenu are unavailable from any of those.
`make install` / `install.sh`, run from a source checkout, is the only path
that provides them today.

## Debian / Ubuntu (.deb)

Build the package:

```bash
sudo apt install devscripts debhelper dh-python python3-all python3-setuptools
dpkg-buildpackage -us -uc
```

Install:

```bash
sudo apt install ../dolphin-context-actions_0.1.0-1_all.deb
```

## Fedora / openSUSE (.rpm)

Build the package:

```bash
sudo dnf install rpm-build python3-devel python3-setuptools
rpmbuild -ba packaging/rpm/dolphin-context-actions.spec
```

Install:

```bash
sudo rpm -i ~/rpmbuild/RPMS/noarch/dolphin-context-actions-0.1.0-1.noarch.rpm
```

## Arch Linux (PKGBUILD)

```bash
cd packaging/arch
makepkg -si
```

Or use an AUR helper:

```bash
yay -S dolphin-context-actions
```

## Snap

```bash
snap install dolphin-context-actions
```

## Flatpak

```bash
flatpak install flathub io.github.bodencrouch.dolphin-context-actions
```

## AppImage

Download the latest AppImage from the [Releases page](https://github.com/bodencrouch/dolphin-context-actions/releases).

```bash
chmod +x dolphin-context-actions-*.AppImage
./dolphin-context-actions-*.AppImage
```

## Requirements

Runtime dependencies:

- **ffmpeg** (≥ 4.4) with `ffprobe`
- **kdialog** (KDE dialog tool)
- **libnotify** (desktop notifications)
- **KIO 6** (Dolphin context-menu integration)

From-source builds also need **Rust 1.75+ (cargo)**. GHNS ships a prebuilt
helper, so that path does not.

General conversions use optional tools. Install only the ones you need:

- PyMuPDF for PDF text extraction
- LibreOffice for office documents
- pandoc for Markdown and HTML
- ImageMagick for image formats

Building from source also requires rustc/cargo, CMake, Extra CMake Modules,
Qt 6 headers, and KIO 6 headers. On Fedora:

```bash
sudo dnf install rust cargo cmake extra-cmake-modules qt6-qtbase-devel \
  kf6-kcoreaddons-devel kf6-kio-devel
```

Install the runtime tools:

```bash
# Debian/Ubuntu
sudo apt install ffmpeg kdialog libnotify-bin

# Fedora
sudo dnf install ffmpeg kdialog libnotify

# Arch
sudo pacman -S ffmpeg kdialog libnotify

# openSUSE
sudo zypper install ffmpeg kdialog libnotify
```

## Service menu location

The `.desktop` files get installed to one of these paths:

| Install method | Path |
|---|---|
| System package (.deb/.rpm/Arch) | `/usr/share/kio/servicemenus/` |
| make / GHNS (user) | `~/.local/share/kio/servicemenus/` |

The link plugin installs under Qt's `kf6/kfileitemaction` plugin directory.
On Fedora, that path is `/usr/lib64/qt6/plugins/kf6/kfileitemaction/`.

To see which files are installed:

```bash
ls -la ~/.local/share/kio/servicemenus/dolphin-context-actions*
```

## Troubleshooting

### Menus don't appear in Dolphin

1. Restart Dolphin: `killall dolphin`
2. Check the service menu files are installed (see above)
3. Check `dolphinlinkfileitemaction.so` and `dolphinarkfileitemaction.so`
   are in Qt's plugin directory
4. Verify the binary is in PATH: `which dolphin-context-actions`
5. Test the CLI directly: `dolphin-context-actions --help`
6. Check Dolphin's service menu directory config under
   **Configure Dolphin → Context Menu → Download New Services…**

### "ffmpeg not found" error

Make sure ffmpeg is installed and available in your PATH:

```bash
ffmpeg -version
```
