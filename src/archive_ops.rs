use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Output, Stdio};

use crate::ui;

const EXTRACT_EXCLUDE_EXTENSIONS: &[&str] = &[
    "3gp", "aac", "ans", "ape", "asc", "asm", "asp", "aspx", "avi", "awk", "bas", "bat", "bmp", "c",
    "cs", "cls", "clw", "cmd", "cpp", "csproj", "css", "ctl", "cxx", "def", "dep", "dlg", "dsp",
    "dsw", "eps", "f", "f77", "f90", "f95", "fla", "flac", "frm", "gif", "h", "hpp", "hta", "htm",
    "html", "hxx", "ico", "idl", "inc", "ini", "inl", "java", "jpeg", "jpg", "js", "la", "lnk",
    "log", "mak", "manifest", "wmv", "mov", "mp3", "mp4", "mpe", "mpeg", "mpg", "m4a", "ofr", "ogg",
    "pac", "pas", "pdf", "php", "php3", "php4", "php5", "phptml", "pl", "pm", "png", "ps", "py",
    "pyo", "ra", "rb", "rc", "reg", "rka", "rm", "rtf", "sed", "sh", "shn", "shtml", "sln", "sql",
    "srt", "swa", "tcl", "tex", "tiff", "tta", "txt", "vb", "vcproj", "vbs", "mkv", "wav", "webm",
    "wma", "wv", "xml", "xsd", "xsl", "xslt",
];

const SPLIT_ARC_EXTS: &[&str] = &["7z", "bz2", "gz", "rar", "zip"];
const NUMBERED_ARC_EXTS: &[&str] = &["7z", "zip", "tar", "wim"];
const HASH_SIDECAR_EXT: &str = "sha256";

pub const HASH_METHODS: &[(&str, &str)] = &[
    ("crc32", "CRC32"),
    ("crc64", "CRC64"),
    ("xxh64", "XXH64"),
    ("md5", "MD5"),
    ("sha1", "SHA1"),
    ("sha256", "SHA256"),
    ("sha384", "SHA384"),
    ("sha512", "SHA512"),
    ("sha3-256", "SHA3-256"),
    ("blake2sp", "BLAKE2sp"),
    ("all", "*"),
];

pub const ARK_ACTIONS: &[&str] = &[
    "open",
    "extract",
    "extract-here",
    "extract-to",
    "test",
    "compress",
    "compress-email",
    "compress-to-7z",
    "compress-to-7z-email",
    "compress-to-zip",
    "compress-to-zip-email",
    "hash-crc32",
    "hash-crc64",
    "hash-xxh64",
    "hash-md5",
    "hash-sha1",
    "hash-sha256",
    "hash-sha384",
    "hash-sha512",
    "hash-sha3-256",
    "hash-blake2sp",
    "hash-all",
    "hash-generate-sha256",
    "hash-test",
];

fn correct_fs_name(name: &str) -> String {
    let cleaned = name.replace('/', "_").replace('\0', "_");
    if cleaned.is_empty() {
        "Archive".into()
    } else {
        cleaned
    }
}

fn extension(name: &str) -> &str {
    match name.rfind('.') {
        Some(dot) if dot + 1 < name.len() => &name[dot + 1..],
        _ => "",
    }
}

pub fn needs_extract(name: &str) -> bool {
    let ext = extension(name);
    if ext.is_empty() || ext.len() > 32 {
        return true;
    }
    !EXTRACT_EXCLUDE_EXTENSIONS
        .iter()
        .any(|item| item.eq_ignore_ascii_case(ext))
}

