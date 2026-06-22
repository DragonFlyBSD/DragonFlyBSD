/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Abdelkader Boudih <dragonflybsd@seuros.com>
 */

/*
 * Apple IR Remote Control Driver
 *
 * USB driver for Apple IR receivers (USB HID, vendor 0x05ac).
 * Supports Apple Remote and generic IR remotes using NEC protocol.
 *
 * The Apple Remote protocol was reverse-engineered by James McKenzie and
 * others; key codes and packet format constants are derived from that work.
 *
 * Apple Remote Protocol (proprietary):
 *   Key down:    [0x25][0x87][0xee][remote_id][key_code]
 *   Key repeat:  [0x26][0x87][0xee][remote_id][key_code]
 *   Battery low: [0x25][0x87][0xe0][remote_id][0x00]
 *   Key decode:  (byte4 >> 1) & 0x0F -> keymap[index]
 *   Two-packet:  bit 6 of key_code (0x40) set -> store index, use on next keydown
 *
 * Generic IR Protocol (NEC-style):
 *   Format:     [0x26][0x7f][0x80][code][~code]
 *   Checksum:   code + ~code = 0xFF
 *
 * NO hardware key-up events -- synthesize via 125ms callout timer.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#include <bus/u4b/usb.h>
#include <bus/u4b/usbdi.h>
#include <bus/u4b/usbdi_util.h>
#include <bus/u4b/usbhid.h>

#include "usbdevs.h"

#define	USB_DEBUG_VAR apple_ir_debug
#include <bus/u4b/usb_debug.h>

#include <dev/misc/evdev/evdev.h>
#include <dev/misc/evdev/input.h>

#ifdef USB_DEBUG
static int apple_ir_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, apple_ir, CTLFLAG_RW, 0,
    "Apple IR Remote Control");
SYSCTL_INT(_hw_usb_apple_ir, OID_AUTO, debug, CTLFLAG_RWTUN,
    &apple_ir_debug, 0, "Debug level");
#endif

/* Protocol constants */
#define	APPLEIR_REPORT_LEN	5
#define	APPLEIR_KEY_MASK	0x0F
#define	APPLEIR_TWO_PKT_FLAG	0x40	/* bit 6: two-packet command */
#define	APPLEIR_KEYUP_TICKS	MAX(1, hz / 8)	/* 125ms */
#define	APPLEIR_TWOPKT_TICKS	MAX(1, hz / 4)	/* 250ms */
#define	APPLEIR_BUFFER_MAX	8

/* Report type markers (byte 0) */
#define	APPLEIR_PKT_KEYDOWN	0x25	/* key down / battery low */
#define	APPLEIR_PKT_REPEAT	0x26	/* key repeat / NEC generic */

/* Apple Remote signature (bytes 1-2) */
#define	APPLEIR_SIG_HI		0x87
#define	APPLEIR_SIG_KEYLO	0xee	/* normal key event */
#define	APPLEIR_SIG_BATTLO	0xe0	/* battery low event */

/* Generic IR NEC signature (bytes 1-2) */
#define	APPLEIR_NEC_HI		0x7f
#define	APPLEIR_NEC_LO		0x80
#define	APPLEIR_NEC_CHECKSUM	0xFF	/* code + ~code must equal this */

/*
 * Apple IR keymap: 17 entries, index = (key_code >> 1) & 0x0F
 * Based on protocol reverse-engineering by James McKenzie and others.
 */
static const uint16_t apple_ir_keymap[] = {
	KEY_RESERVED,		/* 0x00 */
	KEY_MENU,		/* 0x01 - menu */
	KEY_PLAYPAUSE,		/* 0x02 - play/pause */
	KEY_FORWARD,		/* 0x03 - >> */
	KEY_BACK,		/* 0x04 - << */
	KEY_VOLUMEUP,		/* 0x05 - + */
	KEY_VOLUMEDOWN,		/* 0x06 - - */
	KEY_RESERVED,		/* 0x07 */
	KEY_RESERVED,		/* 0x08 */
	KEY_RESERVED,		/* 0x09 */
	KEY_RESERVED,		/* 0x0A */
	KEY_RESERVED,		/* 0x0B */
	KEY_RESERVED,		/* 0x0C */
	KEY_RESERVED,		/* 0x0D */
	KEY_ENTER,		/* 0x0E - middle button (two-packet) */
	KEY_PLAYPAUSE,		/* 0x0F - play/pause (two-packet) */
	KEY_RESERVED,		/* 0x10 - out of range guard */
};
#define	APPLEIR_NKEYS	(nitems(apple_ir_keymap))

