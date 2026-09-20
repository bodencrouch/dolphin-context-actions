use std::collections::HashMap;
use std::fs::{self, OpenOptions};
use std::io::{self, ErrorKind};
use std::os::unix::fs::{symlink, MetadataExt, OpenOptionsExt};
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use walkdir::WalkDir;

use crate::ui;

const MAX_RENAME_ATTEMPTS: usize = 50;

#[derive(Serialize, Deserialize, Default)]
struct PickedSources {
    sources: Vec<String>,
}

fn picked_sources_file() -> PathBuf {
    if let Ok(override_path) = std::env::var("DOLPHIN_LINK_SOURCES_FILE") {
        return PathBuf::from(override_path);
    }
    dirs::home_dir()
        .unwrap_or_else(|| PathBuf::from("."))
        .join(".cache")
        .join("dolphin-link-extension")
        .join("picked_sources.json")
}

fn ensure_cache_dir() {
    if let Some(parent) = picked_sources_file().parent() {
        let _ = fs::create_dir_all(parent);
    }
}

fn save_picked_sources(sources: &[String]) {
    ensure_cache_dir();
    let data = PickedSources {
        sources: sources.to_vec(),
    };
    if let Ok(text) = serde_json::to_string(&data) {
        let _ = fs::write(picked_sources_file(), text);
    }
}

fn load_picked_sources() -> Vec<String> {
    ensure_cache_dir();
    let path = picked_sources_file();
    if let Ok(text) = fs::read_to_string(path) {
        if let Ok(data) = serde_json::from_str::<PickedSources>(&text) {
            return data.sources;
        }
    }
    Vec::new()
}

fn clear_picked_sources() {
    let _ = fs::remove_file(picked_sources_file());
}

#[derive(Default)]
pub struct LinkSource {
    sources: Vec<String>,
    loaded: bool,
}

impl LinkSource {
    pub fn load(&mut self) -> &[String] {
        if !self.loaded {
            self.sources = load_picked_sources();
            self.loaded = true;
        }
        &self.sources
    }

    pub fn save(&mut self, sources: Vec<String>) {
        self.sources = sources;
        save_picked_sources(&self.sources);
    }

    pub fn pick(&mut self, paths: &[String]) -> Vec<String> {
        let absolute: Vec<String> = paths
            .iter()
            .map(|p| std::path::absolute(p).unwrap_or_else(|_| PathBuf::from(p)).display().to_string())
            .collect();
        self.save(absolute.clone());
        absolute
    }

    pub fn get(&mut self) -> Vec<String> {
        self.load().to_vec()
    }

    pub fn clear(&mut self) {
        self.sources.clear();
        clear_picked_sources();
    }

    pub fn has_sources(&mut self) -> bool {
        !self.get().is_empty()
    }
}

fn with_source_manager<T>(f: impl FnOnce(&mut LinkSource) -> T) -> T {
    // Each call reloads from disk so CLI invocations and tests share state.
    let mut manager = LinkSource::default();
    f(&mut manager)
}

pub fn candidate_leaf_name(source_name: &str, kind: &str, attempt: usize, split_extension: bool) -> String {
    if attempt == 0 {
        return source_name.to_string();
    }
    let (stem, suffix) = if split_extension {
        if let Some(dot) = source_name.rfind('.') {
            if dot > 0 {
                (&source_name[..dot], &source_name[dot..])
            } else {
                (source_name, "")
            }
        } else {
            (source_name, "")
        }
    } else {
        (source_name, "")
    };
    if attempt == 1 {
        format!("{stem} - {kind}{suffix}")
    } else {
        format!("{stem} - {kind} ({attempt}){suffix}")
    }
}

fn splits_extension(source: &Path) -> bool {
    !source.is_dir()
}

fn create_with_rename<F>(parent: &Path, source_name: &str, kind: &str, split_extension: bool, mut create: F) -> io::Result<PathBuf>
where
    F: FnMut(&Path) -> io::Result<()>,
{
    for attempt in 0..=MAX_RENAME_ATTEMPTS {
        let destination = parent.join(candidate_leaf_name(source_name, kind, attempt, split_extension));
        match create(&destination) {
            Ok(()) => return Ok(destination),
            Err(err) if err.kind() == ErrorKind::AlreadyExists => continue,
            Err(err) => return Err(err),
        }
    }
    Err(io::Error::other(format!(
        "Too many name collisions in {} for {source_name}.",
        parent.display()
    )))
}

