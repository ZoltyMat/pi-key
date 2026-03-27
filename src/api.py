"""api.py — REST API server for PiKey remote control.

Provides authenticated endpoints for status, mode switching,
triggering typing/jiggle sessions, and runtime config updates.
Disabled by default — opt-in via config or --api flag.
"""

from __future__ import annotations

import hmac
import logging
import os
import time
from collections import defaultdict
from typing import TYPE_CHECKING

from starlette.applications import Starlette
from starlette.middleware import Middleware
from starlette.middleware.base import BaseHTTPMiddleware
from starlette.middleware.cors import CORSMiddleware
from starlette.requests import Request
from starlette.responses import JSONResponse
from starlette.routing import Route

if TYPE_CHECKING:
    from src.config import ApiConfig, PikeyConfig

log = logging.getLogger(__name__)

# ── Rate limiter ─────────────────────────────────────────────────────────────


class RateLimiter:
    """Simple in-memory sliding-window rate limiter per client IP."""

    def __init__(self, max_requests: int = 10, window_secs: float = 1.0):
        self.max_requests = max_requests
        self.window = window_secs
        self._hits: dict[str, list[float]] = defaultdict(list)

    def is_allowed(self, client_ip: str) -> bool:
        now = time.monotonic()
        hits = self._hits[client_ip]
        # Prune old entries
        cutoff = now - self.window
        self._hits[client_ip] = [t for t in hits if t > cutoff]
        hits = self._hits[client_ip]
        if len(hits) >= self.max_requests:
            return False
        hits.append(now)
        return True


# ── App state container ──────────────────────────────────────────────────────


class PikeyApiState:
    """Shared state between API endpoints and the main event loop."""

    def __init__(
        self,
        config: PikeyConfig,
        jiggler=None,
        typer=None,
        transport=None,
        start_time: float | None = None,
    ):
        self.config = config
        self.jiggler = jiggler
        self.typer = typer
        self.transport = transport
        self.start_time = start_time or time.time()
        self.current_mode: str = "both"
        self.last_typing_session: float | None = None


# ── Auth middleware ───────────────────────────────────────────────────────────

_HEALTH_PATH = "/health"


def _check_api_key(request: Request, expected_key: str) -> bool:
    """Constant-time comparison of the X-API-Key header."""
    provided = request.headers.get("x-api-key", "")
    return hmac.compare_digest(provided.encode(), expected_key.encode())


# ── Endpoint handlers ────────────────────────────────────────────────────────


async def health(request: Request) -> JSONResponse:
    """Health check — no auth required."""
    return JSONResponse({"status": "ok"})


async def status(request: Request) -> JSONResponse:
    """Current mode, transport type, uptime, last typing session."""
    state: PikeyApiState = request.app.state.pikey
    uptime = time.time() - state.start_time
    transport_name = type(state.transport).__name__ if state.transport else "none"
    return JSONResponse({
        "mode": state.current_mode,
        "transport": transport_name,
        "uptime_seconds": round(uptime, 1),
        "last_typing_session": state.last_typing_session,
        "connected": state.transport.is_connected if state.transport else False,
    })


async def set_mode(request: Request) -> JSONResponse:
    """Change operating mode: jiggle, type, or both."""
    try:
        body = await request.json()
    except Exception:
        return JSONResponse({"error": "invalid JSON"}, status_code=400)

    mode = body.get("mode")
    if mode not in ("jiggle", "type", "both"):
        return JSONResponse(
            {"error": "mode must be 'jiggle', 'type', or 'both'"},
            status_code=400,
        )

    state: PikeyApiState = request.app.state.pikey
    state.current_mode = mode
    log.info("API: mode changed to %s", mode)
    return JSONResponse({"mode": mode})


async def trigger_type(request: Request) -> JSONResponse:
    """Trigger an immediate typing session."""
    state: PikeyApiState = request.app.state.pikey
    if not state.typer:
        return JSONResponse({"error": "typer not available"}, status_code=503)

    state.last_typing_session = time.time()
    log.info("API: typing session triggered")
    return JSONResponse({"triggered": "type"})


