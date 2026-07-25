# Installation

## Quick reference

| Method | Command |
|---|---|
| pipx | `pipx install dolphin-convert-actions` |
| uvx | `uvx dolphin-convert-actions` |
| pip (user) | `pip install --user dolphin-convert-actions` |
| pip (system) | `sudo pip install dolphin-convert-actions` |
| From source | `make install` |
| Debian/Ubuntu | `sudo apt install ./dolphin-convert-actions_1.0.0-1_all.deb` |
| Fedora | `sudo dnf install dolphin-convert-actions-1.0.0-1.noarch.rpm` |
| openSUSE | `sudo zypper install dolphin-convert-actions-1.0.0-1.noarch.rpm` |
| Arch Linux | `makepkg -si` (from `packaging/arch/`) |
| Snap | `snap install dolphin-convert-actions` |
| Flatpak | `flatpak install --from https://flathub.org/…` |
| AppImage | Download from Releases, `chmod +x`, run |

After any method, restart Dolphin to load the service menus:

```bash
killall dolphin
```

---

## pipx (recommended)

[pipx](https://pipx.pypa.io/stable/) installs Python applications in
isolated environments and makes them available globally.

```bash
pipx install dolphin-convert-actions
```

### From source with pipx

```bash
git clone https://github.com/brunner56/dolphin-convert-actions.git
cd dolphin-convert-actions
pipx install .
```

## uvx

Using [uv](https://docs.astral.sh/uv/):

```bash
uvx dolphin-convert-actions
```

Or install permanently:

```bash
uv tool install dolphin-convert-actions
```

## pip (user install)

```bash
pip install --user dolphin-convert-actions
```

Ensure `~/.local/bin` is in your `PATH`.

## From source (make)

```bash
git clone https://github.com/brunner56/dolphin-convert-actions.git
cd dolphin-convert-actions
make install
```

This runs `pip install -e .` and copies the service menu `.desktop` files.

## Debian / Ubuntu (.deb)

Build the package:

```bash
sudo apt install devscripts debhelper dh-python python3-all python3-setuptools
dpkg-buildpackage -us -uc
```

Install:

```bash
sudo apt install ../dolphin-convert-actions_1.0.0-1_all.deb
```

## Fedora / openSUSE (.rpm)

Build the package:

```bash
sudo dnf install rpm-build python3-devel python3-setuptools
rpmbuild -ba packaging/rpm/dolphin-convert-actions.spec
```

Install:

```bash
sudo rpm -i ~/rpmbuild/RPMS/noarch/dolphin-convert-actions-1.0.0-1.noarch.rpm
```

## Arch Linux (PKGBUILD)

```bash
cd packaging/arch
makepkg -si
```

Or use an AUR helper:

```bash
yay -S dolphin-convert-actions
```

## Snap

```bash
snap install dolphin-convert-actions
```

## Flatpak

```bash
flatpak install flathub io.github.brunner56.dolphin-convert-actions
```

## AppImage

Download the latest AppImage from the [Releases page](https://github.com/brunner56/dolphin-convert-actions/releases).

```bash
chmod +x dolphin-convert-actions-*.AppImage
./dolphin-convert-actions-*.AppImage
```

## Requirements

All installation methods require these runtime dependencies:

- **Python 3.10+**
- **ffmpeg** (≥ 4.4) with `ffprobe`
- **kdialog** (KDE dialog tool)
- **libnotify** (desktop notifications)

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

To see which files are installed:

```bash
ls -la ~/.local/share/kio/servicemenus/dolphin-convert-actions*
```

## Troubleshooting

### Menus don't appear in Dolphin

1. Restart Dolphin: `killall dolphin`
2. Check the service menu files are installed (see above)
3. Verify the binary is in PATH: `which dolphin-convert-actions`
4. Test the CLI directly: `dolphin-convert-actions --help`
5. Check Dolphin's service menu directory config under
   **Configure Dolphin → Context Menu → Download New Services…**

### "ffmpeg not found" error

Make sure ffmpeg is installed and available in your PATH:

```bash
ffmpeg -version
```
