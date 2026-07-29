import os
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

from .. import ui

GIF_PRESETS = {
    "small": (10, 480),
    "medium": (15, 800),
    "large": (24, 1280),
    "source": (15, -1),
}

GIF_PRESET_NAMES = {
    "small": "Small (10 fps, 480px)",
    "medium": "Medium (15 fps, 800px)",
    "large": "Large (24 fps, 1280px)",
    "source": "Source size (15 fps)",
}


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


def video_filters(fps: int, width: int) -> str:
    parts = [f"fps={fps}"]
    if width != -1:
        parts.append(f"scale={width}:-1:flags=lanczos")
    return ",".join(parts)


def to_gif(files: list[str], preset: str):
    fps, width = GIF_PRESETS[preset]
    total = len(files)
    errors = []
    done = 0
    handle = ui.pbar_open(
        f"Video → GIF - {GIF_PRESET_NAMES[preset]}",
        f"Starting… (0 of {total})",
    )

    for idx, filepath in enumerate(files):
        input_path = Path(filepath)
        if not input_path.exists():
            errors.append(f"File not found: {filepath}")
            continue

        output_path = input_path.with_suffix(".gif")
        if output_path == input_path:
            output_path = input_path.with_stem(input_path.stem + "_gif").with_suffix(".gif")

        short = input_path.name[:50]
        label = f"[{idx + 1}/{total}] {short}"
        slice_start = int(idx / total * 100)
        slice_end = int((idx + 1) / total * 100)
        palette_end = slice_start + int((slice_end - slice_start) * 0.15)

        ui.pbar_set(handle, slice_start, f"Building palette: {label}")

        duration = get_duration(filepath)
        palette_fd, palette_path = tempfile.mkstemp(prefix="dca_pal_", suffix=".png")
        os.close(palette_fd)

        vf_base = video_filters(fps, width)
        vf_pal = f"{vf_base},palettegen=stats_mode=diff"
        pal_cmd = ["ffmpeg", "-y", "-i", filepath, "-vf", vf_pal, "-loglevel", "error", palette_path]
        pal_proc = subprocess.run(pal_cmd, capture_output=True)
        if pal_proc.returncode != 0:
            errors.append(f"{input_path.name} (palette):\n{pal_proc.stderr.decode(errors='replace')[:400]}")
            _cleanup(palette_path)
            continue

        alive = ui.pbar_set(handle, palette_end, f"Converting: {label}")
        if not alive:
            _cleanup(palette_path, output_path)
            ui.pbar_close(handle)
            ui.notify("Video → GIF - Cancelled", f"Cancelled on file {idx + 1}", "dialog-cancel")
            return

        prog_fd, prog_path = tempfile.mkstemp(prefix="dca_vid_", suffix=".txt")
        os.close(prog_fd)

        lavfi = f"{vf_base}[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=3"
        cmd = ["ffmpeg", "-y", "-i", filepath, "-i", palette_path,
               "-lavfi", lavfi, "-progress", prog_path, "-nostats", "-loglevel", "error",
               str(output_path)]

        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        cancelled = False
        last_pct = palette_end

        while proc.poll() is None:
            time.sleep(0.5)
            try:
                with open(prog_path) as pf:
                    prog = pf.read()
                matches = re.findall(r"^out_time_ms=(\d+)", prog, re.MULTILINE)
                if matches and duration and duration > 0:
                    us = int(matches[-1])
                    file_pct = min(1.0, us / 1e6 / duration)
                    overall = palette_end + int(file_pct * (slice_end - palette_end))
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
        _cleanup(prog_path, palette_path)

        if cancelled:
            _cleanup(output_path)
            ui.pbar_close(handle)
            ui.notify("Video → GIF - Cancelled", f"Cancelled on file {idx + 1}", "dialog-cancel")
            return

        if proc.returncode != 0:
            stderr = proc.stderr.read() if proc.stderr else b""
            errors.append(f"{input_path.name}:\n{stderr.decode(errors='replace')[:400]}")
            _cleanup(output_path)
        else:
            done += 1
            ui.pbar_set(handle, slice_end, f"Done: {label}")

    ui.pbar_close(handle)

    if errors:
        preview = "\n\n".join(errors[:3])
        if len(errors) > 3:
            preview += f"\n\n…and {len(errors) - 3} more"
        ui.error_dialog("Video → GIF - Errors", f"Converted {done} of {total}.\n\nErrors:\n{preview}")
        ui.notify("Video → GIF - Finished with errors", f"{done}/{total} converted", "dialog-error")
    elif done > 0:
        ui.notify("Video → GIF - Done", f"✔ {done} file{'s' if done != 1 else ''} → GIF", "image-gif")


def to_mp4(files: list[str], target_ext: str = ".mp4"):
    _transcode_to(files, target_ext, {
        ".mp4": {"codec_v": "libx264", "codec_a": "aac", "opts": ["-preset", "medium", "-crf", "23"]},
        ".webm": {"codec_v": "libvpx-vp9", "codec_a": "libopus", "opts": ["-crf", "30", "-b:v", "0"]},
        ".mkv": {"codec_v": "libx264", "codec_a": "aac", "opts": ["-preset", "medium", "-crf", "23"]},
    }[target_ext], f"→ {target_ext.upper()[1:]}")