/*
 * Generic IR keymap (NEC protocol codes).
 * Maps raw NEC codes to evdev KEY_* codes.
 */
struct generic_ir_map {
	uint8_t		code;		/* NEC IR code */
	uint16_t	key;		/* evdev KEY_* */
};

static const struct generic_ir_map generic_keymap[] = {
	{ 0xe1, KEY_VOLUMEUP },
	{ 0xe9, KEY_VOLUMEDOWN },
	{ 0xed, KEY_CHANNELUP },
	{ 0xf3, KEY_CHANNELDOWN },
	{ 0xf5, KEY_PLAYPAUSE },
	{ 0xf9, KEY_POWER },
	{ 0xfb, KEY_MUTE },
	{ 0xfe, KEY_OK },
};
#define	GENERIC_NKEYS	(nitems(generic_keymap))

static uint16_t
generic_ir_lookup(uint8_t code)
{
	unsigned int i;

	for (i = 0; i < GENERIC_NKEYS; i++) {
		if (generic_keymap[i].code == code)
			return (generic_keymap[i].key);
	}
	return (KEY_RESERVED);
}

enum {
	APPLEIR_INTR_DT,
	APPLEIR_N_TRANSFER,
};

struct apple_ir_softc {
	device_t		sc_dev;
	struct usb_xfer		*sc_xfer[APPLEIR_N_TRANSFER];
	struct lock		sc_lock;
	struct evdev_dev	*sc_evdev;
	struct callout		sc_co;		/* key-up timer */
	struct callout		sc_twoco;	/* two-packet timeout */
	uint16_t		sc_current_key;	/* evdev keycode (0=none) */
	int			sc_prev_key_idx;/* two-packet state (0=none) */
	uint8_t			sc_batt_warned;
	uint8_t			sc_buf[APPLEIR_BUFFER_MAX];
};

/*
 * Callout: synthesize key-up event (no hardware key-up from remote).
 * Runs with sc_lock held (callout_init_lk).
 */
static void
apple_ir_keyup(void *arg)
{
	struct apple_ir_softc *sc = arg;

	KKASSERT(lockowned(&sc->sc_lock));

	if (sc->sc_current_key != 0) {
		evdev_push_key(sc->sc_evdev, sc->sc_current_key, 0);
		evdev_sync(sc->sc_evdev);
		sc->sc_current_key = 0;
		sc->sc_prev_key_idx = 0;
	}
}

static void
apple_ir_twopacket_timeout(void *arg)
{
	struct apple_ir_softc *sc = arg;

	KKASSERT(lockowned(&sc->sc_lock));
	sc->sc_prev_key_idx = 0;
}

/*
 * Process 5-byte USB interrupt report.
 */
