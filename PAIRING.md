# PAIRING.md — How to pair PiKey with a target machine

## First-time pairing

On the Pi, run:
```bash
bluetoothctl
  power on
  agent on
  default-agent
  discoverable on
  pairable on
```

On the target machine (Windows/Mac/Linux):
- Open Bluetooth settings
- You should see **"Logitech K380 Multi-Device Keyboard"**
- Click pair — accept any PIN prompts (try `0000`)

Back on the Pi in bluetoothctl:
```
trust AA:BB:CC:DD:EE:FF     # MAC of target machine shown during pairing
```

Now set `target_mac` in config.yaml to that MAC so PiKey auto-connects on startup.

## Re-pairing / troubleshooting

```bash
# Remove old pairing and start fresh
bluetoothctl remove AA:BB:CC:DD:EE:FF
```

## Notes

- **macOS**: May show a PIN entry dialog. Enter `0000` or just click Connect.
- **Windows**: Works as a standard HID keyboard — no Logitech software needed.
- **Linux**: `bluetoothctl` pair as above. May need to `trust` + `connect` manually first time.
- The Pi must be paired **before** running pikey in auto-connect mode.
