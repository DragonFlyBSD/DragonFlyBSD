/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * CYAPA - Cypress APA trackpad with I2C Interface driver
 *
 * Written from scratch but referenced the linux cyapa.c driver to
 * figure out the bootstrapping and commands.
 *
 * Unable to locate any datasheet for the device.
 *
 * Attaches under smbus but uses an I2C protocol (no count field).
 * This driver should override the "smb" device for the specific unit
 * we validate against (smb0-67).
 *
 * xorg.conf:
 *
 * Section "InputDevice"
 *         Identifier  "Mouse0"
 *         Driver      "mouse"
 *         Option      "Protocol" "imps/2"		(slider)
 * #       Option      "Protocol" "ps/2"		(basic mouse)
 * #       Option      "Protocol" "explorerps/2"	(not working well yet)
 *							(for b4/b5)
 *         Option      "Device" "/dev/cyapa0-67"
 * EndSection
 *
 * NOTE: In explorerps/2 mode the slider has only 4 bits of delta resolution
 *	 and may not work as smoothly.  Buttons are recognized as button
 *	 8 and button 9.
 *
 *				    FEATURES
 *
 * Jitter supression	- Implements 2-pixel hysteresis with memory.
 *
 * False-finger supression- Two-fingers-down does not emulate anything,
 *			    on purpose.
 *
 * False-emulated button handling-
 *			  Buttons are emulated when three fingers are
 *			  placed on the pad.  If you place all three
 *			  fingers down simultaniously, this condition
 *			  is detected and will not emulate any button.
 *
 * Slider jesture	- Tap right hand side and slide up or down.
 *
 *			  (Three finger jestures)
 * left button jesture	- Two fingers down on the left, tap/hold right
 * middle button jesture- Two fingers down left & right, tap/hold middle
 * right button jesture - Two fingers down on the right, tap/hold left
 *
 * track-pad button     - Tap/push physical button, left, middle, or right
 *			  side of the trackpad will issue a LEFT, MIDDLE, or
 *			  RIGHT button event.
 *
 * track-pad button     - Any tap/slide of more than 32 pixels and pushing
 *			  harder to articulate the trackpad physical button
 *			  always issues a LEFT button event.
 *
 * first-finger tracking- The X/Y coordinates always track the first finger
 *			  down.  If you have multiple fingers down and lift
 *			  up the designated first finger, a new designated
 *			  first finger will be selected without causing the
 *			  mouse to jump (delta's are reset).
 *
 *				WARNINGS
 *
 * These trackpads get confused when three or more fingers are down on the
 * same horizontal axis and will start to glitch the finger detection.
 * Removing your hand for a few seconds will allow the trackpad to
 * recalibrate.  Generally speaking, when using three or more fingers
 * please try to place at least one finger off-axis (a little above or
 * below) the other two.
 *
 * button-4/button-5 'claw' (4 and 5-finger) sequences have similar
 * problems.
 */
#include <sys/kernel.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
/*#include <sys/input.h>*/
#include <sys/vnode.h>
#include <sys/sysctl.h>
#include <sys/event.h>
#include <sys/devfs.h>

#include <bus/smbus/smbconf.h>
#include <bus/smbus/smbus.h>
#include "cyapa.h"

#include "smbus_if.h"
#include "bus_if.h"
#include "device_if.h"

#define CYAPA_BUFSIZE	128			/* power of 2 */
#define CYAPA_BUFMASK	(CYAPA_BUFSIZE - 1)

#define ZSCALE		10

struct cyapa_fifo {
	int	rindex;
	int	windex;
	char	buf[CYAPA_BUFSIZE];
};

struct cyapa_softc {
	device_t dev;
	int	count;			/* >0 if device opened */
	int	unit;
	int	addr;
	cdev_t	devnode;
	struct kqinfo kqinfo;
	struct lock lk;

	int	cap_resx;
	int	cap_resy;
	int	cap_phyx;
	int	cap_phyy;
	uint8_t	cap_buttons;

	int	poll_flags;
	thread_t poll_td;
#if 0
	struct inputev iev;		/* subr_input.c */
#endif

	/*
	 * PS/2 mouse emulation
	 */
	short	track_x;		/* current tracking */
	short	track_y;
	short	track_z;
	uint16_t track_but;
	char 	track_id1;		/* first finger id */
	char 	track_id2;		/* second finger id */
	int	track_nfingers;
	short	delta_x;		/* accumulation -> report */
	short	delta_y;
	short	delta_z;
	short	fuzz_x;
	short	fuzz_y;
	short	fuzz_z;
	short	touch_x;		/* touch down coordinates */
	short	touch_y;
	short	touch_z;
	int	finger1_ticks;
	int	finger2_ticks;
	int	finger3_ticks;
	uint16_t reported_but;

	struct cyapa_fifo rfifo;	/* device->host */
	struct cyapa_fifo wfifo;	/* host->device */
	uint8_t	ps2_cmd;		/* active p2_cmd waiting for data */
	uint8_t ps2_acked;
	int	active_tick;
	int	data_signal;
	int	blocked;
	int	reporting_mode;		/* 0=disabled 1=enabled */
	int	scaling_mode;		/* 0=1:1 1=2:1 */
	int	remote_mode;		/* 0 for streaming mode */
	int	resolution;		/* count/mm */
	int	sample_rate;		/* samples/sec */
	int	zenabled;		/* z-axis enabled (mode 1 or 2) */
	int	poll_ticks;
};

#define CYPOLL_SHUTDOWN	0x0001

#define SIMULATE_BUT4	0x0100
#define SIMULATE_BUT5	0x0200
#define SIMULATE_LOCK	0x8000

static void cyapa_poll_thread(void *arg);
static int cyapa_raw_input(struct cyapa_softc *sc, struct cyapa_regs *regs);

static int fifo_empty(struct cyapa_fifo *fifo);
static size_t fifo_ready(struct cyapa_fifo *fifo);
#if 0
static size_t fifo_total_ready(struct cyapa_fifo *fifo);
#endif
static char *fifo_read(struct cyapa_fifo *fifo, size_t n);
static char *fifo_write(struct cyapa_fifo *fifo, size_t n);
static uint8_t fifo_read_char(struct cyapa_fifo *fifo);
static void fifo_write_char(struct cyapa_fifo *fifo, uint8_t c);
static size_t fifo_space(struct cyapa_fifo *fifo);
static void fifo_reset(struct cyapa_fifo *fifo);

static short cyapa_fuzz(short delta, short *fuzz);

static int cyapa_idle_freq = 1;
SYSCTL_INT(_debug, OID_AUTO, cyapa_idle_freq, CTLFLAG_RW,
		&cyapa_idle_freq, 0, "");
static int cyapa_slow_freq = 20;
SYSCTL_INT(_debug, OID_AUTO, cyapa_slow_freq, CTLFLAG_RW,
		&cyapa_slow_freq, 0, "");
static int cyapa_norm_freq = 100;
SYSCTL_INT(_debug, OID_AUTO, cyapa_norm_freq, CTLFLAG_RW,
		&cyapa_norm_freq, 0, "");
static int cyapa_minpressure = 16;
SYSCTL_INT(_debug, OID_AUTO, cyapa_minpressure, CTLFLAG_RW,
		&cyapa_minpressure, 0, "");

static int cyapa_debug = 0;
SYSCTL_INT(_debug, OID_AUTO, cyapa_debug, CTLFLAG_RW,
		&cyapa_debug, 0, "");

static
void
cyapa_lock(struct cyapa_softc *sc)
{
	lockmgr(&sc->lk, LK_EXCLUSIVE);
}

static
void
cyapa_unlock(struct cyapa_softc *sc)
{
	lockmgr(&sc->lk, LK_RELEASE);
}

/*
 * Notify if possible receive data ready.  Must be called
 * without the lock held to avoid deadlocking in kqueue.
 */
static
void
cyapa_notify(struct cyapa_softc *sc)
{
	if (sc->data_signal || !fifo_empty(&sc->rfifo)) {
		KNOTE(&sc->kqinfo.ki_note, 0);
		if (sc->blocked) {
			cyapa_lock(sc);
			sc->blocked = 0;
			wakeup(&sc->blocked);
			cyapa_unlock(sc);
		}
	}
}

/*
 * Initialize the device
 */
static
int
init_device(device_t dev, struct cyapa_cap *cap, int addr, int probe)
{
	static char bl_exit[] = {
			0x00, 0xff, 0xa5, 0x00, 0x01,
			0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
	static char bl_deactivate[] = {
			0x00, 0xff, 0x3b, 0x00, 0x01,
			0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };
	device_t bus;
	struct cyapa_boot_regs boot;
	int error;
	int retries;


	bus = device_get_parent(dev);	/* smbus */

	/*
	 * Get status
	 */
	error = smbus_trans(bus, addr, CMD_BOOT_STATUS,
			    SMB_TRANS_NOCNT | SMB_TRANS_7BIT,
			    NULL, 0, (void *)&boot, sizeof(boot), NULL);
	if (error)
		goto done;

	/*
	 * Bootstrap the device if necessary.  It can take up to 2 seconds
	 * for the device to fully initialize.
	 */
	retries = 2 * 10;
	while ((boot.stat & CYAPA_STAT_RUNNING) == 0 && retries > 0) {
		if (boot.boot & CYAPA_BOOT_BUSY) {
			/*
			 * Busy, wait loop.
			 */
		} else if (boot.error & CYAPA_ERROR_BOOTLOADER) {
			/*
			 * Magic
			 */
			error = smbus_trans(bus, addr, CMD_BOOT_STATUS,
					    SMB_TRANS_NOCNT | SMB_TRANS_7BIT,
					    bl_deactivate,
					    sizeof(bl_deactivate),
					    NULL, 0, NULL);
			if (error)
				goto done;
		} else {
			/*
			 * Magic
			 */
			error = smbus_trans(bus, addr, CMD_BOOT_STATUS,
					    SMB_TRANS_NOCNT | SMB_TRANS_7BIT,
					    bl_exit,
					    sizeof(bl_exit),
					    NULL, 0, NULL);
			if (error)
				goto done;
		}
		tsleep(&error, 0, "cyapabt", hz / 10);
		--retries;
		error = smbus_trans(bus, addr, CMD_BOOT_STATUS,
				    SMB_TRANS_NOCNT | SMB_TRANS_7BIT,
				    NULL, 0, (void *)&boot, sizeof(boot), NULL);
		if (error)
			goto done;
	}

	if (retries == 0) {
		device_printf(dev, "Unable to bring device out of bootstrap\n");
		error = ENXIO;
		goto done;
	}

	/*
	 * Check identity
	 */
	error = smbus_trans(bus, addr, CMD_QUERY_CAPABILITIES,
			    SMB_TRANS_NOCNT | SMB_TRANS_7BIT,
			    NULL, 0, (void *)cap, sizeof(*cap), NULL);

	if (strncmp(cap->prod_ida, "CYTRA", 5) != 0) {
		device_printf(dev, "Product ID \"%5.5s\" mismatch\n",
			     cap->prod_ida);
		error = ENXIO;
	}

done:
	if (error)
		device_printf(dev, "Unable to initialize\n");
	return error;
}

/*
 * Device infrastructure
 */
#define CYAPA_SOFTC(unit) \
	((struct cyapa_softc *)devclass_get_softc(cyapa_devclass, (unit)))

static void cyapa_identify(driver_t *driver, device_t parent);
static int cyapa_probe(device_t);
static int cyapa_attach(device_t);
static int cyapa_detach(device_t);

static devclass_t cyapa_devclass;

static device_method_t cyapa_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	cyapa_identify),
	DEVMETHOD(device_probe,		cyapa_probe),
	DEVMETHOD(device_attach,	cyapa_attach),
	DEVMETHOD(device_detach,	cyapa_detach),

#if 0
	/* smbus interface */
	DEVMETHOD(smbus_intr,		smbus_generic_intr),
#endif

	DEVMETHOD_END
};

