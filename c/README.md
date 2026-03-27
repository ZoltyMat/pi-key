# PiKey — C Implementation

Bluetooth HID spoofer + LLM auto-typer for Raspberry Pi.
Presents as a Logitech K380 keyboard+mouse combo over Bluetooth or USB gadget mode.

## Dependencies

```
sudo apt install libbluetooth-dev libcurl4-openssl-dev libyaml-dev
```

## Build

```
make
```

## Run

```
# Both jiggler + typer (default), Bluetooth transport
sudo ./pikey --mode both

# Mouse jiggler only
sudo ./pikey --mode jiggle

# LLM typer only
sudo ./pikey --mode type

# USB gadget mode instead of Bluetooth
sudo ./pikey --mode both --transport usb

# Custom config path
sudo ./pikey --config /path/to/config.yaml
```

## Config

Copy `../config.example.yaml` to `config.yaml` and set your LLM endpoint.

## Architecture

- `main.c` — arg parsing, config load, transport init, worker startup
- `config.c` — YAML config parser (libyaml)
- `keymap.c` — HID USB keycode mappings + typo neighbor keys
- `transport.c` — Bluetooth L2CAP and USB gadget HID transports
- `jiggler.c` — Mouse jiggler (pthread)
- `typer.c` — Human-like typing engine (pthread)
- `llm_client.c` — HTTP client for OpenAI/Ollama APIs (libcurl)
- `cjson/` — Minimal embedded JSON parser
