import argparse
import shutil
import subprocess
import sys
from pathlib import Path

from . import config as cfgmod
from . import link_ops, ui, uploaders
from .converters import audio, video


def _ffmpeg_check():
    for tool in ("ffmpeg", "ffprobe"):
        if not shutil.which(tool):
            ui.error_dialog(
                "Context Actions - Missing Dependency",
                f"<b>{tool}</b> not found.\n\n"
                "Install ffmpeg:\n"
                "• <tt>sudo apt install ffmpeg</tt> (Debian/Ubuntu/Mint)\n"
                "• <tt>sudo dnf install ffmpeg</tt> (Fedora)\n"
                "• <tt>sudo pacman -S ffmpeg</tt> (Arch/Manjaro)\n"
                "• <tt>sudo zypper install ffmpeg</tt> (openSUSE)",
            )
            sys.exit(1)


def mime_type(filepath: str) -> str | None:
    try:
        r = subprocess.run(
            ["file", "--mime-type", "--brief", filepath],
            capture_output=True, text=True, timeout=5,
        )
        return r.stdout.strip()
    except Exception:
        return None


def smart_menu(files: list[str]):
    if not files:
        ui.error_dialog("Context Actions", "No files selected.")
        return

    _ffmpeg_check()
    mime = mime_type(files[0]) or ""
    path = Path(files[0])
    ext = path.suffix.lower()

    is_gif = mime == "image/gif" or ext == ".gif"
    is_video = mime.startswith("video/") or ext in (".mp4", ".webm", ".mkv", ".avi", ".mov", ".wmv", ".flv")
    is_audio = mime.startswith("audio/") or ext in (".mp3", ".ogg", ".flac", ".wav", ".m4a", ".opus", ".wma", ".aac")
    is_image = mime.startswith("image/") and not is_gif

    choices = []

    if is_image:
        choices = [
            ("upload_imgur", "Upload to Imgur"),
        ]
    elif is_gif:
        choices = [
            ("to_mp4", "Convert to MP4"),
            ("to_webm", "Convert to WebM"),
            ("to_mkv", "Convert to MKV"),
            ("extract_audio", "Extract Audio"),
            ("upload_imgur", "Upload to Imgur"),
        ]
        for fmt in sorted(audio.AUDIO_FORMATS):
            preset = audio.AUDIO_PRESETS[fmt]
            fmt_name = fmt.upper() if fmt != "m4a" else "M4A"
            label_extra = f" ({preset['name'].split('(', 1)[1].rstrip(')')})" if '(' in preset['name'] else ""
            choices.append((f"audio_{fmt}", f"Convert Audio to {fmt_name}{label_extra}"))
    elif is_video:
        choices = [
            ("to_gif", "Convert to GIF…"),
            ("to_mp4", "Convert to MP4"),
            ("to_webm", "Convert to WebM"),
            ("to_mkv", "Convert to MKV"),
            ("extract_audio", "Extract Audio"),
            ("upload_imgur", "Upload to Imgur"),
        ]
    elif is_audio:
        source_fmt = ext.lstrip(".")
        choices = []
        for fmt in sorted(audio.AUDIO_FORMATS):
            if fmt == source_fmt:
                continue
            preset = audio.AUDIO_PRESETS[fmt]
            choices.append((f"audio_{fmt}", f"Convert to {preset['name']}"))
        if not choices:
            ui.info_dialog("Audio Converter", f"Already in the only supported format ({source_fmt.upper()}).")
            return
    else:
        ui.error_dialog("Context Actions", f"Unsupported file type: {mime or ext}")
        return

    choice = ui.menu_dialog(
        "Context Actions",
        f"Actions for: {Path(files[0]).name}",
        choices,
    )

    if not choice:
        return

    if choice == "to_gif":
        preset_choice = ui.menu_dialog(
            "Convert to GIF",
            "Select quality preset:",
            [(k, v) for k, v in video.GIF_PRESET_NAMES.items()],
        )
        if preset_choice:
            video.to_gif(files, preset_choice)
    elif choice == "to_mp4":
        video.to_mp4(files, ".mp4")
    elif choice == "to_webm":
        video.to_mp4(files, ".webm")
    elif choice == "to_mkv":
        video.to_mp4(files, ".mkv")
    elif choice == "extract_audio":
        video.extract_audio(files)
    elif choice == "upload_imgur":
        uploaders.imgur_upload(files)
    elif choice.startswith("audio_"):
        fmt = choice[6:]
        audio.convert_files(files, fmt)
    elif choice == "configure":
        run_configure()


