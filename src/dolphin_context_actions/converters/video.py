import os
import re
import subprocess
import tempfile
import time
from pathlib import Path

from .. import ui
from . import ffmpeg_tools, get_duration, unique_output

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


def video_filters(fps: int, width: int) -> str:
    parts = [f"fps={fps}"]
    if width != -1:
        parts.append(f"scale={width}:-1:flags=lanczos")
    return ",".join(parts)


def to_gif(files: list[str], preset: str):
    if preset not in GIF_PRESETS:
        ui.error_dialog("Video → GIF", f"Unknown GIF preset: {preset}")
        return
    fps, width = GIF_PRESETS[preset]

    ffmpeg_bin = ffmpeg_tools.resolve("gif")
    if not ffmpeg_bin:
        ui.error_dialog("Video → GIF", ffmpeg_tools.missing_encoder_message("GIF", "gif"))
        return

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
        output_path = unique_output(output_path)

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
        pal_cmd = [ffmpeg_bin, "-y", "-i", filepath, "-vf", vf_pal, "-loglevel", "error", palette_path]
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
        err_fd, err_path = tempfile.mkstemp(prefix="dca_vid_err_", suffix=".txt")

        lavfi = f"{vf_base}[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=3"
        cmd = [ffmpeg_bin, "-y", "-i", filepath, "-i", palette_path,
               "-lavfi", lavfi, "-progress", prog_path, "-nostats", "-loglevel", "error",
               "--", str(output_path)]

        # stderr to a file, not PIPE: nothing drains a PIPE here, so a full OS
        # pipe buffer would deadlock ffmpeg (the same hang class already hit
        # once with notify-send).
        with os.fdopen(err_fd, "wb") as err_file:
            proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=err_file)
        cancelled = False
        last_pct = palette_end

        while proc.poll() is None:
            time.sleep(0.5)
            # Recomputed every iteration so the cancel check below always
            # runs, even when ffmpeg hasn't written new progress.
            overall = last_pct
            try:
                with open(prog_path) as pf:
                    prog = pf.read()
                matches = re.findall(r"^out_time_ms=(\d+)", prog, re.MULTILINE)
                if matches and duration and duration > 0:
                    us = int(matches[-1])
                    file_pct = min(1.0, us / 1e6 / duration)
                    overall = palette_end + int(file_pct * (slice_end - palette_end))
            except Exception:
                pass
            if overall > last_pct:
                last_pct = overall
            alive = ui.pbar_set(handle, last_pct, f"Converting: {label}")
            if not alive:
                proc.kill()
                cancelled = True
                break

        proc.wait()
        _cleanup(prog_path, palette_path)

        if cancelled:
            _cleanup(output_path, err_path)
            ui.pbar_close(handle)
            ui.notify("Video → GIF - Cancelled", f"Cancelled on file {idx + 1}", "dialog-cancel")
            return

        if proc.returncode != 0:
            try:
                stderr = Path(err_path).read_bytes()
            except Exception:
                stderr = b""
            errors.append(f"{input_path.name}:\n{stderr.decode(errors='replace')[:400]}")
            _cleanup(output_path)
        else:
            done += 1
            ui.pbar_set(handle, slice_end, f"Done: {label}")
        _cleanup(err_path)

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
    ffmpeg_bin = ffmpeg_tools.resolve(params["codec_v"], params["codec_a"])
    if not ffmpeg_bin:
        ui.error_dialog(
            f"Video{label_suffix}",
            ffmpeg_tools.missing_encoder_message(
                target_ext.upper()[1:], params["codec_v"], params["codec_a"]
            ),
        )
        return

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
        output_path = unique_output(output_path)

        short = input_path.name[:50]
        label = f"[{idx + 1}/{total}] {short}"
        slice_start = int(idx / total * 100)
        slice_end = int((idx + 1) / total * 100)

        ui.pbar_set(handle, slice_start, f"Converting: {label}")
        duration = get_duration(filepath)

        prog_fd, prog_path = tempfile.mkstemp(prefix="dca_tr_", suffix=".txt")
        os.close(prog_fd)
        err_fd, err_path = tempfile.mkstemp(prefix="dca_tr_err_", suffix=".txt")

        cmd = [ffmpeg_bin, "-y", "-i", filepath,
               "-c:v", params["codec_v"],
               "-c:a", params["codec_a"],
               *params["opts"],
               "-progress", prog_path, "-nostats", "-loglevel", "error",
               "--", str(output_path)]

        # stderr to a file, not PIPE: nothing drains a PIPE here, so a full OS
        # pipe buffer would deadlock ffmpeg.
        with os.fdopen(err_fd, "wb") as err_file:
            proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=err_file)
        cancelled = False
        last_pct = slice_start

        while proc.poll() is None:
            time.sleep(0.5)
            # Recomputed every iteration so the cancel check below always
            # runs, even when ffmpeg hasn't written new progress.
            overall = last_pct
            try:
                with open(prog_path) as pf:
                    prog = pf.read()
                matches = re.findall(r"^out_time_ms=(\d+)", prog, re.MULTILINE)
                if matches and duration and duration > 0:
                    us = int(matches[-1])
                    pct = min(1.0, us / 1e6 / duration)
                    overall = slice_start + int(pct * (slice_end - slice_start))
            except Exception:
                pass
            if overall > last_pct:
                last_pct = overall
            alive = ui.pbar_set(handle, last_pct, f"Converting: {label}")
            if not alive:
                proc.kill()
                cancelled = True
                break

        proc.wait()
        _cleanup(prog_path)

        if cancelled:
            _cleanup(output_path, err_path)
            ui.pbar_close(handle)
            ui.notify(f"Video{label_suffix} - Cancelled", f"Cancelled on file {idx + 1}", "dialog-cancel")
            return

        if proc.returncode != 0:
            try:
                stderr = Path(err_path).read_bytes()
            except Exception:
                stderr = b""
            errors.append(f"{input_path.name}:\n{stderr.decode(errors='replace')[:400]}")
            _cleanup(output_path)
        else:
            done += 1
            ui.pbar_set(handle, slice_end, f"Done: {label}")
        _cleanup(err_path)

    ui.pbar_close(handle)

    if errors:
        preview = "\n\n".join(errors[:3])
        if len(errors) > 3:
            preview += f"\n\n…and {len(errors) - 3} more"
        ui.error_dialog(f"Video{label_suffix} - Errors", f"Converted {done} of {total}.\n\nErrors:\n{preview}")
    elif done > 0:
        ui.notify(f"Video{label_suffix} - Done", f"✔ {done} file{'s' if done != 1 else ''}{label_suffix}", "video-x-generic")


