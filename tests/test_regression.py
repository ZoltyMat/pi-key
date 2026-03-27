"""Regression / integration tests — end-to-end flows with mocked I/O."""

import asyncio
import os
import sys
from unittest.mock import AsyncMock, MagicMock

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

from config import PikeyConfig, DeviceConfig, JigglerConfig, TyperConfig, LLMConfig
from jiggler import MouseJiggler
from typer import Typer
from llm_client import LLMClient
from conftest import MockHIDTransport


def _full_config(llm_url="http://test:1234"):
    return PikeyConfig(
        device=DeviceConfig(name="Test Device"),
        jiggler=JigglerConfig(
            enabled=True,
            interval_min=0.01,
            interval_max=0.02,
            max_delta=3,
            big_move_chance=0.0,
        ),
        typer=TyperConfig(
            enabled=True,
            interval_min=0.01,
            interval_max=0.02,
            cpm_min=6000,
            cpm_max=12000,
            typo_rate=0.0,
            think_pause_chance=0.0,
            think_pause_secs=[0.001, 0.002],
        ),
        llm=LLMConfig(
            url=llm_url,
            api_style="ollama",
            model="test",
            max_tokens=50,
            prompts=["Write hello world."],
        ),
    )


class TestFullTypingSessionE2E:
    """Config -> LLM client (mocked) -> typer -> transport (mocked)."""

    @pytest.mark.asyncio
    async def test_complete_typing_flow(self):
        cfg = _full_config()
        transport = MockHIDTransport()
        transport._connected = True

        llm = LLMClient(cfg.llm)

        mock_response = MagicMock()
        mock_response.json.return_value = {"response": "hello"}
        mock_response.raise_for_status = MagicMock()

        mock_http = AsyncMock()
        mock_http.post = AsyncMock(return_value=mock_response)
        mock_http.is_closed = False
        llm._client = mock_http

        text = await llm.fetch_text()
        assert text == "hello"

        typer = Typer(transport, llm, cfg.typer)
        await typer._type_text(text)

        # 5 chars * 2 reports (press + release) = 10
        assert len(transport.keyboard_reports) == 10

        await llm.close()


class TestFullJiggleSessionE2E:
    """Config -> jiggler -> transport (mocked), verify mouse reports."""

    @pytest.mark.asyncio
    async def test_jiggle_produces_mouse_reports(self):
        cfg = _full_config()
        transport = MockHIDTransport()
        transport._connected = True

        jiggler = MouseJiggler(transport, cfg.jiggler)

        task = jiggler.start()
        await asyncio.sleep(0.3)
        jiggler.stop()

        try:
            await task
        except asyncio.CancelledError:
            pass

        assert len(transport.mouse_reports) >= 2


class TestModeBothRunsBoth:
    """When both jiggler and typer are enabled, both produce output."""

    @pytest.mark.asyncio
    async def test_both_workers_produce_output(self):
        cfg = _full_config()
        transport = MockHIDTransport()
        transport._connected = True

        jiggler = MouseJiggler(transport, cfg.jiggler)
        jiggle_task = jiggler.start()
        await asyncio.sleep(0.1)
        jiggler.stop()
        try:
            await jiggle_task
        except asyncio.CancelledError:
            pass

        llm = LLMClient(cfg.llm)
        mock_response = MagicMock()
        mock_response.json.return_value = {"response": "hi"}
        mock_response.raise_for_status = MagicMock()
        mock_http = AsyncMock()
        mock_http.post = AsyncMock(return_value=mock_response)
        mock_http.is_closed = False
        llm._client = mock_http

        typer = Typer(transport, llm, cfg.typer)
        text = await llm.fetch_text()
        await typer._type_text(text)

        assert len(transport.mouse_reports) > 0, "Jiggler should produce mouse reports"
        assert len(transport.keyboard_reports) > 0, "Typer should produce keyboard reports"

        await llm.close()


class TestGracefulShutdown:
    """Workers should stop cleanly when cancelled."""

    @pytest.mark.asyncio
    async def test_jiggler_stops_on_cancel(self):
        cfg = _full_config()
        transport = MockHIDTransport()
        transport._connected = True

        jiggler = MouseJiggler(transport, cfg.jiggler)
        task = jiggler.start()
        await asyncio.sleep(0.05)
        jiggler.stop()

        try:
            await task
        except asyncio.CancelledError:
            pass

        assert task.done()

    @pytest.mark.asyncio
    async def test_typer_disabled_returns_immediately(self):
        cfg = _full_config()
        transport = MockHIDTransport()
        transport._connected = True

        llm = LLMClient(cfg.llm)
        typer = Typer(transport, llm, TyperConfig(enabled=False))

        await typer.run()

        assert len(transport.keyboard_reports) == 0
        await llm.close()


class TestConfigVariations:
    """Verify behavior with different config values."""

    def test_different_cpm_affects_delay(self):
        transport = MockHIDTransport()
        llm = LLMClient(LLMConfig(url="http://x", prompts=["test"]))

        slow_typer = Typer(transport, llm, TyperConfig(cpm_min=60, cpm_max=60))
        fast_typer = Typer(transport, llm, TyperConfig(cpm_min=1200, cpm_max=1200))

        slow_delays = [slow_typer._char_delay() for _ in range(50)]
        fast_delays = [fast_typer._char_delay() for _ in range(50)]

        assert sum(slow_delays) / 50 > sum(fast_delays) / 50
