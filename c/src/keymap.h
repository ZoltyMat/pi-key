/*
 * keymap.h — HID keycode mappings
 */
#ifndef PIKEY_KEYMAP_H
#define PIKEY_KEYMAP_H

#include <stdint.h>

/*
 * Fill report[8] with HID keyboard report for character c.
 * report layout: [report_id, modifier, reserved, key1..key6]
 * Returns 0 on success, -1 if character is unmapped.
 */
int char_to_hid_report(char c, uint8_t report[8]);

/*
 * Fill report[8] with an all-zeros release report.
 */
void release_report(uint8_t report[8]);

/*
 * Get a nearby QWERTY key for typo simulation.
 * Returns the neighbor character, or c itself if no neighbors defined.
 */
char get_neighbor_key(char c);

/* HID keycode for backspace */
#define HID_KEY_BACKSPACE 0x2A

#endif /* PIKEY_KEYMAP_H */