fn claim_directory(parent: &Path, source_name: &str, kind: &str) -> io::Result<PathBuf> {
    create_with_rename(parent, source_name, kind, false, |path| fs::create_dir(path))
}

fn claim_file(parent: &Path, source_name: &str, kind: &str, split_extension: bool) -> io::Result<PathBuf> {
    create_with_rename(parent, source_name, kind, split_extension, |path| {
        OpenOptions::new()
            .write(true)
            .create_new(true)
            .mode(0o600)
            .open(path)
            .map(|_| ())
    })
}

fn is_writable_dir(path: &Path) -> bool {
    use std::os::unix::ffi::OsStrExt;
    let Ok(c) = std::ffi::CString::new(path.as_os_str().as_bytes()) else {
        return false;
    };
    unsafe { libc_access_w(&c) }
}

fn libc_access_w(path: &std::ffi::CString) -> bool {
    // W_OK == 2
    unsafe { libc_access(path.as_ptr(), 2) == 0 }
}

unsafe fn libc_access(path: *const libc_char, mode: i32) -> i32 {
    extern "C" {
        fn access(path: *const libc_char, mode: i32) -> i32;
    }
    access(path, mode)
}

type libc_char = std::os::raw::c_char;

fn is_symlink(path: &Path) -> bool {
    path.symlink_metadata()
        .map(|m| m.file_type().is_symlink())
        .unwrap_or(false)
}

pub fn pick_link_source(paths: &[String]) -> bool {
    if paths.is_empty() {
        return false;
    }
    let mut valid = Vec::new();
    for p in paths {
        let path = Path::new(p);
        if !path.exists() && !is_symlink(path) {
            ui::error_dialog("Pick Link Source", &format!("Path does not exist: {p}"));
            return false;
        }
        valid.push(p.clone());
    }
    with_source_manager(|mgr| mgr.pick(&valid));
    ui::notify(
        "Link Source Picked",
        &format!("Picked {} item(s) as link source", valid.len()),
        "edit-link",
    );
    true
}

pub fn cancel_link_creation() -> bool {
    with_source_manager(|mgr| {
        if mgr.has_sources() {
            mgr.clear();
            ui::notify("Link Creation Cancelled", "Pick operation cancelled", "edit-delete");
            true
        } else {
            ui::info_dialog("Cancel Link Creation", "No active pick operation to cancel.", 0, 0);
            false
        }
    })
}

pub fn drop_hardlink(target_dir: &str) -> bool {
    let sources = with_source_manager(|mgr| mgr.get());
    if sources.is_empty() {
        ui::error_dialog("Drop Hardlink", "No link source picked. Pick a source first.");
        return false;
    }
    let target_path = Path::new(target_dir);
    if !target_path.exists() || !target_path.is_dir() {
        ui::error_dialog("Drop Hardlink", &format!("Target is not a valid directory: {target_dir}"));
        return false;
    }
    if !is_writable_dir(target_path) {
        ui::error_dialog("Drop Hardlink", &format!("No write permission in: {target_dir}"));
        return false;
    }

    let mut created = Vec::new();
    let mut failed = Vec::new();
    for source in &sources {
        let source_path = Path::new(source);
        let is_real_file = source_path.is_file() && !is_symlink(source_path);
        match (|| -> io::Result<PathBuf> {
            if is_symlink(source_path) {
                return Err(io::Error::other("Symlinks cannot be hardlinked. Use Drop Symlink."));
            }
            if !is_real_file {
                return Err(io::Error::other("Directories cannot be hardlinked. Use Drop Symlink."));
            }
            create_with_rename(
                target_path,
                &source_path.file_name().unwrap_or_default().to_string_lossy(),
                "Hardlink",
                splits_extension(source_path),
                |destination| fs::hard_link(source, destination),
            )
        })() {
            Ok(dest) => created.push(dest.display().to_string()),
            Err(e) => failed.push((source.clone(), e.to_string())),
        }
    }

    if created.len() == 1 {
        ui::notify("Hardlink Created", &format!("Created: {}", created[0]), "edit-link");
    } else if created.len() > 1 {
        ui::notify("Hardlinks Created", &format!("Created {} hardlink(s)", created.len()), "edit-link");
    }
    if !failed.is_empty() {
        let msgs: Vec<String> = failed.iter().map(|(src, err)| format!("{src}: {err}")).collect();
        ui::error_dialog("Hardlink Errors", &msgs.join("\n"));
        with_source_manager(|mgr| mgr.save(failed.into_iter().map(|(s, _)| s).collect()));
    } else {
        with_source_manager(|mgr| mgr.clear());
    }
    !created.is_empty()
}

