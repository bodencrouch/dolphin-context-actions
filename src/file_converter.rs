use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use serde::Deserialize;
use serde_json::Value;

use crate::ui;

pub const APP_NAME: &str = "Dolphin Context Actions";
const BUNDLED_REGISTRY: &str = include_str!("../assets/conversions.yaml");

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Conversion {
    pub id: String,
    pub label: String,
    pub source_extensions: Vec<String>,
    pub source_mimetypes: Vec<String>,
    pub target_extension: String,
    pub featured: bool,
    pub requires_commands: Vec<String>,
    pub requires_packages: Vec<String>,
    pub engine: String,
    pub icon: String,
}

#[derive(Debug, Deserialize)]
struct RegistryFile {
    conversions: Vec<RegistryEntry>,
}

#[derive(Debug, Deserialize)]
struct RegistryEntry {
    id: String,
    label: String,
    #[serde(default)]
    source_extensions: Vec<String>,
    #[serde(default)]
    source_mimetypes: Vec<String>,
    target_extension: String,
    #[serde(default)]
    featured: bool,
    #[serde(default)]
    requires_commands: Vec<String>,
    #[serde(default)]
    requires_packages: Vec<String>,
    engine: String,
    #[serde(default = "default_icon")]
    icon: String,
}

fn default_icon() -> String {
    "document-convert".into()
}

pub fn load_registry_file(path: &Path) -> Result<serde_json::Value, String> {
    if path.extension().and_then(|e| e.to_str()) == Some("json") {
        let text = fs::read_to_string(path).map_err(|e| e.to_string())?;
        return serde_json::from_str(&text).map_err(|e| e.to_string());
    }
    let text = match fs::read_to_string(path) {
        Ok(text) => text,
        Err(_) => {
            let sibling = path.with_extension("json");
            if sibling.exists() {
                let text = fs::read_to_string(sibling).map_err(|e| e.to_string())?;
                return serde_json::from_str(&text).map_err(|e| e.to_string());
            }
            return Err(format!("cannot read {}", path.display()));
        }
    };
    serde_yaml::from_str(&text).map_err(|e| e.to_string())
}

pub fn load_bundled_registry() -> serde_json::Value {
    serde_yaml::from_str(BUNDLED_REGISTRY).expect("bundled conversions.yaml is valid")
}

pub fn registry_path() -> Result<PathBuf, String> {
    let config = crate::config::config_dir().join("conversions.yaml");
    let system = PathBuf::from("/usr/share/dolphin-context-actions/conversions.yaml");
    for base in [config, system] {
        if base.exists() {
            return Ok(base);
        }
        let json = base.with_extension("json");
        if json.exists() {
            return Ok(json);
        }
    }
    Ok(PathBuf::from("<bundled>"))
}

pub fn parse_registry(data: &Value) -> Vec<Conversion> {
    let parsed: RegistryFile = serde_json::from_value(data.clone()).unwrap_or(RegistryFile {
        conversions: Vec::new(),
    });
    parsed
        .conversions
        .into_iter()
        .map(|entry| Conversion {
            source_extensions: entry
                .source_extensions
                .into_iter()
                .map(|ext| {
                    if ext.starts_with('.') {
                        ext.to_ascii_lowercase()
                    } else {
                        format!(".{}", ext.to_ascii_lowercase())
                    }
                })
                .collect(),
            id: entry.id,
            label: entry.label,
            source_mimetypes: entry.source_mimetypes,
            target_extension: entry.target_extension,
            featured: entry.featured,
            requires_commands: entry.requires_commands,
            requires_packages: entry.requires_packages,
            engine: entry.engine,
            icon: entry.icon,
        })
        .collect()
}

pub fn command_available(name: &str) -> bool {
    which::which(name).is_ok()
}

pub fn package_available(name: &str) -> bool {
    match name {
        "python3-PyMuPDF" | "pymupdf" | "PyMuPDF" => command_available("pdftotext"),
        "PyYAML" | "tomli-w" | "tomli_w" => true,
        _ => false,
    }
}

