/*-
 * 1. Redistributions of source code must retain the 
 * Copyright (c) 1997 Amancio Hasty, 1999 Roger Hardiman
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Amancio Hasty and
 *      Roger Hardiman
 * 4. The name of the author may not be used to endorse or promote products 
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/bktr/bktr_os.c,v 1.54 2007/02/23 12:18:34 piso Exp $
 */

/*
 * This is part of the Driver for Video Capture Cards (Frame grabbers)
 * and TV Tuner cards using the Brooktree Bt848, Bt848A, Bt849A, Bt878, Bt879
 * chipset.
 * Copyright Roger Hardiman and Amancio Hasty.
 *
 * bktr_os : This has all the Operating System dependant code,
 *             probe/attach and open/close/ioctl/read/mmap
 *             memory allocation
 *             PCI bus interfacing
 */

#include "opt_bktr.h"		/* include any kernel config options */

#define FIFO_RISC_DISABLED      0
#define ALL_INTS_DISABLED       0

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/uio.h>
#include <sys/kernel.h>
#include <sys/signalvar.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/event.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/thread2.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include "pcidevs.h"

#include <sys/sysctl.h>
int bt848_card = -1; 
int bt848_tuner = -1;
int bt848_reverse_mute = -1; 
int bt848_format = -1;
int bt848_slow_msp_audio = -1;
#ifdef BKTR_NEW_MSP34XX_DRIVER
int bt848_stereo_once = 0;	/* no continuous stereo monitoring */
int bt848_amsound = 0;		/* hard-wire AM sound at 6.5 Hz (france),
				   the autoscan seems work well only with FM... */
int bt848_dolby = 0;
#endif

SYSCTL_NODE(_hw, OID_AUTO, bt848, CTLFLAG_RW, 0, "Bt848 Driver mgmt");
SYSCTL_INT(_hw_bt848, OID_AUTO, card, CTLFLAG_RW, &bt848_card, -1, "");
SYSCTL_INT(_hw_bt848, OID_AUTO, tuner, CTLFLAG_RW, &bt848_tuner, -1, "");
SYSCTL_INT(_hw_bt848, OID_AUTO, reverse_mute, CTLFLAG_RW, &bt848_reverse_mute, -1, "");
SYSCTL_INT(_hw_bt848, OID_AUTO, format, CTLFLAG_RW, &bt848_format, -1, "");
SYSCTL_INT(_hw_bt848, OID_AUTO, slow_msp_audio, CTLFLAG_RW, &bt848_slow_msp_audio, -1, "");
#ifdef BKTR_NEW_MSP34XX_DRIVER
SYSCTL_INT(_hw_bt848, OID_AUTO, stereo_once, CTLFLAG_RW, &bt848_stereo_once, 0, "");
SYSCTL_INT(_hw_bt848, OID_AUTO, amsound, CTLFLAG_RW, &bt848_amsound, 0, "");
SYSCTL_INT(_hw_bt848, OID_AUTO, dolby, CTLFLAG_RW, &bt848_dolby, 0, "");
#endif

#include <dev/video/meteor/ioctl_meteor.h>
#include <dev/video/bktr/ioctl_bt848.h>	/* extensions to ioctl_meteor.h */
#include <dev/video/bktr/bktr_reg.h>
#include <dev/video/bktr/bktr_tuner.h>
#include <dev/video/bktr/bktr_card.h>
#include <dev/video/bktr/bktr_audio.h>
#include <dev/video/bktr/bktr_core.h>
#include <dev/video/bktr/bktr_os.h>

#if defined(BKTR_USE_FREEBSD_SMBUS)
#include <dev/video/bktr/bktr_i2c.h>

#include "iicbb_if.h"
#include "smbus_if.h"
#endif

static int	bktr_probe( device_t dev );
static int	bktr_attach( device_t dev );
static int	bktr_detach( device_t dev );
static int	bktr_shutdown( device_t dev );
static void	bktr_intr(void *arg) { common_bktr_intr(arg); }