pub fn drop_symlink(target_dir: &str, relative: bool) -> bool {
    let sources = with_source_manager(|mgr| mgr.get());
    if sources.is_empty() {
        ui::error_dialog("Drop Symlink", "No link source picked. Pick a source first.");
        return false;
    }
    let target_path = Path::new(target_dir);
    if !target_path.exists() || !target_path.is_dir() {
        ui::error_dialog("Drop Symlink", &format!("Target is not a valid directory: {target_dir}"));
        return false;
    }
    if !is_writable_dir(target_path) {
        ui::error_dialog("Drop Symlink", &format!("No write permission in: {target_dir}"));
        return false;
    }

    let target_dir_abs = target_path.canonicalize().unwrap_or_else(|_| target_path.to_path_buf());
    let mut created = Vec::new();
    let mut failed = Vec::new();
    for source in &sources {
        let source_path = Path::new(source);
        let link_value = if relative {
            let parent = source_path
                .parent()
                .and_then(|p| p.canonicalize().ok())
                .unwrap_or_else(|| source_path.parent().unwrap_or(Path::new(".")).to_path_buf());
            let source_abs = parent.join(source_path.file_name().unwrap_or_default());
            pathdiff(&source_abs, &target_dir_abs)
        } else {
            source.clone()
        };
        match create_with_rename(
            target_path,
            &source_path.file_name().unwrap_or_default().to_string_lossy(),
            "Symlink",
            splits_extension(source_path),
            |destination| symlink(&link_value, destination),
        ) {
            Ok(dest) => created.push(dest.display().to_string()),
            Err(e) => failed.push((source.clone(), e.to_string())),
        }
    }

    if created.len() == 1 {
        ui::notify("Symlink Created", &format!("Created: {}", created[0]), "edit-link");
    } else if created.len() > 1 {
        ui::notify("Symlinks Created", &format!("Created {} symlink(s)", created.len()), "edit-link");
    }
    if !failed.is_empty() {
        let msgs: Vec<String> = failed.iter().map(|(src, err)| format!("{src}: {err}")).collect();
        ui::error_dialog("Symlink Errors", &msgs.join("\n"));
        with_source_manager(|mgr| mgr.save(failed.into_iter().map(|(s, _)| s).collect()));
    } else {
        with_source_manager(|mgr| mgr.clear());
    }
    !created.is_empty()
}

fn pathdiff(source: &Path, from_dir: &Path) -> String {
    pathdiff_impl(source, from_dir).unwrap_or_else(|| source.display().to_string())
}

fn pathdiff_impl(path: &Path, base: &Path) -> Option<String> {
    let path_c = path.components().collect::<Vec<_>>();
    let base_c = base.components().collect::<Vec<_>>();
    let mut i = 0;
    while i < path_c.len() && i < base_c.len() && path_c[i] == base_c[i] {
        i += 1;
    }
    let mut rel = PathBuf::new();
    for _ in i..base_c.len() {
        rel.push("..");
    }
    for component in &path_c[i..] {
        rel.push(component);
    }
    if rel.as_os_str().is_empty() {
        Some(".".into())
    } else {
        Some(rel.display().to_string())
    }
}

