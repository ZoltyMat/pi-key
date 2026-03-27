#!/bin/bash
# setup.sh — PiKey system configuration
# Run as root on a fresh Raspberry Pi OS Lite install
#
# Usage:
#   sudo ./setup.sh          # Bluetooth only
#   sudo ./setup.sh --usb    # Bluetooth + USB gadget mode

set -euo pipefail

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[+]${NC} $1"; }
warn()  { echo -e "${YELLOW}[!]${NC} $1"; }
error() { echo -e "${RED}[x]${NC} $1"; exit 1; }

SETUP_USB=false
for arg in "$@"; do
    case "$arg" in
        --usb) SETUP_USB=true ;;
        --help|-h)
            echo "Usage: sudo ./setup.sh [--usb]"
            echo "  --usb    Enable USB gadget mode (Pi Zero 2W / Pi 4 only)"
            exit 0
            ;;
    esac
done

[[ $EUID -ne 0 ]] && error "Run as root: sudo ./setup.sh"

info "Updating system packages..."
apt-get update -qq
apt-get install -y --no-install-recommends \
    python3 python3-pip python3-dbus \
    bluez bluez-tools \
    python3-gi gobject-introspection \
    git curl wget

info "Configuring BlueZ for HID + experimental mode..."
cat > /etc/bluetooth/main.conf << 'EOF'
[General]
Name = Logitech K380 Multi-Device Keyboard
Class = 0x002540
DiscoverableTimeout = 0
Experimental = true
KernelExperimental = true

[Policy]
AutoEnable = true
EOF

info "Enabling BlueZ experimental D-Bus plugins..."
# Patch the service unit to add --experimental flag
sed -i 's|ExecStart=/usr/lib/bluetooth/bluetoothd|ExecStart=/usr/lib/bluetooth/bluetoothd --experimental --noplugin=sap|' \
    /lib/systemd/system/bluetooth.service

info "Installing Python dependencies..."
pip3 install --break-system-packages \
    pydbus \
    pyyaml \
    httpx \
    click \
    rich

# ── USB Gadget Mode Setup ─────────────────────────────────────────────────────
if $SETUP_USB; then
    info "Setting up USB gadget mode (dwc2 + libcomposite)..."

    # Enable dwc2 overlay in /boot/config.txt (or /boot/firmware/config.txt)
    BOOT_CONFIG=""
    if [[ -f /boot/firmware/config.txt ]]; then
        BOOT_CONFIG="/boot/firmware/config.txt"
    elif [[ -f /boot/config.txt ]]; then
        BOOT_CONFIG="/boot/config.txt"
    else
        warn "Cannot find /boot/config.txt — skipping dtoverlay setup"
    fi

    if [[ -n "$BOOT_CONFIG" ]]; then
        if ! grep -q "^dtoverlay=dwc2" "$BOOT_CONFIG"; then
            info "Adding dtoverlay=dwc2 to $BOOT_CONFIG"
            echo "" >> "$BOOT_CONFIG"
            echo "# PiKey USB gadget mode" >> "$BOOT_CONFIG"
            echo "dtoverlay=dwc2" >> "$BOOT_CONFIG"
        else
            info "dtoverlay=dwc2 already present in $BOOT_CONFIG"
        fi
    fi

    # Add dwc2 to /etc/modules if not already present
    if ! grep -q "^dwc2" /etc/modules 2>/dev/null; then
        echo "dwc2" >> /etc/modules
    fi
    if ! grep -q "^libcomposite" /etc/modules 2>/dev/null; then
        echo "libcomposite" >> /etc/modules
    fi

    # Try loading modules now (will fail if not on OTG-capable hardware)
    modprobe dwc2 2>/dev/null || warn "dwc2 module not available — will load after reboot on OTG hardware"
    modprobe libcomposite 2>/dev/null || warn "libcomposite module not available — will load after reboot"

    info "USB gadget mode configured. Reboot required for dtoverlay to take effect."
fi

info "Installing Ollama (optional — skip if using remote)..."
if ! command -v ollama &>/dev/null; then
    read -rp "Install Ollama locally on this Pi? [y/N] " yn
    if [[ $yn == [Yy]* ]]; then
        curl -fsSL https://ollama.ai/install.sh | sh
        warn "Pull a model after boot: ollama pull llama3.2:1b"
        warn "(Use llama3.2:1b or phi3.5 — smallest models for Pi)"
    fi
fi

info "Creating systemd service..."
PIKEY_DIR="$(cd "$(dirname "$0")" && pwd)"
cat > /etc/systemd/system/pikey.service << EOF
[Unit]
Description=PiKey BT HID Service
After=bluetooth.service
Requires=bluetooth.service

[Service]
Type=simple
User=root
WorkingDirectory=${PIKEY_DIR}
ExecStart=/usr/bin/python3 -m src.main --mode both --transport auto
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

info "Reloading systemd..."
systemctl daemon-reload
systemctl enable bluetooth
systemctl restart bluetooth

info "Copying example config..."
[[ ! -f "${PIKEY_DIR}/config.yaml" ]] && \
    cp "${PIKEY_DIR}/config.example.yaml" "${PIKEY_DIR}/config.yaml"

echo ""
echo -e "${GREEN}=== Setup complete ===${NC}"
echo ""
echo "Next steps:"
echo "  1. Edit config.yaml (set llm.url, target MAC, etc.)"
echo "  2. Pair with target machine: see PAIRING.md"
echo "  3. python3 -m src.main --mode both --transport auto"
echo "  4. Or: systemctl start pikey"
if $SETUP_USB; then
    echo ""
    echo "  USB gadget mode: reboot first, then connect Pi to target via USB."
fi
