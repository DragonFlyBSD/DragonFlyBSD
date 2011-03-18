/*-
 * (MPSAFE)
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/isa/vga_isa.c,v 1.17 2000/01/29 15:08:56 peter Exp $
 */

#include "opt_vga.h"
#include "opt_fb.h"
#include "opt_syscons.h"	/* should be removed in the future, XXX */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/bus.h>
#include <sys/fbio.h>
#include <sys/rman.h>
#include <sys/thread.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/md_var.h>
#include <machine/pc/bios.h>

#include <dev/video/fb/fbreg.h>
#include <dev/video/fb/vgareg.h>

#include "isareg.h"
#include "isavar.h"

#define VGA_SOFTC(unit)		\
	((vga_softc_t *)devclass_get_softc(isavga_devclass, unit))

static devclass_t	isavga_devclass;

static int		isavga_probe(device_t dev);
static int		isavga_attach(device_t dev);
static int		isavga_suspend(device_t dev);
static int		isavga_resume(device_t dev);

static device_method_t isavga_methods[] = {
	DEVMETHOD(device_probe,		isavga_probe),
	DEVMETHOD(device_attach,	isavga_attach),
	DEVMETHOD(device_suspend,	isavga_suspend),
	DEVMETHOD(device_resume,	isavga_resume),

	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	{ 0, 0 }
};

static driver_t isavga_driver = {
	VGA_DRIVER_NAME,
	isavga_methods,
	sizeof(vga_softc_t),
};

DRIVER_MODULE(vga, isa, isavga_driver, isavga_devclass, 0, 0);

#ifdef FB_INSTALL_CDEV

static d_open_t		isavga_open;
static d_close_t	isavga_close;
static d_read_t		isavga_read;
static d_write_t	isavga_write;
static d_ioctl_t	isavga_ioctl;
static d_mmap_t		isavga_mmap;

static struct dev_ops isavga_ops = {
	{ VGA_DRIVER_NAME, -1, 0 },
	.d_open =	isavga_open,
	.d_close =	isavga_close,
	.d_read =	isavga_read,
	.d_write =	isavga_write,
	.d_ioctl =	isavga_ioctl,
	.d_mmap =	isavga_mmap,
};

#endif /* FB_INSTALL_CDEV */

static int
isavga_probe(device_t dev)
{
	video_adapter_t adp;
	int error;

	/* No pnp support */
	if (isa_get_vendorid(dev))
		return (ENXIO);

	device_set_desc(dev, "Generic ISA VGA");
	error = vga_probe_unit(device_get_unit(dev), &adp, device_get_flags(dev));
	if (error == 0) {
		bus_set_resource(dev, SYS_RES_IOPORT, 0,
				 adp.va_io_base, adp.va_io_size);
		bus_set_resource(dev, SYS_RES_MEMORY, 0,
				 adp.va_mem_base, adp.va_mem_size);
#if 0
		isa_set_port(dev, adp.va_io_base);
		isa_set_portsize(dev, adp.va_io_size);
		isa_set_maddr(dev, adp.va_mem_base);
		isa_set_msize(dev, adp.va_mem_size);
#endif
	}
	return error;
}

static int
isavga_attach(device_t dev)
{
	vga_softc_t *sc;
	int unit;
	int rid;
	int error;

	unit = device_get_unit(dev);
	sc = device_get_softc(dev);

	rid = 0;
	bus_alloc_resource(dev, SYS_RES_IOPORT, &rid,
			   0, ~0, 0, RF_ACTIVE | RF_SHAREABLE);
	rid = 0;
	bus_alloc_resource(dev, SYS_RES_MEMORY, &rid,
			   0, ~0, 0, RF_ACTIVE | RF_SHAREABLE);

	error = vga_attach_unit(unit, sc, device_get_flags(dev));
	if (error)
		return error;

#ifdef FB_INSTALL_CDEV
	/* attach a virtual frame buffer device */
	sc->devt = make_dev(&isavga_ops, VGA_MKMINOR(unit), 0, 0, 02660, "vga%x", VGA_MKMINOR(unit));
	reference_dev(sc->devt);
	error = fb_attach(sc->devt, sc->adp);
	if (error)
		return error;
#endif /* FB_INSTALL_CDEV */

	if (bootverbose)
		(*vidsw[sc->adp->va_index]->diag)(sc->adp, bootverbose);

#if 0 /* experimental */
	device_add_child(dev, "fb", -1);
	bus_generic_attach(dev);
#endif

	return 0;
}

static int
isavga_suspend(device_t dev)
{
	vga_softc_t *sc;
	int err, nbytes;

	sc = device_get_softc(dev);
	err = bus_generic_suspend(dev);
	if (err)
		return (err);

	/* Save the video state across the suspend. */
	if (sc->state_buf != NULL) {
		kfree(sc->state_buf, M_TEMP);
		sc->state_buf = NULL;
	}
	nbytes = (*vidsw[sc->adp->va_index]->save_state)(sc->adp, NULL, 0);
	if (nbytes <= 0)
		return (0);
	sc->state_buf = kmalloc(nbytes, M_TEMP, M_NOWAIT | M_ZERO);
	if (sc->state_buf == NULL)
		return (0);
	if (bootverbose)
		device_printf(dev, "saving %d bytes of video state\n", nbytes);
	lwkt_gettoken(&tty_token);
	if ((*vidsw[sc->adp->va_index]->save_state)(sc->adp, sc->state_buf,
	    nbytes) != 0) {
		device_printf(dev, "failed to save state (nbytes=%d)\n",
		    nbytes);
		kfree(sc->state_buf, M_TEMP);
		sc->state_buf = NULL;
	}
	lwkt_reltoken(&tty_token);
	return (0);
}

static int
isavga_resume(device_t dev)
{
	vga_softc_t *sc;

	sc = device_get_softc(dev);
	if (sc->state_buf != NULL) {
		if ((*vidsw[sc->adp->va_index]->load_state)(sc->adp,
		    sc->state_buf) != 0)
			device_printf(dev, "failed to reload state\n");
		kfree(sc->state_buf, M_TEMP);
		sc->state_buf = NULL;
	}

	bus_generic_resume(dev);
	return 0;
}

#ifdef FB_INSTALL_CDEV

static int
isavga_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;

	return vga_open(dev, VGA_SOFTC(VGA_UNIT(dev)), ap->a_oflags,
			ap->a_devtype, ap->a_cred);
}

static int
isavga_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;

	return vga_close(dev, VGA_SOFTC(VGA_UNIT(dev)),
			 ap->a_fflag, ap->a_devtype);
}

static int
isavga_read(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;

	return vga_read(dev, VGA_SOFTC(VGA_UNIT(dev)), ap->a_uio, ap->a_ioflag);
}

static int
isavga_write(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;

	return vga_write(dev, VGA_SOFTC(VGA_UNIT(dev)), ap->a_uio, ap->a_ioflag);
}

static int
isavga_ioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;

	return vga_ioctl(dev, VGA_SOFTC(VGA_UNIT(dev)), ap->a_cmd, ap->a_data, ap->a_fflag, ap->a_cred);
}

static int
isavga_mmap(struct dev_mmap_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;

	ap->a_result = vga_mmap(dev, VGA_SOFTC(VGA_UNIT(dev)), ap->a_offset, ap->a_nprot);
	return(0);
}

#endif /* FB_INSTALL_CDEV */
