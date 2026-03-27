"""llm_client.py — Async HTTP client for LLM endpoints.

Supports OpenAI-compatible (/v1/chat/completions) and Ollama
(/api/generate) API styles. Picks a random prompt from config
and returns cleaned text.
"""

from __future__ import annotations

import logging
import random
import re

import httpx

from src.config import LLMConfig

log = logging.getLogger(__name__)

MAX_RETRIES = 3
RETRY_DELAY = 10.0  # seconds
TIMEOUT = 60.0


def _strip_markdown(text: str) -> str:
    """Remove common markdown formatting from LLM output."""
    # Remove code fences
    text = re.sub(r"```[\w]*\n?", "", text)
    # Remove bold/italic markers
    text = text.replace("**", "").replace("__", "")
    # Remove heading markers at line start
    text = re.sub(r"^#{1,6}\s+", "", text, flags=re.MULTILINE)
    return text.strip()


class LLMClient:
    """Async client for fetching generated text from an LLM endpoint."""

    def __init__(self, cfg: LLMConfig) -> None:
        self.cfg = cfg
        self._client: httpx.AsyncClient | None = None

    async def _get_client(self) -> httpx.AsyncClient:
        if self._client is None or self._client.is_closed:
            headers = {"Content-Type": "application/json"}
            if self.cfg.api_key:
                headers["Authorization"] = f"Bearer {self.cfg.api_key}"
            self._client = httpx.AsyncClient(headers=headers, timeout=TIMEOUT)
        return self._client

    async def close(self) -> None:
        """Close the underlying HTTP client."""
        if self._client and not self._client.is_closed:
            await self._client.aclose()
            self._client = None

    async def fetch_text(self) -> str:
        """Fetch generated text from the LLM, with retries.

        Returns cleaned text or empty string on persistent failure.
        """
        prompt = random.choice(self.cfg.prompts)
        url = self.cfg.url.rstrip("/")

        for attempt in range(1, MAX_RETRIES + 1):
            try:
                raw = await self._request(url, prompt)
                return _strip_markdown(raw)
            except httpx.ConnectError:
                log.error(
                    "Cannot reach LLM at %s (attempt %d/%d)",
                    url, attempt, MAX_RETRIES,
                )
            except httpx.HTTPStatusError as e:
                log.error(
                    "LLM returned %d (attempt %d/%d): %s",
                    e.response.status_code, attempt, MAX_RETRIES, e,
                )
            except Exception as e:
                log.error(
                    "LLM request failed (attempt %d/%d): %s",
                    attempt, MAX_RETRIES, e,
                )

            if attempt < MAX_RETRIES:
                import asyncio
                await asyncio.sleep(RETRY_DELAY)

        log.warning("All %d LLM attempts failed, returning empty", MAX_RETRIES)
        return ""

    async def _request(self, url: str, prompt: str) -> str:
        """Make the actual HTTP request to the LLM endpoint."""
        client = await self._get_client()

        if self.cfg.api_style == "ollama":
            return await self._request_ollama(client, url, prompt)
        else:
            return await self._request_openai(client, url, prompt)

    async def _request_openai(
        self, client: httpx.AsyncClient, url: str, prompt: str
    ) -> str:
        """OpenAI-compatible /v1/chat/completions."""
        payload: dict = {
            "messages": [{"role": "user", "content": prompt}],
            "max_tokens": self.cfg.max_tokens,
            "stream": False,
        }
        if self.cfg.model:
            payload["model"] = self.cfg.model

        log.debug("POST %s/v1/chat/completions model=%s", url, self.cfg.model)
        resp = await client.post(f"{url}/v1/chat/completions", json=payload)
        resp.raise_for_status()
        return resp.json()["choices"][0]["message"]["content"].strip()

    async def _request_ollama(
        self, client: httpx.AsyncClient, url: str, prompt: str
    ) -> str:
        """Ollama /api/generate."""
        payload: dict = {
            "prompt": prompt,
            "stream": False,
            "options": {"num_predict": self.cfg.max_tokens},
        }
        if self.cfg.model:
            payload["model"] = self.cfg.model

        log.debug("POST %s/api/generate model=%s", url, self.cfg.model)
        resp = await client.post(f"{url}/api/generate", json=payload)
        resp.raise_for_status()
        return resp.json().get("response", "").strip()
