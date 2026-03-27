"""typer.py — LLM-powered typing engine with human-like timing.

Fetches text from an LLM endpoint and types it character by
character via HID reports, simulating realistic human keystrokes
including typos, corrections, and think pauses.
"""

from __future__ import annotations

import asyncio
import logging
import random
import time

from src.config import TyperConfig
from src.hid_transport import HIDTransport
from src.keymap import (
    encode_backspace_report,
    encode_keyboard_report,
    get_neighbor,
    release_report,
)
from src.llm_client import LLMClient

log = logging.getLogger(__name__)


class Typer:
    """Async typing engine that fetches LLM text and types it via HID."""

    def __init__(
        self,
        transport: HIDTransport,
        llm: LLMClient,
        cfg: TyperConfig,
    ) -> None:
        self.transport = transport
        self.llm = llm
        self.cfg = cfg
        self._task: asyncio.Task | None = None

    async def run(self) -> None:
        """Main typing loop — runs until cancelled."""
        if not self.cfg.enabled:
            log.info("Typer disabled in config")
            return

        log.info(
            "Typer started (interval %d–%ds, CPM %d–%d)",
            self.cfg.interval_min,
            self.cfg.interval_max,
            self.cfg.cpm_min,
            self.cfg.cpm_max,
        )

        try:
            while True:
                interval = random.uniform(
                    self.cfg.interval_min, self.cfg.interval_max
                )
                log.debug("Next typing session in %.0fs", interval)
                await asyncio.sleep(interval)

                try:
                    log.info("Fetching LLM text...")
                    text = await self.llm.fetch_text()
                    if text:
                        log.info("Typing %d chars...", len(text))
                        await self._type_text(text)
                        log.info("Typing session complete")
                    else:
                        log.warning("LLM returned empty text, skipping")
                except asyncio.CancelledError:
                    raise
                except Exception as e:
                    log.warning("Typer error: %s", e)
        except asyncio.CancelledError:
            log.info("Typer stopped")

    async def _type_text(self, text: str) -> None:
        """Type text character by character with human-like timing."""
        for ch in text:
            # Think pause
            if random.random() < self.cfg.think_pause_chance:
                lo, hi = self.cfg.think_pause_secs
                pause = random.uniform(lo, hi)
                log.debug("Think pause: %.1fs", pause)
                await asyncio.sleep(pause)

            # Typo simulation
            if random.random() < self.cfg.typo_rate:
                neighbor = get_neighbor(ch)
                if neighbor:
                    log.debug("Typo: %r -> %r (correcting)", ch, neighbor)
                    await self._press_char(neighbor)
                    await asyncio.sleep(self._char_delay() * 1.5)
                    await self._press_backspace()
                    await asyncio.sleep(random.uniform(0.1, 0.3))

            # Type the actual character
            await self._press_char(ch)
            await asyncio.sleep(self._char_delay())

    async def _press_char(self, ch: str) -> None:
        """Send a key press and release for a single character."""
        report = encode_keyboard_report(ch)
        if report is None:
            log.debug("Unmapped character: %r", ch)
            return
        self.transport.send_keyboard_report(report)
        await asyncio.sleep(0.008)  # brief hold
        self.transport.send_keyboard_report(release_report())

    async def _press_backspace(self) -> None:
        """Send a backspace press and release."""
        self.transport.send_keyboard_report(encode_backspace_report())
        await asyncio.sleep(0.008)
        self.transport.send_keyboard_report(release_report())

    def _char_delay(self) -> float:
        """Compute delay between keystrokes based on CPM config.

        Uses gaussian jitter around the base delay for natural variation.
        """
        cpm = random.uniform(self.cfg.cpm_min, self.cfg.cpm_max)
        base = 60.0 / cpm
        jitter = random.gauss(0, base * 0.2)
        return max(0.02, base + jitter)

    def start(self) -> asyncio.Task:
        """Create and return the typer asyncio task."""
        self._task = asyncio.create_task(self.run(), name="typer")
        return self._task

    def stop(self) -> None:
        """Cancel the typer task."""
        if self._task and not self._task.done():
            self._task.cancel()
