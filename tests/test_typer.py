"""Tests for src/typer.py — Async LLM typing engine."""

import asyncio
import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

from typer import Typer
from config import TyperConfig, LLMConfig
from llm_client import LLMClient
from conftest import MockHIDTransport


def _make_typer(**typer_overrides):
    """Build a Typer with a MockHIDTransport and mock LLM client."""
    defaults = dict(
        enabled=True,
        interval_min=1,
        interval_max=2,
        cpm_min=220,
        cpm_max=360,
        typo_rate=0.0,
        think_pause_chance=0.0,
        think_pause_secs=[0.01, 0.02],
    )
    defaults.update(typer_overrides)
    cfg = TyperConfig(**defaults)
    llm_cfg = LLMConfig(
        url="http://test:1234",
        api_style="ollama",
        model="test",
        max_tokens=50,
        prompts=["test"],
    )
    transport = MockHIDTransport()
    transport._connected = True
    llm = LLMClient(llm_cfg)
    typer = Typer(transport, llm, cfg)
    return typer, transport, llm


class TestCharDelay:
    """_char_delay should return values consistent with CPM range."""

    def test_delay_within_bounds(self):
        typer, _, _ = _make_typer()
        delays = [typer._char_delay() for _ in range(200)]
        for d in delays:
            assert d >= 0.02, f"Delay {d} below floor"
            assert d < 0.5, f"Delay {d} unreasonably large"

    def test_delay_is_positive(self):
        typer, _, _ = _make_typer()
        for _ in range(100):
            assert typer._char_delay() > 0


class TestTypoGeneratesBackspace:
    """When a typo occurs, the typer should send wrong key, backspace, then correct key."""

    @pytest.mark.asyncio
    async def test_typo_produces_extra_reports(self):
        typer, transport, _ = _make_typer(typo_rate=1.0)

        await typer._type_text("a")

        # With 100% typo rate on 'a' (has neighbors):
        # typo press + release + backspace press + release + correct press + release = 6
        assert len(transport.keyboard_reports) > 2

    @pytest.mark.asyncio
    async def test_no_typo_when_rate_zero(self):
        typer, transport, _ = _make_typer(typo_rate=0.0)

        await typer._type_text("hello")

        # 5 chars * 2 reports each (press + release) = 10
        assert len(transport.keyboard_reports) == 10


class TestThinkPause:
    """Think pauses should be triggered when configured."""

    @pytest.mark.asyncio
    async def test_think_pause_fires(self):
        typer, transport, _ = _make_typer(
            think_pause_chance=1.0,
            think_pause_secs=[0.001, 0.002],
        )

        await typer._type_text("ab")

        # Should still type both characters
        assert len(transport.keyboard_reports) == 4  # 2 chars * (press + release)


class TestTypingSession:
    """Full typing with mock transport — verify reports are sent."""

    @pytest.mark.asyncio
    async def test_type_text_sends_reports(self):
        typer, transport, _ = _make_typer()

        await typer._type_text("Hi")

        # 2 chars * 2 reports (press + release) = 4
        assert len(transport.keyboard_reports) == 4

    @pytest.mark.asyncio
    async def test_empty_text_sends_nothing(self):
        typer, transport, _ = _make_typer()

        await typer._type_text("")

        assert len(transport.keyboard_reports) == 0

    @pytest.mark.asyncio
    async def test_unmapped_char_skipped(self):
        typer, transport, _ = _make_typer()

        await typer._type_text("\x00")

        assert len(transport.keyboard_reports) == 0


class TestTyperDisabled:
    """Typer should return immediately when disabled."""

    @pytest.mark.asyncio
    async def test_disabled_returns_immediately(self):
        typer, transport, _ = _make_typer(enabled=False)

        await typer.run()

        assert len(transport.keyboard_reports) == 0
