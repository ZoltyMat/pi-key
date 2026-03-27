"""main.py — PiKey CLI entry point.

Usage:
  python3 -m src.main --mode both --config config.yaml --transport auto
"""

from __future__ import annotations

import asyncio
import logging
import signal
import sys

import click
from rich.console import Console
from rich.logging import RichHandler

from src.config import PikeyConfig, load_config, validate_for_typing
from src.hid_transport import HIDTransport
from src.jiggler import MouseJiggler
from src.llm_client import LLMClient
from src.typer import Typer

console = Console()

logging.basicConfig(
    level=logging.INFO,
    format="%(message)s",
    handlers=[RichHandler(console=console, rich_tracebacks=True, show_path=False)],
)
log = logging.getLogger("pikey")


def _build_transport(transport: str, cfg: PikeyConfig) -> HIDTransport:
    """Build the HID transport based on the --transport flag."""
    if transport == "usb":
        from src.usb_hid import USBGadgetTransport
        return USBGadgetTransport()

    if transport == "bt":
        from src.bt_hid import BluetoothHIDTransport
        return BluetoothHIDTransport(cfg.device)

    # auto: try USB gadget first, fall back to Bluetooth
    from src.usb_hid import usb_gadget_available

    if usb_gadget_available():
        log.info("USB gadget mode available, using USB transport")
        from src.usb_hid import USBGadgetTransport
        return USBGadgetTransport()
    else:
        log.info("USB gadget not available, falling back to Bluetooth")
        from src.bt_hid import BluetoothHIDTransport
        return BluetoothHIDTransport(cfg.device)


async def _run(mode: str, cfg: PikeyConfig, transport_type: str) -> None:
    """Async main — set up transport, start tasks, wait for shutdown."""
    loop = asyncio.get_running_loop()
    shutdown_event = asyncio.Event()

    # Signal handling
    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, shutdown_event.set)

    # Transport
    hid = _build_transport(transport_type, cfg)
    console.rule("[bold green]PiKey — HID Spoofer")
    log.info("Mode: [bold]%s[/bold]", mode, extra={"markup": True})
    log.info("Transport: [bold]%s[/bold]", transport_type, extra={"markup": True})
    log.info("Spoofing as: %s", cfg.device.name)

    try:
        await hid.connect()
    except Exception as e:
        log.error("Failed to connect HID transport: %s", e)
        sys.exit(1)

    tasks: list[asyncio.Task] = []
    llm_client: LLMClient | None = None

    try:
        # Start jiggler
        if mode in ("jiggle", "both"):
            jiggler = MouseJiggler(hid, cfg.jiggler)
            tasks.append(jiggler.start())

        # Start typer
        if mode in ("type", "both"):
            llm_client = LLMClient(cfg.llm)
            typer = Typer(hid, llm_client, cfg.typer)
            tasks.append(typer.start())

        log.info(
            "[green]Running[/green] — send SIGINT/SIGTERM to stop",
            extra={"markup": True},
        )

        # Wait for shutdown signal
        await shutdown_event.wait()
        log.info("Shutting down...")

    finally:
        # Cancel all tasks
        for task in tasks:
            task.cancel()
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)

        # Cleanup
        if llm_client:
            await llm_client.close()
        await hid.disconnect()

    log.info("Goodbye")


@click.command()
@click.option(
    "--mode",
    default="both",
    type=click.Choice(["jiggle", "type", "both"]),
    help="Operating mode",
)
@click.option(
    "--config",
    "config_path",
    default="config.yaml",
    help="Path to config file",
)
@click.option(
    "--transport",
    default="auto",
    type=click.Choice(["bt", "usb", "auto"]),
    help="HID transport: bt, usb, or auto (try USB first)",
)
@click.option("--verbose", is_flag=True, help="Enable debug logging")
def main(mode: str, config_path: str, transport: str, verbose: bool) -> None:
    """PiKey — Bluetooth/USB HID spoofer with LLM-powered auto-typing."""
    if verbose:
        logging.getLogger().setLevel(logging.DEBUG)

    cfg = load_config(config_path)

    # Validate LLM config if typing is enabled
    if mode in ("type", "both") and cfg.typer.enabled:
        try:
            validate_for_typing(cfg)
        except ValueError as e:
            log.error(str(e))
            sys.exit(1)

    asyncio.run(_run(mode, cfg, transport))


if __name__ == "__main__":
    main()
