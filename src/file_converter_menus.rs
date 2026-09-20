use std::collections::BTreeMap;
use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::{Path, PathBuf};

use crate::file_converter::{self, Conversion};

const MENU_PREFIX: &str = "dolphin-context-actions-convert";

pub fn slug_for_extensions(extensions: &[String]) -> String {
    extensions
        .iter()
        .map(|ext| ext.trim_start_matches('.').to_ascii_lowercase())
        .collect::<Vec<_>>()
        .join("-")
}

pub fn render_desktop(mimetypes: &[String], featured: &[&Conversion], converter_bin: &Path) -> String {
    let mut action_ids: Vec<String> = featured
        .iter()
        .map(|conv| file_converter::desktop_action_id(&conv.id).unwrap_or_else(|_| conv.id.clone()))
        .collect();
    action_ids.push("moreConversions".into());
    let actions_line = format!("{};", action_ids.join(";"));

    let mut lines = vec![
        "[Desktop Entry]".into(),
        "Type=Service".into(),
        format!("MimeType={};", mimetypes.join(";")),
        format!("Actions={actions_line}"),
        "X-KDE-Submenu=Convert".into(),
        "Icon=document-convert".into(),
        String::new(),
    ];

    for (conv, action_id) in featured.iter().zip(action_ids.iter()) {
        lines.extend([
            format!("[Desktop Action {action_id}]"),
            format!("Name={}", conv.label),
            format!("Icon={}", conv.icon),
            format!("Exec=\"{}\" --file-convert {} %F", converter_bin.display(), conv.id),
            String::new(),
        ]);
    }

    lines.extend([
        "[Desktop Action moreConversions]".into(),
        "Name=More…".into(),
        "Icon=view-list-details".into(),
        format!("Exec=\"{}\" --pick-file-conversion %F", converter_bin.display()),
        String::new(),
    ]);
    lines.join("\n")
}

fn group_conversions(conversions: &[Conversion]) -> BTreeMap<Vec<String>, Vec<Conversion>> {
    let mut groups: BTreeMap<Vec<String>, Vec<Conversion>> = BTreeMap::new();
    for conv in conversions {
        let mut key = conv.source_extensions.clone();
        key.sort();
        groups.entry(key).or_default().push(conv.clone());
    }
    groups
}

pub fn generate(output_dir: &Path, converter_bin: &Path, registry_path: Option<&Path>) -> Vec<PathBuf> {
    let conversions = if let Some(registry) = registry_path {
        file_converter::load_conversions_from(registry, true)
    } else {
        file_converter::load_conversions(true)
    };

    let _ = fs::create_dir_all(output_dir);
    if let Ok(entries) = fs::read_dir(output_dir) {
        for entry in entries.flatten() {
            let name = entry.file_name();
            if name.to_string_lossy().starts_with(&format!("{MENU_PREFIX}-"))
                && name.to_string_lossy().ends_with(".desktop")
            {
                let _ = fs::remove_file(entry.path());
            }
        }
    }

    let mut written = Vec::new();
    for (extensions, group) in group_conversions(&conversions) {
        let slug = slug_for_extensions(&extensions);
        let mut mimetypes: Vec<String> = group.iter().flat_map(|c| c.source_mimetypes.clone()).collect();
        mimetypes.sort();
        mimetypes.dedup();
        if mimetypes.is_empty() {
            mimetypes.push("application/octet-stream".into());
        }
        let featured: Vec<&Conversion> = group.iter().filter(|c| c.featured).collect();
        let content = render_desktop(&mimetypes, &featured, converter_bin);
        let target = output_dir.join(format!("{MENU_PREFIX}-{slug}.desktop"));
        if fs::write(&target, content).is_ok() {
            let _ = fs::set_permissions(&target, fs::Permissions::from_mode(0o755));
            written.push(target);
        }
    }
    written
}

pub fn run(output_dir: &Path, converter_bin: &Path, registry: Option<&Path>) -> i32 {
    let written = generate(output_dir, converter_bin, registry);
    if written.is_empty() {
        eprintln!("WARN: no service menus generated (no available conversions)");
        return 1;
    }
    for path in written {
        println!("{}", path.display());
    }
    0
}
