/*
 * keymap.c — HID keycode mappings and typo neighbor keys
 */
#include "keymap.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ── HID Usage IDs ─────────────────────────────────────────────────────────── */
/* Each entry: { modifier, keycode } */

typedef struct {
    char ch;
    uint8_t modifier;
    uint8_t keycode;
} keymap_entry_t;

static const keymap_entry_t KEYMAP[] = {
    /* Whitespace */
    { ' ',  0x00, 0x2C }, { '\n', 0x00, 0x28 }, { '\t', 0x00, 0x2B },

    /* Lowercase letters */
    { 'a', 0x00, 0x04 }, { 'b', 0x00, 0x05 }, { 'c', 0x00, 0x06 },
    { 'd', 0x00, 0x07 }, { 'e', 0x00, 0x08 }, { 'f', 0x00, 0x09 },
    { 'g', 0x00, 0x0A }, { 'h', 0x00, 0x0B }, { 'i', 0x00, 0x0C },
    { 'j', 0x00, 0x0D }, { 'k', 0x00, 0x0E }, { 'l', 0x00, 0x0F },
    { 'm', 0x00, 0x10 }, { 'n', 0x00, 0x11 }, { 'o', 0x00, 0x12 },
    { 'p', 0x00, 0x13 }, { 'q', 0x00, 0x14 }, { 'r', 0x00, 0x15 },
    { 's', 0x00, 0x16 }, { 't', 0x00, 0x17 }, { 'u', 0x00, 0x18 },
    { 'v', 0x00, 0x19 }, { 'w', 0x00, 0x1A }, { 'x', 0x00, 0x1B },
    { 'y', 0x00, 0x1C }, { 'z', 0x00, 0x1D },

    /* Uppercase letters (Left Shift = 0x02) */
    { 'A', 0x02, 0x04 }, { 'B', 0x02, 0x05 }, { 'C', 0x02, 0x06 },
    { 'D', 0x02, 0x07 }, { 'E', 0x02, 0x08 }, { 'F', 0x02, 0x09 },
    { 'G', 0x02, 0x0A }, { 'H', 0x02, 0x0B }, { 'I', 0x02, 0x0C },
    { 'J', 0x02, 0x0D }, { 'K', 0x02, 0x0E }, { 'L', 0x02, 0x0F },
    { 'M', 0x02, 0x10 }, { 'N', 0x02, 0x11 }, { 'O', 0x02, 0x12 },
    { 'P', 0x02, 0x13 }, { 'Q', 0x02, 0x14 }, { 'R', 0x02, 0x15 },
    { 'S', 0x02, 0x16 }, { 'T', 0x02, 0x17 }, { 'U', 0x02, 0x18 },
    { 'V', 0x02, 0x19 }, { 'W', 0x02, 0x1A }, { 'X', 0x02, 0x1B },
    { 'Y', 0x02, 0x1C }, { 'Z', 0x02, 0x1D },

    /* Numbers */
    { '1', 0x00, 0x1E }, { '2', 0x00, 0x1F }, { '3', 0x00, 0x20 },
    { '4', 0x00, 0x21 }, { '5', 0x00, 0x22 }, { '6', 0x00, 0x23 },
    { '7', 0x00, 0x24 }, { '8', 0x00, 0x25 }, { '9', 0x00, 0x26 },
    { '0', 0x00, 0x27 },

    /* Shifted number symbols */
    { '!', 0x02, 0x1E }, { '@', 0x02, 0x1F }, { '#', 0x02, 0x20 },
    { '$', 0x02, 0x21 }, { '%', 0x02, 0x22 }, { '^', 0x02, 0x23 },
    { '&', 0x02, 0x24 }, { '*', 0x02, 0x25 }, { '(', 0x02, 0x26 },
    { ')', 0x02, 0x27 },

    /* Punctuation */
    { '-', 0x00, 0x2D }, { '_', 0x02, 0x2D }, { '=', 0x00, 0x2E },
    { '+', 0x02, 0x2E }, { '[', 0x00, 0x2F }, { '{', 0x02, 0x2F },
    { ']', 0x00, 0x30 }, { '}', 0x02, 0x30 }, { '\\', 0x00, 0x31 },
    { '|', 0x02, 0x31 }, { ';', 0x00, 0x33 }, { ':', 0x02, 0x33 },
    { '\'', 0x00, 0x34 }, { '"', 0x02, 0x34 }, { '`', 0x00, 0x35 },
    { '~', 0x02, 0x35 }, { ',', 0x00, 0x36 }, { '<', 0x02, 0x36 },
    { '.', 0x00, 0x37 }, { '>', 0x02, 0x37 }, { '/', 0x00, 0x38 },
    { '?', 0x02, 0x38 },
};

#define KEYMAP_SIZE (sizeof(KEYMAP) / sizeof(KEYMAP[0]))

int char_to_hid_report(char c, uint8_t report[8]) {
    for (size_t i = 0; i < KEYMAP_SIZE; i++) {
        if (KEYMAP[i].ch == c) {
            report[0] = 0x01;              /* Report ID (keyboard) */
            report[1] = KEYMAP[i].modifier;
            report[2] = 0x00;              /* Reserved */
            report[3] = KEYMAP[i].keycode;
            report[4] = 0x00;
            report[5] = 0x00;
            report[6] = 0x00;
            report[7] = 0x00;
            return 0;
        }
    }
    return -1;
}

void release_report(uint8_t report[8]) {
    report[0] = 0x01;  /* Report ID (keyboard) */
    memset(report + 1, 0, 7);
}

/* ── Nearby keys for typo simulation ──────────────────────────────────────── */

typedef struct {
    char key;
    const char *neighbors;
} neighbor_entry_t;

static const neighbor_entry_t NEARBY_KEYS[] = {
    { 'a', "sqwz"   }, { 'b', "vghn"   }, { 'c', "xdfv"   },
    { 'd', "sfgxce" }, { 'e', "wsdr"   }, { 'f', "dgrtvc" },
    { 'g', "fhtybv" }, { 'h', "gjyun"  }, { 'i', "uojk"   },
    { 'j', "hkuim"  }, { 'k', "jloi"   }, { 'l', "kop"    },
    { 'm', "njk"    }, { 'n', "bhmj"   }, { 'o', "iklp"   },
    { 'p', "ol"     }, { 'q', "wa"     }, { 'r', "etfd"   },
    { 's', "adwxze" }, { 't', "ryfg"   }, { 'u', "yhji"   },
    { 'v', "cfgb"   }, { 'w', "qase"   }, { 'x', "zsdc"   },
    { 'y', "tghu"   }, { 'z', "asx"    },
};

#define NEARBY_SIZE (sizeof(NEARBY_KEYS) / sizeof(NEARBY_KEYS[0]))

char get_neighbor_key(char c) {
    char lower = (char)tolower((unsigned char)c);
    for (size_t i = 0; i < NEARBY_SIZE; i++) {
        if (NEARBY_KEYS[i].key == lower) {
            const char *n = NEARBY_KEYS[i].neighbors;
            size_t len = strlen(n);
            if (len == 0) return c;
            return n[rand() % len];
        }
    }
    return c;
}