static device_method_t bktr_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,         bktr_probe),
	DEVMETHOD(device_attach,        bktr_attach),
	DEVMETHOD(device_detach,        bktr_detach),
	DEVMETHOD(device_shutdown,      bktr_shutdown),

#if defined(BKTR_USE_FREEBSD_SMBUS)
	/* iicbb interface */
	DEVMETHOD(iicbb_callback,	bti2c_iic_callback),
	DEVMETHOD(iicbb_setsda,		bti2c_iic_setsda),
	DEVMETHOD(iicbb_setscl,		bti2c_iic_setscl),
	DEVMETHOD(iicbb_getsda,		bti2c_iic_getsda),
	DEVMETHOD(iicbb_getscl,		bti2c_iic_getscl),
	DEVMETHOD(iicbb_reset,		bti2c_iic_reset),
	
	/* smbus interface */
	DEVMETHOD(smbus_callback,	bti2c_smb_callback),
	DEVMETHOD(smbus_writeb,		bti2c_smb_writeb),
	DEVMETHOD(smbus_writew,		bti2c_smb_writew),
	DEVMETHOD(smbus_readb,		bti2c_smb_readb),
#endif

	DEVMETHOD_END
};

static driver_t bktr_driver = {
	"bktr",
	bktr_methods,
	sizeof(struct bktr_softc),
};

static devclass_t bktr_devclass;

static	d_open_t	bktr_open;
static	d_close_t	bktr_close;
static	d_read_t	bktr_read;
static	d_write_t	bktr_write;
static	d_ioctl_t	bktr_ioctl;
static	d_mmap_t	bktr_mmap;
static	d_kqfilter_t	bktr_kqfilter;

static void bktr_filter_detach(struct knote *);
static int bktr_filter(struct knote *, long);

static struct dev_ops bktr_ops = {
	{ "bktr", 0, 0 },
	.d_open =	bktr_open,
	.d_close =	bktr_close,
	.d_read =	bktr_read,
	.d_write =	bktr_write,
	.d_ioctl =	bktr_ioctl,
	.d_kqfilter =	bktr_kqfilter,
	.d_mmap =	bktr_mmap,
};

DRIVER_MODULE(bktr, pci, bktr_driver, bktr_devclass, NULL, NULL);
MODULE_DEPEND(bktr, bktr_mem, 1,1,1);
MODULE_VERSION(bktr, 1);

/*
 * the boot time probe routine.
 */
static int
bktr_probe( device_t dev )
{
	unsigned int type = pci_get_devid(dev);
        unsigned int rev  = pci_get_revid(dev);

	if (PCI_VENDOR(type) == PCI_VENDOR_BROOKTREE)
	{
		switch (PCI_PRODUCT(type)) {
		case PCI_PRODUCT_BROOKTREE_BT848:
			if (rev == 0x12)
				device_set_desc(dev, "BrookTree 848A");
			else
				device_set_desc(dev, "BrookTree 848");
			return BUS_PROBE_DEFAULT;
		case PCI_PRODUCT_BROOKTREE_BT849:
			device_set_desc(dev, "BrookTree 849A");
			return BUS_PROBE_DEFAULT;
		case PCI_PRODUCT_BROOKTREE_BT878:
			device_set_desc(dev, "BrookTree 878");
			return BUS_PROBE_DEFAULT;
		case PCI_PRODUCT_BROOKTREE_BT879:
			device_set_desc(dev, "BrookTree 879");
			return BUS_PROBE_DEFAULT;
		}
	}

        return ENXIO;
}


/*
 * the attach routine.
 */