pub fn extract_subfolder_name(arc_name: &str) -> String {
    let Some(dot) = arc_name.rfind('.') else {
        return format!("{}~", correct_fs_name(arc_name));
    };
    let ext = &arc_name[dot + 1..];
    let mut res = arc_name[..dot].trim_end().to_string();
    if let Some(inner_dot) = res.rfind('.') {
        if inner_dot > 0 {
            let ext2 = &res[inner_dot + 1..];
            let part = ext2.to_ascii_lowercase();
            let strip = (ext.eq_ignore_ascii_case("001")
                && SPLIT_ARC_EXTS.iter().any(|e| e.eq_ignore_ascii_case(ext2)))
                || (ext.eq_ignore_ascii_case("rar")
                    && matches!(part.as_str(), "part001" | "part01" | "part1"));
            if strip {
                res = res[..inner_dot].trim_end().to_string();
            }
        }
    }
    correct_fs_name(&res)
}

pub fn reduce_string(text: &str, max_size: usize) -> String {
    if text.len() <= max_size {
        return text.to_string();
    }
    let half = max_size / 2;
    format!("{} ... {}", &text[..half], &text[text.len() - half..])
}

pub fn quoted_reduced(name: &str) -> String {
    format!("\"{}\"", reduce_string(name, 64).replace('&', "&&"))
}

pub fn create_archive_name(paths: &[PathBuf], is_hash: bool) -> String {
    if paths.is_empty() {
        return "Archive".into();
    }
    let mut name = "Archive".to_string();
    let mut fi_name = None;
    let mut fi_is_dir = false;
    if paths.len() == 1 {
        fi_name = Some(paths[0].file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default());
        fi_is_dir = paths[0].is_dir();
    } else if let Some(parent) = paths[0].parent() {
        if let Some(parent_name) = parent.file_name() {
            if !parent_name.is_empty() {
                name = parent_name.to_string_lossy().into_owned();
            }
        }
    }
    if let Some(fi_name) = fi_name {
        name = fi_name;
        if !fi_is_dir && !is_hash {
            if let Some(dot) = name.find('.') {
                if dot > 0 && !name[dot + 1..].contains('.') {
                    name = name[..dot].to_string();
                }
            }
        }
    }
    name = correct_fs_name(&name);
    let numbered: &[&str] = if is_hash {
        &[HASH_SIDECAR_EXT]
    } else {
        NUMBERED_ARC_EXTS
    };
    if needs_numeric_suffix(paths, &name, numbered) {
        name = format!("{name}_2");
    }
    name
}

fn needs_numeric_suffix(paths: &[PathBuf], name: &str, exts: &[&str]) -> bool {
    let prefixes: Vec<String> = exts
        .iter()
        .map(|ext| format!("{name}.{ext}").to_ascii_lowercase())
        .collect();
    paths.iter().any(|path| {
        path.file_name()
            .map(|n| prefixes.contains(&n.to_string_lossy().to_ascii_lowercase()))
            .unwrap_or(false)
    })
}

pub fn destination_dir(paths: &[PathBuf]) -> PathBuf {
    paths[0]
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_else(|| PathBuf::from("."))
}

pub fn relative_to_destination(paths: &[PathBuf]) -> (PathBuf, Vec<String>) {
    let dest = destination_dir(paths);
    let dest_resolved = dest.canonicalize().unwrap_or_else(|_| dest.clone());
    let names = paths
        .iter()
        .map(|path| {
            let resolved = path.canonicalize().unwrap_or_else(|_| path.clone());
            resolved
                .strip_prefix(&dest_resolved)
                .map(|p| p.display().to_string())
                .unwrap_or_else(|_| resolved.display().to_string())
        })
        .collect();
    (dest, names)
}

pub fn selection_wants_extract(paths: &[PathBuf]) -> bool {
    if paths.is_empty() || paths.iter().any(|p| p.is_dir()) {
        return false;
    }
    paths.iter().all(|p| {
        p.file_name()
            .map(|n| needs_extract(&n.to_string_lossy()))
            .unwrap_or(true)
    })
}

pub fn find_7z() -> Option<String> {
    which::which("7z")
        .or_else(|_| which::which("7za"))
        .ok()
        .map(|p| p.display().to_string())
}

fn find_ark() -> Option<String> {
    which::which("ark").ok().map(|p| p.display().to_string())
}