pub fn conversion_available(conv: &Conversion) -> bool {
    if !conv.requires_commands.is_empty()
        && !conv.requires_commands.iter().any(|cmd| command_available(cmd))
    {
        return false;
    }
    if !conv.requires_packages.is_empty()
        && !conv.requires_packages.iter().all(|pkg| package_available(pkg))
    {
        return false;
    }
    true
}

pub fn load_conversions(available_only: bool) -> Vec<Conversion> {
    let data = match registry_path() {
        Ok(path) if path == PathBuf::from("<bundled>") => load_bundled_registry(),
        Ok(path) => load_registry_file(&path).unwrap_or_else(|_| load_bundled_registry()),
        Err(_) => load_bundled_registry(),
    };
    let conversions = parse_registry(&data);
    if available_only {
        conversions.into_iter().filter(conversion_available).collect()
    } else {
        conversions
    }
}

pub fn load_conversions_from(path: &Path, available_only: bool) -> Vec<Conversion> {
    let data = load_registry_file(path).unwrap_or_else(|_| load_bundled_registry());
    let conversions = parse_registry(&data);
    if available_only {
        conversions.into_iter().filter(conversion_available).collect()
    } else {
        conversions
    }
}

pub fn matches_conversion(path: &Path, conv: &Conversion) -> bool {
    path.is_file()
        && path
            .extension()
            .map(|ext| {
                let dotted = format!(".{}", ext.to_string_lossy().to_ascii_lowercase());
                conv.source_extensions.contains(&dotted)
            })
            .unwrap_or(false)
}

pub fn applicable_conversions<'a>(paths: &[PathBuf], conversions: &'a [Conversion]) -> Vec<&'a Conversion> {
    conversions
        .iter()
        .filter(|conv| paths.iter().any(|path| matches_conversion(path, conv)))
        .collect()
}

pub fn output_path_for(source: &Path, conv: &Conversion) -> PathBuf {
    let mut ext = conv.target_extension.clone();
    if !ext.starts_with('.') {
        ext.insert(0, '.');
    }
    source.with_extension(ext.trim_start_matches('.'))
}

fn find_libreoffice() -> Option<Vec<String>> {
    for cmd in ["libreoffice", "soffice"] {
        if command_available(cmd) {
            return Some(vec![cmd.into()]);
        }
    }
    if command_available("flatpak") {
        return Some(vec![
            "flatpak".into(),
            "run".into(),
            "--command=libreoffice".into(),
            "org.libreoffice.LibreOffice".into(),
        ]);
    }
    None
}

pub fn find_imagemagick() -> Option<Vec<String>> {
    if command_available("magick") {
        Some(vec!["magick".into()])
    } else if command_available("convert") {
        Some(vec!["convert".into()])
    } else {
        None
    }
}

pub fn convert_pymupdf_text(source: &Path, target: &Path) -> Result<(), String> {
    if !command_available("pdftotext") {
        return Err("pdftotext is not installed (poppler-utils)".into());
    }
    let output = Command::new("pdftotext")
        .args(["-layout", "-enc", "UTF-8"])
        .arg(source)
        .arg(target)
        .output()
        .map_err(|e| e.to_string())?;
    if !output.status.success() {
        return Err(String::from_utf8_lossy(&output.stderr).chars().take(500).collect());
    }
    Ok(())
}

pub fn convert_libreoffice_headless(source: &Path, target: &Path) -> Result<(), String> {
    let prefix = find_libreoffice().ok_or_else(|| "LibreOffice is not installed".to_string())?;
    let outdir = source.parent().unwrap_or(Path::new("."));
    let mut cmd = Command::new(&prefix[0]);
    cmd.args(&prefix[1..]);
    cmd.args([
        "--headless",
        "--convert-to",
        target.extension().and_then(|e| e.to_str()).unwrap_or("pdf"),
        "--outdir",
        &outdir.display().to_string(),
    ]);
    cmd.arg(source);
    let result = cmd.output().map_err(|e| e.to_string())?;
    if !result.status.success() {
        let stderr = if !result.stderr.is_empty() {
            String::from_utf8_lossy(&result.stderr)
        } else {
            String::from_utf8_lossy(&result.stdout)
        };
        return Err(stderr.chars().take(500).collect());
    }
    let produced = outdir.join(format!(
        "{}{}",
        source.file_stem().unwrap_or_default().to_string_lossy(),
        target
            .extension()
            .map(|e| format!(".{e}", e = e.to_string_lossy()))
            .unwrap_or_default()
    ));
    if produced != target && produced.exists() {
        fs::rename(produced, target).map_err(|e| e.to_string())?;
    }
    Ok(())
}

