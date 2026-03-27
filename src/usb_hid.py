"""usb_hid.py — USB gadget HID transport implementation.

Configures a Linux USB gadget via ConfigFS to present as a
Logitech K380 keyboard+mouse combo. Writes HID reports to
/dev/hidg0 (keyboard) and /dev/hidg1 (mouse).

Requires root and the dwc2 overlay enabled on the Pi.
"""

from __future__ import annotations

import asyncio
import logging
import os
from pathlib import Path

from src.hid_transport import HIDTransport

log = logging.getLogger(__name__)

GADGET_BASE = Path("/sys/kernel/config/usb_gadget")
GADGET_NAME = "pikey"
GADGET_PATH = GADGET_BASE / GADGET_NAME

# Logitech K380 identifiers
ID_VENDOR = "0x046d"
ID_PRODUCT = "0xb342"
BCD_DEVICE = "0x0100"
BCD_USB = "0x0200"
SERIAL = "PK000001"
MANUFACTURER = "Logitech"
PRODUCT = "K380 Multi-Device Keyboard"

# Standard boot-protocol keyboard HID report descriptor (8-byte reports)
KEYBOARD_REPORT_DESC = bytes([
    0x05, 0x01,        # Usage Page (Generic Desktop)
    0x09, 0x06,        # Usage (Keyboard)
    0xA1, 0x01,        # Collection (Application)
    0x05, 0x07,        #   Usage Page (Key Codes)
    0x19, 0xE0,        #   Usage Minimum (224)
    0x29, 0xE7,        #   Usage Maximum (231)
    0x15, 0x00,        #   Logical Minimum (0)
    0x25, 0x01,        #   Logical Maximum (1)
    0x75, 0x01,        #   Report Size (1)
    0x95, 0x08,        #   Report Count (8)
    0x81, 0x02,        #   Input (Data, Variable, Absolute) — modifiers
    0x95, 0x01,        #   Report Count (1)
    0x75, 0x08,        #   Report Size (8)
    0x81, 0x03,        #   Input (Constant) — reserved
    0x95, 0x05,        #   Report Count (5)
    0x75, 0x01,        #   Report Size (1)
    0x05, 0x08,        #   Usage Page (LEDs)
    0x19, 0x01,        #   Usage Minimum (1)
    0x29, 0x05,        #   Usage Maximum (5)
    0x91, 0x02,        #   Output (Data, Variable, Absolute) — LEDs
    0x95, 0x01,        #   Report Count (1)
    0x75, 0x03,        #   Report Size (3)
    0x91, 0x03,        #   Output (Constant) — LED padding
    0x95, 0x06,        #   Report Count (6)
    0x75, 0x08,        #   Report Size (8)
    0x15, 0x00,        #   Logical Minimum (0)
    0x25, 0x65,        #   Logical Maximum (101)
    0x05, 0x07,        #   Usage Page (Key Codes)
    0x19, 0x00,        #   Usage Minimum (0)
    0x29, 0x65,        #   Usage Maximum (101)
    0x81, 0x00,        #   Input (Data, Array) — key slots
    0xC0,              # End Collection
])

