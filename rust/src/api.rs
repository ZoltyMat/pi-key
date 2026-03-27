use anyhow::Result;
use axum::{
    extract::State,
    http::{HeaderMap, Request, StatusCode},
    middleware::{self, Next},
    response::{IntoResponse, Json},
    routing::{get, patch, post},
    Router,
};
use log::info;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::sync::Arc;
use std::time::Instant;
use tokio::sync::Mutex;

use crate::config::{ApiConfig, PikeyConfig};

// ── Shared state ────────────────────────────────────────────────────────────

pub struct ApiState {
    pub config: Mutex<PikeyConfig>,
    pub current_mode: Mutex<String>,
    pub start_time: Instant,
    pub last_typing_session: Mutex<Option<f64>>,
    pub api_key: String,
    pub rate_limiter: Mutex<RateLimiter>,
}

// ── Rate limiter ────────────────────────────────────────────────────────────

pub struct RateLimiter {
    max_requests: u32,
    window_secs: f64,
    hits: HashMap<String, Vec<Instant>>,
}

impl RateLimiter {
    pub fn new(max_requests: u32, window_secs: f64) -> Self {
        Self {
            max_requests,
            window_secs,
            hits: HashMap::new(),
        }
    }

    pub fn is_allowed(&mut self, client_ip: &str) -> bool {
        let now = Instant::now();
        let window = std::time::Duration::from_secs_f64(self.window_secs);
        let hits = self.hits.entry(client_ip.to_string()).or_default();
        hits.retain(|t| now.duration_since(*t) < window);
        if hits.len() >= self.max_requests as usize {
            return false;
        }
        hits.push(now);
        true
    }
}

// ── Auth + rate limit middleware ─────────────────────────────────────────────

async fn auth_middleware(
    State(state): State<Arc<ApiState>>,
    headers: HeaderMap,
    request: Request<axum::body::Body>,
    next: Next,
) -> impl IntoResponse {
    let path = request.uri().path().to_string();

    // /health is unauthenticated
    if path == "/health" {
        return next.run(request).await.into_response();
    }

    // Rate limit by peer IP (use X-Forwarded-For or fallback)
    let client_ip = headers
        .get("x-forwarded-for")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("unknown")
        .to_string();

    {
        let mut rl = state.rate_limiter.lock().await;
        if !rl.is_allowed(&client_ip) {
            return (
                StatusCode::TOO_MANY_REQUESTS,
                Json(json!({"error": "rate limit exceeded"})),
            )
                .into_response();
        }
    }

    // API key check (constant-time via hmac compare)
    let provided = headers
        .get("x-api-key")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("");

    if !constant_time_eq(provided.as_bytes(), state.api_key.as_bytes()) {
        return (
            StatusCode::UNAUTHORIZED,
            Json(json!({"error": "unauthorized"})),
        )
            .into_response();
    }

    next.run(request).await.into_response()
}

fn constant_time_eq(a: &[u8], b: &[u8]) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let mut diff = 0u8;
    for (x, y) in a.iter().zip(b.iter()) {
        diff |= x ^ y;
    }
    diff == 0
}

// ── Endpoint handlers ───────────────────────────────────────────────────────

async fn health() -> Json<Value> {
    Json(json!({"status": "ok"}))
}

async fn status(State(state): State<Arc<ApiState>>) -> Json<Value> {
    let mode = state.current_mode.lock().await.clone();
    let uptime = state.start_time.elapsed().as_secs_f64();
    let last_typing = *state.last_typing_session.lock().await;
    Json(json!({
        "mode": mode,
        "uptime_seconds": (uptime * 10.0).round() / 10.0,
        "last_typing_session": last_typing,
    }))
}

#[derive(Deserialize)]
struct ModeRequest {
    mode: String,
}

async fn set_mode(
    State(state): State<Arc<ApiState>>,
    Json(body): Json<ModeRequest>,
) -> impl IntoResponse {
    if !["jiggle", "type", "both"].contains(&body.mode.as_str()) {
        return (
            StatusCode::BAD_REQUEST,
            Json(json!({"error": "mode must be 'jiggle', 'type', or 'both'"})),
        );
    }
    *state.current_mode.lock().await = body.mode.clone();
    info!("API: mode changed to {}", body.mode);
    (StatusCode::OK, Json(json!({"mode": body.mode})))
}

async fn trigger_type(State(state): State<Arc<ApiState>>) -> Json<Value> {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_secs_f64();
    *state.last_typing_session.lock().await = Some(now);
    info!("API: typing session triggered");
    Json(json!({"triggered": "type"}))
}

async fn trigger_jiggle() -> Json<Value> {
    info!("API: jiggle triggered");
    Json(json!({"triggered": "jiggle"}))
}

