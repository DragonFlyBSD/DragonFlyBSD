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
 * ATMEL_MXT - Atmel MXT touchscreen driver
 *
 *	PRELIMINARY DRIVER PRELIMINARY DRIVER PRELIMINARY DRIVERE
 *
 * (everything is pretty much hardwired and we assume the device is already
 *  operational, which it appears to be.  ONLY TESTED ON ACER C720).
 *
 * This driver attaches to Acer TouchScreen MXT chipsets and currently
 * emulates the ELO touchscreen serial protocol "elographics" for X:
 *
 *	Section "InputDevice"
 *		Identifier  "TouchScreen0"
 *		Driver      "elographics"
 *		Option      "Device" "/dev/atmel1-4a"
 *	EndSection
 *
 * The MXT chipsets typically attach on haswell chromebooks on one of the I2C
 * busses at address 0x4A.  On my Acer C720 it attaches to the ig4 driver's
 * I2C bus #1 at 0x4A.  kldload ig4; kldload atmel_mxt.
 *
 * The kernel driver and test code is written from scratch, but some code has
 * been snarfed (in separate files) from linux development as referenced
 * here:
 *
 * www.atmel.com/products/touchsolutions/touchscreens/unlimited_touch.aspx
 * git://github.com/atmel-maxtouch/obp-utils.git
 *
 * The linux driver was also consulted, but not used.  Note that the linux
 * driver appears to be GPL'd but uses code from obp-utils.git on github
 * which is (c)Copyright by Atmel and uses more of a BSD-like license.  The
 * obp-* source files contain the snarfed code and include this license.
 *
 * The ELO touchscreen serial protocol uses 10-byte fixed-length packets:
 *
 *	Byte 0:	    ELO_SYNC_BYTE ('U')
 *	Byte 1-8:   Packet data
 *	Byte 9:	    checksum of bytes 0 to 8
 *
 * Our interface reads and writes only whole packets and does not do any
 * buffer stitching.  It is compatible with Xorg.
 *
 * Control Commands sent from Userland (only an Ack packet is returned)
 *
 *	Byte 0:	    ELO_SYNC_BYTE ('U')
 *	Byte 1:	    ELO_MODE ('m')
 *	Byte 2:	    Flags
 *	    0x80 -
 *	    0x40 Tracking mode
 *	    0x20 -
 *	    0x10 -
 *	    0x08 Scaling mode
 *	    0x04 Untouch mode
 *	    0x02 Streaming mode
 *	    0x01 Touch mode
 *
 *	Byte 0:	    ELO_SYNC_BYTE ('U')
 *	Byte 1:	    ELO_REPORT ('r')
 *
 * Query Command sent from Userland: (expect response packet and Ack packet)
 *
 *	Byte 0:	    ELO_SYNC_BYTE ('U')		Request ID
 *	Byte 1:	    ELO_ID ('i')
 *
 *	Byte 0:	    ELO_SYNC_BYTE ('U')`	Request Owner
 *	Byte 1:	    ELO_OWNER ('o')
 *
 * Streaming packets sent from the driver to userland
 *
 *	Byte 0:	    ELO_SYNC_BYTE ('U')
 *	Byte 1:	    ELO_TOUCH ('T')
 *	Byte 2:	    Packet type
 *	  Bit 2 : Pen Up	(Release)	0x04
 *	  Bit 1 : Position	(Stream)	0x02
 *	  Bit 0 : Pen Down	(Press)		0x01
 *	Byte 3:	    X coordinate lsb
 *	Byte 4:	    X coordinate msb
 *	Byte 5:	    Y coordinate lsb
 *	Byte 6:	    Y coordinate msb
 *	Byte 7:	    Z coordinate lsb
 *	Byte 8:	    Z coordinate msb
 *
 * Responses to commands: (one or two packets returned)
 *
 *	Byte 0:	    ELO_SYNC_BYTE ('U')		(if in response to query)
 *	Byte 1:	    toupper(command_byte)	(control commands have no
 *	Byte 2-8:   ... depends ....		 response)
 *
 *	Byte 0:	    ELO_SYNC_BYTE ('U')		(unconditional ack)
 *	Byte 1:	    'A'ck
 *	Byte 2-8:   ... depends ....
 *
 * NOTE!  For the most part we ignore commands other than doing the handwaving
 *	  to send a dummied-up response and ack as assumed by the X driver.
 *	  Also, the read() and write() support only reads and writes whole
 *	  packets.  There is NO byte-partial buffering.  The X driver appears
 *	  to be compatible with these restrictions.
 *
 * figure out the bootstrapping and commands.
 *
 * Unable to locate any datasheet for the device.
 *
 *				    FEATURES
 *
 * Currently no features.  Only one finger is supported by this attachment
 * for now and I haven't written any de-jitter and finger-transfer code.
 * This is good enough for moving and resizing windows (given big enough
 * widgets), and hitting browser buttons and hotlinks.
 *
 * Currently no scrolling control or other features.  We would need to
 * basically implement either the linux general input even infrastructure
 * which is a LOT of code, or a mouse emulator to handle scrolling emulation.
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
#include "atmel_mxt.h"

#include "smbus_if.h"
#include "bus_if.h"
#include "device_if.h"

struct elopacket {
	uint8_t	sync;
	uint8_t	cmd;
	uint8_t	byte2;
	uint8_t	byte3;
	uint8_t	byte4;
	uint8_t	byte5;
	uint8_t	byte6;
	uint8_t	byte7;
	uint8_t	byte8;
	uint8_t	csum;
} __packed;

typedef struct elopacket elopacket_t;

typedef struct atmel_track {
	uint16_t x;
	uint16_t y;
	uint16_t pressure;
	int	status;
	int	report;		/* what we have to report */
} atmel_track_t;

#define ATMEL_TRACK_RELEASED		0
#define ATMEL_TRACK_PRESSED		1

#define ATMEL_REPORT_PRESS	0x0001
#define ATMEL_REPORT_MOVE	0x0002
#define ATMEL_REPORT_RELEASE	0x0004

#define ATMEL_MAXTRACK		10

struct atmel_mxt_softc {
	device_t dev;
	int	count;			/* >0 if device opened */
	int	unit;
	int	addr;
	cdev_t	devnode;
	struct kqinfo kqinfo;
	struct lock lk;

	int	poll_flags;
	thread_t poll_td;

	/*
	 * Hardware state
	 */
	struct mxt_rollup	core;
	struct mxt_object	*msgprocobj;
	struct mxt_object	*cmdprocobj;

	/*
	 * Capabilities
	 */
	short	cap_resx;
	short	cap_resy;

	/*
	 * Emulation
	 */
	atmel_track_t track[ATMEL_MAXTRACK];
	int	tracking;
	int	track_fingers;

	elopacket_t pend_rep;		/* pending reply to command */
	int	pend_ack;		/* pending reply mode */

	int	last_active_tick;
	int	last_calibrate_tick;
	int	data_signal;		/* something ready to read */
	int	blocked;		/* someone is blocking */
	int	reporting_mode;
	int	sample_rate;		/* samples/sec */
	int	poll_ticks;
};

typedef struct atmel_mxt_softc atmel_mxt_softc_t;

#define ATMEL_POLL_SHUTDOWN	0x0001

#define PEND_ACK_NONE		0	/* no reply to command pending */
#define PEND_ACK_RESPOND	1	/* reply w/response and ack */
#define PEND_ACK_ACK		2	/* reply w/ack only */

#define REPORT_NONE		0x0000
#define REPORT_ALL		0x0001

/*
 * Async debug variable commands are executed by the poller and will
 * auto-clear.
 */
static int atmel_mxt_idle_freq = 1;
SYSCTL_INT(_debug, OID_AUTO, atmel_mxt_idle_freq, CTLFLAG_RW,
		&atmel_mxt_idle_freq, 0, "");
static int atmel_mxt_slow_freq = 20;
SYSCTL_INT(_debug, OID_AUTO, atmel_mxt_slow_freq, CTLFLAG_RW,
		&atmel_mxt_slow_freq, 0, "");
static int atmel_mxt_norm_freq = 100;
SYSCTL_INT(_debug, OID_AUTO, atmel_mxt_norm_freq, CTLFLAG_RW,
		&atmel_mxt_norm_freq, 0, "");
static int atmel_mxt_minpressure = 16;
SYSCTL_INT(_debug, OID_AUTO, atmel_mxt_minpressure, CTLFLAG_RW,
		&atmel_mxt_minpressure, 0, "");

/*
 * Run a calibration command every N seconds only when idle.  0 to disable.
 * Default every 30 seconds.
 */
static int atmel_mxt_autocalibrate = 30;
SYSCTL_INT(_debug, OID_AUTO, atmel_mxt_autocalibrate, CTLFLAG_RW,
		&atmel_mxt_autocalibrate, 0, "");

/*
 * run a calibration on module startup.
 */
static int atmel_mxt_debug = 0;
SYSCTL_INT(_debug, OID_AUTO, atmel_mxt_debug, CTLFLAG_RW,
		&atmel_mxt_debug, 0, "");

static void atmel_mxt_poll_thread(void *arg);
static void atmel_find_active_state(atmel_mxt_softc_t *sc);
static int atmel_mxt_raw_input(atmel_mxt_softc_t *sc, mxt_message_t *msg);
static struct mxt_object *mxt_findobject(struct mxt_rollup *core, int type);
static int mxt_read_reg(atmel_mxt_softc_t *sc, uint16_t reg,
			void *rbuf, int bytes);
static int mxt_write_reg_buf(atmel_mxt_softc_t *sc, uint16_t reg,
			void *xbuf, int bytes);
static int mxt_write_reg(atmel_mxt_softc_t *sc, uint16_t reg, uint8_t val);
static int mxt_read_object(atmel_mxt_softc_t *sc, struct mxt_object *obj,
			void *rbuf, int rbytes);
static int mxt_write_object_off(atmel_mxt_softc_t *sc, struct mxt_object *obj,
			int offset, uint8_t val);

static
const char *
msgflagsstr(uint8_t flags)
{
	static char buf[9];

	buf[0] = (flags & MXT_MSGF_DETECT) ? 'D' : '.';
	buf[1] = (flags & MXT_MSGF_PRESS) ? 'P' : '.';
	buf[2] = (flags & MXT_MSGF_RELEASE) ? 'R' : '.';
	buf[3] = (flags & MXT_MSGF_MOVE) ? 'M' : '.';
	buf[4] = (flags & MXT_MSGF_VECTOR) ? 'V' : '.';
	buf[5] = (flags & MXT_MSGF_AMP) ? 'A' : '.';
	buf[6] = (flags & MXT_MSGF_SUPPRESS) ? 'S' : '.';
	buf[7] = (flags & MXT_MSGF_UNGRIP) ? 'U' : '.';

	return buf;
}

static
void
atmel_mxt_lock(atmel_mxt_softc_t *sc)
{
	lockmgr(&sc->lk, LK_EXCLUSIVE);
}

static
void
atmel_mxt_unlock(atmel_mxt_softc_t *sc)
{
	lockmgr(&sc->lk, LK_RELEASE);
}

/*
 * Notify if possible receive data ready.  Must be called
 * without the lock held to avoid deadlocking in kqueue.
 */
static
void
atmel_mxt_notify(atmel_mxt_softc_t *sc)
{
	if (sc->data_signal) {
		KNOTE(&sc->kqinfo.ki_note, 0);
		atmel_mxt_lock(sc);
		if (sc->blocked) {
			sc->blocked = 0;
			wakeup(&sc->blocked);
		}
		atmel_mxt_unlock(sc);
	}
}

/*
 * Initialize the device
 */
static
int
init_device(atmel_mxt_softc_t *sc, int probe)
{
	int blksize;
	int totsize;
	uint32_t crc;

	if (mxt_read_reg(sc, 0, &sc->core.info, sizeof(sc->core.info))) {
		device_printf(sc->dev, "init_device read-reg failed\n");
		return ENXIO;
	}
	sc->core.nobjs = sc->core.info.num_objects;
	if (!probe) {
		device_printf(sc->dev,
			      "%d configuration objects\n",
			      sc->core.info.num_objects);
	}
	if (sc->core.nobjs < 0 || sc->core.nobjs > 1024) {
		device_printf(sc->dev,
			      "init_device nobjs (%d) out of bounds\n",
			      sc->core.nobjs);
		return ENXIO;
	}
	blksize = sizeof(sc->core.info) +
		  sc->core.nobjs * sizeof(struct mxt_object);
	totsize = blksize + sizeof(struct mxt_raw_crc);

	sc->core.buf = kmalloc(totsize, M_DEVBUF, M_WAITOK | M_ZERO);
	if (mxt_read_reg(sc, 0, sc->core.buf, totsize)) {
		device_printf(sc->dev,
			      "init_device cannot read configuration space\n");
		goto done;
	}
	kprintf("COREBUF %p %d\n", sc->core.buf, blksize);
	crc = obp_convert_crc((void *)((uint8_t *)sc->core.buf + blksize));
	if (obp_crc24(sc->core.buf, blksize) != crc) {
		device_printf(sc->dev,
			      "init_device: configuration space "
			      "crc mismatch %08x/%08x\n",
			      crc, obp_crc24(sc->core.buf, blksize));
		/*goto done;*/
	}
	{
		int i;

		kprintf("info:   ");
		for (i = 0; i < sizeof(sc->core.info); ++i)
			kprintf(" %02x", sc->core.buf[i]);
		kprintf("\nconfig: ");
		while (i < blksize) {
			kprintf(" %02x", sc->core.buf[i]);
			++i;
		}
		kprintf("\n");
	}
	sc->core.objs = (void *)((uint8_t *)sc->core.buf +
				 sizeof(sc->core.info));
	sc->msgprocobj = mxt_findobject(&sc->core, MXT_GEN_MESSAGEPROCESSOR);
	sc->cmdprocobj = mxt_findobject(&sc->core, MXT_GEN_COMMANDPROCESSOR);
	if (sc->msgprocobj == NULL) {
		device_printf(sc->dev,
			      "init_device: cannot find msgproc config\n");
		goto done;
	}

done:
	if (sc->msgprocobj == NULL) {
		if (sc->core.buf) {
			kfree(sc->core.buf, M_DEVBUF);
			sc->core.buf = NULL;
		}
		return ENXIO;
	} else {
		if (probe) {
			kfree(sc->core.buf, M_DEVBUF);
			sc->core.buf = NULL;
		}
		return 0;
	}
}

/*
 * Device infrastructure
 */
#define ATMEL_SOFTC(unit) \
	((atmel_mxt_softc_t *)devclass_get_softc(atmel_mxt_devclass, (unit)))

static void atmel_mxt_identify(driver_t *driver, device_t parent);
static int atmel_mxt_probe(device_t);
static int atmel_mxt_attach(device_t);
static int atmel_mxt_detach(device_t);

static devclass_t atmel_mxt_devclass;

static device_method_t atmel_mxt_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	atmel_mxt_identify),
	DEVMETHOD(device_probe,		atmel_mxt_probe),
	DEVMETHOD(device_attach,	atmel_mxt_attach),
	DEVMETHOD(device_detach,	atmel_mxt_detach),

#if 0
	/* smbus interface */
	DEVMETHOD(smbus_intr,		smbus_generic_intr),
#endif

	DEVMETHOD_END
};

static driver_t atmel_mxt_driver = {
	"atmel_mxt",
	atmel_mxt_methods,
	sizeof(atmel_mxt_softc_t),
};

static	d_open_t	atmel_mxtopen;
static	d_close_t	atmel_mxtclose;
static	d_ioctl_t	atmel_mxtioctl;
static	d_read_t	atmel_mxtread;
static	d_write_t	atmel_mxtwrite;
static	d_kqfilter_t	atmel_mxtkqfilter;

static struct dev_ops atmel_mxt_ops = {
	{ "atmel_mxt", 0, 0 },
	.d_open =	atmel_mxtopen,
	.d_close =	atmel_mxtclose,
	.d_ioctl =	atmel_mxtioctl,
	.d_read =	atmel_mxtread,
	.d_write =	atmel_mxtwrite,
	.d_kqfilter =	atmel_mxtkqfilter,
};

static void
atmel_mxt_identify(driver_t *driver, device_t parent)
{
	if (device_find_child(parent, "atmel_mxt", -1) == NULL)
		BUS_ADD_CHILD(parent, parent, 0, "atmel_mxt", -1);
}

static int
atmel_mxt_probe(device_t dev)
{
	atmel_mxt_softc_t sc;
	int error;

	bzero(&sc, sizeof(sc));
	sc.dev = dev;
	sc.unit = device_get_unit(dev);

	/*
	 * Only match against specific addresses to avoid blowing up
	 * other I2C devices (?).  At least for now.
	 *
	 * 0x400 (from smbus) - means specific device address probe,
	 *			rather than generic.
	 *
	 * 0x4A - cypress trackpad on the acer c720.
	 */
	if ((sc.unit & 0x04FF) != (0x0400 | 0x04A))
		return ENXIO;
	sc.addr = sc.unit & 0x3FF;
	error = init_device(&sc, 1);
	if (error)
		return ENXIO;

	device_set_desc(dev, "Atmel MXT TouchScreen");

	return (BUS_PROBE_VENDOR);
}

static int
atmel_mxt_attach(device_t dev)
{
	atmel_mxt_softc_t *sc;

	sc = (atmel_mxt_softc_t *)device_get_softc(dev);
	if (!sc)
		return ENOMEM;

	bzero(sc, sizeof(*sc));

	lockinit(&sc->lk, "atmel_mxt", 0, 0);
	sc->reporting_mode = 1;

	sc->dev = dev;
	sc->unit = device_get_unit(dev);
	if ((sc->unit & 0x04FF) != (0x0400 | 0x04A))
		return ENXIO;
	sc->addr = sc->unit & 0x3FF;
	sc->last_active_tick = ticks;
	sc->last_calibrate_tick = ticks - atmel_mxt_autocalibrate * hz;

	if (init_device(sc, 0))
		return ENXIO;

	if (sc->unit & 0x0400) {
		sc->devnode = make_dev(&atmel_mxt_ops, sc->unit,
				UID_ROOT, GID_WHEEL, 0600,
				"atmel%d-%02x",
				sc->unit >> 11, sc->unit & 1023);
	} else {
		sc->devnode = make_dev(&atmel_mxt_ops, sc->unit,
				UID_ROOT, GID_WHEEL, 0600, "atmel%d", sc->unit);
	}
	device_printf(dev, "atmel mxt touchscreen driver attached\n");
	/* device_printf(dev, "...."); */

	/*
	 * Start the polling thread.
	 */
	lwkt_create(atmel_mxt_poll_thread, sc,
		    &sc->poll_td, NULL, 0, -1, "atmel_mxt-poll");

	return (0);
}

static int
atmel_mxt_detach(device_t dev)
{
	atmel_mxt_softc_t *sc;

	sc = (atmel_mxt_softc_t *)device_get_softc(dev);

	/*
	 * Cleanup our poller thread
	 */
	atomic_set_int(&sc->poll_flags, ATMEL_POLL_SHUTDOWN);
	while (sc->poll_td) {
		wakeup(&sc->poll_flags);
		tsleep(&sc->poll_td, 0, "atmel_mxtdet", hz);
	}

	if (sc->devnode)
		dev_ops_remove_minor(&atmel_mxt_ops, device_get_unit(dev));
	if (sc->devnode)
		devfs_assume_knotes(sc->devnode, &sc->kqinfo);
	if (sc->core.buf) {
		kfree(sc->core.buf, M_DEVBUF);
		sc->core.buf = NULL;
	}
	sc->msgprocobj = NULL;
	sc->cmdprocobj = NULL;

	return (0);
}

/*
 * USER DEVICE I/O FUNCTIONS
 */
static int
atmel_mxtopen (struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	atmel_mxt_softc_t *sc = ATMEL_SOFTC(minor(dev));

	if (sc == NULL)
		return (ENXIO);

	if (sc->count != 0)
		return (EBUSY);

	sc->count++;

	return (0);
}

static int
atmel_mxtclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	atmel_mxt_softc_t *sc = ATMEL_SOFTC(minor(dev));

	if (sc == NULL)
		return (ENXIO);

	if (sc->count == 0) {
		/* This is not supposed to happen. */
		return (0);
	}

	if (sc->count-- == 0) {
		sc->reporting_mode = 0;
	}

	return (0);
}

static int
atmel_mxtread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	atmel_mxt_softc_t *sc = ATMEL_SOFTC(minor(dev));
	int error;
	struct uio *uio = ap->a_uio;
	int ioflag = ap->a_ioflag;
	size_t n;
	elopacket_t pkt;
	atmel_track_t *track;

	/*
	 * Load next ready event, block if necessary.
	 */
	atmel_mxt_lock(sc);
	for (;;) {
		error = 0;

		switch(sc->pend_ack) {
		case PEND_ACK_NONE:
			if (sc->tracking && sc->track[sc->tracking].report) {
				/*
				 * Report ready
				 */
				track = &sc->track[sc->tracking];
				bzero(&pkt, sizeof(pkt));
				pkt.cmd = 'T';
				if (track->report & ATMEL_REPORT_PRESS) {
					pkt.byte2 |= 0x01;
					track->report &= ~ATMEL_REPORT_PRESS;
				} else if (track->report & ATMEL_REPORT_MOVE) {
					pkt.byte2 |= 0x02;
					track->report &= ~ATMEL_REPORT_MOVE;
				} else if (track->report &
					   ATMEL_REPORT_RELEASE) {
					pkt.byte2 |= 0x04;
					track->report &= ~ATMEL_REPORT_RELEASE;
				}
				pkt.byte3 = track->x & 0xFF;
				pkt.byte4 = track->x >> 8;
				pkt.byte5 = track->y & 0xFF;
				pkt.byte6 = track->y >> 8;
				pkt.byte7 = track->pressure & 0xFF;
				pkt.byte8 = track->pressure >> 8;
			} else if (ioflag & IO_NDELAY) {
				/*
				 * Non-blocking, nothing ready
				 */
				error = EWOULDBLOCK;
			} else {
				/*
				 * Blocking, nothing ready
				 */
				sc->data_signal = 0;
				sc->blocked = 1;
				error = lksleep(&sc->blocked, &sc->lk,
						PCATCH, "atmelw", 0);
				if (error == 0)
					continue;
			}
			break;
		case PEND_ACK_RESPOND:
			pkt = sc->pend_rep;
			sc->pend_ack = PEND_ACK_ACK;
			break;
		case PEND_ACK_ACK:
			bzero(&pkt, sizeof(pkt));
			pkt.cmd = 'A';
			sc->pend_ack = PEND_ACK_NONE;
			break;
		}
		atmel_find_active_state(sc);
		break;
	}
	atmel_mxt_unlock(sc);

	/*
	 * If no error we can return the event loaded into pkt.
	 */
	if (error == 0) {
		uint8_t csum = 0xAA;
		int i;

		pkt.sync = 'U';
		for (i = 0; i < sizeof(pkt) - 1; ++i)
			csum += ((uint8_t *)&pkt)[i];
		pkt.csum = csum;
		n = uio->uio_resid;
		if (n > sizeof(pkt))
			n = sizeof(pkt);
		error = uiomove((void *)&pkt, n, uio);
	}

	return error;
}

static int
atmel_mxtwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	atmel_mxt_softc_t *sc = ATMEL_SOFTC(minor(dev));
	struct uio *uio = ap->a_uio;
	elopacket_t pkt;
	int error;
	size_t n;

	error = 0;

	while (uio->uio_resid) {
		bzero(&pkt, sizeof(pkt));
		n = uio->uio_resid;
		if (n > sizeof(pkt))
			n = sizeof(pkt);
		error = uiomove((void *)&pkt, n, uio);
		if (error)
			break;
		atmel_mxt_lock(sc);
		switch(pkt.cmd) {
		case 'i':
			/*
			 * ELO_ID request id
			 */
			bzero(&sc->pend_rep, sizeof(sc->pend_rep));
			sc->pend_rep.cmd = 'I';
			sc->pend_ack = PEND_ACK_RESPOND;
			break;
		case 'o':
			/*
			 * ELO_OWNER request owner
			 */
			bzero(&sc->pend_rep, sizeof(sc->pend_rep));
			sc->pend_rep.cmd = 'O';
			sc->pend_ack = PEND_ACK_RESPOND;
			break;
		case 'm':
			/*
			 * ELO_MODE control packet
			 */
			sc->pend_ack = PEND_ACK_ACK;
			break;
		case 'r':
			/*
			 * ELO_REPORT control packet
			 */
			sc->pend_ack = PEND_ACK_ACK;
			break;
		}
		atmel_mxt_unlock(sc);
	}
	return error;
}

static void atmel_mxt_filt_detach(struct knote *);
static int atmel_mxt_filt(struct knote *, long);

static struct filterops atmel_mxt_filtops =
        { FILTEROP_ISFD, NULL, atmel_mxt_filt_detach, atmel_mxt_filt };

static int
atmel_mxtkqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	atmel_mxt_softc_t *sc = ATMEL_SOFTC(minor(dev));
	struct knote *kn = ap->a_kn;
	struct klist *klist;

	switch(kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &atmel_mxt_filtops;
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
atmel_mxt_filt_detach(struct knote *kn)
{
	atmel_mxt_softc_t *sc = (atmel_mxt_softc_t *)kn->kn_hook;
	struct klist *klist;

	klist = &sc->kqinfo.ki_note;
	knote_remove(klist, kn);
}

static int
atmel_mxt_filt(struct knote *kn, long hint)
{
	atmel_mxt_softc_t *sc = (atmel_mxt_softc_t *)kn->kn_hook;
	int ready;

	atmel_mxt_lock(sc);
	if (sc->data_signal)
		ready = 1;
	else
		ready = 0;
	atmel_mxt_unlock(sc);

	return (ready);
}

static int
atmel_mxtioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	device_t bus;		/* smbbus */
	/*struct atmel_mxtcmd *s = (struct atmel_mxtcmd *)ap->a_data;*/
	void *s = NULL;
	atmel_mxt_softc_t *sc = ATMEL_SOFTC(minor(dev));
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
atmel_mxt_poll_thread(void *arg)
{
	atmel_mxt_softc_t *sc = arg;
	int error;
	int freq = atmel_mxt_norm_freq;
	int isidle = 0;

	while ((sc->poll_flags & ATMEL_POLL_SHUTDOWN) == 0) {
		if (sc->msgprocobj)
			error = 0;
		else
			error = ENXIO;
		if (error == 0) {
			mxt_message_t msg;

			error = mxt_read_object(sc, sc->msgprocobj,
					    &msg, sizeof(msg));
			if (error == 0)
				isidle = atmel_mxt_raw_input(sc, &msg);
			else
				isidle = 1;
		}

		/*
		 * don't let the last_active_tick or last_calibrate_tick
		 * delta calculation overflow.
		 */
		if ((ticks - sc->last_active_tick) > 1000 * hz)
			sc->last_active_tick = ticks - 1000 * hz;
		if ((ticks - sc->last_calibrate_tick) > 1000 * hz)
			sc->last_calibrate_tick = ticks - 1000 * hz;

		/*
		 * Automatically calibrate when the touchpad has been
		 * idle atmel_mxt_autocalibrate seconds, and recalibrate
		 * on the same interval while it remains idle.
		 *
		 * If we don't do this the touchscreen can get really out
		 * of whack over time and basically stop functioning properly.
		 * It's unclear why the device does not do this automatically.
		 *
		 * Response occurs in the message stream (which we just
		 * ignore).
		 */
		if (sc->cmdprocobj && atmel_mxt_autocalibrate &&
		    ((ticks - sc->last_calibrate_tick) >
		     atmel_mxt_autocalibrate * hz) &&
		    ((ticks - sc->last_active_tick) >
		     atmel_mxt_autocalibrate * hz)) {
			sc->last_calibrate_tick = ticks;
			mxt_write_object_off(sc, sc->cmdprocobj,
					 MXT_CMDPROC_CALIBRATE_OFF, 1);
		}
		tsleep(&sc->poll_flags, 0, "atmpol", (hz + freq - 1) / freq);
		++sc->poll_ticks;
		if (sc->count == 0)
			freq = atmel_mxt_idle_freq;
		else if (isidle)
			freq = atmel_mxt_slow_freq;
		else
			freq = atmel_mxt_norm_freq;
	}
	sc->poll_td = NULL;
	wakeup(&sc->poll_td);
}

/*
 * Calculate currently active state, if any
 */
static
void
atmel_find_active_state(atmel_mxt_softc_t *sc)
{
	atmel_track_t *track;
	int i;

	track = &sc->track[sc->tracking];
	if (track->report == 0) {
		for (i = 0; i < ATMEL_MAXTRACK; ++i) {
			track = &sc->track[i];
			if (track->report) {
				sc->tracking = i;
				break;
			}
		}
	}
	if (track->report == 0 && sc->pend_ack == PEND_ACK_NONE) {
		sc->data_signal = 0;
	} else {
		sc->data_signal = 1;
	}
}

/*
 * Return non-zero if we are idle
 */
static
int
atmel_mxt_raw_input(atmel_mxt_softc_t *sc, mxt_message_t *msg)
{
	atmel_track_t *track;
	int donotify = 0;

	if (atmel_mxt_debug) {
		kprintf("track=%02x f=%s x=%-4d y=%-4d p=%d amp=%d\n",
			msg->any.reportid,
			msgflagsstr(msg->touch.flags),
			(msg->touch.pos[0] << 4) |
				((msg->touch.pos[2] >> 4) & 0x0F),
			(msg->touch.pos[1] << 4) |
				((msg->touch.pos[2]) & 0x0F),
			msg->touch.area,
			msg->touch.amplitude);
	}
	atmel_mxt_lock(sc);

	/*
	 * If message buffer is empty and no fingers are currently pressed
	 * return idle, else we are not idle.
	 */
	if (msg->any.reportid == 0xFF)
		goto done;

	/*
	 * Process message buffer.  For now ignore any messages with
	 * reportids that we do not understand.
	 *
	 * note: reportid==1  typicallk acknowledges calibrations (?)
	 */
	if (msg->any.reportid < 3 || msg->any.reportid >= ATMEL_MAXTRACK)
		goto done;

	sc->last_active_tick = ticks;

	track = &sc->track[msg->any.reportid];
	track->x = (msg->touch.pos[0] << 4) |
		   ((msg->touch.pos[2] >> 4) & 0x0F);
	track->y = (msg->touch.pos[1] << 4) |
		   (msg->touch.pos[2] & 0x0F);
	track->pressure = msg->touch.amplitude;

	track->x = track->x * 3000 / 1361;
	track->y = track->y * 3000 / 3064;

	if (msg->touch.flags & MXT_MSGF_DETECT) {
		track->status = ATMEL_TRACK_PRESSED;
		if (msg->touch.flags & MXT_MSGF_PRESS) {
			track->report |= ATMEL_REPORT_PRESS;
		}
		if (msg->touch.flags & MXT_MSGF_MOVE) {
			track->report |= ATMEL_REPORT_MOVE;
		}
	} else {
		track->status = ATMEL_TRACK_RELEASED;
		track->report |= ATMEL_REPORT_RELEASE;
	}
	atmel_find_active_state(sc);
	donotify = 1;
done:
	atmel_mxt_unlock(sc);
	if (donotify)
		atmel_mxt_notify(sc);
	if (sc->track_fingers)
		return 0;
	else
		return 1;
}

/*
 * Support functions
 */
static
struct mxt_object *
mxt_findobject(struct mxt_rollup *core, int type)
{
	int i;

	for (i = 0; i < core->nobjs; ++i) {
		if (core->objs[i].type == type)
			return(&core->objs[i]);
	}
	return NULL;
}

static int
mxt_read_reg(atmel_mxt_softc_t *sc, uint16_t reg, void *rbuf, int bytes)
{
	uint8_t wreg[2];
	device_t bus;
	int error;
	int rbytes;

	wreg[0] = reg & 255;
	wreg[1] = reg >> 8;

	bus = device_get_parent(sc->dev);
	if ((error = smbus_request_bus(bus, sc->dev, SMB_WAIT | SMB_INTR)) != 0)
		return error;
	error = smbus_trans(bus, sc->addr, 0,
			    SMB_TRANS_NOCNT |
			    SMB_TRANS_NOCMD |
			    SMB_TRANS_7BIT,
			    wreg, 2,
			    rbuf, bytes, &rbytes);
	smbus_release_bus(bus, sc->dev);

	if (bytes != rbytes) {
		device_printf(sc->dev,
			      "smbus_trans reg %d short read %d/%d\n",
			      reg, bytes, rbytes);
		error = EINVAL;
	}

	return error;
}

static int
mxt_write_reg_buf(atmel_mxt_softc_t *sc, uint16_t reg, void *xbuf, int bytes)
{
	uint8_t wbuf[256];
	device_t bus;
	int error;

	KKASSERT(bytes < sizeof(wbuf) - 2);
	wbuf[0] = reg & 255;
	wbuf[1] = reg >> 8;
	bcopy(xbuf, wbuf + 2, bytes);

	bus = device_get_parent(sc->dev);
	if ((error = smbus_request_bus(bus, sc->dev, SMB_WAIT | SMB_INTR)) != 0)
		return error;
	error = smbus_trans(bus, sc->addr, 0,
			    SMB_TRANS_NOCNT |
			    SMB_TRANS_NOCMD |
			    SMB_TRANS_7BIT,
			    wbuf, bytes + 2,
			    NULL, 0, NULL);
	smbus_release_bus(bus, sc->dev);
	return error;
}

static int
mxt_write_reg(atmel_mxt_softc_t *sc, uint16_t reg, uint8_t val)
{
	return mxt_write_reg_buf(sc, reg, &val, 1);
}

static int
mxt_read_object(atmel_mxt_softc_t *sc, struct mxt_object *obj,
	        void *rbuf, int rbytes)
{
	uint16_t reg = obj->start_pos_lsb + (obj->start_pos_msb << 8);
	int bytes = obj->size_minus_one + 1;

	if (bytes > rbytes)
		bytes = rbytes;
	return mxt_read_reg(sc, reg, rbuf, bytes);
}

static int
mxt_write_object_off(atmel_mxt_softc_t *sc, struct mxt_object *obj,
		 int offset, uint8_t val)
{
	uint16_t reg = obj->start_pos_lsb + (obj->start_pos_msb << 8);

	reg += offset;
	return mxt_write_reg(sc, reg, val);
}

DRIVER_MODULE(atmel_mxt, smbus, atmel_mxt_driver, atmel_mxt_devclass,
	      NULL, NULL);
MODULE_DEPEND(atmel_mxt, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(atmel_mxt, 1);