static void
apple_ir_process_report(struct apple_ir_softc *sc, uint8_t *report, int len)
{
	int index;
	uint16_t new_key;

	if (len != APPLEIR_REPORT_LEN) {
		DPRINTFN(1, "bad report len: %d\n", len);
		return;
	}

	/* Battery low: [KEYDOWN][SIG_HI][SIG_BATTLO] -- log and ignore */
	if (report[0] == APPLEIR_PKT_KEYDOWN &&
	    report[1] == APPLEIR_SIG_HI && report[2] == APPLEIR_SIG_BATTLO) {
		if (!sc->sc_batt_warned) {
			device_printf(sc->sc_dev,
			    "remote battery may be low\n");
			sc->sc_batt_warned = 1;
		}
		return;
	}

	/* Key down: [KEYDOWN][SIG_HI][SIG_KEYLO][remote_id][key_code] */
	if (report[0] == APPLEIR_PKT_KEYDOWN &&
	    report[1] == APPLEIR_SIG_HI && report[2] == APPLEIR_SIG_KEYLO) {
		/* Release previous key if held */
		if (sc->sc_current_key != 0) {
			evdev_push_key(sc->sc_evdev, sc->sc_current_key, 0);
			evdev_sync(sc->sc_evdev);
			sc->sc_current_key = 0;
		}

		if (sc->sc_prev_key_idx > 0) {
			/* Second packet of a two-packet command */
			index = sc->sc_prev_key_idx;
			sc->sc_prev_key_idx = 0;
			callout_stop(&sc->sc_twoco);
		} else if (report[4] & APPLEIR_TWO_PKT_FLAG) {
			/* First packet -- wait for next */
			sc->sc_prev_key_idx = (report[4] >> 1) &
			    APPLEIR_KEY_MASK;
			callout_reset(&sc->sc_twoco, APPLEIR_TWOPKT_TICKS,
			    apple_ir_twopacket_timeout, sc);
			return;
		} else {
			index = (report[4] >> 1) & APPLEIR_KEY_MASK;
		}

		new_key = (index < (int)APPLEIR_NKEYS) ?
		    apple_ir_keymap[index] : KEY_RESERVED;
		if (new_key != KEY_RESERVED) {
			sc->sc_current_key = new_key;
			evdev_push_key(sc->sc_evdev, new_key, 1);
			evdev_sync(sc->sc_evdev);
			callout_reset(&sc->sc_co, APPLEIR_KEYUP_TICKS,
			    apple_ir_keyup, sc);
		}
		return;
	}

	/* Key repeat: [REPEAT][SIG_HI][SIG_KEYLO][remote_id][key_code] */
	if (report[0] == APPLEIR_PKT_REPEAT &&
	    report[1] == APPLEIR_SIG_HI && report[2] == APPLEIR_SIG_KEYLO) {
		uint16_t repeat_key;
		int repeat_idx;

		if (sc->sc_prev_key_idx > 0)
			return;
		if (report[4] & APPLEIR_TWO_PKT_FLAG)
			return;

		repeat_idx = (report[4] >> 1) & APPLEIR_KEY_MASK;
		repeat_key = (repeat_idx < (int)APPLEIR_NKEYS) ?
		    apple_ir_keymap[repeat_idx] : KEY_RESERVED;
		if (repeat_key == KEY_RESERVED ||
		    repeat_key != sc->sc_current_key)
			return;

		evdev_push_key(sc->sc_evdev, repeat_key, 1);
		evdev_sync(sc->sc_evdev);
		callout_reset(&sc->sc_co, APPLEIR_KEYUP_TICKS,
		    apple_ir_keyup, sc);
		return;
	}

	/* Generic IR (NEC protocol): [REPEAT][NEC_HI][NEC_LO][code][~code] */
	if (report[0] == APPLEIR_PKT_REPEAT &&
	    report[1] == APPLEIR_NEC_HI && report[2] == APPLEIR_NEC_LO) {
		uint8_t code = report[3];
		uint8_t checksum = report[4];

		sc->sc_prev_key_idx = 0;
		callout_stop(&sc->sc_twoco);

		if ((uint8_t)(code + checksum) != APPLEIR_NEC_CHECKSUM) {
			DPRINTFN(1, "generic IR: bad checksum %02x+%02x\n",
			    code, checksum);
			return;
		}

		new_key = generic_ir_lookup(code);
		if (new_key == KEY_RESERVED)
			return;

		if (sc->sc_current_key != new_key) {
			if (sc->sc_current_key != 0)
				evdev_push_key(sc->sc_evdev,
				    sc->sc_current_key, 0);
			sc->sc_current_key = new_key;
			evdev_push_key(sc->sc_evdev, new_key, 1);
			evdev_sync(sc->sc_evdev);
		} else {
			evdev_push_key(sc->sc_evdev, new_key, 1);
			evdev_sync(sc->sc_evdev);
		}
		callout_reset(&sc->sc_co, APPLEIR_KEYUP_TICKS,
		    apple_ir_keyup, sc);
		return;
	}

	DPRINTFN(1, "unknown report: %02x %02x %02x\n",
	    report[0], report[1], report[2]);
}

static void
apple_ir_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct apple_ir_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	int len;

	usbd_xfer_status(xfer, &len, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);
		if (len > (int)sizeof(sc->sc_buf))
			len = sizeof(sc->sc_buf);
		usbd_copy_out(pc, 0, sc->sc_buf, len);
		apple_ir_process_report(sc, sc->sc_buf, len);
		/* FALLTHROUGH */

	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;

	default:	/* Error */
		if (error != USB_ERR_CANCELLED) {
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static const struct usb_config apple_ir_config[APPLEIR_N_TRANSFER] = {
	[APPLEIR_INTR_DT] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {
			.pipe_bof = 0,
			.short_xfer_ok = 1,
		},
		.bufsize = APPLEIR_BUFFER_MAX,
		.callback = &apple_ir_intr_callback,
	},
};

/* Device match table */
static const struct usb_device_id apple_ir_devs[] = {
	{ USB_VPI(USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IR_RECV1, 0) },
	{ USB_VPI(USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IR_RECV2, 0) },
	{ USB_VPI(USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IR_RECV3, 0) },
	{ USB_VPI(USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IR_RECV4, 0) },
	{ USB_VPI(USB_VENDOR_APPLE, USB_PRODUCT_APPLE_IR_RECV5, 0) },
};