def _transcode_to(files: list[str], target_ext: str, params: dict, label_suffix: str):
    total = len(files)
    errors = []
    done = 0
    handle = ui.pbar_open(f"Video{label_suffix}", f"Starting… (0 of {total})")

    for idx, filepath in enumerate(files):
        input_path = Path(filepath)
        if not input_path.exists():
            errors.append(f"File not found: {filepath}")
            continue

        output_path = input_path.with_suffix(target_ext)
        if output_path == input_path:
            output_path = input_path.with_stem(input_path.stem + "_converted").with_suffix(target_ext)

        short = input_path.name[:50]
        label = f"[{idx + 1}/{total}] {short}"
        slice_start = int(idx / total * 100)
        slice_end = int((idx + 1) / total * 100)

        ui.pbar_set(handle, slice_start, f"Converting: {label}")
        duration = get_duration(filepath)

        prog_fd, prog_path = tempfile.mkstemp(prefix="dca_tr_", suffix=".txt")
        os.close(prog_fd)

        cmd = ["ffmpeg", "-y", "-i", filepath,
               "-c:v", params["codec_v"],
               "-c:a", params["codec_a"],
               *params["opts"],
               "-progress", prog_path, "-nostats", "-loglevel", "error",
               str(output_path)]

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
        _cleanup(prog_path)

        if cancelled:
            _cleanup(output_path)
            ui.pbar_close(handle)
            ui.notify(f"Video{label_suffix} - Cancelled", f"Cancelled on file {idx + 1}", "dialog-cancel")
            return

        if proc.returncode != 0:
            stderr = proc.stderr.read() if proc.stderr else b""
            errors.append(f"{input_path.name}:\n{stderr.decode(errors='replace')[:400]}")
            _cleanup(output_path)
        else:
            done += 1
            ui.pbar_set(handle, slice_end, f"Done: {label}")

    ui.pbar_close(handle)

    if errors:
        preview = "\n\n".join(errors[:3])
        if len(errors) > 3:
            preview += f"\n\n…and {len(errors) - 3} more"
        ui.error_dialog(f"Video{label_suffix} - Errors", f"Converted {done} of {total}.\n\nErrors:\n{preview}")
    elif done > 0:
        ui.notify(f"Video{label_suffix} - Done", f"✔ {done} file{'s' if done != 1 else ''}{label_suffix}", "video-x-generic")


def extract_audio(files: list[str]):
    total = len(files)
    errors = []
    done = 0
    handle = ui.pbar_open("Extract Audio", f"Starting… (0 of {total})")

    for idx, filepath in enumerate(files):
        input_path = Path(filepath)
        if not input_path.exists():
            errors.append(f"File not found: {filepath}")
            continue

        output_path = input_path.with_suffix(".mp3")
        if output_path == input_path:
            output_path = input_path.with_stem(input_path.stem + "_audio").with_suffix(".mp3")

        short = input_path.name[:50]
        label = f"[{idx + 1}/{total}] {short}"
        slice_start = int(idx / total * 100)
        slice_end = int((idx + 1) / total * 100)

        ui.pbar_set(handle, slice_start, f"Extracting: {label}")
        duration = get_duration(filepath)

        prog_fd, prog_path = tempfile.mkstemp(prefix="dca_ext_", suffix=".txt")
        os.close(prog_fd)

        cmd = ["ffmpeg", "-y", "-i", filepath,
               "-vn", "-c:a", "libmp3lame", "-aq", "2",
               "-progress", prog_path, "-nostats", "-loglevel", "error",
               str(output_path)]

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
                        alive = ui.pbar_set(handle, overall, f"Extracting: {label}")
                        if not alive:
                            proc.kill()
                            cancelled = True
                            break
            except Exception:
                pass

        proc.wait()
        _cleanup(prog_path)

        if cancelled:
            _cleanup(output_path)
            ui.pbar_close(handle)
            ui.notify("Extract Audio - Cancelled", f"Cancelled on file {idx + 1}", "dialog-cancel")
            return

        if proc.returncode != 0:
            stderr = proc.stderr.read() if proc.stderr else b""
            errors.append(f"{input_path.name}:\n{stderr.decode(errors='replace')[:400]}")
            _cleanup(output_path)
        else:
            done += 1
            ui.pbar_set(handle, slice_end, f"Done: {label}")

    ui.pbar_close(handle)

    if errors:
        preview = "\n\n".join(errors[:3])
        if len(errors) > 3:
            preview += f"\n\n…and {len(errors) - 3} more"
        ui.error_dialog("Extract Audio - Errors", f"Extracted {done} of {total}.\n\nErrors:\n{preview}")
    elif done > 0:
        ui.notify("Extract Audio - Done", f"✔ {done} file{'s' if done != 1 else ''} → MP3", "audio-x-mp3")


def _cleanup(*paths):
    for p in paths:
        try:
            Path(p).unlink()
        except Exception:
            pass
