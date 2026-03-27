"""keymap.py — USB HID keycode mappings and report builders."""

from __future__ import annotations

import struct
from typing import Dict, List, Optional, Tuple

# (modifier_byte, keycode) — modifier 0x00 = none, 0x02 = left shift
KEYMAP: Dict[str, Tuple[int, int]] = {
    # Whitespace
    " ": (0x00, 0x2C),   # Space
    "\n": (0x00, 0x28),   # Enter
    "\t": (0x00, 0x2B),   # Tab
    # Lowercase a-z
    "a": (0x00, 0x04), "b": (0x00, 0x05), "c": (0x00, 0x06),
    "d": (0x00, 0x07), "e": (0x00, 0x08), "f": (0x00, 0x09),
    "g": (0x00, 0x0A), "h": (0x00, 0x0B), "i": (0x00, 0x0C),
    "j": (0x00, 0x0D), "k": (0x00, 0x0E), "l": (0x00, 0x0F),
    "m": (0x00, 0x10), "n": (0x00, 0x11), "o": (0x00, 0x12),
    "p": (0x00, 0x13), "q": (0x00, 0x14), "r": (0x00, 0x15),
    "s": (0x00, 0x16), "t": (0x00, 0x17), "u": (0x00, 0x18),
    "v": (0x00, 0x19), "w": (0x00, 0x1A), "x": (0x00, 0x1B),
    "y": (0x00, 0x1C), "z": (0x00, 0x1D),
    # Uppercase A-Z (shift modifier = 0x02)
    "A": (0x02, 0x04), "B": (0x02, 0x05), "C": (0x02, 0x06),
    "D": (0x02, 0x07), "E": (0x02, 0x08), "F": (0x02, 0x09),
    "G": (0x02, 0x0A), "H": (0x02, 0x0B), "I": (0x02, 0x0C),
    "J": (0x02, 0x0D), "K": (0x02, 0x0E), "L": (0x02, 0x0F),
    "M": (0x02, 0x10), "N": (0x02, 0x11), "O": (0x02, 0x12),
    "P": (0x02, 0x13), "Q": (0x02, 0x14), "R": (0x02, 0x15),
    "S": (0x02, 0x16), "T": (0x02, 0x17), "U": (0x02, 0x18),
    "V": (0x02, 0x19), "W": (0x02, 0x1A), "X": (0x02, 0x1B),
    "Y": (0x02, 0x1C), "Z": (0x02, 0x1D),
    # Numbers
    "1": (0x00, 0x1E), "2": (0x00, 0x1F), "3": (0x00, 0x20),
    "4": (0x00, 0x21), "5": (0x00, 0x22), "6": (0x00, 0x23),
    "7": (0x00, 0x24), "8": (0x00, 0x25), "9": (0x00, 0x26),
    "0": (0x00, 0x27),
    # Shifted number-row symbols
    "!": (0x02, 0x1E), "@": (0x02, 0x1F), "#": (0x02, 0x20),
    "$": (0x02, 0x21), "%": (0x02, 0x22), "^": (0x02, 0x23),
    "&": (0x02, 0x24), "*": (0x02, 0x25), "(": (0x02, 0x26),
    ")": (0x02, 0x27),
    # Punctuation / symbols
    "-": (0x00, 0x2D), "_": (0x02, 0x2D),
    "=": (0x00, 0x2E), "+": (0x02, 0x2E),
    "[": (0x00, 0x2F), "{": (0x02, 0x2F),
    "]": (0x00, 0x30), "}": (0x02, 0x30),
    "\\": (0x00, 0x31), "|": (0x02, 0x31),
    ";": (0x00, 0x33), ":": (0x02, 0x33),
    "'": (0x00, 0x34), '"': (0x02, 0x34),
    "`": (0x00, 0x35), "~": (0x02, 0x35),
    ",": (0x00, 0x36), "<": (0x02, 0x36),
    ".": (0x00, 0x37), ">": (0x02, 0x37),
    "/": (0x00, 0x38), "?": (0x02, 0x38),
}

BACKSPACE: Tuple[int, int] = (0x00, 0x2A)

# Neighboring keys on a QWERTY layout for realistic typo simulation
NEARBY_KEYS: Dict[str, List[str]] = {
    "a": ["s", "q", "w", "z"],
    "b": ["v", "g", "h", "n"],
    "c": ["x", "d", "f", "v"],
    "d": ["s", "f", "e", "r", "x", "c"],
    "e": ["w", "s", "d", "r"],
    "f": ["d", "g", "r", "t", "v", "c"],
    "g": ["f", "h", "t", "y", "b", "v"],
    "h": ["g", "j", "y", "u", "n", "b"],
    "i": ["u", "o", "j", "k"],
    "j": ["h", "k", "u", "i", "m", "n"],
    "k": ["j", "l", "i", "o", "m"],
    "l": ["k", "o", "p"],
    "m": ["n", "j", "k"],
    "n": ["b", "h", "m", "j"],
    "o": ["i", "k", "l", "p"],
    "p": ["o", "l"],
    "q": ["w", "a"],
    "r": ["e", "t", "f", "d"],
    "s": ["a", "d", "w", "x", "z", "e"],
    "t": ["r", "y", "f", "g"],
    "u": ["y", "h", "j", "i"],
    "v": ["c", "f", "g", "b"],
    "w": ["q", "a", "s", "e"],
    "x": ["z", "s", "d", "c"],
    "y": ["t", "g", "h", "u"],
    "z": ["a", "s", "x"],
}


def encode_keyboard_report(char: str) -> Optional[bytes]:
    """Encode a character into an 8-byte HID keyboard report.

    Report format: [modifier, 0x00, keycode, 0, 0, 0, 0, 0]
    Returns None if the character is not in the keymap.
    """
    entry = KEYMAP.get(char)
    if entry is None:
        return None
    modifier, keycode = entry
    return struct.pack("BBBBBBBB", modifier, 0x00, keycode, 0, 0, 0, 0, 0)


def encode_backspace_report() -> bytes:
    """Return an 8-byte HID report for Backspace."""
    modifier, keycode = BACKSPACE
    return struct.pack("BBBBBBBB", modifier, 0x00, keycode, 0, 0, 0, 0, 0)


def release_report() -> bytes:
    """Return an 8-byte all-zeros key-release report."""
    return b"\x00" * 8


def get_neighbor(char: str) -> Optional[str]:
    """Return a random neighboring key for typo simulation, or None."""
    import random
    neighbors = NEARBY_KEYS.get(char.lower())
    if not neighbors:
        return None
    neighbor = random.choice(neighbors)
    return neighbor.upper() if char.isupper() else neighbor
