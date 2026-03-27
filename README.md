# PiKey — BT HID Spoofer + LLM Auto-Typer

<p align="center">
  <a href="https://github.com/ZoltyMat/pi-key/actions/workflows/test.yml"><img src="https://img.shields.io/github/actions/workflow/status/ZoltyMat/pi-key/test.yml?branch=main&label=tests&style=flat-square&logo=github" alt="Tests"></a>
  <a href="https://github.com/ZoltyMat/pi-key/blob/main/LICENSE"><img src="https://img.shields.io/github/license/ZoltyMat/pi-key?style=flat-square" alt="License"></a>
  <img src="https://img.shields.io/badge/platform-Raspberry%20Pi-c51a4a?style=flat-square&logo=raspberrypi&logoColor=white" alt="Raspberry Pi">
  <img src="https://img.shields.io/badge/python-3.11%2B-3776ab?style=flat-square&logo=python&logoColor=white" alt="Python 3.11+">
  <img src="https://img.shields.io/badge/rust-1.75%2B-dea584?style=flat-square&logo=rust&logoColor=white" alt="Rust">
  <img src="https://img.shields.io/badge/C-c99-555555?style=flat-square&logo=c&logoColor=white" alt="C99">
</p>

> **Inspired by a thread on [r/overemployed](https://www.reddit.com/r/overemployed/s/S4Q1bJTxUp):**
>
> **[`u/thr0waway12324`](https://www.reddit.com/user/thr0waway12324)** (62 upvotes):
> *"Every time I see this, I will pose the same suggestion: someone please create a jetson nano
> or raspberry pi type of microcontroller that will use its Bluetooth to present as a keyboard +
> mouse. Run a local LLM that can auto type any type of text (novels, code, random notes, etc).
> And then sell this. I would buy it. Many others would too. Any technical/entrepreneurial people,
> please pick this up. I would but I literally just don't have the time."*
>
> — [Original thread](https://www.reddit.com/r/overemployed/s/S4Q1bJTxUp)

Consider this the answer to that call. PiKey types things that aren't smart — and that's exactly the point.

---

A Raspberry Pi that presents as a **Logitech K380 Bluetooth keyboard+mouse**, jiggles the mouse to prevent idle detection, and uses an LLM to type realistic-looking text — code snippets, Slack messages, commit messages, meeting notes — as if a human were working.

## How It Works

```
                    ┌────────────────────────────┐
                    │       Raspberry Pi          │
                    │                             │
                    │  ┌─────────┐ ┌──────────┐  │
                    │  │ Jiggler │ │  Typer   │  │
                    │  │ (mouse) │ │  (keys)  │  │
                    │  └────┬────┘ └────┬─────┘  │
                    │       └─────┬─────┘        │
                    │             │               │
                    │      HIDTransport           │
                    │       ┌─────┴─────┐        │
                    │       │           │        │
                    │  ┌────▼───┐ ┌─────▼────┐   │
                    │  │  BT    │ │   USB    │   │
                    │  │(BlueZ) │ │(ConfigFS)│   │
                    │  └───┬────┘ └────┬─────┘   │
                    │      │           │         │
                    │  ┌───▼───────────▼─────┐   │
                    │  │    LLM Endpoint     │   │
                    │  │ (Ollama / OpenAI /  │   │
                    │  │  LiteLLM / any)     │   │
                    │  └─────────────────────┘   │
                    └──────┬──────────┬──────────┘
                           │          │
                  Bluetooth│          │USB cable
                           ▼          ▼
                    ┌─────────────────────┐
                    │   Target Machine    │
                    │                     │
                    │  Sees: "Logitech    │
                    │    K380 Keyboard"   │
                    └─────────────────────┘
```

The target machine sees a **standard Logitech K380** — no drivers needed, no software to install, nothing suspicious in Device Manager. Just a keyboard that happens to type when nobody's touching it.

## Hardware

| Board | Bluetooth | USB Gadget | Recommendation |
| --- | --- | --- | --- |
| **Pi Zero 2W** | ✓ | ✓ | **Best choice** — $15, tiny, both transports |
| Pi 4 | ✓ | ✓ | Works, overkill for this |
| Pi 3B/3B+ | ✓ | ✗ | BT only — no USB OTG |
| Pi 5 | ✓ | ✗ | BT only — no USB OTG |

**You also need:**
- A micro SD card (8GB+) with Raspberry Pi OS Lite (64-bit)
- An LLM endpoint (Ollama on your network, OpenAI API key, or any OpenAI-compatible server)
- For USB mode: a data USB cable (not charge-only)

## Quick Start

### 1. Set up the Pi

```bash
# Flash Raspberry Pi OS Lite (64-bit) to SD card
# Enable SSH during flashing (use Raspberry Pi Imager)
# Boot, SSH in, then:

git clone https://github.com/ZoltyMat/pi-key.git
cd pi-key
sudo ./setup.sh            # Bluetooth only
# or
sudo ./setup.sh --usb      # Bluetooth + USB gadget mode
```

### 2. Configure

```bash
cp config.example.yaml config.yaml
nano config.yaml
```

The config has four sections:

| Section | What to set | Required? |
| --- | --- | --- |
| `device` | Name, target MAC address | MAC optional (leave blank to wait for pairing) |
| `jiggler` | Jiggle intervals, movement size | Has sensible defaults |
| `typer` | Typing speed (CPM), typo rate, pause frequency | Has sensible defaults |
| `llm` | **URL**, API style, model, API key, prompts | **Yes — URL is required for typing mode** |

**Minimum viable config** — set just these two fields:

```yaml
llm:
  url: "http://your-ollama-host:11434"   # or any OpenAI-compatible endpoint
  api_style: "ollama"                     # "ollama" or "openai"
```

### 3. Pair (Bluetooth mode)

```bash
bluetoothctl
  power on
  agent on
  default-agent
  discoverable on
  pairable on
```

On the target machine, open Bluetooth settings and pair with **"Logitech K380 Multi-Device Keyboard"**. Accept any PIN prompt with `0000`.

See [PAIRING.md](PAIRING.md) for detailed instructions per OS.

### 4. Run

```bash
# Bluetooth (default)
python3 -m src.main --mode both

# USB gadget (connect Pi via USB cable first)
python3 -m src.main --mode both --transport usb

# Auto-detect (tries USB, falls back to BT)
python3 -m src.main --mode both --transport auto

# Mouse jiggler only (no LLM needed)
python3 -m src.main --mode jiggle
```

### 5. Run as a service (optional)

`setup.sh` creates a systemd service. Enable it to start on boot:

```bash
sudo systemctl enable pikey
sudo systemctl start pikey
# Check status
sudo systemctl status pikey
```

## Transports

PiKey supports two ways to deliver HID reports to the target:

### Bluetooth (`--transport bt`)

The default. The Pi pairs wirelessly and appears as a Bluetooth keyboard+mouse. Uses BlueZ D-Bus for SDP registration and raw L2CAP sockets (PSM 17 control, PSM 19 interrupt) for HID reports.

**Pros:** Wireless, no cable, works from across the room.
**Cons:** Some corporate machines block Bluetooth pairing.

### USB Gadget (`--transport usb`)

The Pi connects via USB cable and uses Linux's USB gadget framework (dwc2 + ConfigFS) to present as a wired USB HID device. Writes directly to `/dev/hidg0` (keyboard) and `/dev/hidg1` (mouse).

**Pros:** Works even when Bluetooth is blocked. No pairing needed.
**Cons:** Requires a USB cable. Only works on Pi Zero 2W and Pi 4 (OTG-capable USB).

**Setup:**

```bash
sudo ./setup.sh --usb   # Enables dwc2 overlay + libcomposite
sudo reboot              # Required for dtoverlay to take effect
```

Then connect the Pi's **data** USB port to the target machine.

## How the Typing Looks

PiKey doesn't just dump text — it simulates a human:

- **Variable speed**: Types at 220–360 CPM (configurable), with gaussian jitter so keystrokes aren't metronomically even
- **Typos + corrections**: 2% of keys are "mistyped" with a neighboring QWERTY key, followed by backspace and the correct key
- **Think pauses**: 5% chance of a 1.5–4 second pause mid-sentence, as if thinking
- **Realistic content**: The LLM generates context-appropriate text — code, Slack messages, git commits, meeting notes — not lorem ipsum

The prompts are configurable in `config.yaml`. The defaults produce text that looks like a developer working:

```yaml
prompts:
  - "Write a realistic Python function with a docstring and comments."
  - "Write a short Slack message to a teammate about a code review."
  - "Write 3 bullet points of notes from a fictitious engineering standup."
  - "Write a realistic git commit message and a 2-sentence description."
  - "Write a brief internal Jira comment about a bug fix in plain language."
```

## CLI Reference

```
Usage: python3 -m src.main [OPTIONS]

Options:
  --mode [jiggle|type|both]     Operating mode (default: both)
  --config PATH                 Config file path (default: config.yaml)
  --transport [bt|usb|auto]     HID transport (default: auto)
  --verbose                     Enable debug logging
  --help                        Show help
```

## Logitech Spoofing

The device advertises as a real Logitech product:

| Property | Value |
| --- | --- |
| Name | `Logitech K380 Multi-Device Keyboard` |
| Class of Device | `0x002540` (Peripheral, Keyboard) |
| Vendor ID | `0x046d` (Logitech) |
| Product ID | `0xb342` (K380) |
| HID Descriptor | Standard combo keyboard + mouse (boot-compatible) |

Most operating systems load the generic HID driver — no Logitech software needed. The device looks completely normal in Bluetooth/USB device lists.

## Three Languages, One Recommendation

This repo contains implementations in Python, Rust, and C. **You only need one.** They all read the same `config.yaml`, send the same HID reports, and produce the same result on the target machine.

Three languages were written because they were requested, not because they were needed. The HID protocol is simple — 8-byte keyboard reports and 4-byte mouse reports over either Bluetooth L2CAP sockets or `/dev/hidgX` file descriptors. The "hard" part (generating convincing text) is outsourced to the LLM. What remains is ~300 lines of transport setup, a typing loop with `sleep()` calls, and config parsing. Python handles that fine.

**My recommendation: use Python.** It's the primary implementation, has the most complete test suite (82 tests), the cleanest async architecture, and is the easiest to modify when you want to change the typing behavior. The Rust and C versions exist as ports — they work, but the Python version is what gets tested in CI and what `setup.sh` installs.

| Language | Directory | When to use |
| --- | --- | --- |
| **Python** | [`src/`](src/) | **Default — start here** |
| Rust | [`rust/`](rust/) | If you want a single static binary with no runtime deps |
| C | [`c/`](c/) | If you're on extremely constrained hardware or just prefer C |

## Project Structure

```
pi-key/
├── src/                    # Python implementation (primary)
│   ├── main.py             #   CLI entry point (Click)
│   ├── config.py            #   YAML config → typed dataclasses
│   ├── hid_transport.py     #   Abstract transport base class
│   ├── bt_hid.py            #   Bluetooth transport (BlueZ/L2CAP)
│   ├── usb_hid.py           #   USB gadget transport (ConfigFS)
│   ├── jiggler.py           #   Async mouse jiggler
│   ├── typer.py             #   Async typing engine
│   ├── llm_client.py        #   HTTP client (OpenAI/Ollama)
│   └── keymap.py            #   HID keycodes + typo neighbors
├── tests/                   # Python test suite (82 tests)
├── rust/                    # Rust implementation
├── c/                       # C implementation
├── config.example.yaml      # Config template
├── setup.sh                 # Pi setup script (BT + optional USB)
├── PAIRING.md               # Bluetooth pairing guide
└── .github/workflows/       # CI (GitHub-hosted runners)
```

## Development

### Running tests

```bash
pip install -r requirements.txt -r requirements-dev.txt
python -m pytest tests/ -v
```

### Contributing

This is a public repo. PRs are welcome. CI runs on GitHub-hosted runners (not self-hosted) and requires `python-tests (3.12)` to pass before merge.

## License

[MIT](LICENSE)
