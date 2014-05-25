/*-
 * Copyright (c) 1997, 1998, 1999 Nicolas Souchu, Michael Smith
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
 * $FreeBSD: src/sys/dev/ppbus/ppi.c,v 1.21.2.3 2000/08/07 18:24:43 peter Exp $
 *
 */
#include "opt_ppb_1284.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <sys/rman.h>

#include <machine/clock.h>

#include <bus/ppbus/ppbconf.h>
#include <bus/ppbus/ppb_msq.h>

#ifdef PERIPH_1284
#include <bus/ppbus/ppb_1284.h>
#endif

#include "ppi.h"

#include "ppbus_if.h"

#include <bus/ppbus/ppbio.h>

#define BUFSIZE		512

struct ppi_data {

    int		ppi_unit;
    int		ppi_flags;
#define HAVE_PPBUS	(1<<0)
#define HAD_PPBUS	(1<<1)

    int		ppi_count;
    int		ppi_mode;			/* IEEE1284 mode */
    char	ppi_buffer[BUFSIZE];

#ifdef PERIPH_1284
    struct resource *intr_resource;	/* interrupt resource */
    void *intr_cookie;			/* interrupt registration cookie */
#endif /* PERIPH_1284 */
};

#define DEVTOSOFTC(dev) \
	((struct ppi_data *)device_get_softc(dev))
#define UNITOSOFTC(unit) \
	((struct ppi_data *)devclass_get_softc(ppi_devclass, (unit)))
#define UNITODEVICE(unit) \
	(devclass_get_device(ppi_devclass, (unit)))

static devclass_t ppi_devclass;

static	d_open_t	ppiopen;
static	d_close_t	ppiclose;
static	d_ioctl_t	ppiioctl;
static	d_write_t	ppiwrite;
static	d_read_t	ppiread;

static struct dev_ops ppi_ops = {
	{ "ppi", 0, 0 },
	.d_open =	ppiopen,
	.d_close =	ppiclose,
	.d_read =	ppiread,
	.d_write =	ppiwrite,
	.d_ioctl =	ppiioctl,
};

#ifdef PERIPH_1284

static void
ppi_enable_intr(device_t ppidev)
{
	char r;
	device_t ppbus = device_get_parent(ppidev);

	r = ppb_rctr(ppbus);
	ppb_wctr(ppbus, r | IRQENABLE);

	return;
}

static void
ppi_disable_intr(device_t ppidev)
{
	char r;
        device_t ppbus = device_get_parent(ppidev);

	r = ppb_rctr(ppbus);
	ppb_wctr(ppbus, r & ~IRQENABLE);

	return;
}

#endif /* PERIPH_1284 */

/*
 * ppi_probe()
 */
static int
ppi_probe(device_t dev)
{
	struct ppi_data *ppi;

	/* probe is always ok */
	device_set_desc(dev, "Parallel I/O");

	ppi = DEVTOSOFTC(dev);
	bzero(ppi, sizeof(struct ppi_data));

	return (0);
}

/*
 * ppi_attach()
 */
static int
ppi_attach(device_t dev)
{
#ifdef PERIPH_1284
	uintptr_t irq;
	int zero = 0;
	struct ppi_data *ppi = DEVTOSOFTC(dev);

	/* retrive the irq */
	BUS_READ_IVAR(device_get_parent(dev), dev, PPBUS_IVAR_IRQ, &irq);

	/* declare our interrupt handler */
	ppi->intr_resource = bus_alloc_legacy_irq_resource(dev, &zero, irq,
	    RF_ACTIVE);
#endif /* PERIPH_1284 */

	make_dev(&ppi_ops, device_get_unit(dev),	/* XXX cleanup */
		 UID_ROOT, GID_WHEEL,
		 0600, "ppi%d", device_get_unit(dev));

	return (0);
}