fn find_mailer() -> Option<String> {
    which::which("xdg-email").ok().map(|p| p.display().to_string())
}

fn require_7z() -> Result<String, i32> {
    find_7z().ok_or_else(|| {
        ui::error_dialog(
            "Archive",
            "<b>7z</b> not found.\n\n\
             The Archive menu uses 7-Zip for extract/compress/hash.\n\
             Install it:\n\
             • <tt>sudo dnf install 7zip</tt> (Fedora)\n\
             • <tt>sudo apt install 7zip</tt> (Debian/Ubuntu)\n\
             • <tt>sudo pacman -S 7zip</tt> (Arch)",
        );
        1
    })
}

fn require_ark() -> Result<String, i32> {
    find_ark().ok_or_else(|| {
        ui::error_dialog("Archive", "<b>ark</b> not found.\n\nInstall the Ark archive manager.");
        1
    })
}

fn run_7z(args: &[&str], cwd: Option<&Path>) -> Result<Output, i32> {
    let exe = require_7z()?;
    let mut cmd = Command::new(exe);
    cmd.args(args);
    if let Some(cwd) = cwd {
        cmd.current_dir(cwd);
    }
    cmd.output().map_err(|_| {
        ui::error_dialog("Archive", "Failed to run 7z.");
        1
    })
}

fn seven_ok(result: &Output) -> bool {
    matches!(result.status.code(), Some(0) | Some(1))
}

fn show_7z_failure(title: &str, result: &Output) {
    let detail = if !result.stderr.is_empty() {
        String::from_utf8_lossy(&result.stderr).trim().to_string()
    } else if !result.stdout.is_empty() {
        String::from_utf8_lossy(&result.stdout).trim().to_string()
    } else {
        format!("7z exited {}", result.status.code().unwrap_or(-1))
    };
    ui::error_dialog(title, &detail);
}

fn show_text(title: &str, text: &str) {
    let body = if text.trim().is_empty() {
        "(no output)".to_string()
    } else {
        text.trim().to_string()
    };
    if body.len() < 1200 {
        ui::info_dialog(title, &body, 640, 400);
        return;
    }
    if let Ok(mut tmp) = tempfile::Builder::new().suffix(".txt").tempfile() {
        use std::io::Write;
        let _ = tmp.write_all(body.as_bytes());
        let path = tmp.path().display().to_string();
        let _ = ui::kdialog(&["--title", title, "--textbox", &path, "80", "24"]);
    }
}

fn start_detached(executable: &str, args: &[String]) -> Result<(), i32> {
    Command::new(executable)
        .args(args)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .map(|_| ())
        .map_err(|exc| {
            ui::error_dialog("Archive", &format!("Cannot start {executable}: {exc}"));
            1
        })
}

fn email_attachment(archive: &Path) -> Result<(), i32> {
    let Some(mailer) = find_mailer() else {
        ui::error_dialog(
            "Archive",
            &format!(
                "No mailer found (xdg-email). The archive was created:\n<tt>{}</tt>",
                archive.display()
            ),
        );
        return Err(1);
    };
    let result = Command::new(mailer)
        .args(["--attach", &archive.display().to_string()])
        .output()
        .map_err(|_| 1)?;
    if !result.status.success() {
        let detail = if !result.stderr.is_empty() {
            String::from_utf8_lossy(&result.stderr).trim().to_string()
        } else {
            "xdg-email failed".into()
        };
        ui::error_dialog("Archive", &detail);
        return Err(1);
    }
    Ok(())
}

fn open_archive(paths: &[PathBuf]) -> Result<(), i32> {
    if paths.len() != 1 || paths[0].is_dir() {
        ui::error_dialog("Archive", "Open archive needs exactly one file.");
        return Err(1);
    }
    start_detached(&require_ark()?, &[paths[0].display().to_string()])
}

