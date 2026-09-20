use std::fs;
use std::path::PathBuf;

use serde::{Deserialize, Serialize};
use serde_json::Value;

pub const APP_NAME: &str = "dolphin-context-actions";

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Config {
    #[serde(default = "default_audio")]
    pub audio_preset: String,
    #[serde(default = "default_video")]
    pub video_preset: String,
    #[serde(default)]
    pub imgur_client_id: String,
}

fn default_audio() -> String {
    "mp3".into()
}

fn default_video() -> String {
    "medium".into()
}

impl Default for Config {
    fn default() -> Self {
        Self {
            audio_preset: default_audio(),
            video_preset: default_video(),
            imgur_client_id: String::new(),
        }
    }
}

pub fn config_dir() -> PathBuf {
    dirs::config_dir()
        .unwrap_or_else(|| PathBuf::from(".").join(".config"))
        .join(APP_NAME)
}

pub fn config_file() -> PathBuf {
    config_dir().join("config.json")
}

pub fn load() -> Config {
    let path = config_file();
    if let Ok(text) = fs::read_to_string(path) {
        if let Ok(mut value) = serde_json::from_str::<Value>(&text) {
            if let Some(obj) = value.as_object_mut() {
                obj.entry("audio_preset").or_insert_with(|| Value::String(default_audio()));
                obj.entry("video_preset").or_insert_with(|| Value::String(default_video()));
                obj.entry("imgur_client_id").or_insert_with(|| Value::String(String::new()));
            }
            if let Ok(cfg) = serde_json::from_value(value) {
                return cfg;
            }
        }
    }
    Config::default()
}

pub fn save(cfg: &Config) {
    let dir = config_dir();
    let _ = fs::create_dir_all(&dir);
    if let Ok(text) = serde_json::to_string_pretty(cfg) {
        let _ = fs::write(config_file(), text);
    }
}
