use anyhow::Result;
use log::{debug, info, warn};
use rand::seq::SliceRandom;
use rand::Rng;
use std::sync::Arc;
use tokio::sync::watch;
use tokio::time::{sleep, Duration};

use crate::config::{LlmConfig, TyperConfig};
use crate::keymap::NEARBY_KEYS;
use crate::llm_client;
use crate::transport::{self, HidTransport};

/// Async LLM-powered typing loop.
pub async fn run<T: HidTransport>(
    transport: Arc<T>,
    typer_cfg: TyperConfig,
    llm_cfg: LlmConfig,
    mut stop: watch::Receiver<bool>,
) -> Result<()> {
    if !typer_cfg.enabled {
        info!("Typer disabled in config");
        return Ok(());
    }

    if llm_cfg.url.trim().is_empty() {
        anyhow::bail!(
            "llm.url is not set in config. Set it to your LiteLLM/Ollama/OpenAI-compatible endpoint."
        );
    }

    info!("LLM typer started");

    loop {
        // Random wait between typing sessions
        let interval = rand::rng().random_range(typer_cfg.interval_min..=typer_cfg.interval_max);
        debug!("Next typing session in {:.0}s", interval);

        let deadline = tokio::time::Instant::now() + Duration::from_secs_f64(interval);
        loop {
            tokio::select! {
                _ = sleep(Duration::from_secs(1).min(deadline - tokio::time::Instant::now())) => {
                    if tokio::time::Instant::now() >= deadline {
                        break;
                    }
                }
                _ = stop.changed() => {
                    if *stop.borrow() {
                        info!("LLM typer stopped");
                        return Ok(());
                    }
                }
            }
        }

        if *stop.borrow() {
            info!("LLM typer stopped");
            return Ok(());
        }

        // Fetch and type
        match llm_client::fetch_text(&llm_cfg).await {
            Ok(text) if text.is_empty() => {
                warn!("LLM returned empty text, skipping");
            }
            Ok(text) => {
                info!("Typing {} chars...", text.len());
                if let Err(e) =
                    type_text(transport.as_ref(), &typer_cfg, &text, &mut stop).await
                {
                    warn!("Typing error: {}", e);
                }
                info!("Typing session complete");
            }
            Err(e) => {
                warn!("LLM fetch error: {} — retrying in 60s", e);
                sleep(Duration::from_secs(60)).await;
            }
        }
    }
}

/// Type text character by character with human-like timing.
async fn type_text<T: HidTransport>(
    transport: &T,
    cfg: &TyperConfig,
    text: &str,
    stop: &mut watch::Receiver<bool>,
) -> Result<()> {
    for ch in text.chars() {
        // Check for stop signal
        if *stop.borrow() {
            return Ok(());
        }

        // Occasional thinking pause
        if rand::rng().random::<f64>() < cfg.think_pause_chance {
            let pause = rand::rng().random_range(cfg.think_pause_secs[0]..=cfg.think_pause_secs[1]);
            debug!("Thinking pause: {:.1}s", pause);
            sleep(Duration::from_secs_f64(pause)).await;
        }

        // Occasional typo + correction
        if should_typo(ch, cfg.typo_rate) {
            if let Some(nearby) = pick_nearby_key(ch) {
                debug!("Typo: {:?} -> {:?} (correcting)", ch, nearby);
                transport::type_char(transport, nearby).await?;
                sleep(Duration::from_secs_f64(char_delay(cfg) * 1.5)).await;
                transport::type_backspace(transport).await?;
                let correction_pause = rand::rng().random_range(0.1..=0.3);
                sleep(Duration::from_secs_f64(correction_pause)).await;
            }
        }

        transport::type_char(transport, ch).await?;
        sleep(Duration::from_secs_f64(char_delay(cfg))).await;
    }

    Ok(())
}

/// Compute inter-keystroke delay based on CPM config with gaussian jitter.
fn char_delay(cfg: &TyperConfig) -> f64 {
    let mut rng = rand::rng();
    let cpm = rng.random_range(cfg.cpm_min..=cfg.cpm_max);
    let base = 60.0 / cpm;
    // Gaussian jitter (+/- 20%)
    let jitter: f64 = rng.random_range(-0.2..=0.2) * base;
    (base + jitter).max(0.02)
}

/// Decide whether to introduce a typo for this character.
fn should_typo(ch: char, rate: f64) -> bool {
    let lower = ch.to_ascii_lowercase();
    NEARBY_KEYS.contains_key(&lower) && rand::rng().random::<f64>() < rate
}

/// Pick a random adjacent key for typo simulation.
fn pick_nearby_key(ch: char) -> Option<char> {
    let lower = ch.to_ascii_lowercase();
    let neighbors = NEARBY_KEYS.get(&lower)?;
    let chars: Vec<char> = neighbors.chars().collect();
    chars.choose(&mut rand::rng()).copied()
}