def batch_convert(files: list[str], mode: str, param: str | None = None):
    _ffmpeg_check()

    # Without an explicit --format, fall back to what Configure… saved.
    cfg = cfgmod.load()

    if mode == "audio":
        fmt = param or cfg.get("audio_preset", "mp3")
        audio.convert_files(files, fmt)
    elif mode == "video-to-gif":
        preset = param or cfg.get("video_preset", "medium")
        video.to_gif(files, preset)
    elif mode == "video-to-mp4":
        video.to_mp4(files, ".mp4")
    elif mode == "video-to-webm":
        video.to_mp4(files, ".webm")
    elif mode == "extract-audio":
        video.extract_audio(files)
    elif mode == "imgur-upload":
        uploaders.imgur_upload(files)


def run_configure():
    cfg = cfgmod.load()
    audio_choices = [(k, v["name"]) for k, v in sorted(audio.AUDIO_PRESETS.items())]
    video_choices = [(k, v) for k, v in video.GIF_PRESET_NAMES.items()]

    current_audio = cfg.get("audio_preset", "mp3")
    current_video = cfg.get("video_preset", "medium")

    sel = ui.menu_dialog(
        "Context Actions - Configure",
        f"Default audio format:  {audio.AUDIO_PRESETS.get(current_audio, {}).get('name', current_audio)}\n"
        f"Default GIF preset:  {video.GIF_PRESET_NAMES.get(current_video, current_video)}\n\n"
        "Choose a setting to change:",
        [
            ("audio", "Default audio format"),
            ("video", "Default GIF preset"),
            ("imgur", "Imgur Client ID"),
        ],
    )
    if not sel:
        return

    if sel == "audio":
        chosen = ui.menu_dialog("Audio Format", "Default audio format:", audio_choices)
        if chosen:
            cfg["audio_preset"] = chosen
            cfgmod.save(cfg)
            ui.notify("Configured", f"Default audio → {audio.AUDIO_PRESETS[chosen]['name']}", "configure")
    elif sel == "video":
        chosen = ui.menu_dialog("GIF Preset", "Default GIF preset:", video_choices)
        if chosen:
            cfg["video_preset"] = chosen
            cfgmod.save(cfg)
            ui.notify("Configured", f"Default GIF → {video.GIF_PRESET_NAMES[chosen]}", "configure")
    elif sel == "imgur":
        current = cfg.get("imgur_client_id", "")
        val = ui.input_dialog("Imgur Client ID", "Client ID:", current)
        if val is not None:
            cfg["imgur_client_id"] = val
            cfgmod.save(cfg)
            ui.notify("Configured", "Imgur Client ID saved", "configure")


# Link Shell Extension operations
#
# Elevation into root-owned directories is handled entirely by the KAuth
# helper in kio-plugin/linkhelper.cpp, called directly from the C++ context
# menu plugin -- not from this CLI. A privileged pkexec-on-a-user-script path
# used to live here; it was replaced because pkexec performs no ownership
# check on its target, so elevating a script under ~/.local/bin was
# equivalent to granting root to anything that could write that file. See
# the KAuth helper for the hardened replacement (D-Bus-activated, root-owned,
# fd-relative link creation).


def handle_pick_link_source(files: list[str]):
    if not files:
        return
    link_ops.pick_link_source(files)


def handle_cancel_link_creation():
    """Handle cancelling link creation."""
    link_ops.cancel_link_creation()


def handle_drop_as(target_dir: str, drop_type: str):
    """Handle Drop As operation."""
    link_ops.drop_as(target_dir, drop_type)


def handle_drop_hardlink(target_dir: str):
    """Handle Drop Hardlink operation."""
    link_ops.drop_hardlink(target_dir)


def handle_drop_symlink(target_dir: str):
    """Handle Drop Symlink operation."""
    link_ops.drop_symlink(target_dir)


def handle_hardlink_clone(files: list[str]):
    """Handle Hardlink Clone operation."""
    if len(files) != 1:
        ui.error_dialog("Hardlink Clone", "Please select exactly one directory.")
        return
    
    # Ask for target directory
    target = ui.input_dialog(
        "Hardlink Clone",
        "Enter target directory path:",
        str(Path(files[0]).parent)
    )
    if target:
        link_ops.hardlink_clone(files[0], target)


def handle_symlink_clone(files: list[str]):
    """Handle Symlink Clone operation."""
    if len(files) != 1:
        ui.error_dialog("Symlink Clone", "Please select exactly one directory.")
        return
    
    target = ui.input_dialog(
        "Symlink Clone",
        "Enter target directory path:",
        str(Path(files[0]).parent)
    )
    if target:
        link_ops.symlink_clone(files[0], target)


