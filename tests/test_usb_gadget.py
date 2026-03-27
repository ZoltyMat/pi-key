"""Tests for src/usb_hid.py — USB gadget transport (mocked filesystem)."""

import os
import sys
from unittest.mock import patch, MagicMock
from pathlib import Path

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

from usb_hid import (
    USBGadgetTransport,
    usb_gadget_available,
    KEYBOARD_REPORT_DESC,
    MOUSE_REPORT_DESC,
    GADGET_BASE,
)


class TestUSBGadgetAvailability:
    """usb_gadget_available() checks for ConfigFS and UDC."""

    @patch("usb_hid.GADGET_BASE")
    @patch("usb_hid.Path")
    def test_not_available_without_configfs(self, mock_path, mock_base):
        mock_base.exists.return_value = False
        assert usb_gadget_available() is False

    def test_not_available_on_macos(self):
        """On macOS, /sys/kernel/config doesn't exist."""
        # This test implicitly verifies behavior on the dev machine
        if sys.platform == "darwin":
            assert usb_gadget_available() is False


class TestReportDescriptors:
    """HID report descriptors should be valid."""

    def test_keyboard_descriptor_not_empty(self):
        assert len(KEYBOARD_REPORT_DESC) > 0

    def test_mouse_descriptor_not_empty(self):
        assert len(MOUSE_REPORT_DESC) > 0

    def test_keyboard_starts_with_usage_page(self):
        """Descriptor should start with Usage Page (Generic Desktop) = 0x05 0x01."""
        assert KEYBOARD_REPORT_DESC[0] == 0x05
        assert KEYBOARD_REPORT_DESC[1] == 0x01

    def test_mouse_starts_with_usage_page(self):
        assert MOUSE_REPORT_DESC[0] == 0x05
        assert MOUSE_REPORT_DESC[1] == 0x01

    def test_keyboard_descriptor_ends_with_end_collection(self):
        """Descriptor should end with End Collection (0xC0)."""
        assert KEYBOARD_REPORT_DESC[-1] == 0xC0

    def test_mouse_descriptor_ends_with_end_collection(self):
        assert MOUSE_REPORT_DESC[-1] == 0xC0


class TestUSBGadgetTransportInit:
    """USBGadgetTransport initialization."""

    def test_starts_disconnected(self):
        t = USBGadgetTransport()
        assert t.is_connected is False

    def test_send_keyboard_raises_when_disconnected(self):
        t = USBGadgetTransport()
        with pytest.raises(RuntimeError, match="not open"):
            t.send_keyboard_report(b"\x00" * 8)

    def test_send_mouse_raises_when_disconnected(self):
        t = USBGadgetTransport()
        with pytest.raises(RuntimeError, match="not open"):
            t.send_mouse_report(b"\x00" * 4)
