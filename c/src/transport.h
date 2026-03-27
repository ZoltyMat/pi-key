/*
 * transport.h — HID transport abstraction (Bluetooth + USB gadget)
 */
#ifndef PIKEY_TRANSPORT_H
#define PIKEY_TRANSPORT_H

#include <stdint.h>

typedef struct hid_transport hid_transport_t;

struct hid_transport {
    /* Connect the transport. Returns 0 on success. */
    int  (*connect)(hid_transport_t *t, const char *target);
    /* Disconnect. */
    void (*disconnect)(hid_transport_t *t);
    /* Send 8-byte keyboard report (report_id + modifier + reserved + 5 keys). */
    int  (*send_keyboard)(hid_transport_t *t, const uint8_t report[8]);
    /* Send mouse report: buttons, dx, dy, wheel. */
    int  (*send_mouse)(hid_transport_t *t, uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel);
    /* Opaque transport-specific data */
    void *priv;
};

/*
 * Create a Bluetooth HID transport using BlueZ L2CAP sockets.
 * PSM 17 (control) and PSM 19 (interrupt).
 * Registers SDP record for Logitech K380.
 * Caller must free with bt_transport_destroy().
 */
hid_transport_t *bt_transport_create(void);
void bt_transport_destroy(hid_transport_t *t);

/*
 * Create a USB gadget HID transport using ConfigFS.
 * Writes to /dev/hidg0 (keyboard) and /dev/hidg1 (mouse).
 * Caller must free with usb_transport_destroy().
 */
hid_transport_t *usb_transport_create(void);
void usb_transport_destroy(hid_transport_t *t);

#endif /* PIKEY_TRANSPORT_H */