pub fn convert_yaml_json(source: &Path, target: &Path) -> Result<(), String> {
    let text = fs::read_to_string(source).map_err(|e| e.to_string())?;
    if source.extension().and_then(|e| e.to_str()).map(|e| e.eq_ignore_ascii_case("json")).unwrap_or(false)
    {
        let data: Value = serde_json::from_str(&text).map_err(|e| e.to_string())?;
        let yaml = serde_yaml::to_string(&data).map_err(|e| e.to_string())?;
        fs::write(target, yaml).map_err(|e| e.to_string())?;
    } else {
        let data: Value = serde_yaml::from_str(&text).map_err(|e| e.to_string())?;
        let json = serde_json::to_string_pretty(&data).map_err(|e| e.to_string())?;
        fs::write(target, format!("{json}\n")).map_err(|e| e.to_string())?;
    }
    Ok(())
}

pub fn convert_pandoc(source: &Path, target: &Path) -> Result<(), String> {
    if !command_available("pandoc") {
        return Err("pandoc is not installed".into());
    }
    let result = Command::new("pandoc")
        .arg(source)
        .arg("-o")
        .arg(target)
        .output()
        .map_err(|e| e.to_string())?;
    if !result.status.success() {
        let stderr = if !result.stderr.is_empty() {
            String::from_utf8_lossy(&result.stderr)
        } else {
            String::from_utf8_lossy(&result.stdout)
        };
        return Err(stderr.chars().take(500).collect());
    }
    Ok(())
}

pub fn convert_csv_json(source: &Path, target: &Path) -> Result<(), String> {
    let source_ext = source.extension().and_then(|e| e.to_str()).unwrap_or("").to_ascii_lowercase();
    let target_ext = target.extension().and_then(|e| e.to_str()).unwrap_or("").to_ascii_lowercase();

    if source_ext == "csv" && target_ext == "json" {
        let mut reader = csv::Reader::from_path(source).map_err(|e| e.to_string())?;
        let rows: Vec<serde_json::Map<String, Value>> = reader
            .deserialize()
            .collect::<Result<_, _>>()
            .map_err(|e| e.to_string())?;
        let json = serde_json::to_string_pretty(&rows).map_err(|e| e.to_string())?;
        fs::write(target, format!("{json}\n")).map_err(|e| e.to_string())?;
        return Ok(());
    }

    if source_ext == "tsv" && target_ext == "csv" {
        let mut reader = csv::ReaderBuilder::new().delimiter(b'\t').from_path(source).map_err(|e| e.to_string())?;
        let headers = reader.headers().map_err(|e| e.to_string())?.clone();
        let rows: Vec<csv::StringRecord> = reader.records().collect::<Result<_, _>>().map_err(|e| e.to_string())?;
        let mut writer = csv::Writer::from_path(target).map_err(|e| e.to_string())?;
        writer.write_record(&headers).map_err(|e| e.to_string())?;
        for row in rows {
            writer.write_record(&row).map_err(|e| e.to_string())?;
        }
        writer.flush().map_err(|e| e.to_string())?;
        return Ok(());
    }

    if source_ext == "json" && target_ext == "csv" {
        let data: Value = serde_json::from_str(&fs::read_to_string(source).map_err(|e| e.to_string())?)
            .map_err(|e| e.to_string())?;
        let rows = data
            .as_array()
            .ok_or_else(|| "JSON must be an array of objects to convert to CSV".to_string())?;
        if !rows.iter().all(|row| row.is_object()) {
            return Err("JSON must be an array of objects to convert to CSV".into());
        }
        let mut writer = csv::Writer::from_path(target).map_err(|e| e.to_string())?;
        if rows.is_empty() {
            writer.flush().map_err(|e| e.to_string())?;
            return Ok(());
        }
        let headers: Vec<String> = rows[0]
            .as_object()
            .unwrap()
            .keys()
            .cloned()
            .collect();
        writer.write_record(&headers).map_err(|e| e.to_string())?;
        for row in rows {
            let obj = row.as_object().unwrap();
            let values: Vec<String> = headers
                .iter()
                .map(|h| match obj.get(h) {
                    Some(Value::String(s)) => s.clone(),
                    Some(other) => other.to_string().trim_matches('"').to_string(),
                    None => String::new(),
                })
                .collect();
            writer.write_record(&values).map_err(|e| e.to_string())?;
        }
        writer.flush().map_err(|e| e.to_string())?;
        return Ok(());
    }

    Err(format!("Unsupported csv_json conversion: .{source_ext} -> .{target_ext}"))
}

