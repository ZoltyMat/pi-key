/*
 * transport.c — HID transport implementations (Bluetooth L2CAP + USB gadget)
 */
#include "transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>

/* BlueZ headers (Linux only) */
#ifdef __linux__
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#endif

/* ── HID Descriptor (keyboard + mouse combo) ──────────────────────────────── */

static const uint8_t HID_DESCRIPTOR[] = {
    /* Keyboard */
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x03, 0x95, 0x06,
    0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07,
    0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x95, 0x05,
    0x75, 0x01, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03,
    0x91, 0x03, 0xC0,
    /* Mouse */
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02,
    0x09, 0x01, 0xA1, 0x00, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03,
    0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x05,
    0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
    0x95, 0x03, 0x81, 0x06, 0xC0, 0xC0,
};

/* ══════════════════════════════════════════════════════════════════════════════
 * Bluetooth Transport (BlueZ L2CAP)
 * ══════════════════════════════════════════════════════════════════════════════ */

#ifdef __linux__

#define PSM_CTRL 0x0011
#define PSM_INTR 0x0013

typedef struct {
    int ctrl_sock;   /* L2CAP control channel */
    int intr_sock;   /* L2CAP interrupt channel */
    int ctrl_server; /* Listening socket (control) */
    int intr_server; /* Listening socket (interrupt) */
} bt_priv_t;

