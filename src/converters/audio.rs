use std::fs;
use std::path::Path;
use std::process::{Command, Stdio};

use tempfile::NamedTempFile;

use super::{
    ffmpeg_tools, get_duration, run_ffmpeg_progress, unique_output, FfmpegOutcome,
};
use crate::ui;

pub struct AudioPreset {
    pub ext: &'static str,
    pub name: &'static str,
    pub codec: &'static str,
    pub opts: &'static [&'static str],
}

pub const AUDIO_FORMATS: &[&str] = &["alac", "flac", "m4a", "mp3", "ogg", "opus", "wav"];

pub fn preset(format: &str) -> Option<&'static AudioPreset> {
    Some(match format {
        "mp3" => &AudioPreset {
            ext: ".mp3",
            name: "MP3 (V0)",
            codec: "libmp3lame",
            opts: &["-aq", "0"],
        },
        "ogg" => &AudioPreset {
            ext: ".ogg",
            name: "OGG (Q6)",
            codec: "libvorbis",
            opts: &["-aq", "6"],
        },
        "flac" => &AudioPreset {
            ext: ".flac",
            name: "FLAC",
            codec: "flac",
            opts: &[],
        },
        "wav" => &AudioPreset {
            ext: ".wav",
            name: "WAV",
            codec: "pcm_s16le",
            opts: &[],
        },
        "m4a" => &AudioPreset {
            ext: ".m4a",
            name: "M4A (AAC, 192k)",
            codec: "aac",
            opts: &["-b:a", "192k"],
        },
        "opus" => &AudioPreset {
            ext: ".opus",
            name: "Opus (128k)",
            codec: "libopus",
            opts: &["-b:a", "128k"],
        },
        "alac" => &AudioPreset {
            ext: ".m4a",
            name: "ALAC (M4A)",
            codec: "alac",
            opts: &[],
        },
        _ => return None,
    })
}

pub fn format_label_extra(format: &str) -> String {
    let Some(preset) = preset(format) else {
        return String::new();
    };
    if let Some((_, rest)) = preset.name.split_once('(') {
        format!(" ({})", rest.trim_end_matches(')'))
    } else {
        String::new()
    }
}

pub fn convert_files(files: &[String], target_format: &str) {
    let Some(preset) = preset(target_format) else {
        ui::error_dialog("Audio Converter", &format!("Unknown format: {target_format}"));
        return;
    };
    let Some(ffmpeg_bin) = ffmpeg_tools::resolve(&[preset.codec]) else {
        ui::error_dialog(
            "Audio Converter",
            &ffmpeg_tools::missing_encoder_message(preset.name, &[preset.codec]),
        );
        return;
    };

    let total = files.len();
    let mut errors = Vec::new();
    let mut done = 0usize;
    let handle = ui::pbar_open(
        &format!("Audio Converter → {}", preset.name),
        &format!("Starting… (0 of {total})"),
    );

    for (idx, filepath) in files.iter().enumerate() {
        let input_path = Path::new(filepath);
        if !input_path.exists() {
            errors.push(format!("File not found: {filepath}"));
            continue;
        }

        let mut output_path = input_path.with_extension(preset.ext.trim_start_matches('.'));
        if output_path == input_path {
            let stem = input_path
                .file_stem()
                .and_then(|s| s.to_str())
                .unwrap_or("converted");
            output_path = input_path.with_file_name(format!(
                "{stem}_converted.{}",
                preset.ext.trim_start_matches('.')
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
        cmd.args(["-y", "-i", filepath, "-c:a", preset.codec]);
        cmd.args(preset.opts);
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
                errors.push(format!("{}:\n{e}", input_path.display()));
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
                let _ = fs::remove_file(&output_path);
                ui::pbar_close(handle.as_ref());
                ui::notify(
                    "Audio Converter - Cancelled",
                    &format!("Cancelled on file {} of {total}", idx + 1),
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
                errors.push(format!(
                    "{}:\n{}",
                    input_path.file_name().unwrap_or_default().to_string_lossy(),
                    text.chars().take(400).collect::<String>()
                ));
                let _ = fs::remove_file(&output_path);
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
            "Audio Converter - Errors",
            &format!("Converted {done} of {total}.\n\nErrors:\n{preview}"),
        );
        ui::notify(
            "Audio Converter - Finished with errors",
            &format!("{done}/{total} converted"),
            "dialog-error",
        );
    } else if done > 0 {
        let plural = if done != 1 { "s" } else { "" };
        ui::notify(
            "Audio Converter - Done",
            &format!("✔ {done} file{plural} → {}", preset.name),
            "audio-x-generic",
        );
    }
}
