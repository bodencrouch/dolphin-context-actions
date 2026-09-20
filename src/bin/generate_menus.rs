use std::path::PathBuf;

use clap::Parser;
use dolphin_context_actions::file_converter_menus;

#[derive(Parser)]
struct Args {
    #[arg(long)]
    output_dir: Option<PathBuf>,
    #[arg(long)]
    converter_bin: Option<PathBuf>,
    #[arg(long)]
    registry: Option<PathBuf>,
}

fn main() {
    let args = Args::parse();
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
    std::process::exit(file_converter_menus::run(
        &output,
        &converter,
        args.registry.as_deref(),
    ));
}