static int
apple_ir_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bInterfaceClass != UICLASS_HID)
		return (ENXIO);

	if (usbd_lookup_id_by_uaa(apple_ir_devs, sizeof(apple_ir_devs),
	    uaa) == 0)
		return (-20);	/* beat uhid(4) whose class match returns 0 */
	return (ENXIO);
}

static int
apple_ir_attach(device_t dev)
{
	struct apple_ir_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	unsigned int i;
	int error;

	sc->sc_dev = dev;
	sc->sc_current_key = 0;
	sc->sc_prev_key_idx = 0;
	sc->sc_batt_warned = 0;

	device_set_usb_desc(dev);
	lockinit(&sc->sc_lock, "apple_ir", 0, LK_CANRECURSE);
	callout_init_lk(&sc->sc_co, &sc->sc_lock);
	callout_init_lk(&sc->sc_twoco, &sc->sc_lock);

	error = usbd_transfer_setup(uaa->device, &uaa->info.bIfaceIndex,
	    sc->sc_xfer, apple_ir_config, APPLEIR_N_TRANSFER, sc, &sc->sc_lock);
	if (error != 0) {
		device_printf(dev, "USB transfer setup failed: %s\n",
		    usbd_errstr(error));
		goto fail;
	}

	sc->sc_evdev = evdev_alloc();
	evdev_set_name(sc->sc_evdev, "Apple IR Receiver");
	evdev_set_phys(sc->sc_evdev, device_get_nameunit(dev));
	evdev_set_id(sc->sc_evdev, BUS_USB,
	    uaa->info.idVendor, uaa->info.idProduct, 0);
	evdev_support_event(sc->sc_evdev, EV_SYN);
	evdev_support_event(sc->sc_evdev, EV_KEY);
	evdev_support_event(sc->sc_evdev, EV_REP);

	for (i = 0; i < APPLEIR_NKEYS; i++) {
		if (apple_ir_keymap[i] != KEY_RESERVED)
			evdev_support_key(sc->sc_evdev, apple_ir_keymap[i]);
	}
	for (i = 0; i < GENERIC_NKEYS; i++)
		evdev_support_key(sc->sc_evdev, generic_keymap[i].key);

	error = evdev_register_mtx(sc->sc_evdev, &sc->sc_lock);
	if (error != 0) {
		device_printf(dev, "evdev registration failed: %d\n", error);
		goto fail;
	}

	lockmgr(&sc->sc_lock, LK_EXCLUSIVE);
	usbd_transfer_start(sc->sc_xfer[APPLEIR_INTR_DT]);
	lockmgr(&sc->sc_lock, LK_RELEASE);

	return (0);

fail:
	if (sc->sc_evdev != NULL)
		evdev_free(sc->sc_evdev);
	usbd_transfer_unsetup(sc->sc_xfer, APPLEIR_N_TRANSFER);
	callout_drain(&sc->sc_co);
	callout_drain(&sc->sc_twoco);
	lockuninit(&sc->sc_lock);
	return (error);
}

static int
apple_ir_detach(device_t dev)
{
	struct apple_ir_softc *sc = device_get_softc(dev);

	lockmgr(&sc->sc_lock, LK_EXCLUSIVE);
	usbd_transfer_stop(sc->sc_xfer[APPLEIR_INTR_DT]);
	lockmgr(&sc->sc_lock, LK_RELEASE);

	usbd_transfer_unsetup(sc->sc_xfer, APPLEIR_N_TRANSFER);
	callout_drain(&sc->sc_co);
	callout_drain(&sc->sc_twoco);
	evdev_free(sc->sc_evdev);
	lockuninit(&sc->sc_lock);

	return (0);
}

static device_method_t apple_ir_methods[] = {
	DEVMETHOD(device_probe,		apple_ir_probe),
	DEVMETHOD(device_attach,	apple_ir_attach),
	DEVMETHOD(device_detach,	apple_ir_detach),
	DEVMETHOD_END
};

static driver_t apple_ir_driver = {
	.name = "apple_ir",
	.methods = apple_ir_methods,
	.size = sizeof(struct apple_ir_softc)
};

static devclass_t apple_ir_devclass;

DRIVER_MODULE(apple_ir, uhub, apple_ir_driver, apple_ir_devclass, NULL, NULL);
MODULE_DEPEND(apple_ir, usb, 1, 1, 1);
MODULE_VERSION(apple_ir, 1);
