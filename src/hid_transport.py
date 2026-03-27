"""hid_transport.py — Abstract HID transport interface."""

from __future__ import annotations

from abc import ABC, abstractmethod


class HIDTransport(ABC):
    """Abstract base for HID transport backends (Bluetooth or USB gadget)."""

    @abstractmethod
    async def connect(self) -> None:
        """Establish the HID connection."""

    @abstractmethod
    async def disconnect(self) -> None:
        """Tear down the HID connection."""

    @abstractmethod
    def send_keyboard_report(self, report: bytes) -> None:
        """Send an 8-byte keyboard HID report."""

    @abstractmethod
    def send_mouse_report(self, report: bytes) -> None:
        """Send a 4-byte mouse HID report (buttons, dx, dy, wheel)."""

    @property
    @abstractmethod
    def is_connected(self) -> bool:
        """Whether the transport is currently connected."""
