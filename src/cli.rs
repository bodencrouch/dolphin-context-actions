use std::path::{Path, PathBuf};
use std::process::Command;

use clap::{ArgAction, Parser};

use crate::archive_ops;
use crate::config;
use crate::converters::{audio, video};
use crate::file_converter;
use crate::file_converter_menus;
use crate::link_ops;
use crate::ui;
use crate::uploaders;

#[derive(Parser, Debug)]
#[command(name = "dolphin-context-actions", about = "Dolphin Context Actions", disable_help_subcommand = true)]
#[command(allow_hyphen_values = false)]
pub struct Args {
    #[arg(long, action = ArgAction::SetTrue)]
    smart_menu: bool,
    #[arg(long, value_parser = ["audio", "video-to-gif", "video-to-mp4", "video-to-webm", "extract-audio", "imgur-upload"])]
    batch: Option<String>,
    #[arg(long)]
    format: Option<String>,
    #[arg(long, action = ArgAction::SetTrue)]
    configure: bool,
    #[arg(long, value_name = "ID")]
    file_convert: Option<String>,
    #[arg(long, action = ArgAction::SetTrue)]
    pick_file_conversion: bool,
    #[arg(long, action = ArgAction::SetTrue)]
    list_file_conversions: bool,
    #[arg(long, action = ArgAction::SetTrue)]
    pick_link_source: bool,
    #[arg(long, action = ArgAction::SetTrue)]
    cancel_link: bool,
    #[arg(long)]
    drop_as: Option<String>,
    #[arg(long, action = ArgAction::SetTrue)]
    drop_hardlink: bool,
    #[arg(long, action = ArgAction::SetTrue)]
    drop_symlink: bool,
    #[arg(long, action = ArgAction::SetTrue)]
    hardlink_clone: bool,
    #[arg(long, action = ArgAction::SetTrue)]
    symlink_clone: bool,
    #[arg(long, action = ArgAction::SetTrue)]
    smart_copy: bool,
    #[arg(long, action = ArgAction::SetTrue)]
    link_properties: bool,
    #[arg(long, action = ArgAction::SetTrue)]
    enumerate_hardlinks: bool,
    #[arg(long)]
    target_dir: Option<String>,
    #[arg(long, value_parser = clap::builder::PossibleValuesParser::new(archive_ops::ARK_ACTIONS))]
    archive: Option<String>,
    #[arg(long, action = ArgAction::SetTrue)]
    generate_menus: bool,
    #[arg(long)]
    output_dir: Option<PathBuf>,
    #[arg(long)]
    converter_bin: Option<PathBuf>,
    #[arg(long)]
    registry: Option<PathBuf>,
    files: Vec<String>,
}

fn ffmpeg_check() -> Result<(), i32> {
    for tool in ["ffmpeg", "ffprobe"] {
        if which::which(tool).is_err() {
            ui::error_dialog(
                "Context Actions - Missing Dependency",
                &format!(
                    "<b>{tool}</b> not found.\n\n\
                     Install ffmpeg:\n\
                     • <tt>sudo apt install ffmpeg</tt> (Debian/Ubuntu/Mint)\n\
                     • <tt>sudo dnf install ffmpeg</tt> (Fedora)\n\
                     • <tt>sudo pacman -S ffmpeg</tt> (Arch/Manjaro)\n\
                     • <tt>sudo zypper install ffmpeg</tt> (openSUSE)"
                ),
            );
            return Err(1);
        }
    }
    Ok(())
}