pub fn hardlink_clone(source_dir: &str, target_dir: &str) -> bool {
    let source_path = std::path::absolute(source_dir).unwrap_or_else(|_| PathBuf::from(source_dir));
    let target_path = std::path::absolute(target_dir).unwrap_or_else(|_| PathBuf::from(target_dir));
    if !source_path.exists() || !source_path.is_dir() {
        ui::error_dialog("Hardlink Clone", &format!("Source is not a valid directory: {source_dir}"));
        return false;
    }
    let Some(parent) = target_path.parent() else {
        ui::error_dialog("Hardlink Clone", "Clone failed: missing parent");
        return false;
    };
    if !is_writable_dir(parent) {
        ui::error_dialog("Hardlink Clone", &format!("No write permission in: {}", parent.display()));
        return false;
    }
    let clone_root = match claim_directory(
        parent,
        &target_path.file_name().unwrap_or_default().to_string_lossy(),
        "Hardlink Clone",
    ) {
        Ok(p) => p,
        Err(error) => {
            ui::error_dialog("Hardlink Clone", &format!("Clone failed: {error}"));
            return false;
        }
    };

    let mut dirs = 0usize;
    let mut files = 0usize;
    let mut symlinks = 0usize;
    let result = (|| -> io::Result<()> {
        for entry in WalkDir::new(&source_path).follow_links(false) {
            let entry = entry?;
            let source_item = entry.path();
            if source_item == source_path {
                continue;
            }
            if source_item == clone_root {
                continue;
            }
            let rel = source_item.strip_prefix(&source_path).unwrap_or(source_item);
            let target_item = clone_root.join(rel);
            if let Some(parent) = target_item.parent() {
                fs::create_dir_all(parent)?;
            }
            if is_symlink(source_item) {
                let dest = fs::read_link(source_item)?;
                symlink(dest, &target_item)?;
                symlinks += 1;
            } else if source_item.is_dir() {
                fs::create_dir_all(&target_item)?;
                dirs += 1;
            } else {
                fs::hard_link(source_item, &target_item)?;
                files += 1;
            }
        }
        Ok(())
    })();
    if let Err(error) = result {
        let _ = fs::remove_dir_all(&clone_root);
        ui::error_dialog("Hardlink Clone", &format!("Clone failed: {error}"));
        return false;
    }

    let mut parts = Vec::new();
    if dirs > 0 {
        parts.push(format!("{dirs} directories"));
    }
    if files > 0 {
        parts.push(format!("{files} hardlinks"));
    }
    if symlinks > 0 {
        parts.push(format!("{symlinks} symlinks"));
    }
    ui::notify(
        "Hardlink Clone Created",
        &format!("{}: {}", clone_root.file_name().unwrap_or_default().to_string_lossy(), parts.join(", ")),
        "folder",
    );
    true
}

pub fn symlink_clone(source_dir: &str, target_dir: &str, relative: bool) -> bool {
    let source_path = std::path::absolute(source_dir).unwrap_or_else(|_| PathBuf::from(source_dir));
    let target_path = std::path::absolute(target_dir).unwrap_or_else(|_| PathBuf::from(target_dir));
    if !source_path.exists() || !source_path.is_dir() {
        ui::error_dialog("Symlink Clone", &format!("Source is not a valid directory: {source_dir}"));
        return false;
    }
    let Some(parent) = target_path.parent() else {
        return false;
    };
    if !is_writable_dir(parent) {
        ui::error_dialog("Symlink Clone", &format!("No write permission in: {}", parent.display()));
        return false;
    }
    let clone_root = match claim_directory(
        parent,
        &target_path.file_name().unwrap_or_default().to_string_lossy(),
        "Symlink Clone",
    ) {
        Ok(p) => p,
        Err(error) => {
            ui::error_dialog("Symlink Clone", &format!("Clone failed: {error}"));
            return false;
        }
    };

    let mut dirs = 0usize;
    let mut files = 0usize;
    let result = (|| -> io::Result<()> {
        for entry in WalkDir::new(&source_path).follow_links(false) {
            let entry = entry?;
            let source_item = entry.path();
            if source_item == source_path || source_item == clone_root {
                continue;
            }
            let rel = source_item.strip_prefix(&source_path).unwrap_or(source_item);
            let target_item = clone_root.join(rel);
            if let Some(parent) = target_item.parent() {
                fs::create_dir_all(parent)?;
            }
            if is_symlink(source_item) || source_item.is_file() {
                let link_target = if relative {
                    pathdiff(source_item, target_item.parent().unwrap_or(Path::new(".")))
                } else {
                    source_item.display().to_string()
                };
                symlink(link_target, &target_item)?;
                files += 1;
            } else if source_item.is_dir() {
                fs::create_dir_all(&target_item)?;
                dirs += 1;
            }
        }
        Ok(())
    })();
    if let Err(error) = result {
        let _ = fs::remove_dir_all(&clone_root);
        ui::error_dialog("Symlink Clone", &format!("Clone failed: {error}"));
        return false;
    }
    let mut parts = Vec::new();
    if dirs > 0 {
        parts.push(format!("{dirs} directories"));
    }
    if files > 0 {
        parts.push(format!("{files} symlinks"));
    }
    ui::notify(
        "Symlink Clone Created",
        &format!("{}: {}", clone_root.file_name().unwrap_or_default().to_string_lossy(), parts.join(", ")),
        "folder",
    );
    true
}