#ifdef PERIPH_1284
/*
 * Cable
 * -----
 *
 * Use an IEEE1284 compliant (DB25/DB25) cable with the following tricks:
 *
 * nStrobe   <-> nAck		1  <-> 10
 * nAutofd   <-> Busy		11 <-> 14
 * nSelectin <-> Select		17 <-> 13
 * nInit     <-> nFault		15 <-> 16
 *
 */
static void
ppiintr(void *arg)
{
	device_t ppidev = (device_t)arg;
        device_t ppbus = device_get_parent(ppidev);
	struct ppi_data *ppi = DEVTOSOFTC(ppidev);

	ppi_disable_intr(ppidev);

	switch (ppb_1284_get_state(ppbus)) {

	/* accept IEEE1284 negociation then wakeup an waiting process to
	 * continue negociation at process level */
	case PPB_FORWARD_IDLE:
		/* Event 1 */
		if ((ppb_rstr(ppbus) & (SELECT | nBUSY)) ==
							(SELECT | nBUSY)) {
			/* IEEE1284 negociation */
#ifdef DEBUG_1284
			kprintf("N");
#endif

			/* Event 2 - prepare for reading the ext. value */
			ppb_wctr(ppbus, (PCD | STROBE | nINIT) & ~SELECTIN);

			ppb_1284_set_state(ppbus, PPB_NEGOCIATION);

		} else {
#ifdef DEBUG_1284
			kprintf("0x%x", ppb_rstr(ppbus));
#endif
			ppb_peripheral_terminate(ppbus, PPB_DONTWAIT);
			break;
		}

		/* wake up any process waiting for negociation from
		 * remote master host */

		/* XXX should set a variable to warn the process about
		 * the interrupt */

		wakeup(ppi);
		break;
	default:
#ifdef DEBUG_1284
		kprintf("?%d", ppb_1284_get_state(ppbus));
#endif
		ppb_1284_set_state(ppbus, PPB_FORWARD_IDLE);
		ppb_set_mode(ppbus, PPB_COMPATIBLE);
		break;
	}

	ppi_enable_intr(ppidev);

	return;
}
#endif /* PERIPH_1284 */

static int
ppiopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	u_int unit = minor(dev);
	struct ppi_data *ppi = UNITOSOFTC(unit);
	device_t ppidev = UNITODEVICE(unit);
        device_t ppbus = device_get_parent(ppidev);
	int res;

	if (!ppi)
		return (ENXIO);

	if (!(ppi->ppi_flags & HAVE_PPBUS)) {
		if ((res = ppb_request_bus(ppbus, ppidev,
			(ap->a_oflags & O_NONBLOCK) ? PPB_DONTWAIT :
						(PPB_WAIT | PPB_INTR))))
			return (res);

		ppi->ppi_flags |= HAVE_PPBUS;

#ifdef PERIPH_1284
		if (ppi->intr_resource) {
			/* register our interrupt handler */
			BUS_SETUP_INTR(device_get_parent(ppidev), ppidev,
				       ppi->intr_resource, 0,
				       ppiintr, dev, 
				       &ppi->intr_cookie, NULL, NULL);
		}
#endif /* PERIPH_1284 */
	}
	ppi->ppi_count += 1;

	return (0);
}

static int
ppiclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	u_int unit = minor(dev);
	struct ppi_data *ppi = UNITOSOFTC(unit);
	device_t ppidev = UNITODEVICE(unit);
        device_t ppbus = device_get_parent(ppidev);

	ppi->ppi_count --;
	if (!ppi->ppi_count) {

#ifdef PERIPH_1284
		switch (ppb_1284_get_state(ppbus)) {
		case PPB_PERIPHERAL_IDLE:
			ppb_peripheral_terminate(ppbus, 0);
			break;
		case PPB_REVERSE_IDLE:
		case PPB_EPP_IDLE:
		case PPB_ECP_FORWARD_IDLE:
		default:
			ppb_1284_terminate(ppbus);
			break;
		}
#endif /* PERIPH_1284 */

		/* unregistration of interrupt forced by release */
		ppb_release_bus(ppbus, ppidev);

		ppi->ppi_flags &= ~HAVE_PPBUS;
	}

	return (0);
}