static driver_t cyapa_driver = {
	"cyapa",
	cyapa_methods,
	sizeof(struct cyapa_softc),
};

static	d_open_t	cyapaopen;
static	d_close_t	cyapaclose;
static	d_ioctl_t	cyapaioctl;
static	d_read_t	cyaparead;
static	d_write_t	cyapawrite;
static	d_kqfilter_t	cyapakqfilter;

static struct dev_ops cyapa_ops = {
	{ "cyapa", 0, 0 },
	.d_open =	cyapaopen,
	.d_close =	cyapaclose,
	.d_ioctl =	cyapaioctl,
	.d_read =	cyaparead,
	.d_write =	cyapawrite,
	.d_kqfilter =	cyapakqfilter,
};

static void
cyapa_identify(driver_t *driver, device_t parent)
{
	if (device_find_child(parent, "cyapa", -1) == NULL)
		BUS_ADD_CHILD(parent, parent, 0, "cyapa", -1);
}

static int
cyapa_probe(device_t dev)
{
	struct cyapa_cap cap;
	int unit;
	int addr;
	int error;

	unit = device_get_unit(dev);

	/*
	 * Only match against specific addresses to avoid blowing up
	 * other I2C devices (?).  At least for now.
	 *
	 * 0x400 (from smbus) - means specific device address probe,
	 *			rather than generic.
	 *
	 * 0x67 - cypress trackpad on the acer c720.
	 */
	if ((unit & 0x04FF) != (0x0400 | 0x067))
		return ENXIO;
	addr = unit & 0x3FF;
	error = init_device(dev, &cap, addr, 1);
	if (error)
		return ENXIO;

	device_set_desc(dev, "Cypress APA I2C Trackpad");

	return (BUS_PROBE_VENDOR);
}