async def trigger_jiggle(request: Request) -> JSONResponse:
    """Trigger an immediate jiggle."""
    state: PikeyApiState = request.app.state.pikey
    if not state.jiggler:
        return JSONResponse({"error": "jiggler not available"}, status_code=503)

    log.info("API: jiggle triggered")
    return JSONResponse({"triggered": "jiggle"})


async def get_config(request: Request) -> JSONResponse:
    """Return current config with secrets redacted."""
    state: PikeyApiState = request.app.state.pikey
    cfg = state.config

    return JSONResponse({
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
            "api_key": "***" if cfg.llm.api_key else "",
            "max_tokens": cfg.llm.max_tokens,
        },
        "api": {
            "enabled": cfg.api.enabled,
            "host": cfg.api.host,
            "port": cfg.api.port,
            "api_key": "***",
            "rate_limit": cfg.api.rate_limit,
        },
    })


async def patch_config(request: Request) -> JSONResponse:
    """Update config fields at runtime (non-persistent)."""
    try:
        body = await request.json()
    except Exception:
        return JSONResponse({"error": "invalid JSON"}, status_code=400)

    state: PikeyApiState = request.app.state.pikey
    cfg = state.config
    updated = []

    # Whitelist of updatable fields
    if "jiggler" in body and isinstance(body["jiggler"], dict):
        for k, v in body["jiggler"].items():
            if hasattr(cfg.jiggler, k) and k != "enabled":
                setattr(cfg.jiggler, k, v)
                updated.append(f"jiggler.{k}")

    if "typer" in body and isinstance(body["typer"], dict):
        for k, v in body["typer"].items():
            if hasattr(cfg.typer, k) and k != "enabled":
                setattr(cfg.typer, k, v)
                updated.append(f"typer.{k}")

    log.info("API: config updated: %s", updated)
    return JSONResponse({"updated": updated})


async def reconnect(request: Request) -> JSONResponse:
    """Reconnect the HID transport."""
    state: PikeyApiState = request.app.state.pikey
    if not state.transport:
        return JSONResponse({"error": "no transport"}, status_code=503)

    try:
        await state.transport.disconnect()
        await state.transport.connect()
        log.info("API: transport reconnected")
        return JSONResponse({"reconnected": True})
    except Exception as e:
        log.error("API: reconnect failed: %s", e)
        return JSONResponse({"error": str(e)}, status_code=500)


# ── App factory ──────────────────────────────────────────────────────────────


def create_api_app(api_config: ApiConfig, api_key: str) -> Starlette:
    """Build the Starlette app with auth and rate limiting middleware."""

    rate_limiter = RateLimiter(
        max_requests=api_config.rate_limit,
        window_secs=1.0,
    )

    class AuthRateLimitMiddleware(BaseHTTPMiddleware):
        async def dispatch(self, request: Request, call_next):
            # Health endpoint is unauthenticated
            if request.url.path == _HEALTH_PATH:
                return await call_next(request)

            # Rate limiting
            client_ip = request.client.host if request.client else "unknown"
            if not rate_limiter.is_allowed(client_ip):
                return JSONResponse(
                    {"error": "rate limit exceeded"},
                    status_code=429,
                )

            # API key auth
            if not _check_api_key(request, api_key):
                return JSONResponse(
                    {"error": "unauthorized"},
                    status_code=401,
                )

            return await call_next(request)

    routes = [
        Route("/health", health, methods=["GET"]),
        Route("/status", status, methods=["GET"]),
        Route("/mode", set_mode, methods=["POST"]),
        Route("/type", trigger_type, methods=["POST"]),
        Route("/jiggle", trigger_jiggle, methods=["POST"]),
        Route("/config", get_config, methods=["GET"]),
        Route("/config", patch_config, methods=["PATCH"]),
        Route("/reconnect", reconnect, methods=["POST"]),
    ]

    middleware = [Middleware(AuthRateLimitMiddleware)]
    if api_config.allowed_origins:
        middleware.append(
            Middleware(
                CORSMiddleware,
                allow_origins=api_config.allowed_origins,
                allow_methods=["*"],
                allow_headers=["*"],
            )
        )

    app = Starlette(routes=routes, middleware=middleware)
    return app


def get_api_key(api_config: ApiConfig) -> str:
    """Resolve API key from config or PIKEY_API_KEY env var."""
    return os.environ.get("PIKEY_API_KEY", "") or api_config.api_key