/*
 * ppiread()
 *
 * IEEE1284 compliant read.
 *
 * First, try negociation to BYTE then NIBBLE mode
 * If no data is available, wait for it otherwise transfer as much as possible
 */
static int
ppiread(struct dev_read_args *ap)
{
#ifdef PERIPH_1284
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	u_int unit = minor(dev);
	struct ppi_data *ppi = UNITOSOFTC(unit);
	device_t ppidev = UNITODEVICE(unit);
        device_t ppbus = device_get_parent(ppidev);
	int len, error = 0;

	switch (ppb_1284_get_state(ppbus)) {
	case PPB_PERIPHERAL_IDLE:
		ppb_peripheral_terminate(ppbus, 0);
		/* fall throught */

	case PPB_FORWARD_IDLE:
		/* if can't negociate NIBBLE mode then try BYTE mode,
		 * the peripheral may be a computer
		 */
		if ((ppb_1284_negociate(ppbus,
			ppi->ppi_mode = PPB_NIBBLE, 0))) {

			/* XXX Wait 2 seconds to let the remote host some
			 * time to terminate its interrupt
			 */
			tsleep(ppi, 0, "ppiread", 2*hz);
			
			if ((error = ppb_1284_negociate(ppbus,
				ppi->ppi_mode = PPB_BYTE, 0)))
				return (error);
		}
		break;

	case PPB_REVERSE_IDLE:
	case PPB_EPP_IDLE:
	case PPB_ECP_FORWARD_IDLE:
	default:
		break;
	}

#ifdef DEBUG_1284
	kprintf("N");
#endif
	/* read data */
	len = 0;
	while (uio->uio_resid) {
		error = ppb_1284_read(ppbus, ppi->ppi_mode, ppi->ppi_buffer,
				     (int)szmin(BUFSIZE, uio->uio_resid),
				     &len);
		if (error)
			goto error;

		if (!len)
			goto error;		/* no more data */

#ifdef DEBUG_1284
		kprintf("d");
#endif
		if ((error = uiomove(ppi->ppi_buffer, (size_t)len, uio)))
			goto error;
	}

error:

#else /* PERIPH_1284 */
	int error = ENODEV;
#endif

	return (error);
}

/*
 * ppiwrite()
 *
 * IEEE1284 compliant write
 *
 * Actually, this is the peripheral side of a remote IEEE1284 read
 *
 * The first part of the negociation (IEEE1284 device detection) is
 * done at interrupt level, then the remaining is done by the writing
 * process
 *
 * Once negociation done, transfer data
 */