pub fn smart_copy(source: &str, target_dir: &str) -> bool {
    let source_path = std::path::absolute(source).unwrap_or_else(|_| PathBuf::from(source));
    let target_path = std::path::absolute(target_dir).unwrap_or_else(|_| PathBuf::from(target_dir));
    if !source_path.exists() && !is_symlink(&source_path) {
        ui::error_dialog("Smart Copy", &format!("Source does not exist: {source}"));
        return false;
    }
    if !target_path.exists() || !target_path.is_dir() {
        ui::error_dialog("Smart Copy", &format!("Target is not a valid directory: {target_dir}"));
        return false;
    }
    if !is_writable_dir(&target_path) {
        ui::error_dialog("Smart Copy", &format!("No write permission in: {target_dir}"));
        return false;
    }

    let mut files = 0usize;
    let mut dirs = 0usize;
    let mut symlink_count = 0usize;
    let mut hardlinks = 0usize;
    let mut copied_inodes: HashMap<(u64, u64), PathBuf> = HashMap::new();

    fn copy_recursive(
        src: &Path,
        dst: &Path,
        files: &mut usize,
        dirs: &mut usize,
        symlink_count: &mut usize,
        hardlinks: &mut usize,
        copied_inodes: &mut HashMap<(u64, u64), PathBuf>,
    ) -> io::Result<()> {
        if is_symlink(src) {
            symlink(fs::read_link(src)?, dst)?;
            *symlink_count += 1;
        } else if src.is_file() {
            let meta = src.metadata()?;
            let key = (meta.dev(), meta.ino());
            if let Some(existing) = copied_inodes.get(&key) {
                fs::hard_link(existing, dst)?;
                *hardlinks += 1;
            } else {
                fs::copy(src, dst)?;
                copied_inodes.insert(key, dst.to_path_buf());
                *files += 1;
            }
        } else if src.is_dir() {
            fs::create_dir_all(dst)?;
            *dirs += 1;
            for item in fs::read_dir(src)? {
                let item = item?;
                copy_recursive(
                    &item.path(),
                    &dst.join(item.file_name()),
                    files,
                    dirs,
                    symlink_count,
                    hardlinks,
                    copied_inodes,
                )?;
            }
        }
        Ok(())
    }

    let split = splits_extension(&source_path);
    let mut target_dest = None;
    let result = (|| -> io::Result<()> {
        if is_symlink(&source_path) {
            let dest = create_with_rename(&target_path, &source_path.file_name().unwrap_or_default().to_string_lossy(), "Smart Copy", split, |destination| {
                symlink(fs::read_link(&source_path)?, destination)
            })?;
            symlink_count += 1;
            target_dest = Some(dest);
        } else if source_path.is_dir() {
            let dest = claim_directory(
                &target_path,
                &source_path.file_name().unwrap_or_default().to_string_lossy(),
                "Smart Copy",
            )?;
            dirs += 1;
            for item in fs::read_dir(&source_path)? {
                let item = item?;
                if item.path() == dest {
                    continue;
                }
                copy_recursive(
                    &item.path(),
                    &dest.join(item.file_name()),
                    &mut files,
                    &mut dirs,
                    &mut symlink_count,
                    &mut hardlinks,
                    &mut copied_inodes,
                )?;
            }
            target_dest = Some(dest);
        } else {
            let dest = claim_file(
                &target_path,
                &source_path.file_name().unwrap_or_default().to_string_lossy(),
                "Smart Copy",
                split,
            )?;
            fs::copy(&source_path, &dest)?;
            if let Ok(meta) = source_path.metadata() {
                copied_inodes.insert((meta.dev(), meta.ino()), dest.clone());
            }
            files += 1;
            target_dest = Some(dest);
        }
        Ok(())
    })();

    if let Err(error) = result {
        if let Some(dest) = &target_dest {
            if dest.is_dir() && !is_symlink(dest) {
                let _ = fs::remove_dir_all(dest);
            } else {
                let _ = fs::remove_file(dest);
            }
        }
        ui::error_dialog("Smart Copy", &format!("Copy failed: {error}"));
        return false;
    }

    let dest_name = target_dest
        .as_ref()
        .and_then(|p| p.file_name())
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default();
    let mut parts = Vec::new();
    if dirs > 0 {
        parts.push(format!("{dirs} directories"));
    }
    if files > 0 {
        parts.push(format!("{files} files"));
    }
    if hardlinks > 0 {
        parts.push(format!("{hardlinks} hardlinks"));
    }
    if symlink_count > 0 {
        parts.push(format!("{symlink_count} symlinks"));
    }
    ui::notify("Smart Copy Created", &format!("{dest_name}: {}", parts.join(", ")), "folder");
    true
}