# Standard relative mouse HID report descriptor (4-byte reports)
MOUSE_REPORT_DESC = bytes([
    0x05, 0x01,        # Usage Page (Generic Desktop)
    0x09, 0x02,        # Usage (Mouse)
    0xA1, 0x01,        # Collection (Application)
    0x09, 0x01,        #   Usage (Pointer)
    0xA1, 0x00,        #   Collection (Physical)
    0x05, 0x09,        #     Usage Page (Button)
    0x19, 0x01,        #     Usage Minimum (1)
    0x29, 0x03,        #     Usage Maximum (3)
    0x15, 0x00,        #     Logical Minimum (0)
    0x25, 0x01,        #     Logical Maximum (1)
    0x95, 0x03,        #     Report Count (3)
    0x75, 0x01,        #     Report Size (1)
    0x81, 0x02,        #     Input (Data, Variable, Absolute) — buttons
    0x95, 0x01,        #     Report Count (1)
    0x75, 0x05,        #     Report Size (5)
    0x81, 0x03,        #     Input (Constant) — padding
    0x05, 0x01,        #     Usage Page (Generic Desktop)
    0x09, 0x30,        #     Usage (X)
    0x09, 0x31,        #     Usage (Y)
    0x09, 0x38,        #     Usage (Wheel)
    0x15, 0x81,        #     Logical Minimum (-127)
    0x25, 0x7F,        #     Logical Maximum (127)
    0x75, 0x08,        #     Report Size (8)
    0x95, 0x03,        #     Report Count (3)
    0x81, 0x06,        #     Input (Data, Variable, Relative)
    0xC0,              #   End Collection
    0xC0,              # End Collection
])

# Device paths for writing HID reports
KEYBOARD_DEV = "/dev/hidg0"
MOUSE_DEV = "/dev/hidg1"


def _write(path: str | Path, value: str) -> None:
    """Write a string to a sysfs/configfs file."""
    Path(path).write_text(value)


def _write_bytes(path: str | Path, data: bytes) -> None:
    """Write raw bytes to a configfs file."""
    Path(path).write_bytes(data)


