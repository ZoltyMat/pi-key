use anyhow::{Context, Result};
use serde::Deserialize;
use std::path::Path;

#[derive(Debug, Deserialize, Clone)]
pub struct DeviceConfig {
    pub name: String,
    #[serde(default = "default_cod")]
    pub cod: String,
    #[serde(default)]
    pub target_mac: String,
}

fn default_cod() -> String {
    "0x002540".to_string()
}

#[derive(Debug, Deserialize, Clone)]
pub struct JigglerConfig {
    #[serde(default = "default_true")]
    pub enabled: bool,
    #[serde(default = "default_interval_min_jiggle")]
    pub interval_min: f64,
    #[serde(default = "default_interval_max_jiggle")]
    pub interval_max: f64,
    #[serde(default = "default_max_delta")]
    pub max_delta: i32,
    #[serde(default = "default_big_move_chance")]
    pub big_move_chance: f64,
}

impl Default for JigglerConfig {
    fn default() -> Self {
        Self {
            enabled: true,
            interval_min: 45.0,
            interval_max: 90.0,
            max_delta: 3,
            big_move_chance: 0.1,
        }
    }
}

#[derive(Debug, Deserialize, Clone)]
pub struct TyperConfig {
    #[serde(default = "default_true")]
    pub enabled: bool,
    #[serde(default = "default_interval_min_typer")]
    pub interval_min: f64,
    #[serde(default = "default_interval_max_typer")]
    pub interval_max: f64,
    #[serde(default = "default_cpm_min")]
    pub cpm_min: f64,
    #[serde(default = "default_cpm_max")]
    pub cpm_max: f64,
    #[serde(default = "default_typo_rate")]
    pub typo_rate: f64,
    #[serde(default = "default_think_pause_chance")]
    pub think_pause_chance: f64,
    #[serde(default = "default_think_pause_secs")]
    pub think_pause_secs: [f64; 2],
}

impl Default for TyperConfig {
    fn default() -> Self {
        Self {
            enabled: true,
            interval_min: 180.0,
            interval_max: 600.0,
            cpm_min: 220.0,
            cpm_max: 360.0,
            typo_rate: 0.02,
            think_pause_chance: 0.05,
            think_pause_secs: [1.5, 4.0],
        }
    }
}

#[derive(Debug, Deserialize, Clone)]
pub struct LlmConfig {
    pub url: String,
    #[serde(default = "default_api_style")]
    pub api_style: String,
    #[serde(default)]
    pub model: String,
    #[serde(default)]
    pub api_key: String,
    #[serde(default = "default_max_tokens")]
    pub max_tokens: u32,
    #[serde(default = "default_prompts")]
    pub prompts: Vec<String>,
}

#[derive(Debug, Deserialize, Clone)]
pub struct TlsConfig {
    #[serde(default)]
    pub enabled: bool,
    #[serde(default)]
    pub cert_path: String,
    #[serde(default)]
    pub key_path: String,
}

impl Default for TlsConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            cert_path: String::new(),
            key_path: String::new(),
        }
    }
}

#[derive(Debug, Deserialize, Clone)]
pub struct ApiConfig {
    #[serde(default)]
    pub enabled: bool,
    #[serde(default = "default_api_host")]
    pub host: String,
    #[serde(default = "default_api_port")]
    pub port: u16,
    #[serde(default)]
    pub api_key: String,
    #[serde(default)]
    pub allowed_origins: Vec<String>,
    #[serde(default = "default_rate_limit")]
    pub rate_limit: u32,
    #[serde(default)]
    pub tls: TlsConfig,
}

impl Default for ApiConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            host: "0.0.0.0".to_string(),
            port: 8099,
            api_key: String::new(),
            allowed_origins: Vec::new(),
            rate_limit: 10,
            tls: TlsConfig::default(),
        }
    }
}

#[derive(Debug, Deserialize, Clone)]
pub struct PikeyConfig {
    pub device: DeviceConfig,
    #[serde(default)]
    pub jiggler: JigglerConfig,
    #[serde(default)]
    pub typer: TyperConfig,
    pub llm: LlmConfig,
    #[serde(default)]
    pub api: ApiConfig,
}

pub fn load_config(path: &Path) -> Result<PikeyConfig> {
    let contents = std::fs::read_to_string(path)
        .with_context(|| format!("Failed to read config file: {}", path.display()))?;
    let config: PikeyConfig =
        serde_yaml::from_str(&contents).context("Failed to parse config YAML")?;
    Ok(config)
}

// Default value functions for serde
fn default_true() -> bool {
    true
}
fn default_interval_min_jiggle() -> f64 {
    45.0
}
fn default_interval_max_jiggle() -> f64 {
    90.0
}
fn default_max_delta() -> i32 {
    3
}
fn default_big_move_chance() -> f64 {
    0.1
}
fn default_interval_min_typer() -> f64 {
    180.0
}
fn default_interval_max_typer() -> f64 {
    600.0
}
fn default_cpm_min() -> f64 {
    220.0
}
fn default_cpm_max() -> f64 {
    360.0
}
fn default_typo_rate() -> f64 {
    0.02
}
fn default_think_pause_chance() -> f64 {
    0.05
}
fn default_think_pause_secs() -> [f64; 2] {
    [1.5, 4.0]
}
fn default_api_host() -> String {
    "0.0.0.0".to_string()
}
fn default_api_port() -> u16 {
    8099
}
fn default_rate_limit() -> u32 {
    10
}
fn default_api_style() -> String {
    "openai".to_string()
}
fn default_max_tokens() -> u32 {
    200
}
fn default_prompts() -> Vec<String> {
    vec![
        "Write a realistic Python function with a docstring and comments.".to_string(),
        "Write a short Slack message to a teammate about a code review.".to_string(),
        "Write 3 bullet points of notes from a fictitious engineering standup.".to_string(),
        "Write a realistic git commit message and a 2-sentence description.".to_string(),
        "Write a brief internal Jira comment about a bug fix in plain language.".to_string(),
    ]
}