fn extract_files(paths: &[PathBuf]) -> Result<(), i32> {
    if paths.is_empty() {
        return Ok(());
    }
    let mut dest = destination_dir(paths);
    if paths.len() == 1 {
        dest = dest.join(extract_subfolder_name(
            &paths[0].file_name().unwrap_or_default().to_string_lossy(),
        ));
    }
    let mut args = vec![
        "--batch".into(),
        "--dialog".into(),
        "--destination".into(),
        dest.display().to_string(),
    ];
    args.extend(paths.iter().map(|p| p.display().to_string()));
    start_detached(&require_ark()?, &args)
}

fn extract_with_7z(paths: &[PathBuf], dest: &Path, elim_dup: bool, title: &str) -> Result<(), i32> {
    fs::create_dir_all(dest).map_err(|_| 1)?;
    let dest_arg = format!("-o{}", dest.display());
    let mut args = vec!["x", "-y"];
    if elim_dup {
        args.push("-spe");
    }
    args.push(&dest_arg);
    args.push("--");
    let path_args: Vec<String> = paths.iter().map(|p| p.display().to_string()).collect();
    let mut owned: Vec<&str> = args;
    for path in &path_args {
        owned.push(path);
    }
    let result = run_7z(&owned, None)?;
    if !seven_ok(&result) {
        show_7z_failure(title, &result);
        return Err(1);
    }
    ui::notify("Archive", &format!("Extracted to {}", dest.display()), "ark");
    Ok(())
}

pub fn extract_here(paths: &[PathBuf]) -> Result<(), i32> {
    extract_with_7z(paths, &destination_dir(paths), false, "Extract Here")
}

pub fn extract_to(paths: &[PathBuf]) -> Result<(), i32> {
    let dest_root = destination_dir(paths);
    if paths.len() == 1 {
        let dest = dest_root.join(extract_subfolder_name(
            &paths[0].file_name().unwrap_or_default().to_string_lossy(),
        ));
        return extract_with_7z(paths, &dest, true, "Extract to");
    }
    let mut errors = false;
    for path in paths {
        let dest = dest_root.join(extract_subfolder_name(
            &path.file_name().unwrap_or_default().to_string_lossy(),
        ));
        if extract_with_7z(&[path.clone()], &dest, true, "Extract to").is_err() {
            errors = true;
        }
    }
    if errors {
        return Err(1);
    }
    ui::notify("Archive", &format!("Extracted {} archive(s)", paths.len()), "ark");
    Ok(())
}

fn test_archive(paths: &[PathBuf]) -> Result<(), i32> {
    let path_args: Vec<String> = paths.iter().map(|p| p.display().to_string()).collect();
    let mut args = vec!["t".to_string(), "--".to_string()];
    args.extend(path_args);
    let borrowed: Vec<&str> = args.iter().map(String::as_str).collect();
    let result = run_7z(&borrowed, None)?;
    let output = format!(
        "{}{}",
        String::from_utf8_lossy(&result.stdout),
        String::from_utf8_lossy(&result.stderr)
    );
    if !seven_ok(&result) {
        show_7z_failure("Test archive", &result);
        return Err(1);
    }
    show_text("Test archive", &output);
    Ok(())
}

fn add_to_archive(paths: &[PathBuf]) -> Result<(), i32> {
    let (dest, names) = relative_to_destination(paths);
    let mut args = vec!["--add".into(), "--changetofirstpath".into()];
    args.extend(names.into_iter().map(|name| dest.join(name).display().to_string()));
    start_detached(&require_ark()?, &args)
}

fn compress_to(paths: &[PathBuf], suffix: &str, arc_type: &str) -> Result<PathBuf, i32> {
    let (dest, names) = relative_to_destination(paths);
    let archive = dest.join(format!("{}{suffix}", create_archive_name(paths, false)));
    let type_arg = format!("-t{arc_type}");
    let archive_s = archive.display().to_string();
    let mut args = vec!["a".to_string(), type_arg, "--".to_string(), archive_s];
    args.extend(names);
    let borrowed: Vec<&str> = args.iter().map(String::as_str).collect();
    let result = run_7z(&borrowed, Some(&dest))?;
    if !seven_ok(&result) {
        show_7z_failure(&format!("Add to {}", archive.file_name().unwrap_or_default().to_string_lossy()), &result);
        return Err(1);
    }
    Ok(archive)
}

