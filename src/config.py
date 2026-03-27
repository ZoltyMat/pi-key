"""config.py — YAML config loader with typed dataclasses."""

from __future__ import annotations

import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import List

import yaml


@dataclass
class DeviceConfig:
    name: str = "Logitech K380 Multi-Device Keyboard"
    cod: str = "0x002540"
    target_mac: str = ""


@dataclass
class JigglerConfig:
    enabled: bool = True
    interval_min: float = 45
    interval_max: float = 90
    max_delta: int = 3
    big_move_chance: float = 0.1


@dataclass
class TyperConfig:
    enabled: bool = True
    interval_min: float = 180
    interval_max: float = 600
    cpm_min: int = 220
    cpm_max: int = 360
    typo_rate: float = 0.02
    think_pause_chance: float = 0.05
    think_pause_secs: List[float] = field(default_factory=lambda: [1.5, 4.0])


@dataclass
class LLMConfig:
    url: str = ""
    api_style: str = "openai"
    model: str = ""
    api_key: str = ""
    max_tokens: int = 200
    prompts: List[str] = field(default_factory=lambda: [
        "Write a realistic Python function with a docstring and comments.",
    ])


@dataclass
class TlsConfig:
    enabled: bool = False
    cert_path: str = ""
    key_path: str = ""


@dataclass
class ApiConfig:
    enabled: bool = False
    host: str = "0.0.0.0"
    port: int = 8099
    api_key: str = ""
    allowed_origins: List[str] = field(default_factory=list)
    rate_limit: int = 10
    tls: TlsConfig = field(default_factory=TlsConfig)


@dataclass
class PikeyConfig:
    device: DeviceConfig = field(default_factory=DeviceConfig)
    jiggler: JigglerConfig = field(default_factory=JigglerConfig)
    typer: TyperConfig = field(default_factory=TyperConfig)
    llm: LLMConfig = field(default_factory=LLMConfig)
    api: ApiConfig = field(default_factory=ApiConfig)


def _build_dataclass(cls, data: dict | None):
    """Build a dataclass from a dict, ignoring unknown keys."""
    if not data:
        return cls()
    valid = {f.name for f in cls.__dataclass_fields__.values()}
    return cls(**{k: v for k, v in data.items() if k in valid})


def load_config(path: str) -> PikeyConfig:
    """Load and validate config.yaml, returning a typed PikeyConfig."""
    p = Path(path)
    if not p.exists():
        print(f"Config not found: {path}\n  Run: cp config.example.yaml config.yaml")
        sys.exit(1)

    with open(p) as f:
        raw = yaml.safe_load(f) or {}

    # Build api config with nested tls
    api_raw = raw.get("api") or {}
    tls_raw = api_raw.pop("tls", None) if isinstance(api_raw, dict) else None
    api_cfg = _build_dataclass(ApiConfig, api_raw)
    if tls_raw:
        api_cfg.tls = _build_dataclass(TlsConfig, tls_raw)

    cfg = PikeyConfig(
        device=_build_dataclass(DeviceConfig, raw.get("device")),
        jiggler=_build_dataclass(JigglerConfig, raw.get("jiggler")),
        typer=_build_dataclass(TyperConfig, raw.get("typer")),
        llm=_build_dataclass(LLMConfig, raw.get("llm")),
        api=api_cfg,
    )

    return cfg


def validate_for_api(cfg: PikeyConfig) -> None:
    """Raise if API is enabled but api_key is empty."""
    if cfg.api.enabled and not cfg.api.api_key.strip():
        raise ValueError(
            "api.api_key is required when the API is enabled. "
            "Set it in config.yaml or via PIKEY_API_KEY env var."
        )


def validate_for_typing(cfg: PikeyConfig) -> None:
    """Raise if LLM config is missing required fields for typing mode."""
    if not cfg.llm.url.strip():
        raise ValueError(
            "llm.url is not set in config.yaml. "
            "Set it to your LiteLLM/Ollama/OpenAI-compatible endpoint, "
            "or run with --mode jiggle."
        )
