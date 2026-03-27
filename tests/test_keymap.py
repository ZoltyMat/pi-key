"""Tests for src/keymap.py — HID keycodes and report builders."""

import os
import sys
import string

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

from keymap import (
    KEYMAP,
    NEARBY_KEYS,
    BACKSPACE,
    encode_keyboard_report,
    encode_backspace_report,
    release_report,
    get_neighbor,
)


class TestLowercaseLetters:
    """a-z should produce keycodes 0x04-0x1D with no modifier."""

    def test_all_lowercase_in_keymap(self):
        for ch in string.ascii_lowercase:
            assert ch in KEYMAP, f"'{ch}' missing from KEYMAP"

    def test_lowercase_keycodes_range(self):
        for i, ch in enumerate(string.ascii_lowercase):
            mod, keycode = KEYMAP[ch]
            assert mod == 0x00, f"'{ch}' should have no modifier"
            assert keycode == 0x04 + i, f"'{ch}' expected keycode {0x04+i:#x}, got {keycode:#x}"

    def test_lowercase_report_encoding(self):
        report = encode_keyboard_report("a")
        assert report is not None
        assert report[0] == 0x00  # no modifier
        assert report[2] == 0x04  # keycode for 'a'


class TestUppercaseLetters:
    """A-Z should produce keycodes 0x04-0x1D with shift modifier (0x02)."""

    def test_all_uppercase_in_keymap(self):
        for ch in string.ascii_uppercase:
            assert ch in KEYMAP, f"'{ch}' missing from KEYMAP"

    def test_uppercase_shift_modifier(self):
        for i, ch in enumerate(string.ascii_uppercase):
            mod, keycode = KEYMAP[ch]
            assert mod == 0x02, f"'{ch}' should have shift modifier 0x02"
            assert keycode == 0x04 + i, f"'{ch}' expected keycode {0x04+i:#x}, got {keycode:#x}"

    def test_uppercase_report_encoding(self):
        report = encode_keyboard_report("Z")
        assert report is not None
        assert report[0] == 0x02  # shift
        assert report[2] == 0x1D  # keycode for z/Z


class TestDigits:
    """0-9 should produce correct keycodes with no modifier."""

    def test_all_digits_in_keymap(self):
        for ch in string.digits:
            assert ch in KEYMAP

    def test_digit_keycodes(self):
        # 1-9 map to 0x1E-0x26, 0 maps to 0x27
        expected = {
            "1": 0x1E, "2": 0x1F, "3": 0x20, "4": 0x21, "5": 0x22,
            "6": 0x23, "7": 0x24, "8": 0x25, "9": 0x26, "0": 0x27,
        }
        for ch, expected_code in expected.items():
            mod, keycode = KEYMAP[ch]
            assert mod == 0x00
            assert keycode == expected_code

    def test_digit_no_modifier(self):
        for ch in string.digits:
            mod, _ = KEYMAP[ch]
            assert mod == 0x00


class TestSymbols:
    """Shifted number-row symbols should have modifier 0x02."""

    def test_shifted_symbols(self):
        symbols = {
            "!": 0x1E, "@": 0x1F, "#": 0x20, "$": 0x21,
            "%": 0x22, "^": 0x23, "&": 0x24, "*": 0x25,
            "(": 0x26, ")": 0x27,
        }
        for ch, expected_code in symbols.items():
            mod, keycode = KEYMAP[ch]
            assert mod == 0x02, f"'{ch}' should have shift modifier"
            assert keycode == expected_code, f"'{ch}' wrong keycode"


class TestSpecialKeys:
    """Space, enter, tab should map correctly."""

    def test_space(self):
        mod, keycode = KEYMAP[" "]
        assert mod == 0x00
        assert keycode == 0x2C

    def test_enter(self):
        mod, keycode = KEYMAP["\n"]
        assert mod == 0x00
        assert keycode == 0x28

    def test_tab(self):
        mod, keycode = KEYMAP["\t"]
        assert mod == 0x00
        assert keycode == 0x2B


class TestReleaseReport:
    """release_report() should return 8 zero bytes."""

    def test_all_zeros(self):
        r = release_report()
        assert r == b"\x00" * 8

    def test_length(self):
        assert len(release_report()) == 8


class TestReportLength:
    """All keyboard reports should be exactly 8 bytes."""

    def test_all_mapped_chars_produce_8_bytes(self):
        for ch in KEYMAP:
            report = encode_keyboard_report(ch)
            assert report is not None
            assert len(report) == 8, f"Report for '{ch}' is {len(report)} bytes"

    def test_backspace_report_length(self):
        assert len(encode_backspace_report()) == 8

    def test_unmapped_char_returns_none(self):
        # A character not in the keymap should return None
        assert encode_keyboard_report("\x00") is None


class TestNeighborKeys:
    """NEARBY_KEYS should have neighbors for common chars, not including the key itself."""

    def test_common_chars_have_neighbors(self):
        for ch in "asdfjkl":
            assert ch in NEARBY_KEYS, f"'{ch}' should have neighbors"
            assert len(NEARBY_KEYS[ch]) > 0

    def test_key_not_in_own_neighbors(self):
        for ch, neighbors in NEARBY_KEYS.items():
            assert ch not in neighbors, f"'{ch}' should not be its own neighbor"

    def test_neighbors_are_lowercase(self):
        for ch, neighbors in NEARBY_KEYS.items():
            for n in neighbors:
                assert n.islower(), f"Neighbor '{n}' of '{ch}' should be lowercase"

    def test_get_neighbor_returns_valid(self):
        """get_neighbor should return a neighbor from NEARBY_KEYS or None."""
        # For a key with neighbors, should return one of them
        result = get_neighbor("a")
        assert result is not None
        assert result in NEARBY_KEYS["a"]

    def test_get_neighbor_uppercase_returns_uppercase(self):
        result = get_neighbor("A")
        if result is not None:
            assert result.isupper()

    def test_get_neighbor_unknown_returns_none(self):
        assert get_neighbor("9") is None