static int
bktr_attach( device_t dev )
{
	u_long		latency;
	u_long		fun;
	u_long		val;
	unsigned int	rev;
	unsigned int	unit;
	int		error = 0;
#ifdef BROOKTREE_IRQ
	u_long		old_irq, new_irq;
#endif 

        struct bktr_softc *bktr = device_get_softc(dev);

	unit = device_get_unit(dev);

	/* build the device name for bktr_name() */
	ksnprintf(bktr->bktr_xname, sizeof(bktr->bktr_xname), "bktr%d",unit);

	/*
	 * Enable bus mastering and Memory Mapped device
	 */
	val = pci_read_config(dev, PCIR_COMMAND, 4);
	val |= (PCIM_CMD_MEMEN|PCIM_CMD_BUSMASTEREN);
	pci_write_config(dev, PCIR_COMMAND, val, 4);

	/*
	 * Map control/status registers.
	 */
	bktr->mem_rid = PCIR_BAR(0);
	bktr->res_mem = bus_alloc_resource_any(dev, SYS_RES_MEMORY, 
					&bktr->mem_rid, RF_ACTIVE);

	if (!bktr->res_mem) {
		device_printf(dev, "could not map memory\n");
		error = ENXIO;
		goto fail;
	}
	bktr->memt = rman_get_bustag(bktr->res_mem);
	bktr->memh = rman_get_bushandle(bktr->res_mem);


	/*
	 * Disable the brooktree device
	 */
	OUTL(bktr, BKTR_INT_MASK, ALL_INTS_DISABLED);
	OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_RISC_DISABLED);


#ifdef BROOKTREE_IRQ		/* from the configuration file */
	old_irq = pci_conf_read(tag, PCI_INTERRUPT_REG);
	pci_conf_write(tag, PCI_INTERRUPT_REG, BROOKTREE_IRQ);
	new_irq = pci_conf_read(tag, PCI_INTERRUPT_REG);
	kprintf("bktr%d: attach: irq changed from %d to %d\n",
		unit, (old_irq & 0xff), (new_irq & 0xff));
#endif 

	/*
	 * Allocate our interrupt.
	 */
	bktr->irq_rid = 0;
	bktr->res_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, 
				&bktr->irq_rid, RF_SHAREABLE | RF_ACTIVE);
	if (bktr->res_irq == NULL) {
		device_printf(dev, "could not map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	error = bus_setup_intr(dev, bktr->res_irq, 0,
                               bktr_intr, bktr, &bktr->res_ih, NULL);
	if (error) {
		device_printf(dev, "could not setup irq\n");
		goto fail;

	}


	/* Update the Device Control Register */
	/* on Bt878 and Bt879 cards           */
	fun = pci_read_config( dev, 0x40, 2);
        fun = fun | 1;	/* Enable writes to the sub-system vendor ID */

#if defined( BKTR_430_FX_MODE )
	if (bootverbose) kprintf("Using 430 FX chipset compatibility mode\n");
        fun = fun | 2;	/* Enable Intel 430 FX compatibility mode */
#endif

#if defined( BKTR_SIS_VIA_MODE )
	if (bootverbose) kprintf("Using SiS/VIA chipset compatibility mode\n");
        fun = fun | 4;	/* Enable SiS/VIA compatibility mode (useful for
                           OPTi chipset motherboards too */
#endif
	pci_write_config(dev, 0x40, fun, 2);

#if defined(BKTR_USE_FREEBSD_SMBUS)
	if (bt848_i2c_attach(dev))
		kprintf("bktr%d: i2c_attach: can't attach\n", unit);
#endif

/*
 * PCI latency timer.  32 is a good value for 4 bus mastering slots, if
 * you have more than four, then 16 would probably be a better value.
 */
#ifndef BROOKTREE_DEF_LATENCY_VALUE
#define BROOKTREE_DEF_LATENCY_VALUE	10
#endif
	latency = pci_read_config(dev, PCI_LATENCY_TIMER, 4);
	latency = (latency >> 8) & 0xff;
	if ( bootverbose ) {
		if (latency)
			kprintf("brooktree%d: PCI bus latency is", unit);
		else
			kprintf("brooktree%d: PCI bus latency was 0 changing to",
				unit);
	}
	if ( !latency ) {
		latency = BROOKTREE_DEF_LATENCY_VALUE;
		pci_write_config(dev, PCI_LATENCY_TIMER, latency<<8, 4);
	}
	if ( bootverbose ) {
		kprintf(" %d.\n", (int) latency);
	}

	/* read the pci device id and revision id */
	fun = pci_get_devid(dev);
        rev = pci_get_revid(dev);

	/* call the common attach code */
	common_bktr_attach( bktr, unit, fun, rev );

	/* make the device entries */
	make_dev(&bktr_ops, unit,    0, 0, 0444, "bktr%d",  unit);
	make_dev(&bktr_ops, unit+16, 0, 0, 0444, "tuner%d", unit);
	make_dev(&bktr_ops, unit+32, 0, 0, 0444, "vbi%d"  , unit);

	return 0;

fail:
	if (bktr->res_irq)
		bus_release_resource(dev, SYS_RES_IRQ, bktr->irq_rid, bktr->res_irq);
	if (bktr->res_mem)
		bus_release_resource(dev, SYS_RES_IRQ, bktr->mem_rid, bktr->res_mem);
	return error;

}

