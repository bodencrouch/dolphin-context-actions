use std::fs;
use std::io::Read;
use std::path::Path;
use std::process::{Command, Stdio};

use crate::{config, ui};

pub fn imgur_upload(files: &[String]) {
    let mut cfg = config::load();
    let mut client_id = cfg.imgur_client_id.clone();
    if client_id.is_empty() {
        let Some(entered) = ui::input_dialog(
            "Imgur Upload",
            "Enter your Imgur Client ID (register at https://api.imgur.com/oauth2/addclient):",
            "",
        ) else {
            return;
        };
        if entered.is_empty() {
            return;
        }
        client_id = entered;
        if ui::confirm_dialog("Imgur Upload", "Save this Client ID for future uploads?") {
            cfg.imgur_client_id = client_id.clone();
            config::save(&cfg);
        }
    }

    let total = files.len();
    let mut successes = Vec::new();
    let mut errors = Vec::new();
    let handle = ui::pbar_open("Upload to Imgur", &format!("Starting… (0 of {total})"));

    for (idx, filepath) in files.iter().enumerate() {
        let path = Path::new(filepath);
        if !path.exists() {
            errors.push(format!("File not found: {filepath}"));
            continue;
        }
        let short = path
            .file_name()
            .map(|n| n.to_string_lossy().chars().take(50).collect::<String>())
            .unwrap_or_default();
        let label = format!("[{}/{total}] {short}", idx + 1);
        ui::pbar_set(
            handle.as_ref(),
            ((idx as f64 / total as f64) * 100.0) as i32,
            Some(&format!("Uploading: {label}")),
        );
        match imgur_upload_single(path, &client_id) {
            Ok(Some(url)) => successes.push((
                path.file_name().unwrap_or_default().to_string_lossy().into_owned(),
                url,
            )),
            Ok(None) => errors.push(format!("{short}: upload failed (check client ID)")),
            Err(e) => errors.push(format!("{short}: {e}")),
        }
    }
    ui::pbar_close(handle.as_ref());

    if !successes.is_empty() {
        let text = successes
            .iter()
            .map(|(name, url)| format!("• {name}: {url}"))
            .collect::<Vec<_>>()
            .join("\n");
        ui::info_dialog(
            "Imgur Upload - Done",
            &format!("Uploaded {} file(s):\n\n{text}", successes.len()),
            0,
            0,
        );
        let plural = if successes.len() != 1 { "s" } else { "" };
        ui::notify(
            "Imgur Upload - Done",
            &format!("✔ {} file{plural} uploaded", successes.len()),
            "internet-web-browser",
        );
        for (_, url) in &successes {
            copy_to_clipboard(url);
        }
    }
    if !errors.is_empty() {
        let mut preview = errors.iter().take(3).cloned().collect::<Vec<_>>().join("\n\n");
        if errors.len() > 3 {
            preview.push_str(&format!("\n\n…and {} more", errors.len() - 3));
        }
        ui::error_dialog("Imgur Upload - Errors", &preview);
    }
}

fn imgur_upload_single(filepath: &Path, client_id: &str) -> Result<Option<String>, String> {
    let mut file_data = Vec::new();
    fs::File::open(filepath)
        .and_then(|mut f| f.read_to_end(&mut file_data))
        .map_err(|e| e.to_string())?;
    let filename = filepath
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_else(|| "image".into());
    let content_type = mime_guess::from_path(filepath)
        .first_or_octet_stream()
        .essence_str()
        .to_string();

    let response = ureq::post("https://api.imgur.com/3/image")
        .set("Authorization", &format!("Client-ID {client_id}"))
        .set("Content-Type", &content_type)
        .send_bytes(&file_data)
        .map_err(|e| e.to_string())?;

    let status = response.status();
    let data: serde_json::Value = response.into_json().map_err(|e| e.to_string())?;
    if status == 200 && data.get("success").and_then(|v| v.as_bool()).unwrap_or(false) {
        return Ok(data
            .get("data")
            .and_then(|d| d.get("link"))
            .and_then(|l| l.as_str())
            .map(|s| s.to_string()));
    }
    let _ = filename;
    Ok(None)
}

fn copy_to_clipboard(text: &str) {
    for tool in ["wl-copy", "xclip", "xsel"] {
        if which::which(tool).is_err() {
            continue;
        }
        let mut cmd = Command::new(tool);
        match tool {
            "xclip" => {
                cmd.args(["-selection", "clipboard"]);
            }
            "xsel" => {
                cmd.args(["--clipboard", "--input"]);
            }
            _ => {}
        }
        cmd.stdin(Stdio::piped());
        if let Ok(mut child) = cmd.spawn() {
            if let Some(stdin) = child.stdin.as_mut() {
                use std::io::Write;
                let _ = stdin.write_all(text.as_bytes());
            }
            let _ = child.wait();
        }
        break;
    }
}