static int
cyapa_attach(device_t dev)
{
	struct cyapa_softc *sc = (struct cyapa_softc *)device_get_softc(dev);
	struct cyapa_cap cap;
	int unit;
	int addr;

	if (!sc)
		return ENOMEM;

	bzero(sc, sizeof(struct cyapa_softc *));

	lockinit(&sc->lk, "cyapa", 0, 0);
	sc->reporting_mode = 1;

	unit = device_get_unit(dev);
	if ((unit & 0x04FF) != (0x0400 | 0x067))
		return ENXIO;
	addr = unit & 0x3FF;
	if (init_device(dev, &cap, addr, 0))
		return ENXIO;

	sc->dev = dev;
	sc->unit = unit;
	sc->addr = addr;

	if (unit & 0x0400) {
		sc->devnode = make_dev(&cyapa_ops, unit,
				UID_ROOT, GID_WHEEL, 0600,
				"cyapa%d-%02x", unit >> 11, unit & 1023);
	} else {
		sc->devnode = make_dev(&cyapa_ops, unit,
				UID_ROOT, GID_WHEEL, 0600, "cyapa%d", unit);
	}

	sc->cap_resx = ((cap.max_abs_xy_high << 4) & 0x0F00) |
			cap.max_abs_x_low;
	sc->cap_resy = ((cap.max_abs_xy_high << 8) & 0x0F00) |
			cap.max_abs_y_low;
	sc->cap_phyx = ((cap.phy_siz_xy_high << 4) & 0x0F00) |
			cap.phy_siz_x_low;
	sc->cap_phyy = ((cap.phy_siz_xy_high << 8) & 0x0F00) |
			cap.phy_siz_y_low;
	sc->cap_buttons = cap.buttons;

	device_printf(dev, "%5.5s-%6.6s-%2.2s buttons=%c%c%c res=%dx%d\n",
		cap.prod_ida, cap.prod_idb, cap.prod_idc,
		((cap.buttons & CYAPA_FNGR_LEFT) ? 'L' : '-'),
		((cap.buttons & CYAPA_FNGR_MIDDLE) ? 'M' : '-'),
		((cap.buttons & CYAPA_FNGR_RIGHT) ? 'R' : '-'),
		sc->cap_resx,
		sc->cap_resy);

	/*
	 * Setup input event tracking
	 */
#if 0
	inputev_init(&sc->iev, "Cypress APA Trackpad (cyapa)");
	inputev_set_evbit(&sc->iev, EV_ABS);
	inputev_set_abs_params(&sc->iev, ABS_MT_POSITION_X,
			       0, sc->cap_resx, 0, 0);
	inputev_set_abs_params(&sc->iev, ABS_MT_POSITION_Y,
			       0, sc->cap_resy, 0, 0);
	inputev_set_abs_params(&sc->iev, ABS_MT_PRESSURE,
			       0, 255, 0, 0);
	if (sc->cap_phyx)
		inputev_set_res(&sc->iev, ABS_MT_POSITION_X,
				sc->cap_resx / sc->cap_phyx);
	if (sc->cap_phyy)
		inputev_set_res(&sc->iev, ABS_MT_POSITION_Y,
				sc->cap_resy / sc->cap_phyy);
	if (cap.buttons & CYAPA_FNGR_LEFT) {
		inputev_set_keybit(&sc->iev, BTN_LEFT);
		inputev_set_propbit(&sc->iev, INPUT_PROP_BUTTONPAD);
	}
	if (cap.buttons & CYAPA_FNGR_MIDDLE)
		inputev_set_keybit(&sc->iev, BTN_MIDDLE);
	if (cap.buttons & CYAPA_FNGR_RIGHT)
		inputev_set_keybit(&sc->iev, BTN_RIGHT);

	inputev_register(&sc->iev);
#endif

	/*
	 * Start the polling thread.
	 */
	lwkt_create(cyapa_poll_thread, sc,
		    &sc->poll_td, NULL, 0, -1, "cyapa-poll");

	return (0);
}

static int
cyapa_detach(device_t dev)
{
	struct cyapa_softc *sc = (struct cyapa_softc *)device_get_softc(dev);

#if 0
	/*
	 * Cleanup input event tracking
	 */
	inputev_deregister(&sc->iev);
#endif

	/*
	 * Cleanup our poller thread
	 */
	atomic_set_int(&sc->poll_flags, CYPOLL_SHUTDOWN);
	while (sc->poll_td) {
		wakeup(&sc->poll_flags);
		tsleep(&sc->poll_td, 0, "cyapadet", hz);
	}

	if (sc->devnode)
		dev_ops_remove_minor(&cyapa_ops, device_get_unit(dev));
	if (sc->devnode)
		devfs_assume_knotes(sc->devnode, &sc->kqinfo);

	return (0);
}

/*
 * USER DEVICE I/O FUNCTIONS
 */
static int
cyapaopen (struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct cyapa_softc *sc = CYAPA_SOFTC(minor(dev));

	if (sc == NULL)
		return (ENXIO);

	if (sc->count != 0)
		return (EBUSY);

	sc->count++;

	return (0);
}

static int
cyapaclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct cyapa_softc *sc = CYAPA_SOFTC(minor(dev));

	if (sc == NULL)
		return (ENXIO);

	if (sc->count == 0)
		/* This is not supposed to happen. */
		return (0);

	sc->count--;

	return (0);
}

