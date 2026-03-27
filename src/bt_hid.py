"""bt_hid.py — Bluetooth HID transport implementation.

Registers a BlueZ HID profile via D-Bus and manages L2CAP
control (PSM 17) and interrupt (PSM 19) sockets.
Spoofs as a Logitech K380 with proper SDP record.
"""

from __future__ import annotations

import asyncio
import logging
import socket
import struct
import subprocess
import threading

import dbus
import dbus.mainloop.glib
import dbus.service

from src.config import DeviceConfig
from src.hid_transport import HIDTransport

log = logging.getLogger(__name__)

# ── HID Report Descriptor ────────────────────────────────────────────────────
# Combo keyboard + mouse (boot-compatible) with report IDs
HID_DESCRIPTOR = bytes([
    # ── Keyboard (Report ID 1) ────────────────────────
    0x05, 0x01,        # Usage Page (Generic Desktop)
    0x09, 0x06,        # Usage (Keyboard)
    0xA1, 0x01,        # Collection (Application)
    0x85, 0x01,        #   Report ID (1)
    0x05, 0x07,        #   Usage Page (Key Codes)
    0x19, 0xE0,        #   Usage Minimum (224)
    0x29, 0xE7,        #   Usage Maximum (231)
    0x15, 0x00,        #   Logical Minimum (0)
    0x25, 0x01,        #   Logical Maximum (1)
    0x75, 0x01,        #   Report Size (1)
    0x95, 0x08,        #   Report Count (8)
    0x81, 0x02,        #   Input (Data, Variable, Absolute) — modifier keys
    0x95, 0x01,        #   Report Count (1)
    0x75, 0x08,        #   Report Size (8)
    0x81, 0x03,        #   Input (Constant) — reserved byte
    0x95, 0x06,        #   Report Count (6)
    0x75, 0x08,        #   Report Size (8)
    0x15, 0x00,        #   Logical Minimum (0)
    0x25, 0x65,        #   Logical Maximum (101)
    0x05, 0x07,        #   Usage Page (Key Codes)
    0x19, 0x00,        #   Usage Minimum (0)
    0x29, 0x65,        #   Usage Maximum (101)
    0x81, 0x00,        #   Input (Data, Array) — 6 key slots
    # LED output report
    0x05, 0x08,        #   Usage Page (LEDs)
    0x19, 0x01,        #   Usage Minimum (1)
    0x29, 0x05,        #   Usage Maximum (5)
    0x95, 0x05,        #   Report Count (5)
    0x75, 0x01,        #   Report Size (1)
    0x91, 0x02,        #   Output (Data, Variable, Absolute)
    0x95, 0x01,        #   Report Count (1)
    0x75, 0x03,        #   Report Size (3)
    0x91, 0x03,        #   Output (Constant) — padding
    0xC0,              # End Collection

    # ── Mouse (Report ID 2) ───────────────────────────
    0x05, 0x01,        # Usage Page (Generic Desktop)
    0x09, 0x02,        # Usage (Mouse)
    0xA1, 0x01,        # Collection (Application)
    0x85, 0x02,        #   Report ID (2)
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

# ── SDP Service Record ────────────────────────────────────────────────────────
# Full SDP XML for a HID keyboard+mouse combo, Logitech K380 attributes.
SDP_RECORD = """<?xml version="1.0" encoding="UTF-8" ?>
<record>
  <!-- ServiceClassIDList: HID -->
  <attribute id="0x0001">
    <sequence><uuid value="0x1124"/></sequence>
  </attribute>
  <!-- ProtocolDescriptorList: L2CAP PSM=0x0011, HIDP -->
  <attribute id="0x0004">
    <sequence>
      <sequence><uuid value="0x0100"/><uint16 value="0x0011"/></sequence>
      <sequence><uuid value="0x0011"/></sequence>
    </sequence>
  </attribute>
  <!-- BrowseGroupList -->
  <attribute id="0x0005">
    <sequence><uuid value="0x1002"/></sequence>
  </attribute>
  <!-- LanguageBaseAttributeIDList -->
  <attribute id="0x0006">
    <sequence>
      <uint16 value="0x656e"/>
      <uint16 value="0x006a"/>
      <uint16 value="0x0100"/>
    </sequence>
  </attribute>
  <!-- BluetoothProfileDescriptorList: HID v1.0 -->
  <attribute id="0x0009">
    <sequence>
      <sequence><uuid value="0x1124"/><uint16 value="0x0100"/></sequence>
    </sequence>
  </attribute>
  <!-- AdditionalProtocolDescriptorLists: L2CAP PSM=0x0013 (interrupt) -->
  <attribute id="0x000d">
    <sequence>
      <sequence>
        <sequence><uuid value="0x0100"/><uint16 value="0x0013"/></sequence>
        <sequence><uuid value="0x0011"/></sequence>
      </sequence>
    </sequence>
  </attribute>
  <!-- ServiceName -->
  <attribute id="0x0100">
    <text value="Logitech K380 Multi-Device Keyboard"/>
  </attribute>
  <!-- ServiceDescription -->
  <attribute id="0x0101">
    <text value="Logitech"/>
  </attribute>
  <!-- ProviderName -->
  <attribute id="0x0102">
    <text value="K380"/>
  </attribute>
  <!-- HIDParserVersion -->
  <attribute id="0x0200"><uint16 value="0x0100"/></attribute>
  <!-- HIDDeviceSubclass: keyboard (0x40) + pointing (0x80) = 0xC0 -->
  <attribute id="0x0201"><uint16 value="0x0111"/></attribute>
  <!-- HIDCountryCode -->
  <attribute id="0x0202"><uint8 value="0x00"/></attribute>
  <!-- HIDVirtualCable -->
  <attribute id="0x0203"><boolean value="true"/></attribute>
  <!-- HIDReconnectInitiate -->
  <attribute id="0x0204"><boolean value="true"/></attribute>
  <!-- HIDDescriptorList -->
  <attribute id="0x0206">
    <sequence>
      <sequence>
        <uint8 value="0x22"/>
        <text encoding="hex" value="{HID_DESC_HEX}"/>
      </sequence>
    </sequence>
  </attribute>
  <!-- HIDLANGIDBaseList -->
  <attribute id="0x0207">
    <sequence>
      <sequence><uint16 value="0x0409"/><uint16 value="0x0100"/></sequence>
    </sequence>
  </attribute>
  <!-- HIDSDPDisable -->
  <attribute id="0x0208"><boolean value="false"/></attribute>
  <!-- HIDBatteryPower -->
  <attribute id="0x0209"><boolean value="true"/></attribute>
  <!-- HIDRemoteWake -->
  <attribute id="0x020a"><boolean value="true"/></attribute>
  <!-- HIDProfileVersion -->
  <attribute id="0x020b"><uint16 value="0x0100"/></attribute>
  <!-- HIDSupervisionTimeout -->
  <attribute id="0x020c"><uint16 value="0x0c80"/></attribute>
  <!-- HIDNormallyConnectable -->
  <attribute id="0x020d"><boolean value="true"/></attribute>
  <!-- HIDBootDevice -->
  <attribute id="0x020e"><boolean value="true"/></attribute>
  <!-- HIDSSRHostMaxLatency -->
  <attribute id="0x020f"><uint16 value="0x0640"/></attribute>
  <!-- HIDSSRHostMinTimeout -->
  <attribute id="0x0210"><uint16 value="0x0320"/></attribute>
</record>""".replace("{HID_DESC_HEX}", HID_DESCRIPTOR.hex())


class BluetoothHIDTransport(HIDTransport):
    """Bluetooth HID transport using BlueZ D-Bus + L2CAP sockets."""

    PSM_CTRL = 0x0011  # HID Control
    PSM_INTR = 0x0013  # HID Interrupt

    def __init__(self, device_cfg: DeviceConfig) -> None:
        self.device_cfg = device_cfg
        self._ctrl_sock: socket.socket | None = None
        self._intr_sock: socket.socket | None = None
        self._lock = threading.Lock()
        self._connected = False

    async def connect(self) -> None:
        """Register HID profile, then connect or listen."""
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, self._setup_profile)

        target = self.device_cfg.target_mac.strip()
        if target:
            log.info("Connecting to paired host: %s", target)
            await loop.run_in_executor(None, self._connect_to, target)
        else:
            await loop.run_in_executor(None, self._make_discoverable)
            log.info("Waiting for incoming HID connection...")
            await loop.run_in_executor(None, self._listen)

    async def disconnect(self) -> None:
        """Close L2CAP sockets."""
        self._connected = False
        for sock in (self._intr_sock, self._ctrl_sock):
            if sock:
                try:
                    sock.close()
                except OSError:
                    pass
        self._intr_sock = None
        self._ctrl_sock = None
        log.info("Bluetooth HID disconnected")

    def send_keyboard_report(self, report: bytes) -> None:
        """Send keyboard report with report ID 1 over interrupt channel."""
        # Prepend report ID 1
        self._send(struct.pack("B", 0x01) + report)

    def send_mouse_report(self, report: bytes) -> None:
        """Send mouse report with report ID 2 over interrupt channel."""
        # Prepend report ID 2
        self._send(struct.pack("B", 0x02) + report)

    @property
    def is_connected(self) -> bool:
        return self._connected

    # ── Internal ──────────────────────────────────────────────────────────────

    def _send(self, data: bytes) -> None:
        if not self._connected or not self._intr_sock:
            raise RuntimeError("Bluetooth HID not connected")
        with self._lock:
            # 0xA1 = HID interrupt DATA INPUT
            self._intr_sock.send(b"\xa1" + data)

    def _setup_profile(self) -> None:
        dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
        bus = dbus.SystemBus()
        manager = dbus.Interface(
            bus.get_object("org.bluez", "/org/bluez"),
            "org.bluez.ProfileManager1",
        )
        opts = {
            "AutoConnect": dbus.Boolean(True),
            "ServiceRecord": SDP_RECORD,
            "Role": "server",
            "RequireAuthentication": dbus.Boolean(False),
            "RequireAuthorization": dbus.Boolean(False),
        }
        manager.RegisterProfile(
            "/org/pikey/hid",
            "00001124-0000-1000-8000-00805f9b34fb",
            opts,
        )
        log.info("HID profile registered with BlueZ")

    def _make_discoverable(self) -> None:
        subprocess.run(["bluetoothctl", "discoverable", "on"], check=True)
        subprocess.run(["bluetoothctl", "pairable", "on"], check=True)
        log.info("Device discoverable as '%s'", self.device_cfg.name)

    def _listen(self) -> None:
        ctrl_server = socket.socket(
            socket.AF_BLUETOOTH, socket.SOCK_SEQPACKET, socket.BTPROTO_L2CAP
        )
        ctrl_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        ctrl_server.bind(("", self.PSM_CTRL))
        ctrl_server.listen(1)

        intr_server = socket.socket(
            socket.AF_BLUETOOTH, socket.SOCK_SEQPACKET, socket.BTPROTO_L2CAP
        )
        intr_server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        intr_server.bind(("", self.PSM_INTR))
        intr_server.listen(1)

        self._ctrl_sock, ctrl_addr = ctrl_server.accept()
        log.info("Control channel connected from %s", ctrl_addr)

        self._intr_sock, intr_addr = intr_server.accept()
        log.info("Interrupt channel connected from %s", intr_addr)

        self._connected = True
        log.info("Bluetooth HID fully connected")

    def _connect_to(self, mac: str) -> None:
        self._ctrl_sock = socket.socket(
            socket.AF_BLUETOOTH, socket.SOCK_SEQPACKET, socket.BTPROTO_L2CAP
        )
        self._ctrl_sock.connect((mac, self.PSM_CTRL))

        self._intr_sock = socket.socket(
            socket.AF_BLUETOOTH, socket.SOCK_SEQPACKET, socket.BTPROTO_L2CAP
        )
        self._intr_sock.connect((mac, self.PSM_INTR))

        self._connected = True
        log.info("Connected to %s", mac)