pub fn enumerate_hardlinks(path: &str) -> Vec<String> {
    let file_path = Path::new(path);
    if !file_path.exists() || !file_path.is_file() {
        return Vec::new();
    }
    let Ok(file_stat) = file_path.metadata() else {
        return Vec::new();
    };
    let key = (file_stat.ino(), file_stat.dev());
    let mut siblings = Vec::new();
    let search_root = file_path.parent().unwrap_or(Path::new("."));
    for entry in WalkDir::new(search_root).follow_links(false).into_iter().flatten() {
        if !entry.file_type().is_file() {
            continue;
        }
        if let Ok(stat) = entry.path().metadata() {
            if (stat.ino(), stat.dev()) == key {
                siblings.push(entry.path().display().to_string());
            }
        }
        if siblings.len() > 100 {
            break;
        }
    }
    siblings.sort();
    siblings.dedup();
    siblings
}

pub fn show_hardlink_properties(path: &str) {
    let file_path = Path::new(path);
    if !file_path.exists() && !is_symlink(file_path) {
        ui::error_dialog("Link Properties", &format!("File does not exist: {path}"));
        return;
    }
    let Ok(file_stat) = file_path.symlink_metadata() else {
        ui::error_dialog("Link Properties", "Error: cannot stat file");
        return;
    };
    let siblings: Vec<String> = enumerate_hardlinks(path)
        .into_iter()
        .filter(|s| s != &file_path.display().to_string())
        .collect();
    let ref_count = siblings.len() + 1;
    let msg = if is_symlink(file_path) {
        let target = fs::read_link(file_path).map(|p| p.display().to_string()).unwrap_or_default();
        let abs = file_path.canonicalize().map(|p| p.display().to_string()).unwrap_or_default();
        format!("<b>Symbolic Link</b><br><br>Target: {target}<br><br>Absolute target: {abs}")
    } else if file_path.is_file() {
        let link_type = if ref_count > 1 { "Hard Link" } else { "Regular File" };
        let mut msg = format!(
            "<b>{link_type}</b><br><br>Reference count: {ref_count}<br><br>Inode: {}<br>Device: {}<br><br>",
            file_stat.ino(),
            file_stat.dev()
        );
        if !siblings.is_empty() {
            msg.push_str("<b>Siblings:</b><br>");
            for s in siblings.iter().take(20) {
                msg.push_str(&format!("• {s}<br>"));
            }
            if siblings.len() > 20 {
                msg.push_str(&format!("• ... and {} more<br>", siblings.len() - 20));
            }
        }
        msg
    } else {
        format!("<b>Directory</b><br><br>Path: {}", file_path.display())
    };
    ui::info_dialog("Link Properties", &msg, 600, 400);
}