/*
 * the detach routine.
 */
static int
bktr_detach( device_t dev )
{
	struct bktr_softc *bktr = device_get_softc(dev);

#ifdef BKTR_NEW_MSP34XX_DRIVER
	/* Disable the soundchip and kernel thread */
	if (bktr->msp3400c_info != NULL)
		msp_detach(bktr);
#endif

	/* Disable the brooktree device */
	OUTL(bktr, BKTR_INT_MASK, ALL_INTS_DISABLED);
	OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_RISC_DISABLED);

#if defined(BKTR_USE_FREEBSD_SMBUS)
	if (bt848_i2c_detach(dev))
		kprintf("bktr%d: i2c_attach: can't attach\n",
		     device_get_unit(dev));
#endif
#ifdef USE_VBIMUTEX
        mtx_destroy(&bktr->vbimutex);
#endif

	/* Note: We do not free memory for RISC programs, grab buffer, vbi buffers */
	/* The memory is retained by the bktr_mem module so we can unload and */
	/* then reload the main bktr driver module */

	/* removing the ops automatically destroys all related devices */
	dev_ops_remove_minor(&bktr_ops, /*0x0f, */device_get_unit(dev));

	/*
	 * Deallocate resources.
	 */
	bus_teardown_intr(dev, bktr->res_irq, bktr->res_ih);
	bus_release_resource(dev, SYS_RES_IRQ, bktr->irq_rid, bktr->res_irq);
	bus_release_resource(dev, SYS_RES_MEMORY, bktr->mem_rid, bktr->res_mem);
	 
	return 0;
}

/*
 * the shutdown routine.
 */
static int
bktr_shutdown( device_t dev )
{
	struct bktr_softc *bktr = device_get_softc(dev);

	/* Disable the brooktree device */
	OUTL(bktr, BKTR_INT_MASK, ALL_INTS_DISABLED);
	OUTW(bktr, BKTR_GPIO_DMA_CTL, FIFO_RISC_DISABLED);

	return 0;
}


/*
 * Special Memory Allocation
 */
vm_offset_t
get_bktr_mem( int unit, unsigned size )
{
	vm_offset_t	addr = 0;

	addr = (vm_offset_t)contigmalloc(size, M_DEVBUF, M_NOWAIT, 0,
	    0xffffffff, 1<<24, 0);
	if (addr == 0)
		addr = (vm_offset_t)contigmalloc(size, M_DEVBUF, M_NOWAIT, 0,
		    0xffffffff, PAGE_SIZE, 0);
	if (addr == 0) {
		kprintf("bktr%d: Unable to allocate %d bytes of memory.\n",
			unit, size);
	}

	return( addr );
}


/*---------------------------------------------------------
**
**	BrookTree 848 character device driver routines
**
**---------------------------------------------------------
*/

#define VIDEO_DEV	0x00
#define TUNER_DEV	0x01
#define VBI_DEV		0x02

#define UNIT(x)		((x) & 0x0f)
#define FUNCTION(x)	(x >> 4)

/*
 * 
 */
