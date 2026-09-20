use std::fs;
use std::path::Path;
use std::process::{Command, Stdio};

use tempfile::NamedTempFile;

use super::{
    ffmpeg_tools, get_duration, run_ffmpeg_progress, unique_output, FfmpegOutcome,
};
use crate::ui;

pub const GIF_PRESET_NAMES: &[(&str, &str)] = &[
    ("small", "Small (10 fps, 480px)"),
    ("medium", "Medium (15 fps, 800px)"),
    ("large", "Large (24 fps, 1280px)"),
    ("source", "Source size (15 fps)"),
];

fn gif_preset(name: &str) -> Option<(i32, i32)> {
    match name {
        "small" => Some((10, 480)),
        "medium" => Some((15, 800)),
        "large" => Some((24, 1280)),
        "source" => Some((15, -1)),
        _ => None,
    }
}

pub fn gif_preset_name(key: &str) -> Option<&'static str> {
    GIF_PRESET_NAMES.iter().find(|(k, _)| *k == key).map(|(_, n)| *n)
}

fn video_filters(fps: i32, width: i32) -> String {
    if width == -1 {
        format!("fps={fps}")
    } else {
        format!("fps={fps},scale={width}:-1:flags=lanczos")
    }
}

fn cleanup(paths: &[&Path]) {
    for path in paths {
        let _ = fs::remove_file(path);
    }
}

pub fn to_gif(files: &[String], preset: &str) {
    let Some((fps, width)) = gif_preset(preset) else {
        ui::error_dialog("Video → GIF", &format!("Unknown GIF preset: {preset}"));
        return;
    };
    let Some(ffmpeg_bin) = ffmpeg_tools::resolve(&["gif"]) else {
        ui::error_dialog("Video → GIF", &ffmpeg_tools::missing_encoder_message("GIF", &["gif"]));
        return;
    };

    let total = files.len();
    let mut errors = Vec::new();
    let mut done = 0usize;
    let title = format!("Video → GIF - {}", gif_preset_name(preset).unwrap_or(preset));
    let handle = ui::pbar_open(&title, &format!("Starting… (0 of {total})"));

    for (idx, filepath) in files.iter().enumerate() {
        let input_path = Path::new(filepath);
        if !input_path.exists() {
            errors.push(format!("File not found: {filepath}"));
            continue;
        }

        let mut output_path = input_path.with_extension("gif");
        if output_path == input_path {
            let stem = input_path.file_stem().and_then(|s| s.to_str()).unwrap_or("out");
            output_path = input_path.with_file_name(format!("{stem}_gif.gif"));
        }
        output_path = unique_output(&output_path);

        let short = input_path
            .file_name()
            .map(|n| n.to_string_lossy().chars().take(50).collect::<String>())
            .unwrap_or_default();
        let label = format!("[{}/{total}] {short}", idx + 1);
        let slice_start = ((idx as f64 / total as f64) * 100.0) as i32;
        let slice_end = (((idx + 1) as f64 / total as f64) * 100.0) as i32;
        let palette_end = slice_start + ((slice_end - slice_start) as f64 * 0.15) as i32;
        ui::pbar_set(handle.as_ref(), slice_start, Some(&format!("Building palette: {label}")));

        let duration = get_duration(filepath);
        let palette = NamedTempFile::with_suffix(".png").ok();
        let Some(palette) = palette else {
            errors.push(format!("{short}: cannot create palette temp file"));
            continue;
        };
        let palette_path = palette.path().to_path_buf();

        let vf_base = video_filters(fps, width);
        let vf_pal = format!("{vf_base},palettegen=stats_mode=diff");
        let pal_status = Command::new(&ffmpeg_bin)
            .args(["-y", "-i", filepath, "-vf", &vf_pal, "-loglevel", "error"])
            .arg(&palette_path)
            .output();
        match pal_status {
            Ok(out) if out.status.success() => {}
            Ok(out) => {
                let err = String::from_utf8_lossy(&out.stderr);
                errors.push(format!(
                    "{short} (palette):\n{}",
                    err.chars().take(400).collect::<String>()
                ));
                continue;
            }
            Err(e) => {
                errors.push(format!("{short} (palette):\n{e}"));
                continue;
            }
        }

        if !ui::pbar_set(handle.as_ref(), palette_end, Some(&format!("Converting: {label}"))) {
            cleanup(&[&output_path]);
            ui::pbar_close(handle.as_ref());
            ui::notify(
                "Video → GIF - Cancelled",
                &format!("Cancelled on file {}", idx + 1),
                "dialog-cancel",
            );
            return;
        }

        let prog = NamedTempFile::new().ok();
        let mut err = NamedTempFile::new().ok();
        let prog_path = prog.as_ref().map(|f| f.path().to_path_buf());
        let lavfi = format!("{vf_base}[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=3");
        let mut cmd = Command::new(&ffmpeg_bin);
        cmd.args(["-y", "-i", filepath, "-i"]);
        cmd.arg(&palette_path);
        cmd.args(["-lavfi", &lavfi]);
        if let Some(prog_path) = &prog_path {
            cmd.args(["-progress", &prog_path.display().to_string()]);
        }
        cmd.args(["-nostats", "-loglevel", "error", "--"]);
        cmd.arg(&output_path);
        cmd.stdout(Stdio::null());
        if let Some(err_file) = err.as_mut() {
            if let Ok(clone) = err_file.as_file().try_clone() {
                cmd.stderr(clone);
            }
        }

        let proc = match cmd.spawn() {
            Ok(p) => p,
            Err(e) => {
                errors.push(format!("{short}:\n{e}"));
                continue;
            }
        };
        let outcome = run_ffmpeg_progress(
            proc,
            prog_path.as_deref(),
            duration,
            handle.as_ref(),
            palette_end,
            slice_end,
            &format!("Converting: {label}"),
        );
        drop(prog);
        drop(palette);

        match outcome {
            FfmpegOutcome::Cancelled => {
                cleanup(&[&output_path]);
                ui::pbar_close(handle.as_ref());
                ui::notify(
                    "Video → GIF - Cancelled",
                    &format!("Cancelled on file {}", idx + 1),
                    "dialog-cancel",
                );
                return;
            }
            FfmpegOutcome::Failed(_) => {
                let stderr = err
                    .as_ref()
                    .and_then(|f| fs::read(f.path()).ok())
                    .unwrap_or_default();
                let text = String::from_utf8_lossy(&stderr);
                errors.push(format!("{short}:\n{}", text.chars().take(400).collect::<String>()));
                cleanup(&[&output_path]);
            }
            FfmpegOutcome::Success => {
                done += 1;
                ui::pbar_set(handle.as_ref(), slice_end, Some(&format!("Done: {label}")));
            }
        }
    }

    ui::pbar_close(handle.as_ref());
    finish_batch("Video → GIF", "image-gif", "GIF", done, total, &errors);
}