pub fn convert_toml_json(source: &Path, target: &Path) -> Result<(), String> {
    let source_ext = source.extension().and_then(|e| e.to_str()).unwrap_or("").to_ascii_lowercase();
    let target_ext = target.extension().and_then(|e| e.to_str()).unwrap_or("").to_ascii_lowercase();
    if source_ext == "toml" && target_ext == "json" {
        let data: toml::Value = fs::read_to_string(source)
            .map_err(|e| e.to_string())?
            .parse()
            .map_err(|e: toml::de::Error| e.to_string())?;
        let json = serde_json::to_string_pretty(&data).map_err(|e| e.to_string())?;
        fs::write(target, format!("{json}\n")).map_err(|e| e.to_string())?;
        return Ok(());
    }
    if source_ext == "json" && target_ext == "toml" {
        let data: toml::Value = serde_json::from_str(&fs::read_to_string(source).map_err(|e| e.to_string())?)
            .map_err(|e| e.to_string())?;
        let text = toml::to_string(&data).map_err(|e| e.to_string())?;
        fs::write(target, text).map_err(|e| e.to_string())?;
        return Ok(());
    }
    Err(format!("Unsupported toml_json conversion: .{source_ext} -> .{target_ext}"))
}

pub fn convert_imagemagick(source: &Path, target: &Path) -> Result<(), String> {
    let prefix = find_imagemagick().ok_or_else(|| "ImageMagick is not installed (magick or convert)".to_string())?;
    let mut cmd = Command::new(&prefix[0]);
    cmd.args(&prefix[1..]);
    cmd.arg(source);
    cmd.arg(target);
    let result = cmd.output().map_err(|e| e.to_string())?;
    if !result.status.success() {
        let stderr = if !result.stderr.is_empty() {
            String::from_utf8_lossy(&result.stderr)
        } else {
            String::from_utf8_lossy(&result.stdout)
        };
        return Err(stderr.chars().take(500).collect());
    }
    Ok(())
}

pub fn run_conversion(source: &Path, conv: &Conversion, overwrite: bool) -> Result<PathBuf, String> {
    if !matches_conversion(source, conv) {
        return Err(format!("{} is not supported for {}", source.display(), conv.id));
    }
    let target = output_path_for(source, conv);
    if target.exists() && !overwrite {
        return Err(format!("Output already exists: {}", target.file_name().unwrap_or_default().to_string_lossy()));
    }
    match conv.engine.as_str() {
        "pymupdf_text" => convert_pymupdf_text(source, &target)?,
        "libreoffice_headless" => convert_libreoffice_headless(source, &target)?,
        "yaml_json" => convert_yaml_json(source, &target)?,
        "pandoc" => convert_pandoc(source, &target)?,
        "csv_json" => convert_csv_json(source, &target)?,
        "toml_json" => convert_toml_json(source, &target)?,
        "imagemagick" => convert_imagemagick(source, &target)?,
        other => return Err(format!("Unknown engine: {other}")),
    }
    Ok(target)
}

