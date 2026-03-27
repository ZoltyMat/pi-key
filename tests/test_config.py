"""Tests for src/config.py — YAML config loader with dataclasses."""

import os
import sys
import pytest
import yaml

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

from config import (
    load_config,
    validate_for_typing,
    PikeyConfig,
    DeviceConfig,
    JigglerConfig,
    TyperConfig,
    LLMConfig,
)


class TestLoadValidConfig:
    """load_config should parse a complete YAML and populate all dataclass fields."""

    def test_load_valid_config(self, tmp_config_yaml):
        cfg = load_config(tmp_config_yaml)

        assert isinstance(cfg, PikeyConfig)
        assert isinstance(cfg.device, DeviceConfig)
        assert isinstance(cfg.jiggler, JigglerConfig)
        assert isinstance(cfg.typer, TyperConfig)
        assert isinstance(cfg.llm, LLMConfig)

        # Device
        assert cfg.device.name == "Logitech K380 Multi-Device Keyboard"
        assert cfg.device.cod == "0x002540"
        assert cfg.device.target_mac == ""

        # Jiggler
        assert cfg.jiggler.enabled is True
        assert cfg.jiggler.interval_min == 45
        assert cfg.jiggler.interval_max == 90
        assert cfg.jiggler.max_delta == 3
        assert cfg.jiggler.big_move_chance == 0.1

        # Typer
        assert cfg.typer.enabled is True
        assert cfg.typer.cpm_min == 220
        assert cfg.typer.cpm_max == 360
        assert cfg.typer.typo_rate == 0.02
        assert cfg.typer.think_pause_secs == [1.5, 4.0]

        # LLM
        assert cfg.llm.url == "http://localhost:11434"
        assert cfg.llm.api_style == "ollama"
        assert cfg.llm.model == "llama3.2:1b"
        assert cfg.llm.max_tokens == 200
        assert len(cfg.llm.prompts) == 2


class TestMissingLLMUrl:
    """validate_for_typing should raise ValueError when llm.url is empty."""

    def test_missing_llm_url_raises(self, tmp_path):
        data = {
            "llm": {"url": "", "api_style": "openai", "model": "x"},
        }
        p = tmp_path / "config.yaml"
        p.write_text(yaml.dump(data))
        cfg = load_config(str(p))
        with pytest.raises(ValueError, match="llm.url is not set"):
            validate_for_typing(cfg)

    def test_whitespace_only_url_raises(self, tmp_path):
        data = {
            "llm": {"url": "   ", "api_style": "openai"},
        }
        p = tmp_path / "config.yaml"
        p.write_text(yaml.dump(data))
        cfg = load_config(str(p))
        with pytest.raises(ValueError, match="llm.url is not set"):
            validate_for_typing(cfg)


class TestDefaultValues:
    """A partial or empty config should produce PikeyConfig with sensible defaults."""

    def test_empty_yaml_gets_defaults(self, tmp_path):
        p = tmp_path / "config.yaml"
        p.write_text("---\n")
        cfg = load_config(str(p))

        assert cfg.device.name == "Logitech K380 Multi-Device Keyboard"
        assert cfg.jiggler.enabled is True
        assert cfg.typer.cpm_min == 220
        assert cfg.llm.max_tokens == 200

    def test_partial_device_config(self, tmp_path):
        data = {"device": {"name": "Custom Keyboard"}}
        p = tmp_path / "config.yaml"
        p.write_text(yaml.dump(data))
        cfg = load_config(str(p))

        assert cfg.device.name == "Custom Keyboard"
        # Other device fields use defaults
        assert cfg.device.cod == "0x002540"
        assert cfg.device.target_mac == ""


class TestMissingFile:
    """load_config should exit (sys.exit) when given a non-existent path."""

    def test_missing_file_raises(self):
        with pytest.raises(SystemExit):
            load_config("/nonexistent/path/config.yaml")
