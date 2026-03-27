mod config;
mod jiggler;
mod keymap;
mod llm_client;
mod transport;
mod typer;

use anyhow::{Context, Result};
use clap::{Parser, ValueEnum};
use log::{error, info};
use std::path::PathBuf;
use std::sync::Arc;
use tokio::sync::watch;
use transport::HidTransport;

#[derive(Debug, Clone, ValueEnum)]
enum Mode {
    Jiggle,
    Type,
    Both,
}

#[derive(Debug, Clone, ValueEnum)]
enum Transport {
    Bt,
    Usb,
    Auto,
}

#[derive(Parser, Debug)]
#[command(name = "pikey", about = "BT HID spoofer + LLM auto-typer for Raspberry Pi")]
struct Args {
    /// Operating mode
    #[arg(long, default_value = "both")]
    mode: Mode,

    /// Path to config YAML file
    #[arg(long, default_value = "config.yaml")]
    config: PathBuf,

    /// Transport layer
    #[arg(long, default_value = "auto")]
    transport: Transport,

    /// Enable debug logging
    #[arg(long)]
    verbose: bool,
}

#[tokio::main]
async fn main() -> Result<()> {
    let args = Args::parse();

    // Init logging
    let log_level = if args.verbose { "debug" } else { "info" };
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or(log_level))
        .format_timestamp_secs()
        .init();

    // Load config
    let cfg = config::load_config(&args.config)
        .with_context(|| format!("Failed to load config from {}", args.config.display()))?;

    // Validate LLM config if typing is enabled
    let typing_enabled = matches!(args.mode, Mode::Type | Mode::Both) && cfg.typer.enabled;
    if typing_enabled && cfg.llm.url.trim().is_empty() {
        error!(
            "llm.url is not set in {}. \
             Set it to your LiteLLM/Ollama/OpenAI-compatible endpoint, \
             or run with --mode jiggle to use only the mouse jiggler.",
            args.config.display()
        );
        std::process::exit(1);
    }

    info!("========== PiKey — BT HID Spoofer ==========");
    info!("Mode: {:?}", args.mode);
    info!("Spoofing as: {}", cfg.device.name);

    // Create transport
    let mut hid: Box<dyn HidTransport> = match args.transport {
        Transport::Bt => Box::new(transport::BluetoothTransport::new(
            cfg.device.target_mac.clone(),
        )),
        Transport::Usb => Box::new(transport::UsbGadgetTransport::new()),
        Transport::Auto => transport::auto_detect_transport(&cfg.device.target_mac),
    };

    // Connect
    hid.connect().await.context("Failed to connect HID transport")?;

    // Stop signal for graceful shutdown
    let (stop_tx, stop_rx) = watch::channel(false);

    // We need the transport to be shareable across tasks.
    // Since we have a Box<dyn HidTransport>, we wrap it in Arc via a concrete wrapper.
    let transport = Arc::new(DynTransport(tokio::sync::Mutex::new(hid)));

    let mut handles = Vec::new();

    // Spawn jiggler
    if matches!(args.mode, Mode::Jiggle | Mode::Both) {
        let t = Arc::clone(&transport);
        let jiggler_cfg = cfg.jiggler.clone();
        let rx = stop_rx.clone();
        handles.push(tokio::spawn(async move {
            if let Err(e) = jiggler::run(t, jiggler_cfg, rx).await {
                error!("Jiggler error: {}", e);
            }
        }));
    }

    // Spawn typer
    if typing_enabled {
        let t = Arc::clone(&transport);
        let typer_cfg = cfg.typer.clone();
        let llm_cfg = cfg.llm.clone();
        let rx = stop_rx.clone();
        handles.push(tokio::spawn(async move {
            if let Err(e) = typer::run(t, typer_cfg, llm_cfg, rx).await {
                error!("Typer error: {}", e);
            }
        }));
    }

    info!("Running — Ctrl+C to stop");

    // Wait for Ctrl+C
    tokio::signal::ctrl_c().await?;
    info!("Shutting down...");
    let _ = stop_tx.send(true);

    // Wait for tasks to finish
    for h in handles {
        let _ = h.await;
    }

    // Disconnect
    let mut guard = transport.0.lock().await;
    let _ = guard.disconnect().await;

    info!("Goodbye");
    Ok(())
}

/// Wrapper to make Box<dyn HidTransport> usable as Arc<T: HidTransport>.
struct DynTransport(tokio::sync::Mutex<Box<dyn HidTransport>>);

impl HidTransport for DynTransport {
    async fn connect(&mut self) -> Result<()> {
        self.0.lock().await.connect().await
    }

    async fn disconnect(&mut self) -> Result<()> {
        self.0.lock().await.disconnect().await
    }

    async fn send_keyboard_report(&self, report: &[u8]) -> Result<()> {
        self.0.lock().await.send_keyboard_report(report).await
    }

    async fn send_mouse_report(&self, report: &[u8]) -> Result<()> {
        self.0.lock().await.send_mouse_report(report).await
    }

    fn is_connected(&self) -> bool {
        // Can't async-lock from a sync fn; optimistic return
        true
    }
}
