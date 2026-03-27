# PiKey — C Implementation

<p align="center">
  <img src="https://img.shields.io/badge/C-c99-555555?style=flat-square&logo=c&logoColor=white" alt="C99">
  <img src="https://img.shields.io/badge/threads-pthreads-orange?style=flat-square" alt="pthreads">
  <img src="https://img.shields.io/badge/platform-linux%2Farm-green?style=flat-square&logo=linux&logoColor=white" alt="Linux">
</p>

C99 implementation of PiKey. Minimal dependencies, compiles natively on the Pi in seconds.

> **Note:** The Python version (`src/`) is the primary implementation with the most complete test suite and CI coverage. Use this C version if you need the smallest possible binary or prefer C.

## Dependencies

On Raspberry Pi OS:

```bash
sudo apt install libbluetooth-dev libcurl4-openssl-dev libyaml-dev
```

## Build

```bash
cd c/
make           # release build
make debug     # debug build with symbols
```

The binary lands at `build/pikey` (release) or `pikey` (debug).

## Run

```bash
# Both jiggler + typer, Bluetooth transport
sudo ./build/pikey --mode both

# Mouse jiggler only (no LLM endpoint needed)
sudo ./build/pikey --mode jiggle

# USB gadget mode
sudo ./build/pikey --mode both --transport usb

# Custom config path
sudo ./build/pikey --config /etc/pikey/config.yaml
```

## Install

```bash
sudo make install    # copies to /usr/local/bin/pikey
```

## Architecture

| File | Purpose |
| --- | --- |
| `main.c` | CLI (getopt_long), config load, transport init, signal handling |
| `config.c` / `config.h` | YAML parser (libyaml) → `pikey_config_t` struct |
| `transport.c` / `transport.h` | Function-pointer vtable for BT + USB gadget transports |
| `keymap.c` / `keymap.h` | HID USB keycodes, char→report, QWERTY neighbor keys |
| `jiggler.c` / `jiggler.h` | Mouse jiggler thread (pthread) |
| `typer.c` / `typer.h` | Typing engine thread with human-like timing (pthread) |
| `llm_client.c` / `llm_client.h` | HTTP client for OpenAI/Ollama (libcurl + cJSON) |
| `cjson/` | Vendored [cJSON](https://github.com/DaveGamble/cJSON) parser |

The transport layer uses a function-pointer struct (`hid_transport_t`) as a C vtable — the same pattern as Linux kernel subsystems:

```c
struct hid_transport {
    int  (*connect)(hid_transport_t *t, const char *target);
    void (*disconnect)(hid_transport_t *t);
    int  (*send_keyboard)(hid_transport_t *t, const uint8_t report[8]);
    int  (*send_mouse)(hid_transport_t *t, uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);
    void *priv;  // transport-specific data
};
```

## Config

Uses the same `config.yaml` format as the Python version. See [`config.example.yaml`](../config.example.yaml) in the project root.
