/*-
 * Copyright (c) 1998, 2001 Nicolas Souchu
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/smbus/smb.c,v 1.34.8.2 2006/09/22 19:19:16 jhb Exp $
 *
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/fcntl.h>

#include "smbconf.h"
#include "smbus.h"
#include "smb.h"

#include "smbus_if.h"
#include "bus_if.h"
#include "device_if.h"

#define BUFSIZE 1024

struct smb_softc {
	device_t sc_dev;
	int sc_count;			/* >0 if device opened */
	int sc_unit;
	cdev_t sc_devnode;
};

#define SMB_SOFTC(unit) \
	((struct smb_softc *)devclass_get_softc(smb_devclass, (unit)))

#define SMB_DEVICE(unit) \
	(devclass_get_device(smb_devclass, (unit)))

static void smb_identify(driver_t *driver, device_t parent);
static int smb_probe(device_t);
static int smb_attach(device_t);
static int smb_detach(device_t);

static devclass_t smb_devclass;

static device_method_t smb_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	smb_identify),
	DEVMETHOD(device_probe,		smb_probe),
	DEVMETHOD(device_attach,	smb_attach),
	DEVMETHOD(device_detach,	smb_detach),

#if 0
	/* bus interface */
	DEVMETHOD(bus_driver_added,     bus_generic_driver_added),
	DEVMETHOD(bus_print_child,      bus_generic_print_child),
#endif

	/* smbus interface */
	DEVMETHOD(smbus_intr,		smbus_generic_intr),

	DEVMETHOD_END
};

static driver_t smb_driver = {
	"smb",
	smb_methods,
	sizeof(struct smb_softc),
};

static	d_open_t	smbopen;
static	d_close_t	smbclose;
static	d_ioctl_t	smbioctl;

static struct dev_ops smb_ops = {
	{ "smb", 0, 0 },
	.d_open =	smbopen,
	.d_close =	smbclose,
	.d_ioctl =	smbioctl,
};

static void
smb_identify(driver_t *driver, device_t parent)
{
	if (device_find_child(parent, "smb", -1) == NULL)
		BUS_ADD_CHILD(parent, parent, 0, "smb", -1);
}

static int
smb_probe(device_t dev)
{
	device_set_desc(dev, "SMBus generic I/O");

	/* Allow other subclasses to override this driver. */
	return (BUS_PROBE_GENERIC);
}

static int
smb_attach(device_t dev)
{
	struct smb_softc *sc = (struct smb_softc *)device_get_softc(dev);
	int unit;

	if (!sc)
		return (ENOMEM);

	bzero(sc, sizeof(struct smb_softc *));
	unit = device_get_unit(dev);
	sc->sc_dev = dev;
	sc->sc_unit = unit;

	if (unit & 0x0400) {
		sc->sc_devnode = make_dev(&smb_ops, unit,
				UID_ROOT, GID_WHEEL, 0600,
				"smb%d-%02x", unit >> 11, unit & 1023);
	} else {
		sc->sc_devnode = make_dev(&smb_ops, unit,
				UID_ROOT, GID_WHEEL, 0600, "smb%d", unit);
	}

	return (0);
}

static int
smb_detach(device_t dev)
{
	struct smb_softc *sc = (struct smb_softc *)device_get_softc(dev);

	if (sc->sc_devnode)
		dev_ops_remove_minor(&smb_ops, device_get_unit(dev));

	return (0);
}

static int
smbopen (struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct smb_softc *sc = SMB_SOFTC(minor(dev));

	if (sc == NULL)
		return (ENXIO);

	if (sc->sc_count != 0)
		return (EBUSY);

	sc->sc_count++;

	return (0);
}

static int
smbclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct smb_softc *sc = SMB_SOFTC(minor(dev));

	if (sc == NULL)
		return (ENXIO);

	if (sc->sc_count == 0)
		/* This is not supposed to happen. */
		return (0);

	sc->sc_count--;

	return (0);
}

#if 0
static int
smbwrite(struct dev_write_args *ap)
{
	return (EINVAL);
}

static int
smbread(struct dev_read_args *ap)
{
	return (EINVAL);
}
#endif