def extract_audio(files: list[str]):
    ffmpeg_bin = ffmpeg_tools.resolve("libmp3lame")
    if not ffmpeg_bin:
        ui.error_dialog("Extract Audio", ffmpeg_tools.missing_encoder_message("MP3", "libmp3lame"))
        return

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
        output_path = unique_output(output_path)

        short = input_path.name[:50]
        label = f"[{idx + 1}/{total}] {short}"
        slice_start = int(idx / total * 100)
        slice_end = int((idx + 1) / total * 100)

        ui.pbar_set(handle, slice_start, f"Extracting: {label}")
        duration = get_duration(filepath)

        prog_fd, prog_path = tempfile.mkstemp(prefix="dca_ext_", suffix=".txt")
        os.close(prog_fd)
        err_fd, err_path = tempfile.mkstemp(prefix="dca_ext_err_", suffix=".txt")

        cmd = [ffmpeg_bin, "-y", "-i", filepath,
               "-vn", "-c:a", "libmp3lame", "-aq", "2",
               "-progress", prog_path, "-nostats", "-loglevel", "error",
               "--", str(output_path)]

        # stderr to a file, not PIPE: nothing drains a PIPE here, so a full OS
        # pipe buffer would deadlock ffmpeg.
        with os.fdopen(err_fd, "wb") as err_file:
            proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=err_file)
        cancelled = False
        last_pct = slice_start

        while proc.poll() is None:
            time.sleep(0.5)
            # Recomputed every iteration so the cancel check below always
            # runs, even when ffmpeg hasn't written new progress.
            overall = last_pct
            try:
                with open(prog_path) as pf:
                    prog = pf.read()
                matches = re.findall(r"^out_time_ms=(\d+)", prog, re.MULTILINE)
                if matches and duration and duration > 0:
                    us = int(matches[-1])
                    pct = min(1.0, us / 1e6 / duration)
                    overall = slice_start + int(pct * (slice_end - slice_start))
            except Exception:
                pass
            if overall > last_pct:
                last_pct = overall
            alive = ui.pbar_set(handle, last_pct, f"Extracting: {label}")
            if not alive:
                proc.kill()
                cancelled = True
                break

        proc.wait()
        _cleanup(prog_path)

        if cancelled:
            _cleanup(output_path, err_path)
            ui.pbar_close(handle)
            ui.notify("Extract Audio - Cancelled", f"Cancelled on file {idx + 1}", "dialog-cancel")
            return

        if proc.returncode != 0:
            try:
                stderr = Path(err_path).read_bytes()
            except Exception:
                stderr = b""
            errors.append(f"{input_path.name}:\n{stderr.decode(errors='replace')[:400]}")
            _cleanup(output_path)
        else:
            done += 1
            ui.pbar_set(handle, slice_end, f"Done: {label}")
        _cleanup(err_path)

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