pub fn compress_to_7z(paths: &[PathBuf]) -> Result<(), i32> {
    let archive = compress_to(paths, ".7z", "7z")?;
    ui::notify(
        "Archive",
        &format!("Created {}", archive.file_name().unwrap_or_default().to_string_lossy()),
        "ark",
    );
    Ok(())
}

pub fn compress_to_zip(paths: &[PathBuf]) -> Result<(), i32> {
    let archive = compress_to(paths, ".zip", "zip")?;
    ui::notify(
        "Archive",
        &format!("Created {}", archive.file_name().unwrap_or_default().to_string_lossy()),
        "ark",
    );
    Ok(())
}

fn compress_and_email(paths: &[PathBuf]) -> Result<(), i32> {
    let dest = destination_dir(paths);
    let suggested = dest.join(format!("{}.7z", create_archive_name(paths, false)));
    let chosen = ui::kdialog(&[
        "--title",
        "Compress and email",
        "--getsavefilename",
        &suggested.display().to_string(),
        "7-Zip (*.7z)|ZIP (*.zip)",
    ]);
    if !chosen.status.success() {
        return Ok(());
    }
    let mut archive = PathBuf::from(String::from_utf8_lossy(&chosen.stdout).trim());
    if archive.file_name().is_none() {
        return Ok(());
    }
    let suffix = archive
        .extension()
        .map(|e| format!(".{}", e.to_string_lossy().to_ascii_lowercase()))
        .unwrap_or_default();
    let mut arc_type = if suffix == ".zip" { "zip" } else { "7z" };
    if suffix != ".7z" && suffix != ".zip" {
        archive.set_extension("7z");
        arc_type = "7z";
    }
    let (dest_dir, names) = relative_to_destination(paths);
    let type_arg = format!("-t{arc_type}");
    let archive_s = archive.display().to_string();
    let mut args = vec!["a".to_string(), type_arg, "--".to_string(), archive_s];
    args.extend(names);
    let borrowed: Vec<&str> = args.iter().map(String::as_str).collect();
    let result = run_7z(&borrowed, Some(&dest_dir))?;
    if !seven_ok(&result) {
        show_7z_failure("Compress and email", &result);
        return Err(1);
    }
    email_attachment(&archive)
}

pub fn hash_files(paths: &[PathBuf], method_key: &str) -> Result<(), i32> {
    let method = HASH_METHODS
        .iter()
        .find(|(k, _)| *k == method_key)
        .map(|(_, v)| *v)
        .ok_or(1)?;
    let (dest, names) = relative_to_destination(paths);
    let method_arg = format!("-scrc{method}");
    let mut args = vec!["h".to_string(), method_arg];
    if paths.iter().any(|p| p.is_dir()) {
        args.push("-r".into());
    }
    args.push("--".into());
    args.extend(names);
    let borrowed: Vec<&str> = args.iter().map(String::as_str).collect();
    let result = run_7z(&borrowed, Some(&dest))?;
    let output = format!(
        "{}{}",
        String::from_utf8_lossy(&result.stdout),
        String::from_utf8_lossy(&result.stderr)
    );
    if !seven_ok(&result) {
        show_7z_failure("CRC SHA", &result);
        return Err(1);
    }
    show_text(&format!("CRC SHA — {method}"), &output);
    Ok(())
}

