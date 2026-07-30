import os
import re
import subprocess
import tempfile
import time
from pathlib import Path

from .. import ui
from . import ffmpeg_tools, unique_output

AUDIO_PRESETS = {
    "mp3": {
        "ext": ".mp3",
        "name": "MP3 (V0)",
        "icon": "audio-x-mp3",
        "codec": "libmp3lame",
        "opts": ["-aq", "0"],
    },
    "ogg": {
        "ext": ".ogg",
        "name": "OGG (Q6)",
        "icon": "audio-x-vorbis+ogg",
        "codec": "libvorbis",
        "opts": ["-aq", "6"],
    },
    "flac": {
        "ext": ".flac",
        "name": "FLAC",
        "icon": "audio-x-flac",
        "codec": "flac",
        "opts": [],
    },
    "wav": {
        "ext": ".wav",
        "name": "WAV",
        "icon": "audio-x-wav",
        "codec": "pcm_s16le",
        "opts": [],
    },
    "m4a": {
        "ext": ".m4a",
        "name": "M4A (AAC, 192k)",
        "icon": "audio-x-m4a",
        "codec": "aac",
        "opts": ["-b:a", "192k"],
    },
    "opus": {
        "ext": ".opus",
        "name": "Opus (128k)",
        "icon": "audio-x-opus+ogg",
        "codec": "libopus",
        "opts": ["-b:a", "128k"],
    },
    "alac": {
        "ext": ".m4a",
        "name": "ALAC (M4A)",
        "icon": "audio-x-generic",
        "codec": "alac",
        "opts": [],
    },
}

AUDIO_FORMATS = sorted(AUDIO_PRESETS.keys())


def get_duration(filepath: str) -> float | None:
    try:
        r = subprocess.run(
            ["ffprobe", "-v", "error", "-show_entries", "format=duration",
             "-of", "default=noprint_wrappers=1:nokey=1", filepath],
            capture_output=True, text=True, timeout=30,
        )
        return float(r.stdout.strip())
    except Exception:
        return None


def convert_files(files: list[str], target_format: str):
    preset = AUDIO_PRESETS.get(target_format)
    if not preset:
        ui.error_dialog("Audio Converter", f"Unknown format: {target_format}")
        return

    ffmpeg_bin = ffmpeg_tools.resolve(preset["codec"])
    if not ffmpeg_bin:
        ui.error_dialog(
            "Audio Converter",
            ffmpeg_tools.missing_encoder_message(preset["name"], preset["codec"]),
        )
        return

    total = len(files)
    errors = []
    done = 0
    handle = ui.pbar_open(
        f"Audio Converter → {preset['name']}",
        f"Starting… (0 of {total})",
    )

    for idx, filepath in enumerate(files):
        input_path = Path(filepath)
        if not input_path.exists():
            errors.append(f"File not found: {filepath}")
            continue

        output_path = input_path.with_suffix(preset["ext"])
        if output_path == input_path:
            output_path = input_path.with_stem(input_path.stem + "_converted").with_suffix(preset["ext"])
        output_path = unique_output(output_path)

        short = input_path.name[:50]
        label = f"[{idx + 1}/{total}] {short}"
        slice_start = int(idx / total * 100)
        slice_end = int((idx + 1) / total * 100)

        ui.pbar_set(handle, slice_start, f"Converting: {label}")

        duration = get_duration(filepath)
        prog_fd, prog_path = tempfile.mkstemp(prefix="dca_aud_", suffix=".txt")
        os.close(prog_fd)

        cmd = [
            ffmpeg_bin, "-y",
            "-i", filepath,
            "-c:a", preset["codec"],
            *preset["opts"],
            "-progress", prog_path,
            "-nostats",
            "-loglevel", "error",
            str(output_path),
        ]

        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        cancelled = False
        last_pct = slice_start

        while proc.poll() is None:
            time.sleep(0.5)
            try:
                with open(prog_path) as pf:
                    prog = pf.read()
                matches = re.findall(r"^out_time_ms=(\d+)", prog, re.MULTILINE)
                if matches and duration and duration > 0:
                    us = int(matches[-1])
                    pct = min(1.0, us / 1e6 / duration)
                    overall = slice_start + int(pct * (slice_end - slice_start))
                    if overall > last_pct:
                        last_pct = overall
                        alive = ui.pbar_set(handle, overall, f"Converting: {label}")
                        if not alive:
                            proc.kill()
                            cancelled = True
                            break
            except Exception:
                pass

        proc.wait()

        try:
            os.unlink(prog_path)
        except Exception:
            pass

        if cancelled:
            try:
                output_path.unlink()
            except Exception:
                pass
            ui.pbar_close(handle)
            ui.notify("Audio Converter - Cancelled", f"Cancelled on file {idx + 1} of {total}", "dialog-cancel")
            return

        if proc.returncode != 0:
            stderr = proc.stderr.read() if proc.stderr else b""
            errors.append(f"{input_path.name}:\n{stderr.decode(errors='replace')[:400]}")
            try:
                output_path.unlink()
            except Exception:
                pass
        else:
            done += 1
            ui.pbar_set(handle, slice_end, f"Done: {label}")

    ui.pbar_close(handle)

    if errors:
        preview = "\n\n".join(errors[:3])
        if len(errors) > 3:
            preview += f"\n\n…and {len(errors) - 3} more"
        ui.error_dialog("Audio Converter - Errors", f"Converted {done} of {total}.\n\nErrors:\n{preview}")
        ui.notify("Audio Converter - Finished with errors", f"{done}/{total} converted", "dialog-error")
    elif done > 0:
        ui.notify("Audio Converter - Done", f"✔ {done} file{'s' if done != 1 else ''} → {preset['name']}", "audio-x-generic")