pub fn drop_as(target_dir: &str, drop_type: &str, relative: bool) -> bool {
    let sources = with_source_manager(|mgr| mgr.get());
    if sources.is_empty() {
        ui::error_dialog("Drop As", "No link source picked. Pick a source first.");
        return false;
    }
    let success = match drop_type {
        "hardlink" => return drop_hardlink(target_dir),
        "symlink" => return drop_symlink(target_dir, relative),
        "hardlink-clone" => {
            if sources.len() != 1 {
                ui::error_dialog("Hardlink Clone", "Please pick exactly one directory for cloning.");
                return false;
            }
            let source = Path::new(&sources[0]);
            hardlink_clone(
                &sources[0],
                &Path::new(target_dir).join(source.file_name().unwrap_or_default()).display().to_string(),
            )
        }
        "symlink-clone" => {
            if sources.len() != 1 {
                ui::error_dialog("Symlink Clone", "Please pick exactly one directory for cloning.");
                return false;
            }
            let source = Path::new(&sources[0]);
            symlink_clone(
                &sources[0],
                &Path::new(target_dir).join(source.file_name().unwrap_or_default()).display().to_string(),
                relative,
            )
        }
        "smart-copy" => {
            if sources.len() != 1 {
                ui::error_dialog("Smart Copy", "Please pick exactly one file or directory for smart copy.");
                return false;
            }
            smart_copy(&sources[0], target_dir)
        }
        other => {
            ui::error_dialog("Drop As", &format!("Unknown drop type: {other}"));
            return false;
        }
    };
    if success {
        with_source_manager(|mgr| mgr.clear());
    }
    success
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::fs::symlink as unix_symlink;
    use tempfile::TempDir;

    struct Isolated {
        dir: TempDir,
        _env: crate::test_env::EnvGuard,
    }

    impl Isolated {
        fn path(&self) -> &Path {
            self.dir.path()
        }
    }

    impl Drop for Isolated {
        fn drop(&mut self) {
            std::env::remove_var("DOLPHIN_LINK_SOURCES_FILE");
        }
    }

    fn isolated() -> Isolated {
        let env = crate::test_env::EnvGuard::lock();
        let dir = TempDir::new().unwrap();
        std::env::set_var("DOLPHIN_LINK_SOURCES_FILE", dir.path().join("picked_sources.json"));
        std::env::set_var("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1");
        Isolated { dir, _env: env }
    }

    #[test]
    fn pick_then_drop_hardlink_and_symlink() {
        let dir = isolated();
        let source = dir.path().join("source.txt");
        let hardlink_dir = dir.path().join("hardlinks");
        let symlink_dir = dir.path().join("symlinks");
        fs::write(&source, "linked content").unwrap();
        fs::create_dir(&hardlink_dir).unwrap();
        fs::create_dir(&symlink_dir).unwrap();

        assert!(pick_link_source(&[source.display().to_string()]));
        assert!(drop_hardlink(&hardlink_dir.display().to_string()));
        assert_eq!(
            source.metadata().unwrap().ino(),
            hardlink_dir.join("source.txt").metadata().unwrap().ino()
        );
        assert!(!with_source_manager(|m| m.has_sources()));

        assert!(pick_link_source(&[source.display().to_string()]));
        assert!(drop_symlink(&symlink_dir.display().to_string(), true));
        let link = symlink_dir.join("source.txt");
        assert!(is_symlink(&link));
        assert!(!fs::read_link(&link).unwrap().to_string_lossy().starts_with('/'));
    }

    #[test]
    fn drop_renames_when_name_is_taken() {
        let dir = isolated();
        let source_dir = dir.path().join("source");
        let target = dir.path().join("target");
        fs::create_dir(&source_dir).unwrap();
        fs::create_dir(&target).unwrap();
        let source = source_dir.join("report.tar.gz");
        fs::write(&source, "linked content").unwrap();
        fs::write(target.join("report.tar.gz"), "something else").unwrap();

        for expected in ["report.tar - Hardlink.gz", "report.tar - Hardlink (2).gz"] {
            pick_link_source(&[source.display().to_string()]);
            assert!(drop_hardlink(&target.display().to_string()));
            assert_eq!(
                source.metadata().unwrap().ino(),
                target.join(expected).metadata().unwrap().ino()
            );
        }
        assert_eq!(fs::read_to_string(target.join("report.tar.gz")).unwrap(), "something else");
    }

    #[test]
    fn drop_rename_keeps_whole_directory_name() {
        let dir = isolated();
        let source = dir.path().join("source").join("v1.2");
        let target = dir.path().join("target");
        fs::create_dir_all(&source).unwrap();
        fs::create_dir(&target).unwrap();
        fs::create_dir(target.join("v1.2")).unwrap();
        pick_link_source(&[source.display().to_string()]);
        assert!(drop_symlink(&target.display().to_string(), true));
        assert!(is_symlink(&target.join("v1.2 - Symlink")));
    }

    #[test]
    fn drop_rename_steps_past_dangling_symlink() {
        let dir = isolated();
        let source_dir = dir.path().join("source");
        let target = dir.path().join("target");
        fs::create_dir(&source_dir).unwrap();
        fs::create_dir(&target).unwrap();
        let source = source_dir.join("note.txt");
        fs::write(&source, "linked content").unwrap();
        unix_symlink("nowhere", target.join("note.txt")).unwrap();
        pick_link_source(&[source.display().to_string()]);
        assert!(drop_hardlink(&target.display().to_string()));
        assert_eq!(
            source.metadata().unwrap().ino(),
            target.join("note - Hardlink.txt").metadata().unwrap().ino()
        );
    }

    #[test]
    fn clone_and_smart_copy_preserve_links() {
        let dir = isolated();
        let source = dir.path().join("source");
        let nested = source.join("nested");
        fs::create_dir_all(&nested).unwrap();
        let original = nested.join("original.txt");
        let sibling = nested.join("sibling.txt");
        fs::write(&original, "source stays intact").unwrap();
        fs::hard_link(&original, &sibling).unwrap();
        unix_symlink("nested/original.txt", source.join("original-link")).unwrap();
        let original_inode = original.metadata().unwrap().ino();

        let hardlink_clone_dir = dir.path().join("hardlink-clone");
        assert!(hardlink_clone(&source.display().to_string(), &hardlink_clone_dir.display().to_string()));
        assert_eq!(
            original_inode,
            hardlink_clone_dir.join("nested/original.txt").metadata().unwrap().ino()
        );

        let symlink_clone_dir = dir.path().join("symlink-clone");
        assert!(symlink_clone(&source.display().to_string(), &symlink_clone_dir.display().to_string(), true));
        assert!(is_symlink(&symlink_clone_dir.join("nested/original.txt")));

        let copies = dir.path().join("copies");
        fs::create_dir(&copies).unwrap();
        assert!(smart_copy(&source.display().to_string(), &copies.display().to_string()));
        let copied = copies.join("source");
        assert_eq!(
            copied.join("nested/original.txt").metadata().unwrap().ino(),
            copied.join("nested/sibling.txt").metadata().unwrap().ino()
        );
        assert_ne!(original_inode, copied.join("nested/original.txt").metadata().unwrap().ino());
        assert_eq!(
            fs::read_link(copied.join("original-link")).unwrap().to_string_lossy(),
            "nested/original.txt"
        );
        assert_eq!(fs::read_to_string(&original).unwrap(), "source stays intact");
    }

    #[test]
    fn clone_and_smart_copy_rename_instead_of_failing() {
        let dir = isolated();
        let source = dir.path().join("source").join("project");
        let target = dir.path().join("target");
        fs::create_dir_all(&source).unwrap();
        fs::create_dir(&target).unwrap();
        fs::write(source.join("file.txt"), "clone me").unwrap();
        fs::create_dir(target.join("project")).unwrap();

        pick_link_source(&[source.display().to_string()]);
        assert!(drop_as(&target.display().to_string(), "hardlink-clone", true));
        assert!(target.join("project - Hardlink Clone/file.txt").exists());
        pick_link_source(&[source.display().to_string()]);
        assert!(drop_as(&target.display().to_string(), "hardlink-clone", true));
        assert!(target.join("project - Hardlink Clone (2)/file.txt").exists());
    }

    #[test]
    fn cloning_a_folder_into_itself_terminates() {
        let dir = isolated();
        let source = dir.path().join("project");
        fs::create_dir(&source).unwrap();
        fs::write(source.join("file.txt"), "clone me").unwrap();
        pick_link_source(&[source.display().to_string()]);
        assert!(drop_as(&source.display().to_string(), "hardlink-clone", true));
        let clone = source.join("project");
        assert!(clone.join("file.txt").exists());
        assert!(!clone.join("project").exists());
    }
}