def handle_smart_copy(files: list[str]):
    """Handle Smart Copy operation."""
    if len(files) != 1:
        ui.error_dialog("Smart Copy", "Please select exactly one file or directory.")
        return
    
    target = ui.input_dialog(
        "Smart Copy",
        "Enter target directory path:",
        str(Path(files[0]).parent)
    )
    if target:
        link_ops.smart_copy(files[0], target)


def handle_link_properties(files: list[str]):
    """Handle Link Properties operation."""
    if not files:
        ui.error_dialog("Link Properties", "No file selected.")
        return
    link_ops.show_hardlink_properties(files[0])


def handle_enumerate_hardlinks(files: list[str]):
    """Handle Enumerate Hardlinks operation."""
    if not files:
        ui.error_dialog("Enumerate Hardlinks", "No file selected.")
        return
    
    # enumerate_hardlinks includes the file itself; only the others are siblings.
    resolved = str(Path(files[0]).resolve())
    siblings = [s for s in link_ops.enumerate_hardlinks(files[0]) if s != resolved]
    if siblings:
        msg = f"Hardlink siblings of {files[0]}:\n\n" + "\n".join(siblings)
        ui.info_dialog("Hardlink Siblings", msg, width=600, height=400)
    else:
        ui.info_dialog("Hardlink Siblings", "No other hardlinks found for this file.")


def main():
    # allow_abbrev=False: "--drop" silently resolving to "--drop-symlink" is
    # confusing from any caller, elevated or not.
    parser = argparse.ArgumentParser(description="Dolphin Context Actions", allow_abbrev=False)
    parser.add_argument("--smart-menu", action="store_true", help="Show smart context menu")
    parser.add_argument("--batch", choices=[
        "audio", "video-to-gif", "video-to-mp4", "video-to-webm",
        "extract-audio", "imgur-upload",
    ], help="Batch convert mode")
    parser.add_argument("--format", help="Target format for batch mode")
    parser.add_argument("--configure", action="store_true", help="Open configuration")
    
    # Link Shell Extension operations
    parser.add_argument("--pick-link-source", action="store_true", help="Pick files as link source")
    parser.add_argument("--cancel-link", action="store_true", help="Cancel link creation")
    parser.add_argument("--drop-as", help="Drop as specific type (hardlink, symlink, etc.)")
    parser.add_argument("--drop-hardlink", action="store_true", help="Drop as hardlink")
    parser.add_argument("--drop-symlink", action="store_true", help="Drop as symlink")
    parser.add_argument("--hardlink-clone", action="store_true", help="Create hardlink clone")
    parser.add_argument("--symlink-clone", action="store_true", help="Create symlink clone")
    parser.add_argument("--smart-copy", action="store_true", help="Smart copy with link preservation")
    parser.add_argument("--link-properties", action="store_true", help="Show link properties")
    parser.add_argument("--enumerate-hardlinks", action="store_true", help="Enumerate hardlinks")
    parser.add_argument("--target-dir", help="Target directory for drop operations")

    parser.add_argument("files", nargs="*")

    args = parser.parse_args()

    if args.configure:
        run_configure()
        return

    if args.batch:
        batch_convert(args.files, args.batch, args.format)
        return

    if args.pick_link_source:
        handle_pick_link_source(args.files)
        return
    
    if args.cancel_link:
        handle_cancel_link_creation()
        return
    
    if args.drop_as and args.target_dir:
        handle_drop_as(args.target_dir, args.drop_as)
        return
    
    if args.drop_hardlink and args.target_dir:
        handle_drop_hardlink(args.target_dir)
        return
    
    if args.drop_symlink and args.target_dir:
        handle_drop_symlink(args.target_dir)
        return
    
    if args.hardlink_clone:
        handle_hardlink_clone(args.files)
        return
    
    if args.symlink_clone:
        handle_symlink_clone(args.files)
        return
    
    if args.smart_copy:
        handle_smart_copy(args.files)
        return
    
    if args.link_properties:
        handle_link_properties(args.files)
        return
    
    if args.enumerate_hardlinks:
        handle_enumerate_hardlinks(args.files)
        return

    if args.smart_menu or not any([
        args.batch, args.configure, args.pick_link_source,
        args.cancel_link, args.drop_as, args.drop_hardlink, args.drop_symlink,
        args.hardlink_clone, args.symlink_clone, args.smart_copy,
        args.link_properties, args.enumerate_hardlinks
    ]):
        smart_menu(args.files)
        return


if __name__ == "__main__":
    main()