static int bt_connect(hid_transport_t *t, const char *target) {
    bt_priv_t *priv = (bt_priv_t *)t->priv;
    struct sockaddr_l2 addr;

    if (target && target[0] != '\0') {
        /* Active connect to a known MAC */
        fprintf(stderr, "[bt] Connecting to %s...\n", target);

        priv->ctrl_sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
        if (priv->ctrl_sock < 0) {
            perror("[bt] socket(ctrl)");
            return -1;
        }

        memset(&addr, 0, sizeof(addr));
        addr.l2_family = AF_BLUETOOTH;
        addr.l2_psm = htobs(PSM_CTRL);
        str2ba(target, &addr.l2_bdaddr);

        if (connect(priv->ctrl_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("[bt] connect(ctrl)");
            return -1;
        }

        priv->intr_sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
        if (priv->intr_sock < 0) {
            perror("[bt] socket(intr)");
            return -1;
        }

        addr.l2_psm = htobs(PSM_INTR);
        if (connect(priv->intr_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("[bt] connect(intr)");
            return -1;
        }

        fprintf(stderr, "[bt] Connected to %s\n", target);
    } else {
        /* Listen mode: accept incoming connections */
        fprintf(stderr, "[bt] Listening for HID connections...\n");

        /* Control channel */
        priv->ctrl_server = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
        if (priv->ctrl_server < 0) {
            perror("[bt] socket(ctrl_server)");
            return -1;
        }

        int reuse = 1;
        setsockopt(priv->ctrl_server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        memset(&addr, 0, sizeof(addr));
        addr.l2_family = AF_BLUETOOTH;
        addr.l2_psm = htobs(PSM_CTRL);
        bacpy(&addr.l2_bdaddr, BDADDR_ANY);

        if (bind(priv->ctrl_server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("[bt] bind(ctrl)");
            return -1;
        }
        listen(priv->ctrl_server, 1);

        /* Interrupt channel */
        priv->intr_server = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
        if (priv->intr_server < 0) {
            perror("[bt] socket(intr_server)");
            return -1;
        }
        setsockopt(priv->intr_server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        addr.l2_psm = htobs(PSM_INTR);
        if (bind(priv->intr_server, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("[bt] bind(intr)");
            return -1;
        }
        listen(priv->intr_server, 1);

        /* Accept control */
        struct sockaddr_l2 peer;
        socklen_t peerlen = sizeof(peer);
        priv->ctrl_sock = accept(priv->ctrl_server, (struct sockaddr *)&peer, &peerlen);
        if (priv->ctrl_sock < 0) {
            perror("[bt] accept(ctrl)");
            return -1;
        }
        char peer_addr[18];
        ba2str(&peer.l2_bdaddr, peer_addr);
        fprintf(stderr, "[bt] Control connected from %s\n", peer_addr);

        /* Accept interrupt */
        priv->intr_sock = accept(priv->intr_server, (struct sockaddr *)&peer, &peerlen);
        if (priv->intr_sock < 0) {
            perror("[bt] accept(intr)");
            return -1;
        }
        fprintf(stderr, "[bt] Interrupt connected — HID ready\n");
    }

    return 0;
}

static void bt_disconnect(hid_transport_t *t) {
    bt_priv_t *priv = (bt_priv_t *)t->priv;
    if (priv->intr_sock >= 0) { close(priv->intr_sock); priv->intr_sock = -1; }
    if (priv->ctrl_sock >= 0) { close(priv->ctrl_sock); priv->ctrl_sock = -1; }
    if (priv->intr_server >= 0) { close(priv->intr_server); priv->intr_server = -1; }
    if (priv->ctrl_server >= 0) { close(priv->ctrl_server); priv->ctrl_server = -1; }
}

static int bt_send_keyboard(hid_transport_t *t, const uint8_t report[8]) {
    bt_priv_t *priv = (bt_priv_t *)t->priv;
    if (priv->intr_sock < 0) return -1;

    /* HID interrupt header (0xA1) + 8-byte report */
    uint8_t buf[9];
    buf[0] = 0xA1;
    memcpy(buf + 1, report, 8);

    ssize_t n = send(priv->intr_sock, buf, sizeof(buf), 0);
    return (n == sizeof(buf)) ? 0 : -1;
}

static int bt_send_mouse(hid_transport_t *t, uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
    bt_priv_t *priv = (bt_priv_t *)t->priv;
    if (priv->intr_sock < 0) return -1;

    /* HID interrupt header + report_id=2 + buttons + dx + dy + wheel */
    uint8_t buf[6];
    buf[0] = 0xA1;
    buf[1] = 0x02;  /* Report ID (mouse) */
    buf[2] = buttons;
    buf[3] = (uint8_t)dx;
    buf[4] = (uint8_t)dy;
    buf[5] = (uint8_t)wheel;

    ssize_t n = send(priv->intr_sock, buf, sizeof(buf), 0);
    return (n == sizeof(buf)) ? 0 : -1;
}

hid_transport_t *bt_transport_create(void) {
    hid_transport_t *t = calloc(1, sizeof(hid_transport_t));
    if (!t) return NULL;

    bt_priv_t *priv = calloc(1, sizeof(bt_priv_t));
    if (!priv) { free(t); return NULL; }

    priv->ctrl_sock = -1;
    priv->intr_sock = -1;
    priv->ctrl_server = -1;
    priv->intr_server = -1;

    t->connect = bt_connect;
    t->disconnect = bt_disconnect;
    t->send_keyboard = bt_send_keyboard;
    t->send_mouse = bt_send_mouse;
    t->priv = priv;

    return t;
}

void bt_transport_destroy(hid_transport_t *t) {
    if (!t) return;
    bt_disconnect(t);
    free(t->priv);
    free(t);
}

#else
/* Non-Linux stub */
hid_transport_t *bt_transport_create(void) {
    fprintf(stderr, "[bt] Bluetooth transport requires Linux\n");
    return NULL;
}
void bt_transport_destroy(hid_transport_t *t) { (void)t; }
#endif /* __linux__ */


/* ══════════════════════════════════════════════════════════════════════════════
 * USB Gadget Transport (ConfigFS + /dev/hidgX)
 * ══════════════════════════════════════════════════════════════════════════════ */

#define USB_GADGET_BASE "/sys/kernel/config/usb_gadget/pikey"
#define HIDG_KEYBOARD   "/dev/hidg0"
#define HIDG_MOUSE      "/dev/hidg1"

typedef struct {
    int fd_keyboard;
    int fd_mouse;
} usb_priv_t;

/* Helper: write a string to a sysfs/configfs file */
static int write_file(const char *path, const char *data, size_t len) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "[usb] open(%s): %s\n", path, strerror(errno));
        return -1;
    }
    ssize_t n = write(fd, data, len);
    close(fd);
    return (n >= 0) ? 0 : -1;
}

/* Helper: write binary data to a configfs attribute */
static int write_file_bin(const char *path, const uint8_t *data, size_t len) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        fprintf(stderr, "[usb] open(%s): %s\n", path, strerror(errno));
        return -1;
    }
    ssize_t n = write(fd, data, len);
    close(fd);
    return ((size_t)n == len) ? 0 : -1;
}

/* Helper: mkdir if not exists */
static void ensure_dir(const char *path) {
    /* Use system mkdir -p for simplicity */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", path);
    (void)system(cmd);
}

static int usb_setup_gadget(void) {
    /*
     * Set up USB gadget via ConfigFS.
     * This requires root and the libcomposite kernel module.
     */
    char path[512];

    /* Load module */
    (void)system("modprobe libcomposite 2>/dev/null");

    /* Create gadget */
    ensure_dir(USB_GADGET_BASE);

    /* IDs — Logitech K380 */
    snprintf(path, sizeof(path), "%s/idVendor", USB_GADGET_BASE);
    write_file(path, "0x046d", 6);

    snprintf(path, sizeof(path), "%s/idProduct", USB_GADGET_BASE);
    write_file(path, "0xb342", 6);

    snprintf(path, sizeof(path), "%s/bcdDevice", USB_GADGET_BASE);
    write_file(path, "0x0100", 6);

    snprintf(path, sizeof(path), "%s/bcdUSB", USB_GADGET_BASE);
    write_file(path, "0x0200", 6);

    /* Strings */
    snprintf(path, sizeof(path), "%s/strings/0x409", USB_GADGET_BASE);
    ensure_dir(path);

    snprintf(path, sizeof(path), "%s/strings/0x409/serialnumber", USB_GADGET_BASE);
    write_file(path, "0000000001", 10);

    snprintf(path, sizeof(path), "%s/strings/0x409/manufacturer", USB_GADGET_BASE);
    write_file(path, "Logitech", 8);

    snprintf(path, sizeof(path), "%s/strings/0x409/product", USB_GADGET_BASE);
    write_file(path, "K380 Multi-Device Keyboard", 26);

    /* Configuration */
    snprintf(path, sizeof(path), "%s/configs/c.1/strings/0x409", USB_GADGET_BASE);
    ensure_dir(path);

    snprintf(path, sizeof(path), "%s/configs/c.1/strings/0x409/configuration", USB_GADGET_BASE);
    write_file(path, "PiKey HID Config", 16);

    snprintf(path, sizeof(path), "%s/configs/c.1/MaxPower", USB_GADGET_BASE);
    write_file(path, "250", 3);

    /* HID function 0 — keyboard */
    snprintf(path, sizeof(path), "%s/functions/hid.keyboard", USB_GADGET_BASE);
    ensure_dir(path);

    snprintf(path, sizeof(path), "%s/functions/hid.keyboard/protocol", USB_GADGET_BASE);
    write_file(path, "1", 1);  /* keyboard */

    snprintf(path, sizeof(path), "%s/functions/hid.keyboard/subclass", USB_GADGET_BASE);
    write_file(path, "1", 1);  /* boot interface */

    snprintf(path, sizeof(path), "%s/functions/hid.keyboard/report_length", USB_GADGET_BASE);
    write_file(path, "8", 1);

    /* Write keyboard portion of HID descriptor */
    snprintf(path, sizeof(path), "%s/functions/hid.keyboard/report_desc", USB_GADGET_BASE);
    write_file_bin(path, HID_DESCRIPTOR, 63);  /* keyboard descriptor ends at byte 63 */

    /* HID function 1 — mouse */
    snprintf(path, sizeof(path), "%s/functions/hid.mouse", USB_GADGET_BASE);
    ensure_dir(path);

    snprintf(path, sizeof(path), "%s/functions/hid.mouse/protocol", USB_GADGET_BASE);
    write_file(path, "2", 1);  /* mouse */

    snprintf(path, sizeof(path), "%s/functions/hid.mouse/subclass", USB_GADGET_BASE);
    write_file(path, "1", 1);  /* boot interface */

    snprintf(path, sizeof(path), "%s/functions/hid.mouse/report_length", USB_GADGET_BASE);
    write_file(path, "5", 1);

    /* Write mouse portion of HID descriptor */
    snprintf(path, sizeof(path), "%s/functions/hid.mouse/report_desc", USB_GADGET_BASE);
    write_file_bin(path, HID_DESCRIPTOR + 63, sizeof(HID_DESCRIPTOR) - 63);

    /* Link functions to configuration */
    char src[512], dst[512];

    snprintf(src, sizeof(src), "%s/functions/hid.keyboard", USB_GADGET_BASE);
    snprintf(dst, sizeof(dst), "%s/configs/c.1/hid.keyboard", USB_GADGET_BASE);
    (void)symlink(src, dst);

    snprintf(src, sizeof(src), "%s/functions/hid.mouse", USB_GADGET_BASE);
    snprintf(dst, sizeof(dst), "%s/configs/c.1/hid.mouse", USB_GADGET_BASE);
    (void)symlink(src, dst);

    /* Bind to UDC */
    snprintf(path, sizeof(path), "%s/UDC", USB_GADGET_BASE);
    /* Find available UDC */
    FILE *fp = popen("ls /sys/class/udc/ 2>/dev/null | head -1", "r");
    if (fp) {
        char udc[128] = {0};
        if (fgets(udc, sizeof(udc), fp)) {
            /* Strip newline */
            size_t len = strlen(udc);
            if (len > 0 && udc[len - 1] == '\n') udc[len - 1] = '\0';
            if (udc[0]) {
                write_file(path, udc, strlen(udc));
                fprintf(stderr, "[usb] Bound to UDC: %s\n", udc);
            }
        }
        pclose(fp);
    }

    return 0;
}

static int usb_connect(hid_transport_t *t, const char *target) {
    (void)target;
    usb_priv_t *priv = (usb_priv_t *)t->priv;

    /* Setup ConfigFS gadget */
    if (usb_setup_gadget() < 0) {
        fprintf(stderr, "[usb] Failed to setup USB gadget (need root + libcomposite)\n");
        return -1;
    }

    /* Open HID device files */
    priv->fd_keyboard = open(HIDG_KEYBOARD, O_WRONLY);
    if (priv->fd_keyboard < 0) {
        fprintf(stderr, "[usb] open(%s): %s\n", HIDG_KEYBOARD, strerror(errno));
        return -1;
    }

    priv->fd_mouse = open(HIDG_MOUSE, O_WRONLY);
    if (priv->fd_mouse < 0) {
        fprintf(stderr, "[usb] open(%s): %s\n", HIDG_MOUSE, strerror(errno));
        close(priv->fd_keyboard);
        priv->fd_keyboard = -1;
        return -1;
    }

    fprintf(stderr, "[usb] USB gadget HID ready\n");
    return 0;
}

static void usb_disconnect(hid_transport_t *t) {
    usb_priv_t *priv = (usb_priv_t *)t->priv;
    if (priv->fd_keyboard >= 0) { close(priv->fd_keyboard); priv->fd_keyboard = -1; }
    if (priv->fd_mouse >= 0) { close(priv->fd_mouse); priv->fd_mouse = -1; }
}

static int usb_send_keyboard(hid_transport_t *t, const uint8_t report[8]) {
    usb_priv_t *priv = (usb_priv_t *)t->priv;
    if (priv->fd_keyboard < 0) return -1;

    /* USB gadget expects the report without the 0xA1 header.
     * report[0] is report_id, followed by modifier + reserved + keys */
    ssize_t n = write(priv->fd_keyboard, report, 8);
    return (n == 8) ? 0 : -1;
}

static int usb_send_mouse(hid_transport_t *t, uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel) {
    usb_priv_t *priv = (usb_priv_t *)t->priv;
    if (priv->fd_mouse < 0) return -1;

    uint8_t buf[5];
    buf[0] = 0x02;  /* Report ID (mouse) */
    buf[1] = buttons;
    buf[2] = (uint8_t)dx;
    buf[3] = (uint8_t)dy;
    buf[4] = (uint8_t)wheel;

    ssize_t n = write(priv->fd_mouse, buf, sizeof(buf));
    return (n == sizeof(buf)) ? 0 : -1;
}

hid_transport_t *usb_transport_create(void) {
    hid_transport_t *t = calloc(1, sizeof(hid_transport_t));
    if (!t) return NULL;

    usb_priv_t *priv = calloc(1, sizeof(usb_priv_t));
    if (!priv) { free(t); return NULL; }

    priv->fd_keyboard = -1;
    priv->fd_mouse = -1;

    t->connect = usb_connect;
    t->disconnect = usb_disconnect;
    t->send_keyboard = usb_send_keyboard;
    t->send_mouse = usb_send_mouse;
    t->priv = priv;

    return t;
}

void usb_transport_destroy(hid_transport_t *t) {
    if (!t) return;
    usb_disconnect(t);
    free(t->priv);
    free(t);
}
