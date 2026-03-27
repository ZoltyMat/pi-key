# Security Policy

## Supported Versions

| Version | Supported |
| --- | --- |
| 0.1.x | Yes |

## Reporting a Vulnerability

If you discover a security vulnerability in PiKey, please report it responsibly:

1. **Do not** open a public GitHub issue for security vulnerabilities
2. Use [GitHub's private vulnerability reporting](https://github.com/ZoltyMat/pi-key/security/advisories/new)
3. Include steps to reproduce, affected versions, and potential impact

You should receive a response within 72 hours. We'll work with you to understand the issue and coordinate a fix before any public disclosure.

## Security Scanning

This repository runs automated security scans on every push to `main` and weekly:

- **pip-audit** — Python dependency CVE scanning
- **cargo-audit** — Rust dependency advisory scanning
- **Trivy** — Docker image and repository vulnerability scanning (results published to GitHub Security tab)
- **Dependabot** — Automated dependency update PRs

Scan results are visible in the repository's [Security tab](https://github.com/ZoltyMat/pi-key/security).

## Security Considerations

PiKey spoofs a Bluetooth/USB HID device. By design, it sends keystrokes to a target machine. Users should be aware:

- **`config.yaml` contains secrets** — LLM API keys and target MAC addresses. This file is `.gitignore`d and should never be committed.
- **Root/privileged access required** — Bluetooth L2CAP sockets and USB gadget ConfigFS require root. The Docker container runs privileged.
- **LLM endpoint trust** — PiKey sends prompts to and receives text from the configured LLM endpoint. Only use trusted endpoints.
- **Physical access** — Anyone with physical access to the Pi can modify `config.yaml` or intercept the LLM responses.
