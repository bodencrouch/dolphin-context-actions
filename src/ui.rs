use std::env;
use std::io;
use std::os::unix::process::ExitStatusExt;
use std::process::{Command, Output, Stdio};

static QDBUS_CANDIDATES: &[&str] = &["qdbus-qt5", "qdbus", "qdbus6"];

pub fn gui_blocked() -> bool {
    env::var_os("DOLPHIN_CONTEXT_ACTIONS_HEADLESS").is_some()
        || env::var_os("PYTEST_CURRENT_TEST").is_some()
}

fn completed(_args: &[&str], status: i32, stdout: &str, stderr: &str) -> Output {
    Output {
        status: ExitStatusExt::from_raw(status << 8),
        stdout: stdout.as_bytes().to_vec(),
        stderr: stderr.as_bytes().to_vec(),
    }
}

pub fn kdialog(args: &[&str]) -> Output {
    if gui_blocked() {
        eprintln!("kdialog skipped (headless): {args:?}");
        return completed(args, 1, "", "headless");
    }
    match Command::new("kdialog").args(args).output() {
        Ok(output) => {
            if !output.status.success() && which::which("kdialog").is_err() {
                eprintln!("kdialog not found. Args: {args:?}");
            }
            output
        }
        Err(exc) => {
            eprintln!("kdialog unavailable: {exc}. Args: {args:?}");
            completed(args, 1, "", &exc.to_string())
        }
    }
}

pub fn notify(title: &str, msg: &str, icon: &str) {
    if gui_blocked() {
        return;
    }
    let _ = Command::new("notify-send")
        .args(["-i", icon, "-a", "Context Actions", title, msg])
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn();
}

pub fn error_dialog(title: &str, msg: &str) {
    let _ = kdialog(&["--title", title, "--error", msg]);
}

pub fn info_dialog(title: &str, msg: &str, width: i32, height: i32) {
    let mut args = vec!["--title", title, "--msgbox", msg];
    let geometry = format!("{width}x{height}");
    if width > 0 && height > 0 {
        args.extend(["--geometry", &geometry]);
    }
    let _ = kdialog(&args);
}

pub fn confirm_dialog(title: &str, msg: &str) -> bool {
    kdialog(&["--title", title, "--yesno", msg]).status.success()
}

pub fn menu_dialog(title: &str, prompt: &str, items: &[(&str, &str)]) -> Option<String> {
    let mut args = vec!["--title".to_string(), title.to_string(), "--menu".to_string(), prompt.to_string()];
    for (key, label) in items {
        if label.is_empty() {
            continue;
        }
        args.push((*key).to_string());
        args.push((*label).to_string());
    }
    let borrowed: Vec<&str> = args.iter().map(String::as_str).collect();
    let result = kdialog(&borrowed);
    if !result.status.success() {
        return None;
    }
    Some(String::from_utf8_lossy(&result.stdout).trim().to_string())
}

pub fn input_dialog(title: &str, prompt: &str, text: &str) -> Option<String> {
    let result = kdialog(&["--title", title, "--inputbox", prompt, text]);
    if !result.status.success() {
        return None;
    }
    Some(String::from_utf8_lossy(&result.stdout).trim().to_string())
}

pub fn pbar_open(title: &str, label: &str) -> Option<(String, String)> {
    let result = kdialog(&["--title", title, "--progressbar", label, "100"]);
    let stdout = String::from_utf8_lossy(&result.stdout);
    let parts: Vec<&str> = stdout.split_whitespace().collect();
    if parts.len() >= 2 {
        Some((parts[0].to_string(), parts[1].to_string()))
    } else if parts.len() == 1 {
        Some((parts[0].to_string(), "/".to_string()))
    } else {
        None
    }
}

fn qdbus() -> Option<String> {
    QDBUS_CANDIDATES
        .iter()
        .find_map(|name| which::which(name).ok().map(|p| p.display().to_string()))
}

pub fn pbar_set(handle: Option<&(String, String)>, value: i32, label: Option<&str>) -> bool {
    let Some((service, path)) = handle else {
        return true;
    };
    let Some(qdbus) = qdbus() else {
        return true;
    };
    let value_s = value.to_string();
    let mut cmd = Command::new(&qdbus);
    cmd.args([service, path, "Set", "", "value", &value_s]);
    match cmd.output() {
        Ok(out) if out.status.success() => {}
        Ok(_) => return false,
        Err(err) if err.kind() == io::ErrorKind::TimedOut => return true,
        Err(_) => return false,
    }
    if let Some(label) = label {
        let _ = Command::new(&qdbus)
            .args([service.as_str(), path.as_str(), "setLabelText", label])
            .output();
    }
    true
}

pub fn pbar_close(handle: Option<&(String, String)>) {
    let Some((service, path)) = handle else {
        return;
    };
    let Some(qdbus) = qdbus() else {
        return;
    };
    let _ = Command::new(qdbus).args([service, path, "close"]).output();
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn kdialog_does_not_exec_when_headless() {
        env::set_var("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1");
        let result = kdialog(&["--title", "Dolphin Context Actions", "--error", "nope"]);
        env::remove_var("DOLPHIN_CONTEXT_ACTIONS_HEADLESS");
        assert!(!result.status.success());
        assert_eq!(String::from_utf8_lossy(&result.stderr), "headless");
    }

    #[test]
    fn gui_blocked_explicit_flag() {
        env::remove_var("PYTEST_CURRENT_TEST");
        env::remove_var("DOLPHIN_CONTEXT_ACTIONS_HEADLESS");
        assert!(!gui_blocked());
        env::set_var("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1");
        assert!(gui_blocked());
        env::remove_var("DOLPHIN_CONTEXT_ACTIONS_HEADLESS");
    }
}