fn picker_label(conv: &Conversion, paths: &[PathBuf]) -> String {
    let mut exts: Vec<String> = paths
        .iter()
        .filter(|p| matches_conversion(p, conv))
        .filter_map(|p| p.extension().map(|e| format!(".{}", e.to_string_lossy().to_ascii_lowercase())))
        .collect();
    exts.sort();
    exts.dedup();
    if exts.is_empty() {
        conv.label.clone()
    } else {
        format!("{} ({})", conv.label, exts.join(", "))
    }
}

pub fn run_pick(paths: &[PathBuf], overwrite: bool) -> i32 {
    let conversions = load_conversions(true);
    let applicable = applicable_conversions(paths, &conversions);
    if applicable.is_empty() {
        ui::error_dialog(APP_NAME, "No conversions are available for the selected file(s).");
        return 1;
    }
    let mut args = vec![
        "--title".to_string(),
        APP_NAME.to_string(),
        "--menu".to_string(),
        "Select a conversion:".to_string(),
    ];
    for conv in &applicable {
        args.push(conv.id.clone());
        args.push(picker_label(conv, paths));
    }
    let borrowed: Vec<&str> = args.iter().map(String::as_str).collect();
    let result = ui::kdialog(&borrowed);
    if !result.status.success() {
        return result.status.code().unwrap_or(1);
    }
    let chosen_id = String::from_utf8_lossy(&result.stdout).trim().to_string();
    if !applicable.iter().any(|c| c.id == chosen_id) {
        ui::error_dialog(APP_NAME, "Invalid conversion selection.");
        return 1;
    }
    run_convert(&chosen_id, paths, overwrite)
}

pub fn run_convert(conversion_id: &str, paths: &[PathBuf], overwrite: bool) -> i32 {
    let conversions = load_conversions(true);
    let Some(conv) = conversions.iter().find(|c| c.id == conversion_id) else {
        ui::error_dialog(APP_NAME, &format!("Unknown or unavailable conversion: {conversion_id}"));
        return 1;
    };
    let targets: Vec<&PathBuf> = paths.iter().filter(|path| matches_conversion(path, conv)).collect();
    if targets.is_empty() {
        ui::error_dialog(APP_NAME, &format!("No selected files support {}.", conv.label));
        return 1;
    }

    let mut converted = Vec::new();
    let mut skipped = Vec::new();
    let mut failed = Vec::new();
    for source in targets {
        match run_conversion(source, conv, overwrite) {
            Ok(path) => converted.push(path),
            Err(exc) if exc.starts_with("Output already exists:") => skipped.push(exc),
            Err(exc) => failed.push(format!("{}: {exc}", source.file_name().unwrap_or_default().to_string_lossy())),
        }
    }

    if converted.len() == 1 {
        ui::notify(
            "Conversion complete",
            &format!("Created {}", converted[0].file_name().unwrap_or_default().to_string_lossy()),
            &conv.icon,
        );
    } else if converted.len() > 1 {
        ui::notify("Conversions complete", &format!("Created {} files", converted.len()), &conv.icon);
    }
    if !skipped.is_empty() && converted.is_empty() {
        ui::error_dialog(APP_NAME, &skipped.join("\n"));
        return 1;
    }
    if !failed.is_empty() {
        ui::error_dialog(APP_NAME, &failed.join("\n"));
        return 1;
    }
    0
}

