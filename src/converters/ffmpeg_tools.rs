use std::collections::HashMap;
use std::process::Command;
use std::sync::Mutex;

use std::sync::OnceLock;

static CANDIDATE_BINARIES: &[&str] = &["ffmpeg", "/usr/bin/ffmpeg", "/usr/local/bin/ffmpeg"];

fn encoder_cache() -> &'static Mutex<HashMap<String, Vec<String>>> {
    static CACHE: OnceLock<Mutex<HashMap<String, Vec<String>>>> = OnceLock::new();
    CACHE.get_or_init(|| Mutex::new(HashMap::new()))
}

fn encoders(binary: &str) -> Vec<String> {
    if let Ok(cache) = encoder_cache().lock() {
        if let Some(names) = cache.get(binary) {
            return names.clone();
        }
    }

    let mut names = Vec::new();
    if let Ok(output) = Command::new(binary)
        .args(["-hide_banner", "-encoders"])
        .output()
    {
        for line in String::from_utf8_lossy(&output.stdout).lines() {
            let parts: Vec<&str> = line.split_whitespace().collect();
            if parts.len() >= 2
                && parts[0].len() == 6
                && parts[1] != "="
                && !line.starts_with(" -")
            {
                names.push(parts[1].to_string());
            }
        }
    }

    if let Ok(mut cache) = encoder_cache().lock() {
        cache.insert(binary.to_string(), names.clone());
    }
    names
}

pub fn resolve(wanted: &[&str]) -> Option<String> {
    let wanted: Vec<&str> = wanted.iter().copied().filter(|e| !e.is_empty()).collect();
    for binary in CANDIDATE_BINARIES {
        let path = which::which(binary).ok()?;
        let path_s = path.display().to_string();
        let have = encoders(&path_s);
        if wanted.iter().all(|enc| have.iter().any(|h| h == enc)) {
            return Some(path_s);
        }
    }
    None
}

pub fn missing_encoder_message(label: &str, encoders: &[&str]) -> String {
    let names = encoders
        .iter()
        .filter(|e| !e.is_empty())
        .map(|e| format!("<b>{e}</b>"))
        .collect::<Vec<_>>()
        .join(", ");
    format!(
        "Cannot convert to {label}.\n\n\
         No ffmpeg on this system provides: {names}\n\n\
         Install an ffmpeg build that includes it:\n\
         • <tt>sudo dnf install ffmpeg</tt> (Fedora)\n\
         • <tt>sudo apt install ffmpeg</tt> (Debian/Ubuntu/Mint)\n\
         • <tt>sudo pacman -S ffmpeg</tt> (Arch/Manjaro)"
    )
}

pub fn clear_cache() {
    if let Ok(mut cache) = encoder_cache().lock() {
        cache.clear();
    }
}