fn mime_type(filepath: &str) -> Option<String> {
    let output = Command::new("file")
        .args(["--mime-type", "--brief", filepath])
        .output()
        .ok()?;
    Some(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

fn smart_menu(files: &[String]) {
    if files.is_empty() {
        ui::error_dialog("Context Actions", "No files selected.");
        return;
    }
    if ffmpeg_check().is_err() {
        return;
    }
    let mime = mime_type(&files[0]).unwrap_or_default();
    let path = Path::new(&files[0]);
    let ext = path
        .extension()
        .map(|e| format!(".{}", e.to_string_lossy().to_ascii_lowercase()))
        .unwrap_or_default();

    let is_gif = mime == "image/gif" || ext == ".gif";
    let is_video = mime.starts_with("video/")
        || matches!(ext.as_str(), ".mp4" | ".webm" | ".mkv" | ".avi" | ".mov" | ".wmv" | ".flv");
    let is_audio = mime.starts_with("audio/")
        || matches!(ext.as_str(), ".mp3" | ".ogg" | ".flac" | ".wav" | ".m4a" | ".opus" | ".wma" | ".aac");
    let is_image = mime.starts_with("image/") && !is_gif;

    let mut choices: Vec<(String, String)> = Vec::new();
    if is_image {
        choices.push(("upload_imgur".into(), "Upload to Imgur".into()));
    } else if is_gif {
        choices.extend([
            ("to_mp4".into(), "Convert to MP4".into()),
            ("to_webm".into(), "Convert to WebM".into()),
            ("to_mkv".into(), "Convert to MKV".into()),
            ("extract_audio".into(), "Extract Audio".into()),
            ("upload_imgur".into(), "Upload to Imgur".into()),
        ]);
        for fmt in audio::AUDIO_FORMATS {
            let extra = audio::format_label_extra(fmt);
            let fmt_name = if *fmt == "m4a" { "M4A".to_string() } else { fmt.to_uppercase() };
            choices.push((format!("audio_{fmt}"), format!("Convert Audio to {fmt_name}{extra}")));
        }
    } else if is_video {
        choices.extend([
            ("to_gif".into(), "Convert to GIF…".into()),
            ("to_mp4".into(), "Convert to MP4".into()),
            ("to_webm".into(), "Convert to WebM".into()),
            ("to_mkv".into(), "Convert to MKV".into()),
            ("extract_audio".into(), "Extract Audio".into()),
            ("upload_imgur".into(), "Upload to Imgur".into()),
        ]);
    } else if is_audio {
        let source_fmt = ext.trim_start_matches('.');
        for fmt in audio::AUDIO_FORMATS {
            if *fmt == source_fmt {
                continue;
            }
            if let Some(preset) = audio::preset(fmt) {
                choices.push((format!("audio_{fmt}"), format!("Convert to {}", preset.name)));
            }
        }
        if choices.is_empty() {
            ui::info_dialog(
                "Audio Converter",
                &format!("Already in the only supported format ({}).", source_fmt.to_uppercase()),
                0,
                0,
            );
            return;
        }
    } else {
        ui::error_dialog("Context Actions", &format!("Unsupported file type: {}", if mime.is_empty() { &ext } else { &mime }));
        return;
    }

    let items: Vec<(&str, &str)> = choices.iter().map(|(k, v)| (k.as_str(), v.as_str())).collect();
    let Some(choice) = ui::menu_dialog(
        "Context Actions",
        &format!("Actions for: {}", path.file_name().unwrap_or_default().to_string_lossy()),
        &items,
    ) else {
        return;
    };

    match choice.as_str() {
        "to_gif" => {
            if let Some(preset) = ui::menu_dialog("Convert to GIF", "Select quality preset:", video::GIF_PRESET_NAMES) {
                video::to_gif(files, &preset);
            }
        }
        "to_mp4" => video::to_mp4(files, ".mp4"),
        "to_webm" => video::to_mp4(files, ".webm"),
        "to_mkv" => video::to_mp4(files, ".mkv"),
        "extract_audio" => video::extract_audio(files),
        "upload_imgur" => uploaders::imgur_upload(files),
        other if other.starts_with("audio_") => {
            audio::convert_files(files, &other[6..]);
        }
        "configure" => run_configure(),
        _ => {}
    }
}

fn batch_convert(files: &[String], mode: &str, param: Option<&str>) {
    if ffmpeg_check().is_err() {
        return;
    }
    let cfg = config::load();
    match mode {
        "audio" => {
            let fmt = param.unwrap_or(&cfg.audio_preset);
            audio::convert_files(files, fmt);
        }
        "video-to-gif" => {
            let preset = param.unwrap_or(&cfg.video_preset);
            video::to_gif(files, preset);
        }
        "video-to-mp4" => video::to_mp4(files, ".mp4"),
        "video-to-webm" => video::to_mp4(files, ".webm"),
        "extract-audio" => video::extract_audio(files),
        "imgur-upload" => uploaders::imgur_upload(files),
        _ => {}
    }
}

fn run_configure() {
    let mut cfg = config::load();
    let audio_choices: Vec<(String, String)> = audio::AUDIO_FORMATS
        .iter()
        .filter_map(|k| audio::preset(k).map(|p| ((*k).to_string(), p.name.to_string())))
        .collect();
    let current_audio = audio::preset(&cfg.audio_preset)
        .map(|p| p.name)
        .unwrap_or(&cfg.audio_preset);
    let current_video = video::gif_preset_name(&cfg.video_preset).unwrap_or(&cfg.video_preset);

    let Some(sel) = ui::menu_dialog(
        "Context Actions - Configure",
        &format!(
            "Default audio format:  {current_audio}\nDefault GIF preset:  {current_video}\n\nChoose a setting to change:"
        ),
        &[
            ("audio", "Default audio format"),
            ("video", "Default GIF preset"),
            ("imgur", "Imgur Client ID"),
        ],
    ) else {
        return;
    };

    match sel.as_str() {
        "audio" => {
            let items: Vec<(&str, &str)> = audio_choices.iter().map(|(k, v)| (k.as_str(), v.as_str())).collect();
            if let Some(chosen) = ui::menu_dialog("Audio Format", "Default audio format:", &items) {
                cfg.audio_preset = chosen.clone();
                config::save(&cfg);
                if let Some(preset) = audio::preset(&chosen) {
                    ui::notify("Configured", &format!("Default audio → {}", preset.name), "configure");
                }
            }
        }
        "video" => {
            if let Some(chosen) = ui::menu_dialog("GIF Preset", "Default GIF preset:", video::GIF_PRESET_NAMES) {
                cfg.video_preset = chosen.clone();
                config::save(&cfg);
                if let Some(name) = video::gif_preset_name(&chosen) {
                    ui::notify("Configured", &format!("Default GIF → {name}"), "configure");
                }
            }
        }
        "imgur" => {
            if let Some(val) = ui::input_dialog("Imgur Client ID", "Client ID:", &cfg.imgur_client_id) {
                cfg.imgur_client_id = val;
                config::save(&cfg);
                ui::notify("Configured", "Imgur Client ID saved", "configure");
            }
        }
        _ => {}
    }
}

fn handle_hardlink_clone(files: &[String]) {
    if files.len() != 1 {
        ui::error_dialog("Hardlink Clone", "Please select exactly one directory.");
        return;
    }
    let default = Path::new(&files[0]).parent().map(|p| p.display().to_string()).unwrap_or_default();
    if let Some(target) = ui::input_dialog("Hardlink Clone", "Destination folder:", &default) {
        let dest = Path::new(&target).join(Path::new(&files[0]).file_name().unwrap_or_default());
        link_ops::hardlink_clone(&files[0], &dest.display().to_string());
    }
}

fn handle_symlink_clone(files: &[String]) {
    if files.len() != 1 {
        ui::error_dialog("Symlink Clone", "Please select exactly one directory.");
        return;
    }
    let default = Path::new(&files[0]).parent().map(|p| p.display().to_string()).unwrap_or_default();
    if let Some(target) = ui::input_dialog("Symlink Clone", "Destination folder:", &default) {
        let dest = Path::new(&target).join(Path::new(&files[0]).file_name().unwrap_or_default());
        link_ops::symlink_clone(&files[0], &dest.display().to_string(), true);
    }
}

fn handle_smart_copy(files: &[String]) {
    if files.len() != 1 {
        ui::error_dialog("Smart Copy", "Please select exactly one file or directory.");
        return;
    }
    let default = Path::new(&files[0]).parent().map(|p| p.display().to_string()).unwrap_or_default();
    if let Some(target) = ui::input_dialog("Smart Copy", "Destination folder:", &default) {
        link_ops::smart_copy(&files[0], &target);
    }
}

fn handle_link_properties(files: &[String]) {
    if files.is_empty() {
        ui::error_dialog("Link Properties", "No file selected.");
        return;
    }
    link_ops::show_hardlink_properties(&files[0]);
}

fn handle_enumerate_hardlinks(files: &[String]) {
    if files.is_empty() {
        ui::error_dialog("Enumerate Hardlinks", "No file selected.");
        return;
    }
    let resolved = Path::new(&files[0])
        .canonicalize()
        .map(|p| p.display().to_string())
        .unwrap_or_else(|_| files[0].clone());
    let siblings: Vec<String> = link_ops::enumerate_hardlinks(&files[0])
        .into_iter()
        .filter(|s| s != &resolved)
        .collect();
    if siblings.is_empty() {
        ui::info_dialog("Hardlink Siblings", "No other hardlinks found for this file.", 0, 0);
    } else {
        ui::info_dialog(
            "Hardlink Siblings",
            &format!("Hardlink siblings of {}:\n\n{}", files[0], siblings.join("\n")),
            600,
            400,
        );
    }
}

pub fn run_with_args(args: Args) -> i32 {
    if args.generate_menus {
        let output = args.output_dir.unwrap_or_else(|| {
            dirs::data_local_dir()
                .unwrap_or_else(|| PathBuf::from(".").join(".local/share"))
                .join("kio/servicemenus")
        });
        let converter = args.converter_bin.unwrap_or_else(|| {
            dirs::home_dir()
                .unwrap_or_else(|| PathBuf::from("."))
                .join(".local/bin/dolphin-context-actions")
        });
        return file_converter_menus::run(&output, &converter, args.registry.as_deref());
    }

    if args.list_file_conversions {
        for conversion in file_converter::load_conversions(false) {
            println!("{}\t{}", conversion.id, conversion.label);
        }
        return 0;
    }
    if let Some(id) = args.file_convert {
        let paths: Vec<PathBuf> = args.files.iter().map(PathBuf::from).collect();
        return file_converter::run_convert(&id, &paths, false);
    }
    if args.pick_file_conversion {
        let paths: Vec<PathBuf> = args.files.iter().map(PathBuf::from).collect();
        return file_converter::run_pick(&paths, false);
    }
    if args.configure {
        run_configure();
        return 0;
    }
    if let Some(mode) = args.batch {
        batch_convert(&args.files, &mode, args.format.as_deref());
        return 0;
    }
    if args.pick_link_source {
        link_ops::pick_link_source(&args.files);
        return 0;
    }
    if args.cancel_link {
        link_ops::cancel_link_creation();
        return 0;
    }
    if let (Some(drop_as), Some(target)) = (args.drop_as.as_ref(), args.target_dir.as_ref()) {
        link_ops::drop_as(target, drop_as, true);
        return 0;
    }
    if args.drop_hardlink {
        if let Some(target) = args.target_dir {
            link_ops::drop_hardlink(&target);
        }
        return 0;
    }
    if args.drop_symlink {
        if let Some(target) = args.target_dir {
            link_ops::drop_symlink(&target, true);
        }
        return 0;
    }
    if args.hardlink_clone {
        handle_hardlink_clone(&args.files);
        return 0;
    }
    if args.symlink_clone {
        handle_symlink_clone(&args.files);
        return 0;
    }
    if args.smart_copy {
        handle_smart_copy(&args.files);
        return 0;
    }
    if args.link_properties {
        handle_link_properties(&args.files);
        return 0;
    }
    if args.enumerate_hardlinks {
        handle_enumerate_hardlinks(&args.files);
        return 0;
    }
    if let Some(action) = args.archive {
        return archive_ops::run_action(&action, &args.files);
    }
    smart_menu(&args.files);
    0
}

pub fn main() -> i32 {
    let args = Args::parse();
    run_with_args(args)
}
