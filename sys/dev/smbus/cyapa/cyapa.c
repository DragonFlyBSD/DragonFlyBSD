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
	uint8_t	track_but;
	char 	track_id;		/* (for movement) */
	short	delta_x;		/* accumulation -> report */
	short	delta_y;
	short	fuzz_x;
	short	fuzz_y;
	short	touch_x;		/* touch down coordinates */
	short	touch_y;
	uint8_t reported_but;

	struct cyapa_fifo rfifo;	/* device->host */
	struct cyapa_fifo wfifo;	/* host->device */
	uint8_t	ps2_cmd;		/* active p2_cmd waiting for data */
	uint8_t ps2_acked;
	int	data_signal;
	int	blocked;
	int	reporting_mode;		/* 0=disabled 1=enabled */
	int	scaling_mode;		/* 0=1:1 1=2:1 */
	int	remote_mode;		/* 0 for streaming mode */
	int	resolution;		/* count/mm */
	int	sample_rate;		/* samples/sec */
};

#define CYPOLL_SHUTDOWN	0x0001

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
		uint8_t but;
		short delta_x;
		short delta_y;

		/*
		 * Accumulate delta_x, delta_y.
		 */
		sc->data_signal = 0;
		delta_x = sc->delta_x;
		delta_y = sc->delta_y;
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
		but = sc->track_but;

		/*
		 * Adjust baseline for next calculation
		 */
		sc->delta_x -= delta_x;
		sc->delta_y -= delta_y;
		sc->reported_but = but;

		/*
		 * Fuzz reduces movement jitter by introducing some
		 * hysteresis.  It operates without cumulative error so
		 * if you swish around quickly and return your finger to
		 * where it started, so to will the mouse.
		 */
		delta_x = cyapa_fuzz(delta_x, &sc->fuzz_x);
		delta_y = cyapa_fuzz(delta_y, &sc->fuzz_y);

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
			break;
		case 0xF2:
			/*
			 * Get Device ID
			 *
			 * byte1: device id (0x00)
			 * (also reset movement counters)
			 */
			fifo_write_char(&sc->rfifo, 0xFA);
			sc->delta_x = 0;
			sc->delta_y = 0;
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
			break;
		default:
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
		if (sc->count == 0)
			freq = cyapa_slow_freq;
		else if (isidle)
			freq = cyapa_idle_freq;
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
	int i;
	short x;
	short y;
	u_char but;

	nfingers = CYAPA_FNGR_NUMFINGERS(regs->fngr);

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
#if 0
		inputev_mt_slot(&sc->iev, regs->touch[i].id - 1);
		inputev_mt_report_slot_state(&sc->iev, MT_TOOL_FINGER, 1);
		inputev_report_abs(&sc->iev, ABS_MT_POSITION_X,
				   CYAPA_TOUCH_X(regs, i));
		inputev_report_abs(&sc->iev, ABS_MT_POSITION_Y,
				   CYAPA_TOUCH_Y(regs, i));
		inputev_report_abs(&sc->iev, ABS_MT_PRESSURE,
				   CYAPA_TOUCH_P(regs, i));
#endif
	}
#if 0
	inputev_mt_sync_frame(&sc->iev);

	if (sc->cap_buttons & CYAPA_FNGR_LEFT)
		inputev_report_key(&sc->iev, BTN_LEFT,
				 regs->fngr & CYAPA_FNGR_LEFT);
	if (sc->cap_buttons & CYAPA_FNGR_MIDDLE)
		inputev_report_key(&sc->iev, BTN_LEFT,
				 regs->fngr & CYAPA_FNGR_MIDDLE);
	if (sc->cap_buttons & CYAPA_FNGR_RIGHT)
		inputev_report_key(&sc->iev, BTN_LEFT,
				 regs->fngr & CYAPA_FNGR_RIGHT);
#endif
	/*
	 * Tracking for local solutions
	 */
	cyapa_lock(sc);
	if (nfingers == 0) {
		sc->track_x = -1;
		sc->track_y = -1;
		sc->fuzz_x = 0;
		sc->fuzz_y = 0;
		sc->touch_x = -1;
		sc->touch_y = -1;
		sc->track_id = -1;
		i = 0;
	} else if (sc->track_id == -1) {
		/*
		 * Touch(es), if not tracking for mouse-movement, assign
		 * mouse-movement to the first finger in the array.
		 */
		i = 0;
		sc->track_id = regs->touch[i].id;
	} else {
		/*
		 * The id assigned on touch can move around in the array,
		 * find it.  If that finger is lifted up, assign some other
		 * finger for mouse tracking and reset track_x and track_y
		 * to avoid a mouse jump.
		 */
		for (i = 0; i < nfingers; ++i) {
			if (sc->track_id == regs->touch[i].id)
				break;
		}
		if (i == nfingers) {
			i = 0;
			sc->track_x = -1;
			sc->track_y = -1;
			sc->track_id = regs->touch[i].id;
		}
	}
	if (nfingers) {
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
	if ((regs->fngr & CYAPA_FNGR_LEFT) &&
	    (abs(sc->touch_x - sc->track_x) > 16 ||
	     abs(sc->touch_y - sc->track_y) > 16)) {
		/*
		 * If you move the mouse enough finger-down before pushing
		 * the button, it will always register as the left button.
		 * Makes moving windows around and hitting GUI buttons easy.
		 */
		but = CYAPA_FNGR_LEFT;
	} else if (regs->fngr & CYAPA_FNGR_LEFT) {
		/*
		 * If you are swiping while holding the button down, the
		 * button registration does not change.  Otherwise the
		 * registered button depends on where you are on the pad.
		 */
		if (sc->track_but)
			but = sc->track_but;
		else if (sc->track_x < sc->cap_resx * 1 / 3)
			but = CYAPA_FNGR_LEFT;
		else if (sc->track_x < sc->cap_resx * 2 / 3)
			but = CYAPA_FNGR_MIDDLE;
		else
			but = CYAPA_FNGR_RIGHT;
	} else {
		but = 0;
	}
	sc->track_but = but;
	if (sc->delta_x || sc->delta_y || sc->track_but != sc->reported_but) {
		if (sc->remote_mode == 0 && sc->reporting_mode)
			sc->data_signal = 1;
	}
	cyapa_unlock(sc);
	cyapa_notify(sc);

	if (cyapa_debug)
		kprintf("\n");
	return(0);
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
