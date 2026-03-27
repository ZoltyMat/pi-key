use anyhow::{Context, Result};
use log::debug;
use rand::seq::SliceRandom;
use serde_json::json;

use crate::config::LlmConfig;

/// Fetch generated text from an OpenAI-compatible or Ollama endpoint.
pub async fn fetch_text(cfg: &LlmConfig) -> Result<String> {
    let prompt = cfg
        .prompts
        .choose(&mut rand::thread_rng())
        .cloned()
        .unwrap_or_else(|| "Write a realistic Python function with a docstring.".to_string());

    let url = cfg.url.trim_end_matches('/');
    let client = reqwest::Client::new();

    let mut headers = reqwest::header::HeaderMap::new();
    headers.insert("Content-Type", "application/json".parse().unwrap());
    if !cfg.api_key.is_empty() {
        headers.insert(
            "Authorization",
            format!("Bearer {}", cfg.api_key).parse().unwrap(),
        );
    }

    debug!("Fetching from LLM: style={} model={}", cfg.api_style, cfg.model);

    match cfg.api_style.as_str() {
        "ollama" => fetch_ollama(&client, url, &prompt, cfg, headers).await,
        _ => fetch_openai(&client, url, &prompt, cfg, headers).await,
    }
}

async fn fetch_ollama(
    client: &reqwest::Client,
    url: &str,
    prompt: &str,
    cfg: &LlmConfig,
    headers: reqwest::header::HeaderMap,
) -> Result<String> {
    let mut payload = json!({
        "prompt": prompt,
        "stream": false,
        "options": { "num_predict": cfg.max_tokens },
    });
    if !cfg.model.is_empty() {
        payload["model"] = json!(cfg.model);
    }

    let resp = client
        .post(format!("{}/api/generate", url))
        .headers(headers)
        .json(&payload)
        .timeout(std::time::Duration::from_secs(60))
        .send()
        .await
        .context("Failed to reach Ollama endpoint")?
        .error_for_status()
        .context("Ollama returned an error")?;

    let body: serde_json::Value = resp.json().await?;
    let text = body["response"]
        .as_str()
        .unwrap_or("")
        .trim()
        .to_string();
    Ok(text)
}

async fn fetch_openai(
    client: &reqwest::Client,
    url: &str,
    prompt: &str,
    cfg: &LlmConfig,
    headers: reqwest::header::HeaderMap,
) -> Result<String> {
    let mut payload = json!({
        "messages": [{"role": "user", "content": prompt}],
        "max_tokens": cfg.max_tokens,
        "stream": false,
    });
    if !cfg.model.is_empty() {
        payload["model"] = json!(cfg.model);
    }

    let resp = client
        .post(format!("{}/v1/chat/completions", url))
        .headers(headers)
        .json(&payload)
        .timeout(std::time::Duration::from_secs(60))
        .send()
        .await
        .context("Failed to reach OpenAI-compatible endpoint")?
        .error_for_status()
        .context("OpenAI-compatible endpoint returned an error")?;

    let body: serde_json::Value = resp.json().await?;
    let text = body["choices"][0]["message"]["content"]
        .as_str()
        .unwrap_or("")
        .trim()
        .to_string();
    Ok(text)
}