static int
bktr_open(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	bktr_ptr_t	bktr;
	int		unit;
	int		result;

	unit = UNIT( minor(dev) );

	/* Get the device data */
	bktr = (struct bktr_softc*)devclass_get_softc(bktr_devclass, unit);
	if (bktr == NULL) {
		/* the device is no longer valid/functioning */
		return (ENXIO);
	}

	if (!(bktr->flags & METEOR_INITALIZED)) /* device not found */
		return( ENXIO );	

	/* Record that the device is now busy */
	device_busy(devclass_get_device(bktr_devclass, unit)); 


	if (bt848_card != -1) {
	  if ((bt848_card >> 8   == unit ) &&
	     ( (bt848_card & 0xff) < Bt848_MAX_CARD )) {
	    if ( bktr->bt848_card != (bt848_card & 0xff) ) {
	      bktr->bt848_card = (bt848_card & 0xff);
	      probeCard(bktr, FALSE, unit);
	    }
	  }
	}

	if (bt848_tuner != -1) {
	  if ((bt848_tuner >> 8   == unit ) &&
	     ( (bt848_tuner & 0xff) < Bt848_MAX_TUNER )) {
	    if ( bktr->bt848_tuner != (bt848_tuner & 0xff) ) {
	      bktr->bt848_tuner = (bt848_tuner & 0xff);
	      probeCard(bktr, FALSE, unit);
	    }
	  }
	}

	if (bt848_reverse_mute != -1) {
	  if ((bt848_reverse_mute >> 8)   == unit ) {
	    bktr->reverse_mute = bt848_reverse_mute & 0xff;
	  }
	}

	if (bt848_slow_msp_audio != -1) {
	  if ((bt848_slow_msp_audio >> 8) == unit ) {
	      bktr->slow_msp_audio = (bt848_slow_msp_audio & 0xff);
	  }
	}

#ifdef BKTR_NEW_MSP34XX_DRIVER
	if (bt848_stereo_once != 0) {
	  if ((bt848_stereo_once >> 8) == unit ) {
	      bktr->stereo_once = (bt848_stereo_once & 0xff);
	  }
	}

	if (bt848_amsound != -1) {
	  if ((bt848_amsound >> 8) == unit ) {
	      bktr->amsound = (bt848_amsound & 0xff);
	  }
	}

	if (bt848_dolby != -1) {
	  if ((bt848_dolby >> 8) == unit ) {
	      bktr->dolby = (bt848_dolby & 0xff);
	  }
	}
#endif

	switch ( FUNCTION( minor(dev) ) ) {
	case VIDEO_DEV:
		result = video_open( bktr );
		break;
	case TUNER_DEV:
		result = tuner_open( bktr );
		break;
	case VBI_DEV:
		result = vbi_open( bktr );
		break;
	default:
		result = ENXIO;
		break;
	}

	/* If there was an error opening the device, undo the busy status */
	if (result != 0)
		device_unbusy(devclass_get_device(bktr_devclass, unit)); 
	return( result );
}


/*
 * 
 */
static int
bktr_close(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	bktr_ptr_t	bktr;
	int		unit;
	int		result;

	unit = UNIT( minor(dev) );

	/* Get the device data */
	bktr = (struct bktr_softc*)devclass_get_softc(bktr_devclass, unit);
	if (bktr == NULL) {
		/* the device is no longer valid/functioning */
		return (ENXIO);
	}

	switch ( FUNCTION( minor(dev) ) ) {
	case VIDEO_DEV:
		result = video_close( bktr );
		break;
	case TUNER_DEV:
		result = tuner_close( bktr );
		break;
	case VBI_DEV:
		result = vbi_close( bktr );
		break;
	default:
		return (ENXIO);
		break;
	}

	device_unbusy(devclass_get_device(bktr_devclass, unit)); 
	return( result );
}


/*
 * 
 */
static int
bktr_read(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	bktr_ptr_t	bktr;
	int		unit;
	
	unit = UNIT(minor(dev));

	/* Get the device data */
	bktr = (struct bktr_softc*)devclass_get_softc(bktr_devclass, unit);
	if (bktr == NULL) {
		/* the device is no longer valid/functioning */
		return (ENXIO);
	}

	switch ( FUNCTION( minor(dev) ) ) {
	case VIDEO_DEV:
		return( video_read( bktr, unit, dev, ap->a_uio ) );
	case VBI_DEV:
		return( vbi_read( bktr, ap->a_uio, ap->a_ioflag ) );
	}
        return( ENXIO );
}


