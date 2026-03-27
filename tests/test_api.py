"""Tests for the PiKey REST API server."""

import time

import pytest
from starlette.testclient import TestClient

from src.api import PikeyApiState, create_api_app, get_api_key, RateLimiter
from src.config import ApiConfig, PikeyConfig


API_KEY = "test-secret-key-12345"


@pytest.fixture
def api_config():
    return ApiConfig(enabled=True, port=8099, api_key=API_KEY, rate_limit=10)


@pytest.fixture
def pikey_config(api_config):
    cfg = PikeyConfig()
    cfg.api = api_config
    return cfg


@pytest.fixture
def app(api_config, pikey_config, mock_transport):
    app = create_api_app(api_config, API_KEY)
    app.state.pikey = PikeyApiState(
        config=pikey_config,
        jiggler=object(),   # stub
        typer=object(),      # stub
        transport=mock_transport,
    )
    return app


@pytest.fixture
def client(app):
    return TestClient(app)


def auth_headers():
    return {"x-api-key": API_KEY}


# ── Health endpoint (no auth) ────────────────────────────────────────────────


class TestHealth:
    def test_health_no_auth(self, client):
        resp = client.get("/health")
        assert resp.status_code == 200
        assert resp.json()["status"] == "ok"

    def test_health_ignores_bad_key(self, client):
        resp = client.get("/health", headers={"x-api-key": "wrong"})
        assert resp.status_code == 200


# ── Auth enforcement ─────────────────────────────────────────────────────────


class TestAuth:
    def test_status_requires_auth(self, client):
        resp = client.get("/status")
        assert resp.status_code == 401
        assert resp.json()["error"] == "unauthorized"

    def test_status_wrong_key(self, client):
        resp = client.get("/status", headers={"x-api-key": "wrong-key"})
        assert resp.status_code == 401

    def test_status_valid_key(self, client):
        resp = client.get("/status", headers=auth_headers())
        assert resp.status_code == 200


# ── Status endpoint ──────────────────────────────────────────────────────────


class TestStatus:
    def test_status_fields(self, client):
        resp = client.get("/status", headers=auth_headers())
        data = resp.json()
        assert "mode" in data
        assert "transport" in data
        assert "uptime_seconds" in data
        assert "connected" in data
        assert data["mode"] == "both"
        assert data["connected"] is True


# ── Mode endpoint ────────────────────────────────────────────────────────────


class TestMode:
    def test_set_mode_jiggle(self, client):
        resp = client.post("/mode", json={"mode": "jiggle"}, headers=auth_headers())
        assert resp.status_code == 200
        assert resp.json()["mode"] == "jiggle"

    def test_set_mode_type(self, client):
        resp = client.post("/mode", json={"mode": "type"}, headers=auth_headers())
        assert resp.status_code == 200
        assert resp.json()["mode"] == "type"

    def test_set_mode_invalid(self, client):
        resp = client.post("/mode", json={"mode": "invalid"}, headers=auth_headers())
        assert resp.status_code == 400

    def test_set_mode_bad_json(self, client):
        resp = client.post(
            "/mode",
            content="not json",
            headers={**auth_headers(), "content-type": "application/json"},
        )
        assert resp.status_code == 400


# ── Trigger endpoints ────────────────────────────────────────────────────────


class TestTriggers:
    def test_trigger_type(self, client):
        resp = client.post("/type", headers=auth_headers())
        assert resp.status_code == 200
        assert resp.json()["triggered"] == "type"

    def test_trigger_jiggle(self, client):
        resp = client.post("/jiggle", headers=auth_headers())
        assert resp.status_code == 200
        assert resp.json()["triggered"] == "jiggle"

    def test_trigger_type_no_typer(self, client, app):
        app.state.pikey.typer = None
        resp = client.post("/type", headers=auth_headers())
        assert resp.status_code == 503

    def test_trigger_jiggle_no_jiggler(self, client, app):
        app.state.pikey.jiggler = None
        resp = client.post("/jiggle", headers=auth_headers())
        assert resp.status_code == 503


# ── Config endpoints ─────────────────────────────────────────────────────────


class TestConfig:
    def test_get_config_redacts_secrets(self, client, pikey_config):
        pikey_config.llm.api_key = "super-secret"
        resp = client.get("/config", headers=auth_headers())
        assert resp.status_code == 200
        data = resp.json()
        assert data["llm"]["api_key"] == "***"
        assert data["api"]["api_key"] == "***"

    def test_get_config_empty_api_key(self, client, pikey_config):
        pikey_config.llm.api_key = ""
        resp = client.get("/config", headers=auth_headers())
        assert resp.json()["llm"]["api_key"] == ""

    def test_get_config_structure(self, client):
        resp = client.get("/config", headers=auth_headers())
        data = resp.json()
        assert "device" in data
        assert "jiggler" in data
        assert "typer" in data
        assert "llm" in data
        assert "api" in data

    def test_patch_config(self, client, pikey_config):
        resp = client.patch(
            "/config",
            json={"jiggler": {"interval_min": 30, "interval_max": 60}},
            headers=auth_headers(),
        )
        assert resp.status_code == 200
        assert "jiggler.interval_min" in resp.json()["updated"]
        assert pikey_config.jiggler.interval_min == 30
        assert pikey_config.jiggler.interval_max == 60

    def test_patch_config_typer(self, client, pikey_config):
        resp = client.patch(
            "/config",
            json={"typer": {"cpm_min": 200}},
            headers=auth_headers(),
        )
        assert resp.status_code == 200
        assert pikey_config.typer.cpm_min == 200


# ── Reconnect endpoint ──────────────────────────────────────────────────────


class TestReconnect:
    def test_reconnect_no_transport(self, client, app):
        app.state.pikey.transport = None
        resp = client.post("/reconnect", headers=auth_headers())
        assert resp.status_code == 503


# ── Rate limiting ────────────────────────────────────────────────────────────


class TestRateLimiter:
    def test_allows_under_limit(self):
        rl = RateLimiter(max_requests=3, window_secs=1.0)
        assert rl.is_allowed("1.2.3.4") is True
        assert rl.is_allowed("1.2.3.4") is True
        assert rl.is_allowed("1.2.3.4") is True

    def test_blocks_over_limit(self):
        rl = RateLimiter(max_requests=2, window_secs=1.0)
        assert rl.is_allowed("1.2.3.4") is True
        assert rl.is_allowed("1.2.3.4") is True
        assert rl.is_allowed("1.2.3.4") is False

    def test_separate_clients(self):
        rl = RateLimiter(max_requests=1, window_secs=1.0)
        assert rl.is_allowed("1.2.3.4") is True
        assert rl.is_allowed("5.6.7.8") is True
        assert rl.is_allowed("1.2.3.4") is False


# ── API key resolution ───────────────────────────────────────────────────────


class TestGetApiKey:
    def test_from_config(self, api_config):
        assert get_api_key(api_config) == API_KEY

    def test_env_var_overrides(self, api_config, monkeypatch):
        monkeypatch.setenv("PIKEY_API_KEY", "env-key")
        assert get_api_key(api_config) == "env-key"

    def test_env_var_empty_falls_back(self, api_config, monkeypatch):
        monkeypatch.setenv("PIKEY_API_KEY", "")
        assert get_api_key(api_config) == API_KEY