class USBGadgetTransport(HIDTransport):
    """USB OTG gadget HID transport via ConfigFS."""

    def __init__(self) -> None:
        self._kb_fd: int | None = None
        self._mouse_fd: int | None = None
        self._connected = False

    async def connect(self) -> None:
        """Configure the USB gadget and open device files."""
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, self._setup_gadget)
        await loop.run_in_executor(None, self._open_devices)
        self._connected = True
        log.info("USB gadget HID connected")

    async def disconnect(self) -> None:
        """Close device files and tear down gadget."""
        self._connected = False
        for fd in (self._kb_fd, self._mouse_fd):
            if fd is not None:
                try:
                    os.close(fd)
                except OSError:
                    pass
        self._kb_fd = None
        self._mouse_fd = None

        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, self._teardown_gadget)
        log.info("USB gadget torn down")

    def send_keyboard_report(self, report: bytes) -> None:
        """Write 8-byte keyboard report to /dev/hidg0."""
        if self._kb_fd is None:
            raise RuntimeError("Keyboard device not open")
        os.write(self._kb_fd, report)

    def send_mouse_report(self, report: bytes) -> None:
        """Write 4-byte mouse report to /dev/hidg1."""
        if self._mouse_fd is None:
            raise RuntimeError("Mouse device not open")
        os.write(self._mouse_fd, report)

    @property
    def is_connected(self) -> bool:
        return self._connected

    # ── Internal ──────────────────────────────────────────────────────────────

    def _open_devices(self) -> None:
        self._kb_fd = os.open(KEYBOARD_DEV, os.O_WRONLY | os.O_NONBLOCK)
        self._mouse_fd = os.open(MOUSE_DEV, os.O_WRONLY | os.O_NONBLOCK)

    def _setup_gadget(self) -> None:
        """Create USB gadget configuration via ConfigFS."""
        if not GADGET_BASE.exists():
            raise RuntimeError(
                "ConfigFS USB gadget not available. "
                "Enable dwc2 overlay and load libcomposite."
            )

        # Clean up any previous instance
        if GADGET_PATH.exists():
            self._teardown_gadget()

        log.info("Setting up USB gadget at %s", GADGET_PATH)
        GADGET_PATH.mkdir(exist_ok=True)

        # Device identifiers (Logitech K380)
        _write(GADGET_PATH / "idVendor", ID_VENDOR)
        _write(GADGET_PATH / "idProduct", ID_PRODUCT)
        _write(GADGET_PATH / "bcdDevice", BCD_DEVICE)
        _write(GADGET_PATH / "bcdUSB", BCD_USB)
        _write(GADGET_PATH / "bDeviceClass", "0x00")
        _write(GADGET_PATH / "bDeviceSubClass", "0x00")
        _write(GADGET_PATH / "bDeviceProtocol", "0x00")

        # Strings
        strings = GADGET_PATH / "strings" / "0x409"
        strings.mkdir(parents=True, exist_ok=True)
        _write(strings / "serialnumber", SERIAL)
        _write(strings / "manufacturer", MANUFACTURER)
        _write(strings / "product", PRODUCT)

        # Configuration
        config = GADGET_PATH / "configs" / "c.1"
        config.mkdir(parents=True, exist_ok=True)
        config_strings = config / "strings" / "0x409"
        config_strings.mkdir(parents=True, exist_ok=True)
        _write(config_strings / "configuration", "PiKey HID Config")
        _write(config / "MaxPower", "100")

        # HID function 0: keyboard
        kb_func = GADGET_PATH / "functions" / "hid.keyboard"
        kb_func.mkdir(parents=True, exist_ok=True)
        _write(kb_func / "protocol", "1")       # keyboard boot protocol
        _write(kb_func / "subclass", "1")        # boot interface subclass
        _write(kb_func / "report_length", "8")
        _write_bytes(kb_func / "report_desc", KEYBOARD_REPORT_DESC)

        # HID function 1: mouse
        mouse_func = GADGET_PATH / "functions" / "hid.mouse"
        mouse_func.mkdir(parents=True, exist_ok=True)
        _write(mouse_func / "protocol", "2")     # mouse boot protocol
        _write(mouse_func / "subclass", "1")      # boot interface subclass
        _write(mouse_func / "report_length", "4")
        _write_bytes(mouse_func / "report_desc", MOUSE_REPORT_DESC)

        # Symlink functions into config
        kb_link = config / "hid.keyboard"
        if not kb_link.exists():
            kb_link.symlink_to(kb_func)
        mouse_link = config / "hid.mouse"
        if not mouse_link.exists():
            mouse_link.symlink_to(mouse_func)

        # Bind to UDC
        udc_list = list(Path("/sys/class/udc").iterdir())
        if not udc_list:
            raise RuntimeError(
                "No UDC found. Ensure dwc2 overlay is enabled: "
                "dtoverlay=dwc2 in /boot/config.txt"
            )
        udc_name = udc_list[0].name
        _write(GADGET_PATH / "UDC", udc_name)
        log.info("USB gadget bound to UDC: %s", udc_name)

    def _teardown_gadget(self) -> None:
        """Remove the USB gadget configuration."""
        if not GADGET_PATH.exists():
            return

        log.debug("Tearing down USB gadget")
        try:
            # Unbind from UDC
            udc_file = GADGET_PATH / "UDC"
            if udc_file.exists():
                _write(udc_file, "")

            # Remove symlinks from config
            config = GADGET_PATH / "configs" / "c.1"
            if config.exists():
                for link in config.iterdir():
                    if link.is_symlink():
                        link.unlink()
                # Remove config strings
                config_strings = config / "strings" / "0x409"
                if config_strings.exists():
                    config_strings.rmdir()
                config.rmdir()

            # Remove functions
            functions = GADGET_PATH / "functions"
            if functions.exists():
                for func in functions.iterdir():
                    func.rmdir()

            # Remove strings
            strings = GADGET_PATH / "strings" / "0x409"
            if strings.exists():
                strings.rmdir()

            GADGET_PATH.rmdir()
        except OSError as e:
            log.warning("Gadget teardown incomplete: %s", e)


def usb_gadget_available() -> bool:
    """Check if USB gadget mode is available on this system."""
    return (
        GADGET_BASE.exists()
        and Path("/sys/class/udc").exists()
        and len(list(Path("/sys/class/udc").iterdir())) > 0
    )