/*
 * 
 */
static int
bktr_write(struct dev_write_args *ap)
{
	return( EINVAL ); /* XXX or ENXIO ? */
}


/*
 * 
 */
static int
bktr_ioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	u_long cmd = ap->a_cmd;
	bktr_ptr_t	bktr;
	int		unit;

	unit = UNIT(minor(dev));

	/* Get the device data */
	bktr = (struct bktr_softc*)devclass_get_softc(bktr_devclass, unit);
	if (bktr == NULL) {
		/* the device is no longer valid/functioning */
		return (ENXIO);
	}

#ifdef BKTR_GPIO_ACCESS
	if (bktr->bigbuf == 0 && cmd != BT848_GPIO_GET_EN &&
	    cmd != BT848_GPIO_SET_EN && cmd != BT848_GPIO_GET_DATA &&
	    cmd != BT848_GPIO_SET_DATA)	/* no frame buffer allocated (ioctl failed) */
		return( ENOMEM );
#else
	if (bktr->bigbuf == 0)	/* no frame buffer allocated (ioctl failed) */
		return( ENOMEM );
#endif

	switch ( FUNCTION( minor(dev) ) ) {
	case VIDEO_DEV:
		return( video_ioctl( bktr, unit, cmd, ap->a_data, curthread ) );
	case TUNER_DEV:
		return( tuner_ioctl( bktr, unit, cmd, ap->a_data, curthread ) );
	}

	return( ENXIO );
}


/*
 * 
 */
static int
bktr_mmap(struct dev_mmap_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int		unit;
	bktr_ptr_t	bktr;

	unit = UNIT(minor(dev));

	if (FUNCTION(minor(dev)) > 0)	/* only allow mmap on /dev/bktr[n] */
		return(EINVAL);

	/* Get the device data */
	bktr = (struct bktr_softc*)devclass_get_softc(bktr_devclass, unit);
	if (bktr == NULL) {
		/* the device is no longer valid/functioning */
		return (ENXIO);
	}

	if (ap->a_nprot & PROT_EXEC)
		return(EINVAL);

	if (ap->a_offset < 0)
		return(EINVAL);

	if (ap->a_offset >= bktr->alloc_pages * PAGE_SIZE)
		return(EINVAL);

	ap->a_result = atop(vtophys(bktr->bigbuf) + ap->a_offset);
	return(0);
}

static struct filterops bktr_filterops =
	{ FILTEROP_ISFD, NULL, bktr_filter_detach, bktr_filter };

static int
bktr_kqfilter(struct dev_kqfilter_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct knote *kn = ap->a_kn;
	struct klist *klist;
	bktr_ptr_t bktr;
	int unit;

	ap->a_result = 0;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		if (FUNCTION(minor(dev)) == VBI_DEV) {
			unit = UNIT(minor(dev));
			/* Get the device data */
			bktr = (struct bktr_softc *)
			    devclass_get_softc(bktr_devclass, unit);
			kn->kn_fop = &bktr_filterops;
			kn->kn_hook = (caddr_t)bktr;
			break;
		}
		/* fall through */
	default:
		ap->a_result = EOPNOTSUPP;
		return (0);
	}

	klist = &bktr->vbi_kq.ki_note;
	knote_insert(klist, kn);

	return (0);
}

static void
bktr_filter_detach(struct knote *kn)
{
	bktr_ptr_t bktr = (bktr_ptr_t)kn->kn_hook;
	struct klist *klist;

	klist = &bktr->vbi_kq.ki_note;
	knote_insert(klist, kn);
}

static int
bktr_filter(struct knote *kn, long hint)
{
	bktr_ptr_t bktr = (bktr_ptr_t)kn->kn_hook;
	int ready = 0;

	if (bktr == NULL) {
		/* the device is no longer valid/functioning */
		kn->kn_flags |= (EV_EOF | EV_NODATA);
		return (1);
	}

	LOCK_VBI(bktr);
	crit_enter();
	if (bktr->vbisize != 0)
		ready = 1;
	crit_exit();
	UNLOCK_VBI(bktr);

	return (ready);
}