static int
cyaparead(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct cyapa_softc *sc = CYAPA_SOFTC(minor(dev));
	int error;
	struct uio *uio = ap->a_uio;
	int ioflag = ap->a_ioflag;
	int didread;
	size_t n;

	/*
	 * If buffer is empty, load a new event if it is ready
	 */
	cyapa_lock(sc);
again:
	if (fifo_empty(&sc->rfifo) &&
	    (sc->data_signal || sc->delta_x || sc->delta_y ||
	     sc->track_but != sc->reported_but)) {
		uint8_t c0;
		uint16_t but;
		short delta_x;
		short delta_y;
		short delta_z;

		/*
		 * Accumulate delta_x, delta_y.
		 */
		sc->data_signal = 0;
		delta_x = sc->delta_x;
		delta_y = sc->delta_y;
		delta_z = sc->delta_z;
		if (delta_x > 255) {
			delta_x = 255;
			sc->data_signal = 1;
		}
		if (delta_x < -256) {
			delta_x = -256;
			sc->data_signal = 1;
		}
		if (delta_y > 255) {
			delta_y = 255;
			sc->data_signal = 1;
		}
		if (delta_y < -256) {
			delta_y = -256;
			sc->data_signal = 1;
		}
		if (delta_z > 255) {
			delta_z = 255;
			sc->data_signal = 1;
		}
		if (delta_z < -256) {
			delta_z = -256;
			sc->data_signal = 1;
		}
		but = sc->track_but;

		/*
		 * Adjust baseline for next calculation
		 */
		sc->delta_x -= delta_x;
		sc->delta_y -= delta_y;
		sc->delta_z -= delta_z;
		sc->reported_but = but;

		/*
		 * Fuzz reduces movement jitter by introducing some
		 * hysteresis.  It operates without cumulative error so
		 * if you swish around quickly and return your finger to
		 * where it started, so to will the mouse.
		 */
		delta_x = cyapa_fuzz(delta_x, &sc->fuzz_x);
		delta_y = cyapa_fuzz(delta_y, &sc->fuzz_y);
		delta_z = cyapa_fuzz(delta_z, &sc->fuzz_z);

		/*
		 * Generate report
		 */
		c0 = 0;
		if (delta_x < 0)
			c0 |= 0x10;
		if (delta_y < 0)
			c0 |= 0x20;
		c0 |= 0x08;
		if (but & CYAPA_FNGR_LEFT)
			c0 |= 0x01;
		if (but & CYAPA_FNGR_MIDDLE)
			c0 |= 0x04;
		if (but & CYAPA_FNGR_RIGHT)
			c0 |= 0x02;

		fifo_write_char(&sc->rfifo, c0);
		fifo_write_char(&sc->rfifo, (uint8_t)delta_x);
		fifo_write_char(&sc->rfifo, (uint8_t)delta_y);
		switch(sc->zenabled) {
		case 1:
			/*
			 * Z axis all 8 bits
			 */
			fifo_write_char(&sc->rfifo, (uint8_t)delta_z);
			break;
		case 2:
			/*
			 * Z axis low 4 bits + 4th button and 5th button
			 * (high 2 bits must be left 0).  Auto-scale
			 * delta_z to fit to avoid a wrong-direction
			 * overflow (don't try to retain the remainder).
			 */
			while (delta_z > 7 || delta_z < -8)
				delta_z >>= 1;
			c0 = (uint8_t)delta_z & 0x0F;
			if (but & SIMULATE_BUT4)
				c0 |= 0x10;
			if (but & SIMULATE_BUT5)
				c0 |= 0x20;
			fifo_write_char(&sc->rfifo, c0);
			break;
		default:
			/* basic PS/2 */
			break;
		}
		cyapa_unlock(sc);
		cyapa_notify(sc);
		cyapa_lock(sc);
	}

	/*
	 * Blocking / Non-blocking
	 */
	error = 0;
	didread = (uio->uio_resid == 0);

	while ((ioflag & IO_NDELAY) == 0 && fifo_empty(&sc->rfifo)) {
		if (sc->data_signal)
			goto again;
		sc->blocked = 1;
		error = lksleep(&sc->blocked, &sc->lk, PCATCH, "cyablk", 0);
		if (error)
			break;
	}

	/*
	 * Return any buffered data
	 */
	while (error == 0 && uio->uio_resid &&
	       (n = fifo_ready(&sc->rfifo)) > 0) {
		if (n > uio->uio_resid)
			n = uio->uio_resid;
#if 0
		{
			uint8_t *ptr = fifo_read(&sc->rfifo, 0);
			size_t v;
			kprintf("read: ");
			for (v = 0; v < n; ++v)
				kprintf(" %02x", ptr[v]);
			kprintf("\n");
		}
#endif
		error = uiomove(fifo_read(&sc->rfifo, 0), n, uio);
		if (error)
			break;
		fifo_read(&sc->rfifo, n);
		didread = 1;
	}
	cyapa_unlock(sc);

	if (error == 0 && didread == 0)
		error = EWOULDBLOCK;

	return error;
}

static int
cyapawrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct cyapa_softc *sc = CYAPA_SOFTC(minor(dev));
	struct uio *uio = ap->a_uio;
	int error;
	int cmd_completed;
	size_t n;
	uint8_t c0;