fn generate_sha256(paths: &[PathBuf]) -> Result<(), i32> {
    let (dest, names) = relative_to_destination(paths);
    let sidecar = dest.join(format!("{}.sha256", create_archive_name(paths, true)));
    let mut args = vec!["h".to_string(), "-scrcSHA256".into(), "-ba".into()];
    if paths.iter().any(|p| p.is_dir()) {
        args.push("-r".into());
    }
    args.push("--".into());
    args.extend(names);
    let borrowed: Vec<&str> = args.iter().map(String::as_str).collect();
    let result = run_7z(&borrowed, Some(&dest))?;
    if !seven_ok(&result) {
        show_7z_failure("SHA-256 -> file.sha256", &result);
        return Err(1);
    }
    fs::write(&sidecar, result.stdout).map_err(|_| 1)?;
    ui::notify(
        "Archive",
        &format!("Wrote {}", sidecar.file_name().unwrap_or_default().to_string_lossy()),
        "ark",
    );
    Ok(())
}

fn test_checksum(paths: &[PathBuf]) -> Result<(), i32> {
    let hash_files_selected: Vec<PathBuf> = paths
        .iter()
        .filter(|p| {
            p.extension()
                .map(|e| {
                    matches!(
                        e.to_string_lossy().to_ascii_lowercase().as_str(),
                        "sha256" | "sha1" | "md5" | "sfv"
                    )
                })
                .unwrap_or(false)
        })
        .cloned()
        .collect();
    if !hash_files_selected.is_empty() {
        let (dest, names) = relative_to_destination(&hash_files_selected);
        let mut args = vec!["t".to_string(), "--".into()];
        args.extend(names);
        let borrowed: Vec<&str> = args.iter().map(String::as_str).collect();
        let result = run_7z(&borrowed, Some(&dest))?;
        let output = format!(
            "{}{}",
            String::from_utf8_lossy(&result.stdout),
            String::from_utf8_lossy(&result.stderr)
        );
        if !seven_ok(&result) {
            show_7z_failure("Test archive : Checksum", &result);
            return Err(1);
        }
        show_text("Test archive : Checksum", &output);
        return Ok(());
    }
    test_archive(paths)
}

pub fn run_action(action: &str, files: &[String]) -> i32 {
    let paths: Vec<PathBuf> = files
        .iter()
        .map(|raw| {
            let path = PathBuf::from(raw);
            if let Ok(expanded) = shellexpand_home(&path) {
                expanded
            } else {
                path
            }
        })
        .filter(|p| p.exists() || p.symlink_metadata().is_ok())
        .collect();
    if paths.is_empty() {
        ui::error_dialog("Archive", "No local files or folders were selected.");
        return 1;
    }
    if let Some(key) = action.strip_prefix("hash-") {
        if HASH_METHODS.iter().any(|(k, _)| *k == key) {
            return hash_files(&paths, key).err().unwrap_or(0);
        }
    }
    let result = match action {
        "open" => open_archive(&paths),
        "extract" => extract_files(&paths),
        "extract-here" => extract_here(&paths),
        "extract-to" => extract_to(&paths),
        "test" => test_archive(&paths),
        "compress" => add_to_archive(&paths),
        "compress-email" => compress_and_email(&paths),
        "compress-to-7z" => compress_to_7z(&paths),
        "compress-to-zip" => compress_to_zip(&paths),
        "compress-to-7z-email" => {
            let archive = compress_to(&paths, ".7z", "7z");
            archive.and_then(|archive| email_attachment(&archive))
        }
        "compress-to-zip-email" => {
            let archive = compress_to(&paths, ".zip", "zip");
            archive.and_then(|archive| email_attachment(&archive))
        }
        "hash-generate-sha256" => generate_sha256(&paths),
        "hash-test" => test_checksum(&paths),
        other => {
            ui::error_dialog("Archive", &format!("Unknown Archive action: {other}"));
            return 1;
        }
    };
    result.err().unwrap_or(0)
}