pub fn desktop_action_id(conversion_id: &str) -> Result<String, String> {
    let cleaned: String = conversion_id
        .replace('_', "-")
        .chars()
        .filter(|c| c.is_ascii_alphanumeric() || *c == '-')
        .collect();
    if cleaned.is_empty() {
        return Err(format!("Invalid conversion id: {conversion_id}"));
    }
    if cleaned.chars().next().unwrap().is_ascii_digit() {
        Ok(format!("convert{cleaned}"))
    } else {
        Ok(cleaned)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    const EXPECTED_IDS: &[&str] = &[
        "pdf-to-md",
        "docx-to-pdf",
        "odt-to-pdf",
        "ods-to-pdf",
        "pptx-to-pdf",
        "xlsx-to-pdf",
        "rtf-to-pdf",
        "txt-to-pdf",
        "md-to-html",
        "html-to-md",
        "md-to-pdf",
        "yaml-to-json",
        "json-to-yaml",
        "csv-to-json",
        "json-to-csv",
        "tsv-to-csv",
        "toml-to-json",
        "json-to-toml",
        "png-to-jpg",
        "jpg-to-png",
        "webp-to-png",
        "webp-to-jpg",
        "png-to-webp",
        "heic-to-jpg",
    ];

    #[test]
    fn parse_bundled_registry() {
        let conversions = parse_registry(&load_bundled_registry());
        let mut ids: Vec<&str> = conversions.iter().map(|c| c.id.as_str()).collect();
        ids.sort();
        let mut expected = EXPECTED_IDS.to_vec();
        expected.sort();
        assert_eq!(ids, expected);
    }

    #[test]
    fn json_registry_matches_yaml() {
        let yaml = load_bundled_registry();
        let dir = TempDir::new().unwrap();
        let json_path = dir.path().join("conversions.json");
        fs::write(&json_path, serde_json::to_string(&yaml).unwrap()).unwrap();
        let json = load_registry_file(&json_path).unwrap();
        assert_eq!(parse_registry(&yaml), parse_registry(&json));
    }

    #[test]
    fn csv_json_round_trip() {
        let dir = TempDir::new().unwrap();
        let source = dir.path().join("sample.csv");
        let target = dir.path().join("sample.json");
        fs::write(&source, "name,age\nAda,36\nBob,40\n").unwrap();
        convert_csv_json(&source, &target).unwrap();
        let data: Value = serde_json::from_str(&fs::read_to_string(&target).unwrap()).unwrap();
        assert_eq!(data.as_array().unwrap().len(), 2);
        assert_eq!(data[0]["name"], "Ada");
        let csv_out = dir.path().join("sample-out.csv");
        convert_csv_json(&target, &csv_out).unwrap();
        assert!(fs::read_to_string(&csv_out).unwrap().contains("Ada"));
    }

    #[test]
    fn csv_json_rejects_non_array_json() {
        let dir = TempDir::new().unwrap();
        let source = dir.path().join("bad.json");
        let target = dir.path().join("bad.csv");
        fs::write(&source, r#"{"name": "Ada"}"#).unwrap();
        assert!(convert_csv_json(&source, &target).is_err());
    }

    #[test]
    fn tsv_to_csv() {
        let dir = TempDir::new().unwrap();
        let source = dir.path().join("sample.tsv");
        let target = dir.path().join("sample.csv");
        fs::write(&source, "name\tage\nAda\t36\n").unwrap();
        convert_csv_json(&source, &target).unwrap();
        assert!(fs::read_to_string(&target).unwrap().contains("Ada,36"));
    }

    #[test]
    fn toml_json_to_json() {
        let dir = TempDir::new().unwrap();
        let source = dir.path().join("sample.toml");
        let target = dir.path().join("sample.json");
        fs::write(&source, "title = \"Hello\"\ncount = 2\n").unwrap();
        convert_toml_json(&source, &target).unwrap();
        let data: Value = serde_json::from_str(&fs::read_to_string(&target).unwrap()).unwrap();
        assert_eq!(data["title"], "Hello");
        assert_eq!(data["count"], 2);
    }

    #[test]
    fn toml_json_json_to_toml() {
        let dir = TempDir::new().unwrap();
        let source = dir.path().join("sample.json");
        let target = dir.path().join("sample.toml");
        fs::write(&source, r#"{"title": "Hello", "count": 2}"#).unwrap();
        convert_toml_json(&source, &target).unwrap();
        assert!(fs::read_to_string(&target).unwrap().contains("title"));
    }

    #[test]
    fn unknown_conversion_error_stays_headless() {
        let _env = crate::test_env::EnvGuard::lock();
        std::env::set_var("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1");
        assert_eq!(run_convert("missing-conversion", &[], false), 1);
    }
}