pub fn to_mp4(files: &[String], target_ext: &str) {
    let params = match target_ext {
        ".mp4" => ("libx264", "aac", &["-preset", "medium", "-crf", "23"][..]),
        ".webm" => ("libvpx-vp9", "libopus", &["-crf", "30", "-b:v", "0"][..]),
        ".mkv" => ("libx264", "aac", &["-preset", "medium", "-crf", "23"][..]),
        _ => {
            ui::error_dialog("Video", &format!("Unknown target: {target_ext}"));
            return;
        }
    };
    let label_suffix = format!("→ {}", target_ext.trim_start_matches('.').to_uppercase());
    transcode_to(files, target_ext, params.0, params.1, params.2, &label_suffix);
}

fn transcode_to(
    files: &[String],
    target_ext: &str,
    codec_v: &str,
    codec_a: &str,
    opts: &[&str],
    label_suffix: &str,
) {
    let Some(ffmpeg_bin) = ffmpeg_tools::resolve(&[codec_v, codec_a]) else {
        ui::error_dialog(
            &format!("Video{label_suffix}"),
            &ffmpeg_tools::missing_encoder_message(
                &target_ext.trim_start_matches('.').to_uppercase(),
                &[codec_v, codec_a],
            ),
        );
        return;
    };

    let total = files.len();
    let mut errors = Vec::new();
    let mut done = 0usize;
    let handle = ui::pbar_open(&format!("Video{label_suffix}"), &format!("Starting… (0 of {total})"));

    for (idx, filepath) in files.iter().enumerate() {
        let input_path = Path::new(filepath);
        if !input_path.exists() {
            errors.push(format!("File not found: {filepath}"));
            continue;
        }
        let mut output_path = input_path.with_extension(target_ext.trim_start_matches('.'));
        if output_path == input_path {
            let stem = input_path.file_stem().and_then(|s| s.to_str()).unwrap_or("out");
            output_path = input_path.with_file_name(format!(
                "{stem}_converted.{}",
                target_ext.trim_start_matches('.')
            ));
        }
        output_path = unique_output(&output_path);

        let short = input_path
            .file_name()
            .map(|n| n.to_string_lossy().chars().take(50).collect::<String>())
            .unwrap_or_default();
        let label = format!("[{}/{total}] {short}", idx + 1);
        let slice_start = ((idx as f64 / total as f64) * 100.0) as i32;
        let slice_end = (((idx + 1) as f64 / total as f64) * 100.0) as i32;
        ui::pbar_set(handle.as_ref(), slice_start, Some(&format!("Converting: {label}")));
        let duration = get_duration(filepath);

        let prog = NamedTempFile::new().ok();
        let mut err = NamedTempFile::new().ok();
        let prog_path = prog.as_ref().map(|f| f.path().to_path_buf());
        let mut cmd = Command::new(&ffmpeg_bin);
        cmd.args(["-y", "-i", filepath, "-c:v", codec_v, "-c:a", codec_a]);
        cmd.args(opts);
        if let Some(prog_path) = &prog_path {
            cmd.args(["-progress", &prog_path.display().to_string()]);
        }
        cmd.args(["-nostats", "-loglevel", "error", "--"]);
        cmd.arg(&output_path);
        cmd.stdout(Stdio::null());
        if let Some(err_file) = err.as_mut() {
            if let Ok(clone) = err_file.as_file().try_clone() {
                cmd.stderr(clone);
            }
        }
        let proc = match cmd.spawn() {
            Ok(p) => p,
            Err(e) => {
                errors.push(format!("{short}:\n{e}"));
                continue;
            }
        };
        let outcome = run_ffmpeg_progress(
            proc,
            prog_path.as_deref(),
            duration,
            handle.as_ref(),
            slice_start,
            slice_end,
            &format!("Converting: {label}"),
        );
        drop(prog);
        match outcome {
            FfmpegOutcome::Cancelled => {
                cleanup(&[&output_path]);
                ui::pbar_close(handle.as_ref());
                ui::notify(
                    &format!("Video{label_suffix} - Cancelled"),
                    &format!("Cancelled on file {}", idx + 1),
                    "dialog-cancel",
                );
                return;
            }
            FfmpegOutcome::Failed(_) => {
                let stderr = err
                    .as_ref()
                    .and_then(|f| fs::read(f.path()).ok())
                    .unwrap_or_default();
                let text = String::from_utf8_lossy(&stderr);
                errors.push(format!("{short}:\n{}", text.chars().take(400).collect::<String>()));
                cleanup(&[&output_path]);
            }
            FfmpegOutcome::Success => {
                done += 1;
                ui::pbar_set(handle.as_ref(), slice_end, Some(&format!("Done: {label}")));
            }
        }
    }

    ui::pbar_close(handle.as_ref());
    if !errors.is_empty() {
        let mut preview = errors.iter().take(3).cloned().collect::<Vec<_>>().join("\n\n");
        if errors.len() > 3 {
            preview.push_str(&format!("\n\n…and {} more", errors.len() - 3));
        }
        ui::error_dialog(
            &format!("Video{label_suffix} - Errors"),
            &format!("Converted {done} of {total}.\n\nErrors:\n{preview}"),
        );
    } else if done > 0 {
        let plural = if done != 1 { "s" } else { "" };
        ui::notify(
            &format!("Video{label_suffix} - Done"),
            &format!("✔ {done} file{plural}{label_suffix}"),
            "video-x-generic",
        );
    }
}

