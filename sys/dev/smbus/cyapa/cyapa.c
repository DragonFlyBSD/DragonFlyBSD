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
#include <sys/input.h>
#include <sys/sysctl.h>

#include <bus/smbus/smbconf.h>
#include <bus/smbus/smbus.h>
#include "cyapa.h"

#include "smbus_if.h"
#include "bus_if.h"
#include "device_if.h"

#define BUFSIZE 1024

struct cyapa_softc {
	device_t dev;
	int	count;			/* >0 if device opened */
	int	unit;
	int	addr;
	cdev_t	devnode;

	int	cap_resx;
	int	cap_resy;
	int	cap_phyx;
	int	cap_phyy;
	uint8_t	cap_buttons;

	int	poll_flags;
	thread_t poll_td;
	struct inputev iev;		/* subr_input.c */
};

#define CYPOLL_SHUTDOWN	0x0001

static void cyapa_poll_thread(void *arg);
static int cyapa_raw_input(struct cyapa_softc *sc, struct cyapa_regs *regs);

static int cyapa_idle_freq = 20;
SYSCTL_INT(_debug, OID_AUTO, cyapa_idle_freq, CTLFLAG_RW,
		&cyapa_idle_freq, 0, "");
static int cyapa_slow_freq = 20;
SYSCTL_INT(_debug, OID_AUTO, cyapa_slow_freq, CTLFLAG_RW,
		&cyapa_slow_freq, 0, "");
static int cyapa_norm_freq = 20;
SYSCTL_INT(_debug, OID_AUTO, cyapa_norm_freq, CTLFLAG_RW,
		&cyapa_norm_freq, 0, "");

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

static struct dev_ops cyapa_ops = {
	{ "cyapa", 0, 0 },
	.d_open =	cyapaopen,
	.d_close =	cyapaclose,
	.d_ioctl =	cyapaioctl,
	.d_read =	cyaparead,
	.d_write =	cyapawrite,
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

	/*
	 * Cleanup input event tracking
	 */
	inputev_deregister(&sc->iev);

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
cyapawrite(struct dev_write_args *ap)
{
#if 0
	cdev_t dev = ap->a_head.a_dev;
	struct cyapa_softc *sc = CYAPA_SOFTC(minor(dev));
#endif

	return (EINVAL);
}

static int
cyaparead(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct cyapa_softc *sc = CYAPA_SOFTC(minor(dev));
	int error;

	error = inputev_read(&sc->iev, ap->a_uio, ap->a_ioflag);
	return error;
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
	case 0:
	default:
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

	nfingers = CYAPA_FNGR_NUMFINGERS(regs->fngr);

	kprintf("stat %02x buttons %c%c%c nfngrs=%d ",
		regs->stat,
		((regs->fngr & CYAPA_FNGR_LEFT) ? 'L' : '-'),
		((regs->fngr & CYAPA_FNGR_MIDDLE) ? 'L' : '-'),
		((regs->fngr & CYAPA_FNGR_RIGHT) ? 'L' : '-'),
		nfingers
	);
	for (i = 0; i < nfingers; ++i) {
		kprintf(" [x=%04d y=%04d p=%d]",
			CYAPA_TOUCH_X(regs, i),
			CYAPA_TOUCH_Y(regs, i),
			CYAPA_TOUCH_P(regs, i));
		inputev_mt_slot(&sc->iev, regs->touch[i].id - 1);
		inputev_mt_report_slot_state(&sc->iev, MT_TOOL_FINGER, 1);
		inputev_report_abs(&sc->iev, ABS_MT_POSITION_X,
				   CYAPA_TOUCH_X(regs, i));
		inputev_report_abs(&sc->iev, ABS_MT_POSITION_Y,
				   CYAPA_TOUCH_Y(regs, i));
		inputev_report_abs(&sc->iev, ABS_MT_PRESSURE,
				   CYAPA_TOUCH_P(regs, i));
	}
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

	kprintf("\n");
	return(0);
}

DRIVER_MODULE(cyapa, smbus, cyapa_driver, cyapa_devclass, NULL, NULL);
MODULE_DEPEND(cyapa, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(cyapa, 1);