fn shellexpand_home(path: &Path) -> Result<PathBuf, ()> {
    let raw = path.to_string_lossy();
    if let Some(rest) = raw.strip_prefix("~/") {
        if let Some(home) = dirs::home_dir() {
            return Ok(home.join(rest));
        }
    }
    Ok(path.to_path_buf())
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    #[test]
    fn needs_extract_matches_7zip_exclude_list() {
        assert!(!needs_extract("notes.txt"));
        assert!(!needs_extract("clip.mp4"));
        assert!(!needs_extract("Photo.JPG"));
        assert!(needs_extract("payload.zip"));
        assert!(needs_extract("payload.7z"));
        assert!(needs_extract("payload.tar.gz"));
        assert!(needs_extract("README"));
        assert!(needs_extract("weird.notalist"));
    }

    #[test]
    fn extract_subfolder_strips_last_extension() {
        assert_eq!(extract_subfolder_name("photos.zip"), "photos");
        assert_eq!(extract_subfolder_name("photos.tar.gz"), "photos.tar");
        assert_eq!(extract_subfolder_name("noext"), "noext~");
        assert_eq!(extract_subfolder_name("archive.7z.001"), "archive");
        assert_eq!(extract_subfolder_name("vol.part1.rar"), "vol");
        assert_eq!(extract_subfolder_name("vol.part01.rar"), "vol");
        assert_eq!(extract_subfolder_name("vol.part001.rar"), "vol");
    }

    #[test]
    fn create_archive_name_single_file() {
        let dir = TempDir::new().unwrap();
        let txt = dir.path().join("letter.txt");
        fs::write(&txt, "hi").unwrap();
        let tgz = dir.path().join("bundle.tar.gz");
        fs::write(&tgz, "x").unwrap();
        assert_eq!(create_archive_name(&[txt], false), "letter");
        assert_eq!(create_archive_name(&[tgz], false), "bundle.tar.gz");
    }

    #[test]
    fn create_archive_name_directory_keeps_dots() {
        let dir = TempDir::new().unwrap();
        let folder = dir.path().join("v1.2");
        fs::create_dir(&folder).unwrap();
        assert_eq!(create_archive_name(&[folder], false), "v1.2");
    }

    #[test]
    fn create_archive_name_multiple_uses_parent_folder() {
        let dir = TempDir::new().unwrap();
        let parent = dir.path().join("Project");
        fs::create_dir(&parent).unwrap();
        let a = parent.join("a.txt");
        let b = parent.join("b.txt");
        fs::write(&a, "a").unwrap();
        fs::write(&b, "b").unwrap();
        assert_eq!(create_archive_name(&[a, b], false), "Project");
    }

    #[test]
    fn create_archive_name_numbers_when_7z_already_selected() {
        let dir = TempDir::new().unwrap();
        let archive = dir.path().join("letter.7z");
        fs::write(&archive, b"PK").unwrap();
        assert_eq!(create_archive_name(&[archive], false), "letter_2");
    }

    #[test]
    fn hash_sidecar_keeps_filename() {
        let dir = TempDir::new().unwrap();
        let txt = dir.path().join("letter.txt");
        fs::write(&txt, "hi").unwrap();
        assert_eq!(create_archive_name(&[txt], true), "letter.txt");
    }

    #[test]
    fn quoted_reduced_escapes_ampersand_and_shortens() {
        assert_eq!(quoted_reduced("A & B"), "\"A && B\"");
        let long = "a".repeat(80);
        let reduced = quoted_reduced(&long);
        assert!(reduced.starts_with('"'));
        assert!(reduced.contains(" ... "));
        assert_eq!(reduced.len(), 2 + 32 + 5 + 32);
    }

    #[test]
    fn selection_wants_extract_cases() {
        let dir = TempDir::new().unwrap();
        let archive = dir.path().join("a.zip");
        let text = dir.path().join("a.txt");
        let folder = dir.path().join("dir");
        fs::write(&archive, b"PK\x05\x06").unwrap();
        fs::write(&text, "x").unwrap();
        fs::create_dir(&folder).unwrap();
        assert!(selection_wants_extract(&[archive.clone()]));
        assert!(!selection_wants_extract(&[text.clone()]));
        assert!(!selection_wants_extract(&[archive, text]));
        assert!(!selection_wants_extract(&[folder]));
    }
}