pub fn extract_audio(files: &[String]) {
    let Some(ffmpeg_bin) = ffmpeg_tools::resolve(&["libmp3lame"]) else {
        ui::error_dialog(
            "Extract Audio",
            &ffmpeg_tools::missing_encoder_message("MP3", &["libmp3lame"]),
        );
        return;
    };

    let total = files.len();
    let mut errors = Vec::new();
    let mut done = 0usize;
    let handle = ui::pbar_open("Extract Audio", &format!("Starting… (0 of {total})"));

    for (idx, filepath) in files.iter().enumerate() {
        let input_path = Path::new(filepath);
        if !input_path.exists() {
            errors.push(format!("File not found: {filepath}"));
            continue;
        }
        let mut output_path = input_path.with_extension("mp3");
        if output_path == input_path {
            let stem = input_path.file_stem().and_then(|s| s.to_str()).unwrap_or("out");
            output_path = input_path.with_file_name(format!("{stem}_audio.mp3"));
        }
        output_path = unique_output(&output_path);

        let short = input_path
            .file_name()
            .map(|n| n.to_string_lossy().chars().take(50).collect::<String>())
            .unwrap_or_default();
        let label = format!("[{}/{total}] {short}", idx + 1);
        let slice_start = ((idx as f64 / total as f64) * 100.0) as i32;
        let slice_end = (((idx + 1) as f64 / total as f64) * 100.0) as i32;
        ui::pbar_set(handle.as_ref(), slice_start, Some(&format!("Extracting: {label}")));
        let duration = get_duration(filepath);

        let prog = NamedTempFile::new().ok();
        let mut err = NamedTempFile::new().ok();
        let prog_path = prog.as_ref().map(|f| f.path().to_path_buf());
        let mut cmd = Command::new(&ffmpeg_bin);
        cmd.args([
            "-y",
            "-i",
            filepath,
            "-vn",
            "-c:a",
            "libmp3lame",
            "-aq",
            "2",
        ]);
        if let Some(prog_path) = &prog_path {
            cmd.args(["-progress", &prog_path.display().to_string()]);
        }
        cmd.args(["-nostats", "-loglevel", "error", "--"]);
        cmd.arg(&output_path);
        cmd.stdout(Stdio::null());
        if let Some(err_file) = err.as_mut() {
            if let Ok(clone) = err_file.as_file().try_clone() {
                cmd.stderr(clone);
            }
        }
        let proc = match cmd.spawn() {
            Ok(p) => p,
            Err(e) => {
                errors.push(format!("{short}:\n{e}"));
                continue;
            }
        };
        let outcome = run_ffmpeg_progress(
            proc,
            prog_path.as_deref(),
            duration,
            handle.as_ref(),
            slice_start,
            slice_end,
            &format!("Extracting: {label}"),
        );
        drop(prog);
        match outcome {
            FfmpegOutcome::Cancelled => {
                cleanup(&[&output_path]);
                ui::pbar_close(handle.as_ref());
                ui::notify(
                    "Extract Audio - Cancelled",
                    &format!("Cancelled on file {}", idx + 1),
                    "dialog-cancel",
                );
                return;
            }
            FfmpegOutcome::Failed(_) => {
                let stderr = err
                    .as_ref()
                    .and_then(|f| fs::read(f.path()).ok())
                    .unwrap_or_default();
                let text = String::from_utf8_lossy(&stderr);
                errors.push(format!("{short}:\n{}", text.chars().take(400).collect::<String>()));
                cleanup(&[&output_path]);
            }
            FfmpegOutcome::Success => {
                done += 1;
                ui::pbar_set(handle.as_ref(), slice_end, Some(&format!("Done: {label}")));
            }
        }
    }

    ui::pbar_close(handle.as_ref());
    if !errors.is_empty() {
        let mut preview = errors.iter().take(3).cloned().collect::<Vec<_>>().join("\n\n");
        if errors.len() > 3 {
            preview.push_str(&format!("\n\n…and {} more", errors.len() - 3));
        }
        ui::error_dialog(
            "Extract Audio - Errors",
            &format!("Extracted {done} of {total}.\n\nErrors:\n{preview}"),
        );
    } else if done > 0 {
        let plural = if done != 1 { "s" } else { "" };
        ui::notify(
            "Extract Audio - Done",
            &format!("✔ {done} file{plural} → MP3"),
            "audio-x-mp3",
        );
    }
}

fn finish_batch(
    title: &str,
    icon: &str,
    target: &str,
    done: usize,
    total: usize,
    errors: &[String],
) {
    if !errors.is_empty() {
        let mut preview = errors.iter().take(3).cloned().collect::<Vec<_>>().join("\n\n");
        if errors.len() > 3 {
            preview.push_str(&format!("\n\n…and {} more", errors.len() - 3));
        }
        ui::error_dialog(
            &format!("{title} - Errors"),
            &format!("Converted {done} of {total}.\n\nErrors:\n{preview}"),
        );
        ui::notify(
            &format!("{title} - Finished with errors"),
            &format!("{done}/{total} converted"),
            "dialog-error",
        );
    } else if done > 0 {
        let plural = if done != 1 { "s" } else { "" };
        ui::notify(
            &format!("{title} - Done"),
            &format!("✔ {done} file{plural} → {target}"),
            icon,
        );
    }
}