again:
	/*
	 * Copy data from userland.  This will also cross-over the end
	 * of the fifo and keep filling.
	 */
	cyapa_lock(sc);
	while ((n = fifo_space(&sc->wfifo)) > 0 && uio->uio_resid) {
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		error = uiomove(fifo_write(&sc->wfifo, 0), n, uio);
		if (error)
			break;
		fifo_write(&sc->wfifo, n);
	}

	/*
	 * Handle commands
	 */
	cmd_completed = (fifo_ready(&sc->wfifo) != 0);
	while (fifo_ready(&sc->wfifo) && cmd_completed && error == 0) {
		if (sc->ps2_cmd == 0)
			sc->ps2_cmd = fifo_read_char(&sc->wfifo);
		switch(sc->ps2_cmd) {
		case 0xE6:
			/*
			 * SET SCALING 1:1
			 */
			sc->scaling_mode = 0;
			fifo_write_char(&sc->rfifo, 0xFA);
			break;
		case 0xE7:
			/*
			 * SET SCALING 2:1
			 */
			sc->scaling_mode = 1;
			fifo_write_char(&sc->rfifo, 0xFA);
			break;
		case 0xE8:
			/*
			 * SET RESOLUTION +1 byte
			 */
			if (sc->ps2_acked == 0) {
				sc->ps2_acked = 1;
				fifo_write_char(&sc->rfifo, 0xFA);
			}
			if (fifo_ready(&sc->wfifo) == 0) {
				cmd_completed = 0;
				break;
			}
			sc->resolution = fifo_read_char(&sc->wfifo);
			fifo_write_char(&sc->rfifo, 0xFA);
			break;
		case 0xE9:
			/*
			 * STATUS REQUEST
			 *
			 * byte1:
			 *	bit 7	0
			 *	bit 6	Mode	(1=remote mode, 0=stream mode)
			 *	bit 5	Enable	(data reporting enabled)
			 *	bit 4	Scaling	(0=1:1 1=2:1)
			 *	bit 3	0
			 *	bit 2	LEFT BUTTON 	(1 if pressed)
			 *	bit 1	MIDDLE BUTTON 	(1 if pressed)
			 *	bit 0	RIGHT BUTTON 	(1 if pressed)
			 *
			 * byte2: resolution counts/mm
			 * byte3: sample rate
			 */
			c0 = 0;
			if (sc->remote_mode)
				c0 |= 0x40;
			if (sc->reporting_mode)
				c0 |= 0x20;
			if (sc->scaling_mode)
				c0 |= 0x10;
			if (sc->track_but & CYAPA_FNGR_LEFT)
				c0 |= 0x04;
			if (sc->track_but & CYAPA_FNGR_MIDDLE)
				c0 |= 0x02;
			if (sc->track_but & CYAPA_FNGR_RIGHT)
				c0 |= 0x01;
			fifo_write_char(&sc->rfifo, 0xFA);
			fifo_write_char(&sc->rfifo, c0);
			fifo_write_char(&sc->rfifo, 0x00);
			fifo_write_char(&sc->rfifo, 100);
			break;
		case 0xEA:
			/*
			 * Set stream mode and reset movement counters
			 */
			sc->remote_mode = 0;
			fifo_write_char(&sc->rfifo, 0xFA);
			sc->delta_x = 0;
			sc->delta_y = 0;
			sc->delta_z = 0;
			break;
		case 0xEB:
			/*
			 * Read Data (if in remote mode).  If not in remote
			 * mode force an event.
			 */
			fifo_write_char(&sc->rfifo, 0xFA);
			sc->data_signal = 1;
			break;
		case 0xEC:
			/*
			 * Reset Wrap Mode (ignored)
			 */
			fifo_write_char(&sc->rfifo, 0xFA);
			break;
		case 0xEE:
			/*
			 * Set Wrap Mode (ignored)
			 */
			fifo_write_char(&sc->rfifo, 0xFA);
			break;
		case 0xF0:
			/*
			 * Set Remote Mode
			 */
			sc->remote_mode = 1;
			fifo_write_char(&sc->rfifo, 0xFA);
			sc->delta_x = 0;
			sc->delta_y = 0;
			sc->delta_z = 0;
			break;
		case 0xF2:
			/*
			 * Get Device ID
			 *
			 * If we send 0x00 - normal PS/2 mouse, no Z-axis
			 *
			 * If we send 0x03 - Intellimouse, data packet has
			 * an additional Z movement byte (8 bits signed).
			 * (also reset movement counters)
			 *
			 * If we send 0x04 - Now includes z-axis and the
			 * 4th and 5th mouse buttons.
			 */
			fifo_write_char(&sc->rfifo, 0xFA);
			switch(sc->zenabled) {
			case 1:
				fifo_write_char(&sc->rfifo, 0x03);
				break;
			case 2:
				fifo_write_char(&sc->rfifo, 0x04);
				break;
			default:
				fifo_write_char(&sc->rfifo, 0x00);
				break;
			}
			sc->delta_x = 0;
			sc->delta_y = 0;
			sc->delta_z = 0;
			break;
		case 0xF3:
			/*
			 * Set Sample Rate
			 *
			 * byte1: the sample rate
			 */
			if (sc->ps2_acked == 0) {
				sc->ps2_acked = 1;
				fifo_write_char(&sc->rfifo, 0xFA);
			}
			if (fifo_ready(&sc->wfifo) == 0) {
				cmd_completed = 0;
				break;
			}
			sc->sample_rate = fifo_read_char(&sc->wfifo);
			fifo_write_char(&sc->rfifo, 0xFA);

			/*
			 * zenabling sequence: 200,100,80 (device id 0x03)
			 *		       200,200,80 (device id 0x04)
			 *
			 * We support id 0x03 (no 4th or 5th button).
			 * We support id 0x04 (w/ 4th and 5th button).
			 */
			if (sc->zenabled == 0 && sc->sample_rate == 200)
				sc->zenabled = -1;
			else if (sc->zenabled == -1 && sc->sample_rate == 100)
				sc->zenabled = -2;
			else if (sc->zenabled == -1 && sc->sample_rate == 200)
				sc->zenabled = -3;
			else if (sc->zenabled == -2 && sc->sample_rate == 80)
				sc->zenabled = 1;	/* z-axis mode */
			else if (sc->zenabled == -3 && sc->sample_rate == 80)
				sc->zenabled = 2;	/* z-axis+but4/5 */
			break;
		case 0xF4:
			/*
			 * Enable data reporting.  Only effects stream mode.
			 */
			fifo_write_char(&sc->rfifo, 0xFA);
			sc->reporting_mode = 1;
			break;
		case 0xF5:
			/*
			 * Disable data reporting.  Only effects stream mode.
			 */
			fifo_write_char(&sc->rfifo, 0xFA);
			sc->reporting_mode = 1;
			break;
		case 0xF6:
			/*
			 * SET DEFAULTS
			 *
			 * (reset sampling rate, resolution, scaling and
			 *  enter stream mode)
			 */
			fifo_write_char(&sc->rfifo, 0xFA);
			sc->sample_rate = 100;
			sc->resolution = 4;
			sc->scaling_mode = 0;
			sc->reporting_mode = 0;
			sc->remote_mode = 0;
			sc->delta_x = 0;
			sc->delta_y = 0;
			sc->delta_z = 0;
			/* signal */
			break;
		case 0xFE:
			/*
			 * RESEND
			 *
			 * Force a resend by guaranteeing that reported_but
			 * differs from track_but.
			 */
			fifo_write_char(&sc->rfifo, 0xFA);
			sc->data_signal = 1;
			break;
		case 0xFF:
			/*
			 * RESET
			 */
			fifo_reset(&sc->rfifo);	/* should we do this? */
			fifo_reset(&sc->wfifo);	/* should we do this? */
			fifo_write_char(&sc->rfifo, 0xFA);
			sc->delta_x = 0;
			sc->delta_y = 0;
			sc->delta_z = 0;
			sc->zenabled = 0;
			break;
		default:
			kprintf("unknown command %02x\n", sc->ps2_cmd);
			break;
		}
		if (cmd_completed) {
			sc->ps2_cmd = 0;
			sc->ps2_acked = 0;
		}
		cyapa_unlock(sc);
		cyapa_notify(sc);
		cyapa_lock(sc);
	}
	cyapa_unlock(sc);
	if (error == 0 && (cmd_completed || uio->uio_resid))
		goto again;
	return error;
}

static void cyapa_filt_detach(struct knote *);
static int cyapa_filt(struct knote *, long);

static struct filterops cyapa_filtops =
        { FILTEROP_ISFD, NULL, cyapa_filt_detach, cyapa_filt };

