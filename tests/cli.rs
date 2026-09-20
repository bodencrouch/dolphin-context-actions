use std::path::PathBuf;
use std::process::Command;

fn bin() -> Command {
    let mut cmd = Command::new(env!("CARGO_BIN_EXE_dolphin-context-actions"));
    cmd.env("DOLPHIN_CONTEXT_ACTIONS_HEADLESS", "1")
        .env("QT_QPA_PLATFORM", "offscreen");
    cmd
}

#[test]
fn lists_general_file_conversions_from_bundled_registry() {
    let out = bin().arg("--list-file-conversions").output().unwrap();
    assert!(out.status.success(), "{}", String::from_utf8_lossy(&out.stderr));
    let stdout = String::from_utf8_lossy(&out.stdout);
    assert!(stdout.contains("pdf-to-md\tTo Markdown"));
    assert!(stdout.contains("csv-to-json\tTo JSON"));
    assert!(stdout.contains("png-to-jpg\tTo JPEG"));
}

#[test]
fn general_conversion_round_trip_uses_unified_cli() {
    let dir = tempfile::tempdir().unwrap();
    let source = dir.path().join("people.csv");
    std::fs::write(&source, "name,age\nAda,36\nBob,40\n").unwrap();
    let to_json = bin().args(["--file-convert", "csv-to-json"]).arg(&source).output().unwrap();
    assert!(to_json.status.success(), "{}", String::from_utf8_lossy(&to_json.stderr));
    let json_path = dir.path().join("people.json");
    assert!(json_path.exists());
    std::fs::remove_file(&source).unwrap();
    let to_csv = bin().args(["--file-convert", "json-to-csv"]).arg(&json_path).output().unwrap();
    assert!(to_csv.status.success(), "{}", String::from_utf8_lossy(&to_csv.stderr));
    assert!(std::fs::read_to_string(dir.path().join("people.csv")).unwrap().contains("Ada,36"));
}

#[test]
fn general_conversion_failure_sets_nonzero_exit_status() {
    let dir = tempfile::tempdir().unwrap();
    let source = dir.path().join("people.csv");
    std::fs::write(&source, "name\nAda\n").unwrap();
    let result = bin()
        .args(["--file-convert", "missing-conversion"])
        .arg(&source)
        .output()
        .unwrap();
    assert!(!result.status.success());
}
