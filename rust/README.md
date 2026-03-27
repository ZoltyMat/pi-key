# PiKey — Rust Implementation

Rust port of the PiKey BT HID spoofer + LLM auto-typer.

## Build

Requires Rust toolchain. Cross-compile for Raspberry Pi (aarch64):

```bash
rustup target add aarch64-unknown-linux-gnu
cargo build --release --target aarch64-unknown-linux-gnu
```

Or build natively on the Pi:

```bash
cargo build --release
```

## Run

```bash
# Copy binary + config to Pi
scp target/aarch64-unknown-linux-gnu/release/pikey pi@raspberrypi:~/
scp config.yaml pi@raspberrypi:~/

# On the Pi (requires root for BT/USB gadget)
sudo ./pikey --mode both --config config.yaml

# Mouse jiggler only (no LLM endpoint needed)
sudo ./pikey --mode jiggle

# Choose transport explicitly
sudo ./pikey --transport bt    # Bluetooth only
sudo ./pikey --transport usb   # USB gadget only
sudo ./pikey --transport auto  # Auto-detect (default)
```

## Config

Uses the same `config.yaml` format as the Python version. See `config.example.yaml` in the project root.

## Platform Notes

- Bluetooth L2CAP and USB gadget code is gated behind `cfg(target_os = "linux")`.
- On non-Linux (macOS, etc.) the project compiles but transport operations return errors at runtime.
- USB gadget mode requires the `dwc2` overlay enabled in `/boot/config.txt`.