static int
cyapakqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct cyapa_softc *sc = CYAPA_SOFTC(minor(dev));
	struct knote *kn = ap->a_kn;
	struct klist *klist;

	switch(kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &cyapa_filtops;
		kn->kn_hook = (void *)sc;
		ap->a_result = 0;
		break;
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}
	klist = &sc->kqinfo.ki_note;
	knote_insert(klist, kn);

	return (0);
}

static void
cyapa_filt_detach(struct knote *kn)
{
	struct cyapa_softc *sc = (struct cyapa_softc *)kn->kn_hook;
	struct klist *klist;

	klist = &sc->kqinfo.ki_note;
	knote_remove(klist, kn);
}

static int
cyapa_filt(struct knote *kn, long hint)
{
	struct cyapa_softc *sc = (struct cyapa_softc *)kn->kn_hook;
	int ready;

	cyapa_lock(sc);
	if (fifo_ready(&sc->rfifo) || sc->data_signal)
		ready = 1;
	else
		ready = 0;
	cyapa_unlock(sc);

	return (ready);
}

static int
cyapaioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	device_t bus;		/* smbbus */
	/*struct cyapacmd *s = (struct cyapacmd *)ap->a_data;*/
	void *s = NULL;
	struct cyapa_softc *sc = CYAPA_SOFTC(minor(dev));
	int error;

	if (sc == NULL)
		return (ENXIO);
	if (s == NULL)
		return (EINVAL);

	/*
	 * NOTE: smbus_*() functions automatically recurse the parent to
	 *	 get to the actual device driver.
	 */
	bus = device_get_parent(sc->dev);	/* smbus */

	/* Allocate the bus. */
	if ((error = smbus_request_bus(bus, sc->dev,
			(ap->a_fflag & O_NONBLOCK) ?
			SMB_DONTWAIT : (SMB_WAIT | SMB_INTR))))
		return (error);

	switch (ap->a_cmd) {
	default:
#if 0
		error = inputev_ioctl(&sc->iev, ap->a_cmd, ap->a_data);
#endif
		error = ENOTTY;
		break;
	}

	smbus_release_bus(bus, sc->dev);

	return (error);
}

/*
 * MAJOR SUPPORT FUNCTIONS
 */
static
void
cyapa_poll_thread(void *arg)
{
	struct cyapa_softc *sc = arg;
	struct cyapa_regs regs;
	device_t bus;		/* smbbus */
	int error;
	int freq = cyapa_norm_freq;
	int isidle = 0;

	bus = device_get_parent(sc->dev);

	while ((sc->poll_flags & CYPOLL_SHUTDOWN) == 0) {
		error = smbus_request_bus(bus, sc->dev, SMB_WAIT);
		if (error == 0) {
			error = smbus_trans(bus, sc->addr, CMD_DEV_STATUS,
					    SMB_TRANS_NOCNT | SMB_TRANS_7BIT,
					    NULL, 0,
					    (void *)&regs, sizeof(regs), NULL);
			if (error == 0) {
				isidle = cyapa_raw_input(sc, &regs);
			}
			smbus_release_bus(bus, sc->dev);
		}
		tsleep(&sc->poll_flags, 0, "cyapw", (hz + freq - 1) / freq);
		++sc->poll_ticks;
		if (sc->count == 0)
			freq = cyapa_idle_freq;
		else if (isidle)
			freq = cyapa_slow_freq;
		else
			freq = cyapa_norm_freq;
	}
	sc->poll_td = NULL;
	wakeup(&sc->poll_td);
}

