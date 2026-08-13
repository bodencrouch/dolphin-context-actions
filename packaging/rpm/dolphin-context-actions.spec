%define pname dolphin-context-actions
%define ver 0.1.0
%define unver 0.1.0

Name:     %{pname}
Version:  %{ver}
Release:  1%{?dist}
Summary:  Media conversion and file-linking actions for Dolphin's context menu

License:  MIT
URL:      https://github.com/bodencrouch/dolphin-context-actions
Source0:  %{url}/archive/v%{unver}/%{pname}-%{unver}.tar.gz

BuildArch: noarch
BuildRequires: python3-devel, python3-setuptools
Requires:  python3 >= 3.10, python3-pyyaml, ffmpeg >= 4.4, kdialog, libnotify

%description
Adds intelligent right-click actions to Dolphin (KDE file manager) for
converting media files. The menu adapts to file type — right-click a GIF
to convert it to MP4 or upload to Imgur; right-click a video to convert
to GIF with quality presets; right-click an audio file to transcode to
any of 7 formats (MP3, OGG, FLAC, WAV, M4A, Opus, ALAC).

Features:
  * Two-directional menus — only relevant actions shown per file type
  * GIF creation from video (4 quality presets via ffmpeg palettegen)
  * Video transcoding (MP4, WebM, MKV) and audio extraction
  * Audio conversion to 7 formats with configurable defaults
  * Imgur upload with clipboard copy
  * Progress bars via kdialog/qdbus
  * Batch processing of multiple files
  * Notifications on completion

%prep
%autosetup -n %{pname}-%{unver}

%build
%py3_build

%install
%py3_install
# Install Dolphin service menus
install -d %{buildroot}%{_datadir}/kio/servicemenus
install -m 644 servicemenus/dolphin-context-actions.desktop \
  %{buildroot}%{_datadir}/kio/servicemenus/
install -m 644 servicemenus/dolphin-audio-converter.desktop \
  %{buildroot}%{_datadir}/kio/servicemenus/

%files
%{_bindir}/dolphin-context-actions
%{python3_sitelib}/dolphin_context_actions*
%{_datadir}/kio/servicemenus/dolphin-context-actions.desktop
%{_datadir}/kio/servicemenus/dolphin-audio-converter.desktop
%license LICENSE
%doc README.md

%changelog
* Sat Jul 25 2026 Brunner56 <brunner56@users.noreply.github.com> - 0.1.0-1
- Initial package
