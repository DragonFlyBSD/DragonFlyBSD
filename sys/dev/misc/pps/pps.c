/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: src/sys/dev/ppbus/pps.c,v 1.24.2.1 2000/05/24 00:20:57 n_hibma Exp $
 *
 * This driver implements a draft-mogul-pps-api-02.txt PPS source.
 *
 * The input pin is pin#10 
 * The echo output pin is pin#14
 *
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/timepps.h>
#include <sys/rman.h>

#include <bus/ppbus/ppbconf.h>
#include <bus/ppbus/ppbio.h>

#include "ppbus_if.h"

#define PPS_NAME	"pps"		/* our official name */

struct pps_data {
	int	pps_open;
	struct	ppb_device pps_dev;	
	struct	pps_state pps;

	struct resource *intr_resource;	/* interrupt resource */
	void *intr_cookie;		/* interrupt registration cookie */
};

static void	ppsintr(void *arg);

#define DEVTOSOFTC(dev) \
	((struct pps_data *)device_get_softc(dev))
#define UNITOSOFTC(unit) \
	((struct pps_data *)devclass_get_softc(pps_devclass, (unit)))
#define UNITODEVICE(unit) \
	(devclass_get_device(pps_devclass, (unit)))

static devclass_t pps_devclass;

static	d_open_t	ppsopen;
static	d_close_t	ppsclose;
static	d_ioctl_t	ppsioctl;

static struct dev_ops pps_ops = {
	{ PPS_NAME, 0, 0 },
	.d_open =	ppsopen,
	.d_close =	ppsclose,
	.d_ioctl =	ppsioctl,
};

static int
ppsprobe(device_t ppsdev)
{
	struct pps_data *sc;

	sc = DEVTOSOFTC(ppsdev);
	bzero(sc, sizeof(struct pps_data));

	device_set_desc(ppsdev, "Pulse per second Timing Interface");

	sc->pps.ppscap = PPS_CAPTUREASSERT | PPS_ECHOASSERT;
	pps_init(&sc->pps);
	return (0);
}

static int
ppsattach(device_t ppsdev)
{
	struct pps_data *sc = DEVTOSOFTC(ppsdev);
	device_t ppbus = device_get_parent(ppsdev);
	uintptr_t irq;
	int unit;
	int zero = 0;

	/* retrieve the ppbus irq */
	BUS_READ_IVAR(ppbus, ppsdev, PPBUS_IVAR_IRQ, &irq);

	if (irq > 0) {
		/* declare our interrupt handler */
		sc->intr_resource = bus_alloc_legacy_irq_resource(ppsdev,
		    &zero, irq, RF_SHAREABLE);
	}
	/* interrupts seem mandatory */
	if (sc->intr_resource == NULL)
		return (ENXIO);

	unit = device_get_unit(ppsdev);
	make_dev(&pps_ops, unit, UID_ROOT, GID_WHEEL,
		 0644, PPS_NAME "%d", unit);
	return (0);
}

static	int
ppsopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	u_int unit = minor(dev);
	struct pps_data *sc = UNITOSOFTC(unit);
	device_t ppsdev = UNITODEVICE(unit);
	device_t ppbus = device_get_parent(ppsdev);
	int error;

	if (!sc->pps_open) {
		if (ppb_request_bus(ppbus, ppsdev, PPB_WAIT|PPB_INTR))
			return (EINTR);

		/* attach the interrupt handler */
		if ((error = BUS_SETUP_INTR(ppbus, ppsdev, sc->intr_resource,
			       0, ppsintr, ppsdev,
			       &sc->intr_cookie, NULL, NULL))) {
			ppb_release_bus(ppbus, ppsdev);
			return (error);
		}

		ppb_wctr(ppbus, 0);
		ppb_wctr(ppbus, IRQENABLE);
		sc->pps_open = 1;
	}

	return(0);
}

static	int
ppsclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	u_int unit = minor(dev);
	struct pps_data *sc = UNITOSOFTC(unit);
	device_t ppsdev = UNITODEVICE(unit);
	device_t ppbus = device_get_parent(ppsdev);

	sc->pps.ppsparam.mode = 0;	/* PHK ??? */

	ppb_wdtr(ppbus, 0);
	ppb_wctr(ppbus, 0);

	/* Note: the interrupt handler is automatically detached */
	ppb_release_bus(ppbus, ppsdev);
	sc->pps_open = 0;
	return(0);
}

static void
ppsintr(void *arg)
{
	device_t ppsdev = (device_t)arg;
	device_t ppbus = device_get_parent(ppsdev);
	struct pps_data *sc = DEVTOSOFTC(ppsdev);
	sysclock_t count;

	count = sys_cputimer->count();
	if (!(ppb_rstr(ppbus) & nACK))
		return;
	if (sc->pps.ppsparam.mode & PPS_ECHOASSERT) 
		ppb_wctr(ppbus, IRQENABLE | AUTOFEED);
	pps_event(&sc->pps, count, PPS_CAPTUREASSERT);
	if (sc->pps.ppsparam.mode & PPS_ECHOASSERT) 
		ppb_wctr(ppbus, IRQENABLE);
}

static int
ppsioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	u_int unit = minor(dev);
	struct pps_data *sc = UNITOSOFTC(unit);

	return (pps_ioctl(ap->a_cmd, ap->a_data, &sc->pps));
}

/*
 * Becuase pps is a static device under any attached ppbus, and not scanned
 * by the ppbus, we need an identify function to create the device.
 */
static device_method_t pps_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	bus_generic_identify),
	DEVMETHOD(device_probe,		ppsprobe),
	DEVMETHOD(device_attach,	ppsattach),

	{ 0, 0 }
};

static driver_t pps_driver = {
	PPS_NAME,
	pps_methods,
	sizeof(struct pps_data),
};
DRIVER_MODULE(pps, ppbus, pps_driver, pps_devclass, NULL, NULL);
