"""Tests for src/jiggler.py — Async mouse jiggler."""

import asyncio
import os
import struct
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

from jiggler import MouseJiggler, _mouse_report
from config import JigglerConfig
from conftest import MockHIDTransport


class TestMouseReport:
    """_mouse_report should produce correctly packed 4-byte reports."""

    def test_zero_movement(self):
        report = _mouse_report(0, 0, 0, 0)
        assert len(report) == 4
        buttons, dx, dy, wheel = struct.unpack("Bbbb", report)
        assert buttons == 0
        assert dx == 0
        assert dy == 0
        assert wheel == 0

    def test_positive_movement(self):
        report = _mouse_report(0, 5, 10, 0)
        _, dx, dy, _ = struct.unpack("Bbbb", report)
        assert dx == 5
        assert dy == 10

    def test_negative_movement(self):
        """Negative values must be correctly signed — this was the original bug."""
        report = _mouse_report(0, -3, -7, 0)
        _, dx, dy, _ = struct.unpack("Bbbb", report)
        assert dx == -3
        assert dy == -7

    def test_clamping_at_bounds(self):
        report = _mouse_report(0, 200, -200, 0)
        _, dx, dy, _ = struct.unpack("Bbbb", report)
        assert dx == 127
        assert dy == -127

    def test_wheel_value(self):
        report = _mouse_report(0, 0, 0, -5)
        _, _, _, wheel = struct.unpack("Bbbb", report)
        assert wheel == -5

    def test_buttons_byte(self):
        report = _mouse_report(0x03, 0, 0, 0)
        assert report[0] == 0x03


class TestJigglerConfig:
    """Jiggler should respect enabled/disabled config."""

    @pytest.mark.asyncio
    async def test_disabled_jiggler_returns_immediately(self):
        cfg = JigglerConfig(enabled=False)
        transport = MockHIDTransport()
        jiggler = MouseJiggler(transport, cfg)

        await jiggler.run()
        assert len(transport.mouse_reports) == 0

    @pytest.mark.asyncio
    async def test_jiggler_sends_paired_reports(self):
        """Each jiggle sends a move + return = 2 reports."""
        cfg = JigglerConfig(
            enabled=True,
            interval_min=0.01,
            interval_max=0.02,
            max_delta=3,
            big_move_chance=0.0,
        )
        transport = MockHIDTransport()
        jiggler = MouseJiggler(transport, cfg)

        task = jiggler.start()
        await asyncio.sleep(0.3)
        jiggler.stop()

        try:
            await task
        except asyncio.CancelledError:
            pass

        assert len(transport.mouse_reports) >= 2

    @pytest.mark.asyncio
    async def test_jiggler_cancellation(self):
        """Jiggler should stop cleanly when cancelled."""
        cfg = JigglerConfig(
            enabled=True,
            interval_min=10.0,
            interval_max=20.0,
        )
        transport = MockHIDTransport()
        jiggler = MouseJiggler(transport, cfg)

        task = jiggler.start()
        await asyncio.sleep(0.05)
        jiggler.stop()

        try:
            await task
        except asyncio.CancelledError:
            pass

        # Completed without error
        assert True


class TestRandomDelta:
    """_random_delta should produce valid movement values."""

    def test_nonzero_movement(self):
        cfg = JigglerConfig(max_delta=3, big_move_chance=0.0)
        transport = MockHIDTransport()
        jiggler = MouseJiggler(transport, cfg)

        for _ in range(100):
            dx, dy = jiggler._random_delta()
            assert not (dx == 0 and dy == 0), "Should never produce zero movement"

    def test_normal_deltas_within_bounds(self):
        cfg = JigglerConfig(max_delta=3, big_move_chance=0.0)
        transport = MockHIDTransport()
        jiggler = MouseJiggler(transport, cfg)

        for _ in range(100):
            dx, dy = jiggler._random_delta()
            assert -3 <= dx <= 3
            assert -3 <= dy <= 3

    def test_big_move_expanded_range(self):
        cfg = JigglerConfig(max_delta=3, big_move_chance=1.0)
        transport = MockHIDTransport()
        jiggler = MouseJiggler(transport, cfg)

        for _ in range(50):
            dx, dy = jiggler._random_delta()
            assert -20 <= dx <= 20
            assert -20 <= dy <= 20
