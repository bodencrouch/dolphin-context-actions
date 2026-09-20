use std::fs;

#[test]
fn helper_name_matches_cargo_bin() {
    let cargo = fs::read_to_string("Cargo.toml").unwrap();
    assert!(cargo.contains("name = \"dolphin-context-actions\""));
    for plugin in [
        "kio-plugin/dolphinlinkfileitemaction.cpp",
        "kio-plugin/dolphinarkfileitemaction.cpp",
    ] {
        let src = fs::read_to_string(plugin).unwrap();
        assert!(
            src.contains("return QStringLiteral(\"dolphin-context-actions\");"),
            "{plugin} helperName drifted"
        );
    }
}
