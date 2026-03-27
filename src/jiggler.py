"""jiggler.py — Async mouse jiggler.

Sends small, randomized mouse movements at configurable intervals
to prevent screen lock / idle detection.
"""

from __future__ import annotations

import asyncio
import logging
import random
import struct

from src.config import JigglerConfig
from src.hid_transport import HIDTransport

log = logging.getLogger(__name__)


def _mouse_report(buttons: int, dx: int, dy: int, wheel: int = 0) -> bytes:
    """Build a 4-byte mouse HID report: [buttons, dx(int8), dy(int8), wheel(int8)]."""
    dx = max(-127, min(127, dx))
    dy = max(-127, min(127, dy))
    wheel = max(-127, min(127, wheel))
    return struct.pack("Bbbb", buttons, dx, dy, wheel)


class MouseJiggler:
    """Async mouse jiggler that sends tiny movements via HID transport."""

    def __init__(self, transport: HIDTransport, cfg: JigglerConfig) -> None:
        self.transport = transport
        self.cfg = cfg
        self._task: asyncio.Task | None = None

    async def run(self) -> None:
        """Main jiggle loop — runs until cancelled."""
        if not self.cfg.enabled:
            log.info("Jiggler disabled in config")
            return

        log.info(
            "Jiggler started (interval %d–%ds, max_delta %d)",
            self.cfg.interval_min,
            self.cfg.interval_max,
            self.cfg.max_delta,
        )

        try:
            while True:
                interval = random.uniform(self.cfg.interval_min, self.cfg.interval_max)
                log.debug("Next jiggle in %.1fs", interval)
                await asyncio.sleep(interval)

                try:
                    await self._jiggle_once()
                except Exception as e:
                    log.warning("Jiggle failed: %s", e)
        except asyncio.CancelledError:
            log.info("Jiggler stopped")

    async def _jiggle_once(self) -> None:
        """Send a small movement then approximately return to origin."""
        dx, dy = self._random_delta()
        log.debug("Jiggle: dx=%d dy=%d", dx, dy)

        # Move
        self.transport.send_mouse_report(_mouse_report(0, dx, dy))
        await asyncio.sleep(random.uniform(0.08, 0.2))

        # Return (slight offset so it's not perfectly zero)
        ret_x = -dx + random.choice([-1, 0, 1])
        ret_y = -dy + random.choice([-1, 0, 1])
        self.transport.send_mouse_report(_mouse_report(0, ret_x, ret_y))

    def _random_delta(self) -> tuple[int, int]:
        """Generate a random movement delta."""
        if random.random() < self.cfg.big_move_chance:
            d = random.randint(10, 20)
        else:
            d = self.cfg.max_delta

        dx = random.randint(-d, d)
        dy = random.randint(-d, d)

        # Avoid zero-movement
        if dx == 0 and dy == 0:
            dx = 1

        return dx, dy

    def start(self) -> asyncio.Task:
        """Create and return the jiggler asyncio task."""
        self._task = asyncio.create_task(self.run(), name="jiggler")
        return self._task

    def stop(self) -> None:
        """Cancel the jiggler task."""
        if self._task and not self._task.done():
            self._task.cancel()