static int
smbioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	device_t bus;		/* smbbus */
	char buf[SMB_MAXBLOCKSIZE];
	struct smbcmd *s = (struct smbcmd *)ap->a_data;
	struct smb_softc *sc = SMB_SOFTC(minor(dev));
	device_t smbdev = SMB_DEVICE(minor(dev));
	int error;
	int unit;
	u_char bcount;

	if (sc == NULL)
		return (ENXIO);
	if (s == NULL)
		return (EINVAL);

	/*
	 * If a specific slave device is being used, override any passed-in
	 * slave.
	 */
	unit = sc->sc_unit;
	if (unit & 0x0400) {
		s->slave = unit & 1023;
	}

	/*
	 * NOTE: smbus_*() functions automatically recurse the parent to
	 *	 get to the actual device driver.
	 */
	bus = device_get_parent(smbdev);	/* smbus */

	/* Allocate the bus. */
	if ((error = smbus_request_bus(bus, smbdev,
			(ap->a_fflag & O_NONBLOCK) ?
			SMB_DONTWAIT : (SMB_WAIT | SMB_INTR))))
		return (error);

	switch (ap->a_cmd) {
	case SMB_QUICK_WRITE:
		error = smbus_error(smbus_quick(bus, s->slave, SMB_QWRITE));
		break;

	case SMB_QUICK_READ:
		error = smbus_error(smbus_quick(bus, s->slave, SMB_QREAD));
		break;

	case SMB_SENDB:
		error = smbus_error(smbus_sendb(bus, s->slave, s->cmd));
		break;

	case SMB_RECVB:
		error = smbus_error(smbus_recvb(bus, s->slave, &s->cmd));
		break;

	case SMB_WRITEB:
		error = smbus_error(smbus_writeb(bus, s->slave, s->cmd,
						s->wdata.byte));
		break;

	case SMB_WRITEW:
		error = smbus_error(smbus_writew(bus, s->slave, s->cmd,
						s->wdata.word));
		break;

	case SMB_READB:
		error = smbus_error(smbus_readb(bus, s->slave, s->cmd,
						&s->rdata.byte));
		if (s->rbuf && s->rcount >= 1) {
			error = copyout(&s->rdata.byte, s->rbuf, 1);
			s->rcount = 1;
		}
		break;

	case SMB_READW:
		error = smbus_error(smbus_readw(bus, s->slave, s->cmd,
						&s->rdata.word));
		if (s->rbuf && s->rcount >= 2) {
			buf[0] = (u_char)s->rdata.word;
			buf[1] = (u_char)(s->rdata.word >> 8);
			error = copyout(buf, s->rbuf, 2);
			s->rcount = 2;
		}
		break;

	case SMB_PCALL:
		error = smbus_error(smbus_pcall(bus, s->slave, s->cmd,
						s->wdata.word, &s->rdata.word));
		if (s->rbuf && s->rcount >= 2) {
			char buf[2];
			buf[0] = (u_char)s->rdata.word;
			buf[1] = (u_char)(s->rdata.word >> 8);
			error = copyout(buf, s->rbuf, 2);
			s->rcount = 2;
		}

		break;

	case SMB_BWRITE:
		if (s->wcount < 0)
			s->wcount = 0;
		if (s->wcount > SMB_MAXBLOCKSIZE)
			s->wcount = SMB_MAXBLOCKSIZE;
		if (s->wcount)
			error = copyin(s->wbuf, buf, s->wcount);
		if (error)
			break;
		error = smbus_error(smbus_bwrite(bus, s->slave, s->cmd,
						 s->wcount, buf));
		break;

	case SMB_BREAD:
		if (s->rcount < 0)
			s->rcount = 0;
		if (s->rcount > SMB_MAXBLOCKSIZE)
			s->rcount = SMB_MAXBLOCKSIZE;
		error = smbus_bread(bus, s->slave, s->cmd, &bcount, buf);
		error = smbus_error(error);
		if (error)
			break;
		if (s->rcount > bcount)
			s->rcount = bcount;
		error = copyout(buf, s->rbuf, s->rcount);
		break;

	case SMB_TRANS:
		if (s->rcount < 0)
			s->rcount = 0;
		if (s->rcount > SMB_MAXBLOCKSIZE)
			s->rcount = SMB_MAXBLOCKSIZE;
		if (s->wcount < 0)
			s->wcount = 0;
		if (s->wcount > SMB_MAXBLOCKSIZE)
			s->wcount = SMB_MAXBLOCKSIZE;
		if (s->wcount)
			error = copyin(s->wbuf, buf, s->wcount);
		if (error)
			break;
		error = smbus_trans(bus, s->slave, s->cmd, s->op,
				    buf, s->wcount, buf, s->rcount, &s->rcount);
		error = smbus_error(error);
		if (error == 0)
			error = copyout(buf, s->rbuf, s->rcount);
		break;
	default:
		error = ENOTTY;
		break;
	}

	smbus_release_bus(bus, smbdev);

	return (error);
}

DRIVER_MODULE(smb, smbus, smb_driver, smb_devclass, NULL, NULL);
MODULE_DEPEND(smb, smbus, SMBUS_MINVER, SMBUS_PREFVER, SMBUS_MAXVER);
MODULE_VERSION(smb, 1);
