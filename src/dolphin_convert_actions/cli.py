#!/usr/bin/env python3
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

from . import config as cfgmod, ui
from .converters import audio, video
from . import uploaders

SCRIPT = Path(__file__).resolve()
APP_DIR = SCRIPT.parent


def _ffmpeg_check():
    for tool in ("ffmpeg", "ffprobe"):
        if not shutil.which(tool):
            ui.error_dialog(
                "Convert Actions - Missing Dependency",
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
        ui.error_dialog("Convert Actions", "No files selected.")
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
            ("separator", ""),
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
        ui.error_dialog("Convert Actions", f"Unsupported file type: {mime or ext}")
        return

    choice = ui.menu_dialog(
        "Convert Actions",
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

    if mode == "audio":
        fmt = param or "mp3"
        audio.convert_files(files, fmt)
    elif mode == "video-to-gif":
        preset = param or "medium"
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
        "Convert Actions - Configure",
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


def main():
    parser = argparse.ArgumentParser(description="Dolphin Convert Actions")
    parser.add_argument("--smart-menu", action="store_true", help="Show smart context menu")
    parser.add_argument("--batch", choices=[
        "audio", "video-to-gif", "video-to-mp4", "video-to-webm",
        "extract-audio", "imgur-upload",
    ], help="Batch convert mode")
    parser.add_argument("--format", help="Target format for batch mode")
    parser.add_argument("--configure", action="store_true", help="Open configuration")
    parser.add_argument("files", nargs="*")

    args = parser.parse_args()

    if args.configure:
        run_configure()
        return

    if args.batch:
        batch_convert(args.files, args.batch, args.format)
        return

    if args.smart_menu or not args.batch:
        smart_menu(args.files)
        return


if __name__ == "__main__":
    main()
