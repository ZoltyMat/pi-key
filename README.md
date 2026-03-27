# PiKey — BT HID Spoofer + LLM Auto-Typer

> **Inspired by a thread on [r/overemployed](https://www.reddit.com/r/overemployed/s/S4Q1bJTxUp):**
>
> **[`u/thr0waway12324`](https://www.reddit.com/user/thr0waway12324)** (62 upvotes):
> *"Every time I see this, I will pose the same suggestion: someone please create a jetson nano
> or raspberry pi type of microcontroller that will use its Bluetooth to present as a keyboard +
> mouse. Run a local LLM that can auto type any type of text (novels, code, random notes, etc).
> And then sell this. I would buy it. Many others would too. Any technical/entrepreneurial people,
> please pick this up. I would but I literally just don't have the time."*
>
> **[`u/lordnacho666`](https://www.reddit.com/user/lordnacho666)** (defining the actual spec):
> *"You mean type things that aren't smart? Because reading the screen and making realistic
> movements is non-trivial. Obviously just typing out War and Peace would be just fine."*
>
> **[`u/deadzol`](https://www.reddit.com/user/deadzol)**:
> *"Don't even OE but I wanna try and do this. If I get rich, I'll buy a beer."* 🍺
>
> — [Original thread](https://www.reddit.com/r/overemployed/s/S4Q1bJTxUp)

Consider this the answer to that call. PiKey types things that aren't smart — and that's exactly the point. 🙏

> **Note to `u/deadzol`:** beer fund contributions welcome via GitHub Sponsors.

---

A Raspberry Pi project that presents as a Logitech K380 Bluetooth keyboard+mouse,
providing mouse jiggling and LLM-powered auto-typing.

## Hardware

| Board | BT | USB Gadget | Notes |
|---|---|---|---|
| Pi Zero 2W | Yes | Yes | Ideal — tiny, built-in BT, OTG USB port |
| Pi 4 | Yes | Yes | USB-C port supports OTG gadget mode |
| Pi 3B/3B+ | Yes | No | BT only — no USB OTG support |
| Pi 5 | Yes | No | BT only — no USB OTG support |

## Transports

PiKey supports two ways to connect to the target machine:

| Transport | How it works | When to use |
|---|---|---|
| `bt` | Bluetooth HID via BlueZ L2CAP | Default — wireless, no cable needed |
| `usb` | USB HID gadget via ConfigFS/dwc2 | Target blocks Bluetooth or BT is unreliable |
| `auto` | Tries USB first, falls back to BT | Best for portable setups |

## Modes

| Mode | Description |
|---|---|
| `jiggle` | Periodic small mouse movements to prevent idle detection |
| `type` | LLM generates text, typed out as real keystrokes |
| `both` | Jiggle + occasional LLM typing (most convincing) |

## Quick Start

### Bluetooth Mode (default)

```bash
# 1. Flash Raspberry Pi OS Lite (64-bit) to SD card
# 2. SSH in, then:

git clone https://github.com/ZoltyMat/pi-key.git
cd pi-key
sudo ./setup.sh

# Edit config
cp config.example.yaml config.yaml
nano config.yaml

# Pair with target machine first (see PAIRING.md)

# Run
python3 src/main.py --mode both
```

### USB Gadget Mode

For machines that block Bluetooth connections. Requires Pi Zero 2W or Pi 4 (OTG-capable USB).

```bash
# 1. Run setup with USB gadget support
sudo ./setup.sh --usb

# 2. Connect Pi to target via USB cable
#    Pi Zero 2W: use the data USB port (not power)
#    Pi 4: use the USB-C port

# 3. Run with USB transport
python3 src/main.py --mode both --transport usb

# Or auto-detect (tries USB first, falls back to BT)
python3 src/main.py --mode both --transport auto
```

## Architecture

```
┌──────────────────────────────────────────────────┐
│                  Raspberry Pi                     │
│                                                   │
│  ┌──────────┐   ┌──────────┐     ┌──────────┐   │
│  │ Jiggler  │   │  Typer   │     │  Config  │   │
│  │ (mouse)  │   │  (kbd)   │     │  Loader  │   │
│  └────┬─────┘   └────┬─────┘     └──────────┘   │
│       │              │                            │
│       └──────┬───────┘                            │
│              │                                    │
│       HIDTransport ABC                            │
│         │          │                              │
│  ┌──────▼────┐  ┌──▼───────────┐                 │
│  │ Bluetooth │  │  USB Gadget  │                  │
│  │  (BlueZ)  │  │  (ConfigFS)  │                  │
│  └─────┬─────┘  └──────┬──────┘                  │
│        │               │                          │
│  ┌─────────────────────────────────────────┐     │
│  │  Any OpenAI-compatible LLM endpoint     │     │
│  │  (LiteLLM, Ollama, OpenAI, etc.)        │     │
│  └─────────────────────────────────────────┘     │
└──────┬─────────────────────┬─────────────────────┘
       │ Bluetooth HID       │ USB HID
       ▼                     ▼
     [Target Machine]   [Target Machine]
     Sees: "Logitech K380"
```

## Implementations

PiKey is available in three languages:

| Language | Directory | Status | Notes |
| --- | --- | --- | --- |
| Python | `src/` | Primary | Async (asyncio), Click CLI, Rich logging |
| Rust | `rust/` | Complete | Tokio async, clap CLI, trait-based transport |
| C | `c/` | Complete | pthreads, libcurl, function-pointer vtable |

All three share the same `config.yaml` format and produce identical HID reports.

## Logitech Spoofing

The device advertises:
- **Name:** `Logitech K380 Multi-Device Keyboard`
- **CoD:** `0x002540` (Peripheral, Keyboard)
- **HID Descriptor:** Standard combo keyboard+mouse
- **Vendor/Product:** Logitech IDs (046d:b342)

Most OS drivers load the standard HID driver — no special Logitech software needed.
