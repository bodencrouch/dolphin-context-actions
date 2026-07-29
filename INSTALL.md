# Installation

## Quick reference

| Method | Command |
|---|---|
| pipx | `pipx install dolphin-context-actions` |
| uvx | `uvx dolphin-context-actions` |
| pip (user) | `pip install --user dolphin-context-actions` |
| pip (system) | `sudo pip install dolphin-context-actions` |
| From source | `make install` |
| Debian/Ubuntu | `sudo apt install ./dolphin-context-actions_0.1.0-1_all.deb` |
| Fedora | `sudo dnf install dolphin-context-actions-0.1.0-1.noarch.rpm` |
| openSUSE | `sudo zypper install dolphin-context-actions-0.1.0-1.noarch.rpm` |
| Arch Linux | `makepkg -si` (from `packaging/arch/`) |
| Snap | `snap install dolphin-context-actions` |
| Flatpak | `flatpak install --from https://flathub.org/…` |
| AppImage | Download from Releases, `chmod +x`, run |

After any method, restart Dolphin to load the menus:

```bash
killall dolphin
```

---

## pipx (recommended)

[pipx](https://pipx.pypa.io/stable/) installs Python applications in
isolated environments and makes them available globally.

```bash
pipx install dolphin-context-actions
```

### From source with pipx

```bash
git clone https://github.com/bodencrouch/dolphin-context-actions.git
cd dolphin-context-actions
pipx install .
```

## uvx

Using [uv](https://docs.astral.sh/uv/):

```bash
uvx dolphin-context-actions
```

Or install permanently:

```bash
uv tool install dolphin-context-actions
```

## pip (user install)

```bash
pip install --user dolphin-context-actions
```

Ensure `~/.local/bin` is in your `PATH`.

## From source (make)

```bash
git clone https://github.com/bodencrouch/dolphin-context-actions.git
cd dolphin-context-actions
make install
```

This installs the Python package and media service menus. It also builds the
KIO plugin that provides the root link actions. The plugin install uses `sudo`.

The packaged Python-only methods do not install the KIO plugin. Use the source
install when you want **Pick Link Source** at the context-menu root.

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

All installation methods require these runtime dependencies:

- **Python 3.10+**
- **ffmpeg** (≥ 4.4) with `ffprobe`
- **kdialog** (KDE dialog tool)
- **libnotify** (desktop notifications)
- **KIO 6** (Dolphin context-menu integration)

Building from source also requires CMake, Extra CMake Modules, Qt 6 headers,
and KIO 6 headers. On Fedora:

```bash
sudo dnf install cmake extra-cmake-modules qt6-qtbase-devel \
  kf6-kcoreaddons-devel kf6-kio-devel
```

Install them:

```bash
# Debian/Ubuntu
sudo apt install ffmpeg kdialog libnotify-bin python3

# Fedora
sudo dnf install ffmpeg kdialog libnotify python3

# Arch
sudo pacman -S ffmpeg kdialog libnotify python

# openSUSE
sudo zypper install ffmpeg kdialog libnotify python3
```

## Service menu location

The `.desktop` files get installed to one of these paths:

| Install method | Path |
|---|---|
| System package (.deb/.rpm/Arch) | `/usr/share/kio/servicemenus/` |
| pip / make / pipx (user) | `~/.local/share/kio/servicemenus/` |

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
3. Check `dolphinlinkfileitemaction.so` is in Qt's plugin directory
4. Verify the binary is in PATH: `which dolphin-context-actions`
5. Test the CLI directly: `dolphin-context-actions --help`
6. Check Dolphin's service menu directory config under
   **Configure Dolphin → Context Menu → Download New Services…**

### "ffmpeg not found" error

Make sure ffmpeg is installed and available in your PATH:

```bash
ffmpeg -version
```