static
int
cyapa_raw_input(struct cyapa_softc *sc, struct cyapa_regs *regs)
{
	int nfingers;
	int afingers;	/* actual fingers after culling */
	int i;
	int j;
	int k;
	int isidle;
	short x;
	short y;
	short z;
	short x1;
	short x2;
	uint16_t but;	/* high bits used for simulated but4/but5 */

	nfingers = CYAPA_FNGR_NUMFINGERS(regs->fngr);
	afingers = nfingers;

	if (cyapa_debug) {
		kprintf("stat %02x buttons %c%c%c nfngrs=%d ",
			regs->stat,
			((regs->fngr & CYAPA_FNGR_LEFT) ? 'L' : '-'),
			((regs->fngr & CYAPA_FNGR_MIDDLE) ? 'L' : '-'),
			((regs->fngr & CYAPA_FNGR_RIGHT) ? 'L' : '-'),
			nfingers
		);
	}
	for (i = 0; i < nfingers; ++i) {
		if (cyapa_debug) {
			kprintf(" [x=%04d y=%04d p=%d]",
				CYAPA_TOUCH_X(regs, i),
				CYAPA_TOUCH_Y(regs, i),
				CYAPA_TOUCH_P(regs, i));
		}
		if (CYAPA_TOUCH_P(regs, i) < cyapa_minpressure)
			--afingers;
	}

	/*
	 * Tracking for local solutions
	 */
	cyapa_lock(sc);

	/*
	 * Track timing for finger-downs.  Used to detect false-3-finger
	 * button-down.
	 */
	switch(afingers) {
	case 0:
		break;
	case 1:
		if (sc->track_nfingers == 0)
			sc->finger1_ticks = sc->poll_ticks;
		break;
	case 2:
		if (sc->track_nfingers <= 0)
			sc->finger1_ticks = sc->poll_ticks;
		if (sc->track_nfingers <= 1)
			sc->finger2_ticks = sc->poll_ticks;
		break;
	case 3:
	default:
		if (sc->track_nfingers <= 0)
			sc->finger1_ticks = sc->poll_ticks;
		if (sc->track_nfingers <= 1)
			sc->finger2_ticks = sc->poll_ticks;
		if (sc->track_nfingers <= 2)
			sc->finger3_ticks = sc->poll_ticks;
		break;
	}
	sc->track_nfingers = afingers;

	/*
	 * Lookup and track finger indexes in the touch[] array.
	 */
	if (afingers == 0) {
		sc->track_x = -1;
		sc->track_y = -1;
		sc->track_z = -1;
		sc->fuzz_x = 0;
		sc->fuzz_y = 0;
		sc->fuzz_z = 0;
		sc->touch_x = -1;
		sc->touch_y = -1;
		sc->touch_z = -1;
		sc->track_id1 = -1;
		sc->track_id2 = -1;
		sc->track_but = 0;
		i = 0;
		j = 0;
		k = 0;
	} else {
		/*
		 * The id assigned on touch can move around in the array,
		 * find it.  If that finger is lifted up, assign some other
		 * finger for mouse tracking and reset track_x and track_y
		 * to avoid a mouse jump.
		 *
		 * If >= 2 fingers are down be sure not to assign i and
		 * j to the same index.
		 */
		for (i = 0; i < nfingers; ++i) {
			if (sc->track_id1 == regs->touch[i].id)
				break;
		}
		if (i == nfingers ||
		    CYAPA_TOUCH_P(regs, i) < cyapa_minpressure) {
			i = 0;
			sc->track_x = -1;
			sc->track_y = -1;
			sc->track_z = -1;
			sc->track_id1 = regs->touch[i].id;
			if (sc->track_id2 == sc->track_id1)
				sc->track_id2 = -1;
		}

		/*
		 * A second finger.
		 */
		for (j = 0; j < nfingers; ++j) {
			if (sc->track_id2 == regs->touch[j].id)
				break;
		}
		if (j == nfingers ||
		    CYAPA_TOUCH_P(regs, j) < cyapa_minpressure) {
			if (afingers >= 2) {
				if (i == 0)
					j = 1;
				else
					j = 0;
				sc->track_id2 = regs->touch[j].id;
			} else {
				sc->track_id2 = -1;
				j = 0;
			}
		}

		/*
		 * The third finger is used to tap or tap-hold to simulate
		 * a button, we don't have to record it persistently.
		 */
		if (afingers >= 3) {
			k = 0;
			if (i == 0 || j == 0)
				k = 1;
			if (i == 1 || j == 1)
				k = 2;
		} else {
			k = 0;
		}
	}

	/*
	 * On initial touch determine if we are in the slider area.  Setting
	 * track_z conditionalizes the delta calculations later on.
	 */
	if (afingers && sc->zenabled > 0 &&
	    sc->track_x == -1 && sc->track_z == -1) {
		x = CYAPA_TOUCH_X(regs, i);
		z = CYAPA_TOUCH_Y(regs, i);
		if (x > sc->cap_resx * 9 / 10)
			sc->track_z = z;
	}

	if (afingers && sc->track_z != -1) {
		/*
		 * Slider emulation (right side of trackpad).  Z is tracked
		 * based on the Y position.  X and Y tracking are disabled.
		 *
		 * Because we are emulating a mouse-wheel, we do not want
		 * to shove events out at the maximum resolution.
		 */
		z = CYAPA_TOUCH_Y(regs, i);
		sc->delta_z += z / ZSCALE - sc->track_z;
		if (sc->touch_z == -1)
			sc->touch_z = z;	/* not used atm */
		sc->track_z = z / ZSCALE;
	} else if (afingers) {
		/*
		 * Normal pad position reporting (track_z is left -1)
		 */
		x = CYAPA_TOUCH_X(regs, i);
		y = CYAPA_TOUCH_Y(regs, i);
		if (sc->track_x != -1) {
			sc->delta_x += x - sc->track_x;
			sc->delta_y -= y - sc->track_y;
			if (sc->delta_x > sc->cap_resx)
				sc->delta_x = sc->cap_resx;
			if (sc->delta_x < -sc->cap_resx)
				sc->delta_x = -sc->cap_resx;
			if (sc->delta_y > sc->cap_resx)
				sc->delta_y = sc->cap_resy;
			if (sc->delta_y < -sc->cap_resy)
				sc->delta_y = -sc->cap_resy;
		}
		if (sc->touch_x == -1) {
			sc->touch_x = x;
			sc->touch_y = y;
		}
		sc->track_x = x;
		sc->track_y = y;
	}
	if (afingers >= 5 && sc->zenabled > 1 && sc->track_z < 0) {
		/*
		 * Simulate the 5th button (when not in slider mode)
		 */
		but = SIMULATE_BUT5;
	} else if (afingers >= 4 && sc->zenabled > 1 && sc->track_z < 0) {
		/*
		 * Simulate the 4th button (when not in slider mode)
		 */
		but = SIMULATE_BUT4;
	} else if (afingers >= 3 && sc->track_z < 0) {
		/*
		 * Simulate the left, middle, or right button with 3
		 * fingers when not in slider mode.
		 *
		 * This makes it ultra easy to hit GUI buttons and move
		 * windows with a light touch, without having to apply the
		 * pressure required to articulate the button.
		 *
		 * However, if we are coming down from 4 or 5 fingers,
		 * do NOT simulate the left button and instead just release
		 * button 4 or button 5.  Leave SIMULATE_LOCK set to
		 * placemark the condition.  We must go down to 2 fingers
		 * to release the lock.
		 *
		 * LEFT BUTTON: Fingers arranged left-to-right 1 2 3,
		 *		move mouse with fingers 2 and 3 and tap
		 *		or hold with finger 1 (to the left of fingers
		 *		2 and 3).
		 *
		 * RIGHT BUTTON: Move mouse with fingers 1 and 2 and tap
		 *		 or hold with finger 3.
		 *
		 * MIDDLE BUTTON: Move mouse with fingers 1 and 3 and tap
		 *		  or hold with finger 2.
		 *
		 * Finally, detect when all three fingers were placed down
		 * within one tick of each other.
		 */
		x1 = CYAPA_TOUCH_X(regs, i);	/* 1st finger down */
		x2 = CYAPA_TOUCH_X(regs, j);	/* 2nd finger down */
		x = CYAPA_TOUCH_X(regs, k);	/* 3rd finger (button) down */
		if (sc->track_but & (SIMULATE_BUT4 |
					    SIMULATE_BUT5 |
					    SIMULATE_LOCK)) {
			but = SIMULATE_LOCK;
		} else if (sc->track_but & ~SIMULATE_LOCK) {
			but = sc->track_but;
		} else if ((int)(sc->finger3_ticks - sc->finger1_ticks) <
				 cyapa_norm_freq / 25 + 1) {
			/*
			 * False 3-finger button detection (but still detect
			 * if the actual physical button is held down).
			 */
			if (regs->fngr & CYAPA_FNGR_LEFT)
				but = CYAPA_FNGR_LEFT;
			else
				but = 0;
		} else if (x < x1 && x < x2) {
			but = CYAPA_FNGR_LEFT;
		} else if (x > x1 && x < x2) {
			but = CYAPA_FNGR_MIDDLE;
		} else if (x > x2 && x < x1) {
			but = CYAPA_FNGR_MIDDLE;
		} else {
			but = CYAPA_FNGR_RIGHT;
		}
	} else if (afingers == 2 || (afingers >= 2 && sc->track_z >= 0)) {
		/*
		 * If 2 fingers are held down or 2 or more fingers are held
		 * down and we are in slider mode, any key press is
		 * interpreted as a left mouse button press.
		 *
		 * If a keypress is already active we retain the active
		 * keypress instead.
		 *
		 * The high-button state is unconditionally cleared with <= 2
		 * fingers.
		 */
		if (regs->fngr & CYAPA_FNGR_LEFT) {
			but = sc->track_but & ~SIMULATE_LOCK;
			if (but == 0)
				but = CYAPA_FNGR_LEFT;
		} else {
			but = 0;
		}
	} else if (afingers == 1 &&
		   (abs(sc->touch_x - sc->track_x) > 32 ||
		    abs(sc->touch_y - sc->track_y) > 32)) {
		/*
		 * When using one finger, any significant mouse movement
		 * will lock you to the left mouse button if you push the
		 * button, regardless of where you are on the pad.
		 *
		 * If a keypress is already active we retain the active
		 * keypress instead.
		 *
		 * The high-button state is unconditionally cleared with <= 2
		 * fingers.
		 */
		if (regs->fngr & CYAPA_FNGR_LEFT) {
			but = sc->track_but & ~SIMULATE_LOCK;
			if (but == 0)
				but = CYAPA_FNGR_LEFT;
		} else {
			but = 0;
		}
	} else if (afingers == 1 && (regs->fngr & CYAPA_FNGR_LEFT)) {
		/*
		 * If you are swiping while holding a button down, the
		 * button registration does not change.  Otherwise the
		 * registered button depends on where you are on the pad.
		 *
		 * Since no significant movement occurred we allow the
		 * button to be pressed while within the slider area
		 * and still be properly registered as the right button.
		 *
		 * The high-button state is unconditionally cleared with <= 2
		 * fingers.
		 */
		if (sc->track_but & ~SIMULATE_LOCK)
			but = sc->track_but & ~SIMULATE_LOCK;
		else if (sc->track_x < sc->cap_resx * 1 / 3)
			but = CYAPA_FNGR_LEFT;
		else if (sc->track_x < sc->cap_resx * 2 / 3)
			but = CYAPA_FNGR_MIDDLE;
		else
			but = CYAPA_FNGR_RIGHT;
	} else if (afingers == 1) {
		/*
		 * Clear all finger state if 1 finger is down and nothing
		 * is pressed.
		 */
		but = 0;
	} else {
		/*
		 * Clear all finger state if no fingers are down.
		 */
		but = 0;
	}

	/*
	 * Detect state change from last reported state and
	 * determine if we have gone idle.
	 */
	sc->track_but = but;
	if (sc->delta_x || sc->delta_y || sc->delta_z ||
	    sc->track_but != sc->reported_but) {
		sc->active_tick = ticks;
		if (sc->remote_mode == 0 && sc->reporting_mode)
			sc->data_signal = 1;
		isidle = 0;
	} else if ((unsigned)(ticks - sc->active_tick) > hz) {
		sc->active_tick = ticks - hz;	/* prevent overflow */
		isidle = 1;
	} else {
		isidle = 0;
	}
	cyapa_unlock(sc);
	cyapa_notify(sc);

	if (cyapa_debug)
		kprintf("\n");
	return(isidle);
}

