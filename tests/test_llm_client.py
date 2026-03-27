"""Tests for src/llm_client.py — Async LLM HTTP client."""

import os
import sys
from unittest.mock import AsyncMock, MagicMock

import httpx
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

from llm_client import LLMClient, _strip_markdown
from config import LLMConfig


class TestStripMarkdown:
    """_strip_markdown should remove common formatting."""

    def test_strips_code_fences(self):
        text = "```python\ndef foo():\n    pass\n```"
        result = _strip_markdown(text)
        assert "```" not in result
        assert "def foo():" in result

    def test_strips_bold(self):
        assert _strip_markdown("**bold text**") == "bold text"

    def test_strips_headings(self):
        assert _strip_markdown("## Heading") == "Heading"

    def test_strips_whitespace(self):
        assert _strip_markdown("  hello world  \n") == "hello world"


class TestOpenAIApiCall:
    """Verify correct URL, headers, body for openai-style API call."""

    @pytest.mark.asyncio
    async def test_openai_request_format(self):
        cfg = LLMConfig(
            url="http://my-openai:4000",
            api_style="openai",
            model="gpt-4o-mini",
            api_key="sk-test-key",
            max_tokens=100,
            prompts=["Hello world"],
        )
        client = LLMClient(cfg)

        mock_response = MagicMock()
        mock_response.json.return_value = {
            "choices": [{"message": {"content": "def hello(): pass"}}]
        }
        mock_response.raise_for_status = MagicMock()

        mock_http = AsyncMock()
        mock_http.post = AsyncMock(return_value=mock_response)
        mock_http.is_closed = False

        client._client = mock_http

        text = await client.fetch_text()
        assert text == "def hello(): pass"

        call_args = mock_http.post.call_args
        assert "v1/chat/completions" in call_args[0][0]
        payload = call_args[1]["json"]
        assert payload["model"] == "gpt-4o-mini"
        assert payload["max_tokens"] == 100

        await client.close()


class TestOllamaApiCall:
    """Verify correct URL and payload for ollama-style API call."""

    @pytest.mark.asyncio
    async def test_ollama_request_format(self):
        cfg = LLMConfig(
            url="http://ollama-host:11434",
            api_style="ollama",
            model="llama3.2:1b",
            max_tokens=200,
            prompts=["Test prompt"],
        )
        client = LLMClient(cfg)

        mock_response = MagicMock()
        mock_response.json.return_value = {"response": "def foo(): return 42"}
        mock_response.raise_for_status = MagicMock()

        mock_http = AsyncMock()
        mock_http.post = AsyncMock(return_value=mock_response)
        mock_http.is_closed = False

        client._client = mock_http

        text = await client.fetch_text()
        assert text == "def foo(): return 42"

        call_args = mock_http.post.call_args
        assert "api/generate" in call_args[0][0]
        payload = call_args[1]["json"]
        assert payload["model"] == "llama3.2:1b"
        assert payload["stream"] is False

        await client.close()


class TestEmptyUrlValidation:
    """LLM client with empty URL should handle gracefully."""

    @pytest.mark.asyncio
    async def test_empty_url_fetch_returns_empty(self):
        cfg = LLMConfig(url="", api_style="openai", model="x", prompts=["test"])
        client = LLMClient(cfg)

        # fetch_text with empty URL should fail all retries and return empty
        text = await client.fetch_text()
        assert text == ""

        await client.close()
