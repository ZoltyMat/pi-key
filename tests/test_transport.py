"""Tests for src/hid_transport.py — Abstract transport interface."""

import os
import sys
from abc import ABC

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

from hid_transport import HIDTransport
from conftest import MockHIDTransport


class TestHIDTransportABC:
    """HIDTransport should be an abstract base class."""

    def test_is_abstract(self):
        assert issubclass(HIDTransport, ABC)

    def test_cannot_instantiate(self):
        with pytest.raises(TypeError):
            HIDTransport()

    def test_required_methods(self):
        abstract_methods = HIDTransport.__abstractmethods__
        assert "connect" in abstract_methods
        assert "disconnect" in abstract_methods
        assert "send_keyboard_report" in abstract_methods
        assert "send_mouse_report" in abstract_methods
        assert "is_connected" in abstract_methods

    def test_concrete_subclass_works(self):
        class TestTransport(HIDTransport):
            async def connect(self):
                pass

            async def disconnect(self):
                pass

            def send_keyboard_report(self, report):
                pass

            def send_mouse_report(self, report):
                pass

            @property
            def is_connected(self):
                return True

        t = TestTransport()
        assert t.is_connected is True


class TestMockTransport:
    """MockHIDTransport should record all reports."""

    def test_records_keyboard_reports(self):
        t = MockHIDTransport()
        t.send_keyboard_report(b"\x00" * 8)
        t.send_keyboard_report(b"\x02\x00\x04" + b"\x00" * 5)

        assert len(t.keyboard_reports) == 2
        assert t.keyboard_reports[0] == b"\x00" * 8

    def test_records_mouse_reports(self):
        t = MockHIDTransport()
        t.send_mouse_report(b"\x00\x05\xfd\x00")  # buttons=0, dx=5, dy=-3, wheel=0

        assert len(t.mouse_reports) == 1

    @pytest.mark.asyncio
    async def test_connect_disconnect(self):
        t = MockHIDTransport()
        assert t.is_connected is False

        await t.connect()
        assert t.is_connected is True

        await t.disconnect()
        assert t.is_connected is False