static int
ppiwrite(struct dev_write_args *ap)
{
#ifdef PERIPH_1284
	cdev_t dev = ap->a_head.a_dev;
	struct uio *uio = ap->a_uio;
	u_int unit = minor(dev);
	struct ppi_data *ppi = UNITOSOFTC(unit);
	device_t ppidev = UNITODEVICE(unit);
        device_t ppbus = device_get_parent(ppidev);
	int len, error = 0, sent;

#if 0
	int ret;

	#define ADDRESS		MS_PARAM(0, 0, MS_TYP_PTR)
	#define LENGTH		MS_PARAM(0, 1, MS_TYP_INT)

	struct ppb_microseq msq[] = {
		  { MS_OP_PUT, { MS_UNKNOWN, MS_UNKNOWN, MS_UNKNOWN } },
		  MS_RET(0)
	};

	/* negociate ECP mode */
	if (ppb_1284_negociate(ppbus, PPB_ECP, 0)) {
		kprintf("ppiwrite: ECP negotiation failed\n");
	}

	while (!error && (len = (int)szmin(uio->uio_resid, BUFSIZE))) {
		uiomove(ppi->ppi_buffer, (size_t)len, uio);

		ppb_MS_init_msq(msq, 2, ADDRESS, ppi->ppi_buffer, LENGTH, len);

		error = ppb_MS_microseq(ppbus, msq, &ret);
	}
#endif

	/* we have to be peripheral to be able to send data, so
	 * wait for the appropriate state
	 */
 	if (ppb_1284_get_state(ppbus) < PPB_PERIPHERAL_NEGOCIATION)
		ppb_1284_terminate(ppbus);

	while (ppb_1284_get_state(ppbus) != PPB_PERIPHERAL_IDLE) {
		/* XXX should check a variable before sleeping */
#ifdef DEBUG_1284
		kprintf("s");
#endif

		ppi_enable_intr(ppidev);

		/* sleep until IEEE1284 negociation starts */
		error = tsleep(ppi, PCATCH, "ppiwrite", 0);

		switch (error) {
		case 0:
			/* negociate peripheral side with BYTE mode */
			ppb_peripheral_negociate(ppbus, PPB_BYTE, 0);
			break;
		case EWOULDBLOCK:
			break;
		default:
			goto error;
		}
	}
#ifdef DEBUG_1284
	kprintf("N");
#endif

	/* negociation done, write bytes to master host */
	while ((len = (int)szmin(uio->uio_resid, BUFSIZE)) != 0) {
		uiomove(ppi->ppi_buffer, (size_t)len, uio);
		if ((error = byte_peripheral_write(ppbus,
						ppi->ppi_buffer, len, &sent)))
			goto error;
#ifdef DEBUG_1284
		kprintf("d");
#endif
	}

error:

#else /* PERIPH_1284 */
	int error = ENODEV;
#endif

	return (error);
}

static int
ppiioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	u_int unit = minor(dev);
	device_t ppidev = UNITODEVICE(unit);
        device_t ppbus = device_get_parent(ppidev);
	int error = 0;
	u_int8_t *val = (u_int8_t *)ap->a_data;

	switch (ap->a_cmd) {
	case PPIGDATA:			/* get data register */
		*val = ppb_rdtr(ppbus);
		break;
	case PPIGSTATUS:		/* get status bits */
		*val = ppb_rstr(ppbus);
		break;
	case PPIGCTRL:			/* get control bits */
		*val = ppb_rctr(ppbus);
		break;
	case PPIGEPPD:			/* get EPP data bits */
		*val = ppb_repp_D(ppbus);
		break;
	case PPIGECR:			/* get ECP bits */
		*val = ppb_recr(ppbus);
		break;
	case PPIGFIFO:			/* read FIFO */
		*val = ppb_rfifo(ppbus);
		break;
	case PPISDATA:			/* set data register */
		ppb_wdtr(ppbus, *val);
		break;
	case PPISSTATUS:		/* set status bits */
		ppb_wstr(ppbus, *val);
		break;
	case PPISCTRL:			/* set control bits */
		ppb_wctr(ppbus, *val);
		break;
	case PPISEPPD:			/* set EPP data bits */
		ppb_wepp_D(ppbus, *val);
		break;
	case PPISECR:			/* set ECP bits */
		ppb_wecr(ppbus, *val);
		break;
	case PPISFIFO:			/* write FIFO */
		ppb_wfifo(ppbus, *val);
		break;
	case PPIGEPPA:			/* get EPP address bits */
		*val = ppb_repp_A(ppbus);
		break;
	case PPISEPPA:			/* set EPP address bits */
		ppb_wepp_A(ppbus, *val);
		break;
	default:
		error = ENOTTY;
		break;
	}
    
	return (error);
}

/*
 * Because ppi is a static device under any attached ppbuf, and not
 * scanned by the ppbuf, we need an identify function to create the
 * device.
 */
static device_method_t ppi_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	bus_generic_identify),
	DEVMETHOD(device_probe,		ppi_probe),
	DEVMETHOD(device_attach,	ppi_attach),

	DEVMETHOD_END
};

static driver_t ppi_driver = {
	"ppi",
	ppi_methods,
	sizeof(struct ppi_data),
};
DRIVER_MODULE(ppi, ppbus, ppi_driver, ppi_devclass, NULL, NULL);
