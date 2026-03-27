"""Shared fixtures for PiKey test suite."""

import os
import sys
import struct
import threading
import pytest
import yaml

# Ensure src/ is importable
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))


@pytest.fixture
def sample_config_dict():
    """A complete, valid config dict matching config.example.yaml structure."""
    return {
        "device": {
            "name": "Logitech K380 Multi-Device Keyboard",
            "cod": "0x002540",
            "target_mac": "",
        },
        "jiggler": {
            "enabled": True,
            "interval_min": 45,
            "interval_max": 90,
            "max_delta": 3,
            "big_move_chance": 0.1,
        },
        "typer": {
            "enabled": True,
            "interval_min": 180,
            "interval_max": 600,
            "cpm_min": 220,
            "cpm_max": 360,
            "typo_rate": 0.02,
            "think_pause_chance": 0.05,
            "think_pause_secs": [1.5, 4.0],
        },
        "llm": {
            "url": "http://localhost:11434",
            "api_style": "ollama",
            "model": "llama3.2:1b",
            "api_key": "",
            "max_tokens": 200,
            "prompts": [
                "Write a Python function.",
                "Write a git commit message.",
            ],
        },
    }


@pytest.fixture
def tmp_config_yaml(tmp_path, sample_config_dict):
    """Write sample_config_dict to a temporary YAML file and return the path."""
    p = tmp_path / "config.yaml"
    p.write_text(yaml.dump(sample_config_dict, default_flow_style=False))
    return str(p)


class MockHIDTransport:
    """Mock HIDTransport that records all reports sent through it.

    Implements the HIDTransport interface (connect, disconnect,
    send_keyboard_report, send_mouse_report, is_connected).
    Records raw bytes for inspection in tests.
    """

    def __init__(self):
        self._connected = False
        self.keyboard_reports: list[bytes] = []
        self.mouse_reports: list[bytes] = []

    async def connect(self) -> None:
        self._connected = True

    async def disconnect(self) -> None:
        self._connected = False

    def send_keyboard_report(self, report: bytes) -> None:
        self.keyboard_reports.append(report)

    def send_mouse_report(self, report: bytes) -> None:
        self.mouse_reports.append(report)

    @property
    def is_connected(self) -> bool:
        return self._connected


@pytest.fixture
def mock_transport():
    """Provide a MockHIDTransport instance (pre-connected)."""
    t = MockHIDTransport()
    t._connected = True
    return t