/*
 * FIFO FUNCTIONS
 */

/*
 * Returns non-zero if the fifo is empty
 */
static
int
fifo_empty(struct cyapa_fifo *fifo)
{
	return(fifo->rindex == fifo->windex);
}

/*
 * Returns the number of characters available for reading from
 * the fifo without wrapping the fifo buffer.
 */
static
size_t
fifo_ready(struct cyapa_fifo *fifo)
{
	size_t n;

	n = CYAPA_BUFSIZE - (fifo->rindex & CYAPA_BUFMASK);
	if (n > (size_t)(fifo->windex - fifo->rindex))
		n = (size_t)(fifo->windex - fifo->rindex);
	return n;
}

#if 0
/*
 * Returns the number of characters available for reading from
 * the fifo including wrapping the fifo buffer.
 */
static
size_t
fifo_total_ready(struct cyapa_fifo *fifo)
{
	return ((size_t)(fifo->windex - fifo->rindex));
}
#endif

/*
 * Returns a read pointer into the fifo and then bumps
 * rindex.  The FIFO must have at least 'n' characters in
 * it.  The value (n) can cause the index to wrap but users
 * of the buffer should never supply a value for (n) that wraps
 * the buffer.
 */
static
char *
fifo_read(struct cyapa_fifo *fifo, size_t n)
{
	char *ptr;

	if (n > (CYAPA_BUFSIZE - (fifo->rindex & CYAPA_BUFMASK))) {
		kprintf("fifo_read: overflow\n");
		return (fifo->buf);
	}
	ptr = fifo->buf + (fifo->rindex & CYAPA_BUFMASK);
	fifo->rindex += n;

	return (ptr);
}

static
uint8_t
fifo_read_char(struct cyapa_fifo *fifo)
{
	uint8_t c;

	if (fifo->rindex == fifo->windex) {
		kprintf("fifo_read_char: overflow\n");
		c = 0;
	} else {
		c = fifo->buf[fifo->rindex & CYAPA_BUFMASK];
		++fifo->rindex;
	}
	return c;
}


/*
 * Write a character to the FIFO.  The character will be discarded
 * if the FIFO is full.
 */
static
void
fifo_write_char(struct cyapa_fifo *fifo, uint8_t c)
{
	if (fifo->windex - fifo->rindex < CYAPA_BUFSIZE) {
		fifo->buf[fifo->windex & CYAPA_BUFMASK] = c;
		++fifo->windex;
	}
}

/*
 * Return the amount of space available for writing without wrapping
 * the fifo.
 */
static
size_t
fifo_space(struct cyapa_fifo *fifo)
{
	size_t n;

	n = CYAPA_BUFSIZE - (fifo->windex & CYAPA_BUFMASK);
	if (n > (size_t)(CYAPA_BUFSIZE - (fifo->windex - fifo->rindex)))
		n = (size_t)(CYAPA_BUFSIZE - (fifo->windex - fifo->rindex));
	return n;
}

static
char *
fifo_write(struct cyapa_fifo *fifo, size_t n)
{
	char *ptr;

	ptr = fifo->buf + (fifo->windex & CYAPA_BUFMASK);
	fifo->windex += n;

	return(ptr);
}

static
void
fifo_reset(struct cyapa_fifo *fifo)
{
	fifo->rindex = 0;
	fifo->windex = 0;
}

/*
 * Fuzz handling
 */
static
short
cyapa_fuzz(short delta, short *fuzzp)
{
    short fuzz;

    fuzz = *fuzzp;
    if (fuzz >= 0 && delta < 0) {
	++delta;
	--fuzz;
    } else if (fuzz <= 0 && delta > 0) {
	--delta;
	++fuzz;
    }
    *fuzzp = fuzz;

    return delta;
}

DRIVER_MODULE(cyapa, smbus, cyapa_driver, cyapa_devclass, NULL, NULL);
MODULE_DEPEND(cyapa, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(cyapa, 1);
