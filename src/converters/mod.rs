pub mod audio;
pub mod ffmpeg_tools;
pub mod video;

use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::thread;
use std::time::Duration;

pub fn get_duration(filepath: &str) -> Option<f64> {
    let output = Command::new("ffprobe")
        .args([
            "-v",
            "error",
            "-show_entries",
            "format=duration",
            "-of",
            "default=noprint_wrappers=1:nokey=1",
            filepath,
        ])
        .output()
        .ok()?;
    String::from_utf8_lossy(&output.stdout).trim().parse().ok()
}

pub fn unique_output(destination: &Path) -> PathBuf {
    if !destination.exists() {
        return destination.to_path_buf();
    }
    let stem = destination.file_stem().and_then(|s| s.to_str()).unwrap_or("out");
    let suffix = destination
        .extension()
        .map(|e| format!(".{}", e.to_string_lossy()))
        .unwrap_or_default();
    let parent = destination.parent().unwrap_or_else(|| Path::new("."));
    let mut counter = 2;
    loop {
        let candidate = parent.join(format!("{stem} ({counter}){suffix}"));
        if !candidate.exists() {
            return candidate;
        }
        counter += 1;
    }
}

pub fn parse_progress_us(progress: &str) -> Option<u64> {
    progress
        .lines()
        .rev()
        .find_map(|line| line.strip_prefix("out_time_ms=")?.parse().ok())
}

pub fn poll_interval() -> Duration {
    Duration::from_millis(500)
}

pub enum FfmpegOutcome {
    Success,
    Cancelled,
    Failed(i32),
}

pub fn run_ffmpeg_progress(
    mut proc: std::process::Child,
    prog_path: Option<&Path>,
    duration: Option<f64>,
    handle: Option<&(String, String)>,
    slice_start: i32,
    slice_end: i32,
    label: &str,
) -> FfmpegOutcome {
    use crate::ui;
    let mut last_pct = slice_start;
    loop {
        match proc.try_wait() {
            Ok(Some(status)) => {
                if status.success() {
                    return FfmpegOutcome::Success;
                }
                return FfmpegOutcome::Failed(status.code().unwrap_or(1));
            }
            Ok(None) => {}
            Err(_) => return FfmpegOutcome::Failed(1),
        }
        thread::sleep(poll_interval());
        let mut overall = last_pct;
        if let (Some(prog_path), Some(duration)) = (prog_path, duration) {
            if duration > 0.0 {
                if let Ok(text) = fs::read_to_string(prog_path) {
                    if let Some(us) = parse_progress_us(&text) {
                        let pct = ((us as f64) / 1e6 / duration).min(1.0);
                        overall = slice_start + (pct * (slice_end - slice_start) as f64) as i32;
                    }
                }
            }
        }
        if overall > last_pct {
            last_pct = overall;
        }
        if !ui::pbar_set(handle, last_pct, Some(label)) {
            let _ = proc.kill();
            let _ = proc.wait();
            return FfmpegOutcome::Cancelled;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::TempDir;

    #[test]
    fn unique_output_renames_on_collision() {
        let dir = TempDir::new().unwrap();
        let dest = dir.path().join("song.mp3");
        fs::write(&dest, b"x").unwrap();
        let next = unique_output(&dest);
        assert_eq!(next.file_name().unwrap(), "song (2).mp3");
    }
}
