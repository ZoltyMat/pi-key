# PiKey — Rust Implementation

<p align="center">
  <img src="https://img.shields.io/badge/rust-1.75%2B-dea584?style=flat-square&logo=rust&logoColor=white" alt="Rust">
  <img src="https://img.shields.io/badge/async-tokio-blue?style=flat-square" alt="Tokio">
  <img src="https://img.shields.io/badge/platform-linux%2Faarch64-green?style=flat-square&logo=linux&logoColor=white" alt="Linux">
</p>

Rust port of the PiKey BT HID spoofer. Produces a single static binary with no Python runtime needed.

> **Note:** The Python version (`src/`) is the primary implementation with the most complete test suite and CI coverage. Use this Rust version if you want a single binary deployment with no runtime dependencies.

## Build

### Cross-compile for Raspberry Pi (from your dev machine)

```bash
rustup target add aarch64-unknown-linux-gnu
cargo build --release --target aarch64-unknown-linux-gnu
```

### Build natively on the Pi

```bash
# Install Rust first (if not already)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source ~/.cargo/env

cd rust/
cargo build --release
```

## Deploy

```bash
# Copy binary + config to Pi
scp target/release/pikey pi@raspberrypi:~/
scp ../config.example.yaml pi@raspberrypi:~/config.yaml

# Edit config on the Pi
ssh pi@raspberrypi nano config.yaml
```

## Run

```bash
# Full mode — jiggle + type, auto-detect transport
sudo ./pikey --mode both

# Mouse jiggler only (no LLM endpoint needed)
sudo ./pikey --mode jiggle

# Choose transport explicitly
sudo ./pikey --transport bt     # Bluetooth only
sudo ./pikey --transport usb    # USB gadget only
sudo ./pikey --transport auto   # Auto-detect (default)

# Custom config path + debug logging
sudo ./pikey --config /etc/pikey/config.yaml --verbose
```

## Architecture

The Rust implementation mirrors the Python version's structure:

| Module | Purpose |
| --- | --- |
| `main.rs` | CLI (clap), transport init, task spawning, signal handling |
| `config.rs` | YAML config → serde structs with defaults |
| `transport.rs` | `HidTransport` trait + Bluetooth/USB gadget implementations |
| `keymap.rs` | HID keycodes, char→report mapping, QWERTY neighbor keys |
| `jiggler.rs` | Async mouse jiggler loop (tokio) |
| `typer.rs` | Async typing engine with human-like timing |
| `llm_client.rs` | HTTP client for OpenAI/Ollama endpoints (reqwest) |

## Platform Notes

- Bluetooth (L2CAP sockets) and USB gadget (ConfigFS) code is behind `#[cfg(target_os = "linux")]`
- On macOS/Windows: compiles successfully but transport operations return errors at runtime
- USB gadget mode requires `dwc2` overlay — run `setup.sh --usb` first
- The `DynTransport` wrapper in `main.rs` allows both transports to be used through a single `Arc<dyn HidTransport>`

## Config

Uses the same `config.yaml` format as the Python version. See [`config.example.yaml`](../config.example.yaml) in the project root.