async fn get_config(State(state): State<Arc<ApiState>>) -> Json<Value> {
    let cfg = state.config.lock().await;
    Json(json!({
        "device": {"name": cfg.device.name, "cod": cfg.device.cod},
        "jiggler": {
            "enabled": cfg.jiggler.enabled,
            "interval_min": cfg.jiggler.interval_min,
            "interval_max": cfg.jiggler.interval_max,
            "max_delta": cfg.jiggler.max_delta,
            "big_move_chance": cfg.jiggler.big_move_chance,
        },
        "typer": {
            "enabled": cfg.typer.enabled,
            "interval_min": cfg.typer.interval_min,
            "interval_max": cfg.typer.interval_max,
            "cpm_min": cfg.typer.cpm_min,
            "cpm_max": cfg.typer.cpm_max,
            "typo_rate": cfg.typer.typo_rate,
            "think_pause_chance": cfg.typer.think_pause_chance,
            "think_pause_secs": cfg.typer.think_pause_secs,
        },
        "llm": {
            "url": cfg.llm.url,
            "api_style": cfg.llm.api_style,
            "model": cfg.llm.model,
            "api_key": if cfg.llm.api_key.is_empty() { "" } else { "***" },
            "max_tokens": cfg.llm.max_tokens,
        },
        "api": {
            "enabled": cfg.api.enabled,
            "host": cfg.api.host,
            "port": cfg.api.port,
            "api_key": "***",
            "rate_limit": cfg.api.rate_limit,
        },
    }))
}

#[derive(Deserialize)]
struct PatchConfig {
    jiggler: Option<HashMap<String, Value>>,
    typer: Option<HashMap<String, Value>>,
}

async fn patch_config(
    State(state): State<Arc<ApiState>>,
    Json(body): Json<PatchConfig>,
) -> Json<Value> {
    let mut cfg = state.config.lock().await;
    let mut updated = Vec::new();

    if let Some(j) = body.jiggler {
        for (k, v) in &j {
            match k.as_str() {
                "interval_min" => {
                    if let Some(n) = v.as_f64() {
                        cfg.jiggler.interval_min = n;
                        updated.push("jiggler.interval_min");
                    }
                }
                "interval_max" => {
                    if let Some(n) = v.as_f64() {
                        cfg.jiggler.interval_max = n;
                        updated.push("jiggler.interval_max");
                    }
                }
                "max_delta" => {
                    if let Some(n) = v.as_i64() {
                        cfg.jiggler.max_delta = n as i32;
                        updated.push("jiggler.max_delta");
                    }
                }
                "big_move_chance" => {
                    if let Some(n) = v.as_f64() {
                        cfg.jiggler.big_move_chance = n;
                        updated.push("jiggler.big_move_chance");
                    }
                }
                _ => {}
            }
        }
    }

    if let Some(t) = body.typer {
        for (k, v) in &t {
            match k.as_str() {
                "interval_min" => {
                    if let Some(n) = v.as_f64() {
                        cfg.typer.interval_min = n;
                        updated.push("typer.interval_min");
                    }
                }
                "interval_max" => {
                    if let Some(n) = v.as_f64() {
                        cfg.typer.interval_max = n;
                        updated.push("typer.interval_max");
                    }
                }
                "cpm_min" => {
                    if let Some(n) = v.as_f64() {
                        cfg.typer.cpm_min = n;
                        updated.push("typer.cpm_min");
                    }
                }
                "cpm_max" => {
                    if let Some(n) = v.as_f64() {
                        cfg.typer.cpm_max = n;
                        updated.push("typer.cpm_max");
                    }
                }
                "typo_rate" => {
                    if let Some(n) = v.as_f64() {
                        cfg.typer.typo_rate = n;
                        updated.push("typer.typo_rate");
                    }
                }
                _ => {}
            }
        }
    }

    info!("API: config updated: {:?}", updated);
    Json(json!({"updated": updated}))
}

async fn reconnect_handler() -> Json<Value> {
    info!("API: reconnect requested");
    Json(json!({"reconnected": true}))
}

// ── Router factory ──────────────────────────────────────────────────────────

pub fn build_router(state: Arc<ApiState>) -> Router {
    Router::new()
        .route("/health", get(health))
        .route("/status", get(status))
        .route("/mode", post(set_mode))
        .route("/type", post(trigger_type))
        .route("/jiggle", post(trigger_jiggle))
        .route("/config", get(get_config))
        .route("/config", patch(patch_config))
        .route("/reconnect", post(reconnect_handler))
        .layer(middleware::from_fn_with_state(state.clone(), auth_middleware))
        .with_state(state)
}

/// Resolve API key: PIKEY_API_KEY env var takes precedence over config.
pub fn resolve_api_key(cfg: &ApiConfig) -> String {
    std::env::var("PIKEY_API_KEY")
        .ok()
        .filter(|s| !s.is_empty())
        .unwrap_or_else(|| cfg.api_key.clone())
}

/// Start the API server as a tokio task.
pub async fn serve(cfg: &ApiConfig, state: Arc<ApiState>) -> Result<()> {
    let router = build_router(state);
    let addr = format!("{}:{}", cfg.host, cfg.port);
    let listener = tokio::net::TcpListener::bind(&addr).await?;
    info!("API server listening on {}", addr);
    axum::serve(listener, router).await?;
    Ok(())
}
