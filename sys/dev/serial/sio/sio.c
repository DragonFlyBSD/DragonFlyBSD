/*-
 * (MPSAFE)
 *
 * Copyright (c) 1991 The Regents of the University of California.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/isa/sio.c,v 1.291.2.35 2003/05/18 08:51:15 murray Exp $
 *	from: @(#)com.c	7.5 (Berkeley) 5/16/91
 *	from: i386/isa sio.c,v 1.234
 */

#include "opt_comconsole.h"
#include "opt_compat.h"
#include "opt_ddb.h"
#include "opt_sio.h"
#include "use_pci.h"
#include "use_puc.h"

/*
 * Serial driver, based on 386BSD-0.1 com driver.
 * Mostly rewritten to use pseudo-DMA.
 * Works for National Semiconductor NS8250-NS16550AF UARTs.
 * COM driver, based on HP dca driver.
 *
 * Changes for PC-Card integration:
 *	- Added PC-Card driver table and handlers
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/reboot.h>
#include <sys/malloc.h>
#include <sys/tty.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/dkstat.h>
#include <sys/fcntl.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/timepps.h>
#include <sys/thread2.h>
#include <sys/devfs.h>
#include <sys/consio.h>

#include <machine/limits.h>

#include <bus/isa/isareg.h>
#include <bus/isa/isavar.h>
#if NPCI > 0
#include <bus/pci/pcireg.h>
#include <bus/pci/pcivar.h>
#endif
#if NPUC > 0
#include <dev/misc/puc/pucvar.h>
#endif
#include <machine/lock.h>

#include <machine/clock.h>

#include "sioreg.h"
#include "sio_private.h"

#ifdef COM_ESP
#include "../ic_layer/esp.h"
#endif

#define	LOTS_OF_EVENTS	64	/* helps separate urgent events from input */

#define	CALLOUT_MASK		0x80
#define	CONTROL_MASK		0x60
#define	CONTROL_INIT_STATE	0x20
#define	CONTROL_LOCK_STATE	0x40
#define	DEV_TO_UNIT(dev)	(MINOR_TO_UNIT(minor(dev)))
#define	MINOR_TO_UNIT(mynor)	((((mynor) & ~0xffffU) >> (8 + 3)) \
				 | ((mynor) & 0x1f))
#define	UNIT_TO_MINOR(unit)	((((unit) & ~0x1fU) << (8 + 3)) \
				 | ((unit) & 0x1f))

#define	com_scr		7	/* scratch register for 16450-16550 (R/W) */

#define	sio_getreg(com, off) \
	(bus_space_read_1((com)->bst, (com)->bsh, (off)))
#define	sio_setreg(com, off, value) \
	(bus_space_write_1((com)->bst, (com)->bsh, (off), (value)))

/*
 * com state bits.
 * (CS_BUSY | CS_TTGO) and (CS_BUSY | CS_TTGO | CS_ODEVREADY) must be higher
 * than the other bits so that they can be tested as a group without masking
 * off the low bits.
 *
 * The following com and tty flags correspond closely:
 *	CS_BUSY		= TS_BUSY (maintained by comstart(), siopoll() and
 *				   comstop())
 *	CS_TTGO		= ~TS_TTSTOP (maintained by comparam() and comstart())
 *	CS_CTS_OFLOW	= CCTS_OFLOW (maintained by comparam())
 *	CS_RTS_IFLOW	= CRTS_IFLOW (maintained by comparam())
 * TS_FLUSH is not used.
 * XXX I think TIOCSETA doesn't clear TS_TTSTOP when it clears IXON.
 * XXX CS_*FLOW should be CF_*FLOW in com->flags (control flags not state).
 */
#define	CS_BUSY		0x80	/* output in progress */
#define	CS_TTGO		0x40	/* output not stopped by XOFF */
#define	CS_ODEVREADY	0x20	/* external device h/w ready (CTS) */
#define	CS_CHECKMSR	1	/* check of MSR scheduled */
#define	CS_CTS_OFLOW	2	/* use CTS output flow control */
#define	CS_DTR_OFF	0x10	/* DTR held off */
#define	CS_ODONE	4	/* output completed */
#define	CS_RTS_IFLOW	8	/* use RTS input flow control */
#define	CSE_BUSYCHECK	1	/* siobusycheck() scheduled */

static	char const * const	error_desc[] = {
#define	CE_OVERRUN			0
	"silo overflow",
#define	CE_INTERRUPT_BUF_OVERFLOW	1
	"interrupt-level buffer overflow",
#define	CE_TTY_BUF_OVERFLOW		2
	"tty-level buffer overflow",
};

#ifdef COM_ESP
static	int	espattach	(struct com_s *com, Port_t esp_port);
#endif
static	int	sio_isa_attach	(device_t dev);

static	timeout_t siobusycheck;
static	u_int	siodivisor	(u_long rclk, speed_t speed);
static	timeout_t siodtrwakeup;
static	void	comhardclose	(struct com_s *com);
static	void	sioinput	(struct com_s *com);
static	void	siointr1	(struct com_s *com);
static	void	siointr		(void *arg);
static	int	commctl		(struct com_s *com, int bits, int how);
static	int	comparam	(struct tty *tp, struct termios *t);
static	inthand2_t siopoll;
static	int	sio_isa_probe	(device_t dev);
static	void	siosettimeout	(void);
static	int	siosetwater	(struct com_s *com, speed_t speed);
static	void	comstart	(struct tty *tp);
static	void	comstop		(struct tty *tp, int rw);
static	timeout_t comwakeup;
static	void	disc_optim	(struct tty	*tp, struct termios *t,
				     struct com_s *com);

#if NPCI > 0
static	int	sio_pci_attach (device_t dev);
static	void	sio_pci_kludge_unit (device_t dev);
static	int	sio_pci_probe (device_t dev);
#endif /* NPCI > 0 */

#if NPUC > 0
static	int	sio_puc_attach (device_t dev);
static	int	sio_puc_probe (device_t dev);
#endif /* NPUC > 0 */

static char driver_name[] = "sio";

/* table and macro for fast conversion from a unit number to its com struct */
devclass_t	sio_devclass;
#define	com_addr(unit)	((struct com_s *) \
			 devclass_get_softc(sio_devclass, unit))

static device_method_t sio_isa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sio_isa_probe),
	DEVMETHOD(device_attach,	sio_isa_attach),

	DEVMETHOD_END
};

static driver_t sio_isa_driver = {
	driver_name,
	sio_isa_methods,
	sizeof(struct com_s),
};

#if NPCI > 0
static device_method_t sio_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sio_pci_probe),
	DEVMETHOD(device_attach,	sio_pci_attach),

	DEVMETHOD_END
};

static driver_t sio_pci_driver = {
	driver_name,
	sio_pci_methods,
	sizeof(struct com_s),
};
#endif /* NPCI > 0 */

#if NPUC > 0
static device_method_t sio_puc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sio_puc_probe),
	DEVMETHOD(device_attach,	sio_puc_attach),

	DEVMETHOD_END
};

static driver_t sio_puc_driver = {
	driver_name,
	sio_puc_methods,
	sizeof(struct com_s),
};
#endif /* NPUC > 0 */

static	d_open_t	sioopen;
static	d_close_t	sioclose;
static	d_read_t	sioread;
static	d_write_t	siowrite;
static	d_ioctl_t	sioioctl;

static struct dev_ops sio_ops = {
	{ driver_name, 0, D_TTY },
	.d_open =	sioopen,
	.d_close =	sioclose,
	.d_read =	sioread,
	.d_write =	siowrite,
	.d_ioctl =	sioioctl,
	.d_kqfilter =	ttykqfilter,
	.d_revoke =	ttyrevoke
};

int	comconsole = -1;
static	volatile speed_t	comdefaultrate = CONSPEED;
static	u_long			comdefaultrclk = DEFAULT_RCLK;
SYSCTL_ULONG(_machdep, OID_AUTO, conrclk, CTLFLAG_RW, &comdefaultrclk, 0, "");
static	u_int	com_events;	/* input chars + weighted output completions */
static	Port_t	siocniobase;
static	int	siocnunit;
static	Port_t	siogdbiobase;
static	int	siogdbunit = -1;
static	bool_t	sio_registered;
static	int	sio_timeout;
static	int	sio_timeouts_until_log;
static	struct	callout	sio_timeout_handle;
static	int	sio_numunits;

#ifdef COM_ESP
/* XXX configure this properly. */
static	Port_t	likely_com_ports[] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8, };
static	Port_t	likely_esp_ports[] = { 0x140, 0x180, 0x280, 0 };
#endif

/*
 * handle sysctl read/write requests for console speed
 * 
 * In addition to setting comdefaultrate for I/O through /dev/console,
 * also set the initial and lock values for the /dev/ttyXX device
 * if there is one associated with the console.  Finally, if the /dev/tty
 * device has already been open, change the speed on the open running port
 * itself.
 */

static int
sysctl_machdep_comdefaultrate(SYSCTL_HANDLER_ARGS)
{
	int error;
	speed_t newspeed;
	struct com_s *com;
	struct tty *tp;

	lwkt_gettoken(&tty_token);
	newspeed = comdefaultrate;

	error = sysctl_handle_opaque(oidp, &newspeed, sizeof newspeed, req);
	if (error || !req->newptr) {
		lwkt_reltoken(&tty_token);
		return (error);
	}

	comdefaultrate = newspeed;

	if (comconsole < 0) {	/* serial console not selected? */
		lwkt_reltoken(&tty_token);
		return (0);
	}

	com = com_addr(comconsole);
	if (com == NULL) {
		lwkt_reltoken(&tty_token);
		return (ENXIO);
	}

	/*
	 * set the initial and lock rates for /dev/ttydXX and /dev/cuaXX
	 * (note, the lock rates really are boolean -- if non-zero, disallow
	 *  speed changes)
	 */
	com->it_in.c_ispeed  = com->it_in.c_ospeed =
	com->lt_in.c_ispeed  = com->lt_in.c_ospeed =
	com->it_out.c_ispeed = com->it_out.c_ospeed =
	com->lt_out.c_ispeed = com->lt_out.c_ospeed = comdefaultrate;

	/*
	 * if we're open, change the running rate too
	 */
	tp = com->tp;
	if (tp && (tp->t_state & TS_ISOPEN)) {
		tp->t_termios.c_ispeed =
		tp->t_termios.c_ospeed = comdefaultrate;
		crit_enter();
		error = comparam(tp, &tp->t_termios);
		crit_exit();
	}
	lwkt_reltoken(&tty_token);
	return error;
}

SYSCTL_PROC(_machdep, OID_AUTO, conspeed, CTLTYPE_INT | CTLFLAG_RW,
	    0, 0, sysctl_machdep_comdefaultrate, "I", "");

#if NPCI > 0
struct pci_ids {
	u_int32_t	type;
	const char	*desc;
	int		rid;
};

static struct pci_ids pci_ids[] = {
	{ 0x100812b9, "3COM PCI FaxModem", 0x10 },
	{ 0x2000131f, "CyberSerial (1-port) 16550", 0x10 },
	{ 0x01101407, "Koutech IOFLEX-2S PCI Dual Port Serial", 0x10 },
	{ 0x01111407, "Koutech IOFLEX-2S PCI Dual Port Serial", 0x10 },
	{ 0x048011c1, "Lucent kermit based PCI Modem", 0x14 },
	{ 0x95211415, "Oxford Semiconductor PCI Dual Port Serial", 0x10 },
	{ 0x7101135e, "SeaLevel Ultra 530.PCI Single Port Serial", 0x18 },
	{ 0x0000151f, "SmartLink 5634PCV SurfRider", 0x10 },
	{ 0x98459710, "Netmos Nm9845 PCI Bridge with Dual UART", 0x10 },
	{ 0x00000000, NULL, 0 }
};

static int
sio_pci_attach(device_t dev)
{
	u_int32_t	type;
	struct pci_ids	*id;

	type = pci_get_devid(dev);
	id = pci_ids;
	while (id->type && id->type != type)
		id++;
	if (id->desc == NULL)
		return (ENXIO);
	sio_pci_kludge_unit(dev);
	return (sioattach(dev, id->rid, 0UL));
}

/*
 * Don't cut and paste this to other drivers.  It is a horrible kludge
 * which will fail to work and also be unnecessary in future versions.
 */
static void
sio_pci_kludge_unit(device_t dev)
{
	devclass_t	dc;
	int		err;
	int		start;
	int		unit;

	unit = 0;
	start = 0;
	while (resource_int_value("sio", unit, "port", &start) == 0 && 
	    start > 0)
		unit++;
	if (device_get_unit(dev) < unit) {
		dc = device_get_devclass(dev);
		while (devclass_get_device(dc, unit))
			unit++;
		device_printf(dev, "moving to sio%d\n", unit);
		err = device_set_unit(dev, unit);	/* EVIL DO NOT COPY */
		if (err)
			device_printf(dev, "error moving device %d\n", err);
	}
}

static int
sio_pci_probe(device_t dev)
{
	u_int32_t	type;
	struct pci_ids	*id;

	type = pci_get_devid(dev);
	id = pci_ids;
	while (id->type && id->type != type)
		id++;
	if (id->desc == NULL)
		return (ENXIO);
	device_set_desc(dev, id->desc);
	return (sioprobe(dev, id->rid, 0UL));
}
#endif /* NPCI > 0 */

#if NPUC > 0
static int
sio_puc_attach(device_t dev)
{
	uintptr_t rclk;

	if (BUS_READ_IVAR(device_get_parent(dev), dev, PUC_IVAR_FREQ,
	    &rclk) != 0)
		rclk = DEFAULT_RCLK;
	return (sioattach(dev, 0, rclk));
}

static int
sio_puc_probe(device_t dev)
{
	uintptr_t rclk;

	if (BUS_READ_IVAR(device_get_parent(dev), dev, PUC_IVAR_FREQ,
	    &rclk) != 0)
		rclk = DEFAULT_RCLK;
	return (sioprobe(dev, 0, rclk));
}
#endif /* NPUC */

static struct isa_pnp_id sio_ids[] = {
	{0x0005d041, "Standard PC COM port"},	/* PNP0500 */
	{0x0105d041, "16550A-compatible COM port"},	/* PNP0501 */
	{0x0205d041, "Multiport serial device (non-intelligent 16550)"}, /* PNP0502 */
	{0x1005d041, "Generic IRDA-compatible device"},	/* PNP0510 */
	{0x1105d041, "Generic IRDA-compatible device"},	/* PNP0511 */
	/* Devices that do not have a compatid */
	{0x12206804, NULL},     /* ACH2012 - 5634BTS 56K Video Ready Modem */
	{0x7602a904, NULL},	/* AEI0276 - 56K v.90 Fax Modem (LKT) */
	{0x00007905, NULL},	/* AKY0000 - 56K Plug&Play Modem */
	{0x21107905, NULL},	/* AKY1021 - 56K Plug&Play Modem */
	{0x01405407, NULL},	/* AZT4001 - AZT3000 PnP SOUND DEVICE, MODEM */
	{0x56039008, NULL},	/* BDP0356 - Best Data 56x2 */
	{0x56159008, NULL},	/* BDP1556 - B.D. Smart One 56SPS,Voice Modem*/
	{0x36339008, NULL},	/* BDP3336 - Best Data Prods. 336F */
	{0x0014490a, NULL},	/* BRI1400 - Boca 33.6 PnP */
	{0x0015490a, NULL},	/* BRI1500 - Internal Fax Data */
	{0x0034490a, NULL},	/* BRI3400 - Internal ACF Modem */
	{0x0094490a, NULL},	/* BRI9400 - Boca K56Flex PnP */
	{0x00b4490a, NULL},	/* BRIB400 - Boca 56k PnP */
	{0x0030320d, NULL},	/* CIR3000 - Cirrus Logic V43 */
	{0x0100440e, NULL},	/* CRD0001 - Cardinal MVP288IV ? */
	{0x01308c0e, NULL},	/* CTL3001 - Creative Labs Phoneblaster */
	{0x36033610, NULL},     /* DAV0336 - DAVICOM 336PNP MODEM */
	{0x01009416, NULL},     /* ETT0001 - E-Tech Bullet 33k6 PnP */
	{0x0000aa1a, NULL},	/* FUJ0000 - FUJITSU Modem 33600 PNP/I2 */
	{0x1200c31e, NULL},	/* GVC0012 - VF1128HV-R9 (win modem?) */
	{0x0303c31e, NULL},	/* GVC0303 - MaxTech 33.6 PnP D/F/V */
	{0x0505c31e, NULL},	/* GVC0505 - GVC 56k Faxmodem */
	{0x0116c31e, NULL},	/* GVC1601 - Rockwell V.34 Plug & Play Modem */
	{0x0050c31e, NULL},	/* GVC5000 - some GVC modem */
	{0x3800f91e, NULL},	/* GWY0038 - Telepath with v.90 */
	{0x9062f91e, NULL},	/* GWY6290 - Telepath with x2 Technology */
	{0x8100e425, NULL},	/* IOD0081 - I-O DATA DEVICE,INC. IFML-560 */
	{0x21002534, NULL},	/* MAE0021 - Jetstream Int V.90 56k Voice Series 2*/
	{0x0000f435, NULL},	/* MOT0000 - Motorola ModemSURFR 33.6 Intern */
	{0x5015f435, NULL},	/* MOT1550 - Motorola ModemSURFR 56K Modem */
	{0xf015f435, NULL},	/* MOT15F0 - Motorola VoiceSURFR 56K Modem */
	{0x6045f435, NULL},	/* MOT4560 - Motorola ? */
	{0x61e7a338, NULL},	/* NECE761 - 33.6Modem */
 	{0x08804f3f, NULL},	/* OZO8008 - Zoom  (33.6k Modem) */
	{0x0f804f3f, NULL},	/* OZO800f - Zoom 2812 (56k Modem) */
	{0x39804f3f, NULL},	/* OZO8039 - Zoom 56k flex */
	{0x00914f3f, NULL},	/* OZO9100 - Zoom 2919 (K56 Faxmodem) */
	{0x3024a341, NULL},	/* PMC2430 - Pace 56 Voice Internal Modem */
	{0x1000eb49, NULL},	/* ROK0010 - Rockwell ? */
	{0x1200b23d, NULL},     /* RSS0012 - OMRON ME5614ISA */
	{0x5002734a, NULL},	/* RSS0250 - 5614Jx3(G) Internal Modem */
	{0x6202734a, NULL},	/* RSS0262 - 5614Jx3[G] V90+K56Flex Modem */
	{0x1010104d, NULL},	/* SHP1010 - Rockwell 33600bps Modem */
	{0xc100ad4d, NULL},	/* SMM00C1 - Leopard 56k PnP */
	{0x9012b04e, NULL},	/* SUP1290 - Supra ? */
	{0x1013b04e, NULL},	/* SUP1310 - SupraExpress 336i PnP */
	{0x8013b04e, NULL},	/* SUP1380 - SupraExpress 288i PnP Voice */
	{0x8113b04e, NULL},	/* SUP1381 - SupraExpress 336i PnP Voice */
	{0x5016b04e, NULL},	/* SUP1650 - Supra 336i Sp Intl */
	{0x7016b04e, NULL},	/* SUP1670 - Supra 336i V+ Intl */
	{0x7420b04e, NULL},	/* SUP2070 - Supra ? */
	{0x8020b04e, NULL},	/* SUP2080 - Supra ? */
	{0x8420b04e, NULL},	/* SUP2084 - SupraExpress 56i PnP */
	{0x7121b04e, NULL},	/* SUP2171 - SupraExpress 56i Sp? */
	{0x8024b04e, NULL},	/* SUP2480 - Supra ? */
	{0x01007256, NULL},	/* USR0001 - U.S. Robotics Inc., Sportster W */
	{0x02007256, NULL},	/* USR0002 - U.S. Robotics Inc. Sportster 33. */
	{0x04007256, NULL},	/* USR0004 - USR Sportster 14.4k */
	{0x06007256, NULL},	/* USR0006 - USR Sportster 33.6k */
	{0x11007256, NULL},	/* USR0011 - USR ? */
	{0x01017256, NULL},	/* USR0101 - USR ? */
	{0x30207256, NULL},	/* USR2030 - U.S.Robotics Inc. Sportster 560 */
	{0x50207256, NULL},	/* USR2050 - U.S.Robotics Inc. Sportster 33. */
	{0x70207256, NULL},	/* USR2070 - U.S.Robotics Inc. Sportster 560 */
	{0x30307256, NULL},	/* USR3030 - U.S. Robotics 56K FAX INT */
	{0x31307256, NULL},	/* USR3031 - U.S. Robotics 56K FAX INT */
	{0x50307256, NULL},	/* USR3050 - U.S. Robotics 56K FAX INT */
	{0x70307256, NULL},	/* USR3070 - U.S. Robotics 56K Voice INT */
	{0x90307256, NULL},	/* USR3090 - USR ? */
	{0x70917256, NULL},	/* USR9170 - U.S. Robotics 56K FAX INT */
	{0x90917256, NULL},	/* USR9190 - USR 56k Voice INT */
	{0x0300695c, NULL},	/* WCI0003 - Fax/Voice/Modem/Speakphone/Asvd */
	{0x01a0896a, NULL},	/* ZTIA001 - Zoom Internal V90 Faxmodem */
	{0x61f7896a, NULL},	/* ZTIF761 - Zoom ComStar 33.6 */
	{0}
};



static int
sio_isa_probe(device_t dev)
{
	/* Check isapnp ids */
	if (ISA_PNP_PROBE(device_get_parent(dev), dev, sio_ids) == ENXIO)
		return (ENXIO);
	return (sioprobe(dev, 0, 0UL));
}

int
sioprobe(device_t dev, int xrid, u_long rclk)
{
#if 0
	static bool_t	already_init;
	device_t	xdev;
#endif
	struct com_s	*com;
	u_int		divisor;
	bool_t		failures[10];
	int		fn;
	device_t	idev;
	Port_t		iobase;
	intrmask_t	irqmap[4];
	intrmask_t	irqs;
	u_char		mcr_image;
	int		result;
	u_long		xirq;
	u_int		flags = device_get_flags(dev);
	int		rid;
	struct resource *port;

	rid = xrid;
	port = bus_alloc_resource(dev, SYS_RES_IOPORT, &rid,
				  0, ~0, IO_COMSIZE, RF_ACTIVE);
	if (!port)
		return (ENXIO);

	lwkt_gettoken(&tty_token);
	com = device_get_softc(dev);
	com->bst = rman_get_bustag(port);
	com->bsh = rman_get_bushandle(port);
	if (rclk == 0)
		rclk = DEFAULT_RCLK;
	com->rclk = rclk;

#if 0
	/*
	 * XXX this is broken - when we are first called, there are no
	 * previously configured IO ports.  We could hard code
	 * 0x3f8, 0x2f8, 0x3e8, 0x2e8 etc but that's probably worse.
	 * This code has been doing nothing since the conversion since
	 * "count" is zero the first time around.
	 */
	if (!already_init) {
		/*
		 * Turn off MCR_IENABLE for all likely serial ports.  An unused
		 * port with its MCR_IENABLE gate open will inhibit interrupts
		 * from any used port that shares the interrupt vector.
		 * XXX the gate enable is elsewhere for some multiports.
		 */
		device_t *devs;
		int count, i, xioport;

		devclass_get_devices(sio_devclass, &devs, &count);
		for (i = 0; i < count; i++) {
			xdev = devs[i];
			if (device_is_enabled(xdev) &&
			    bus_get_resource(xdev, SYS_RES_IOPORT, 0, &xioport,
					     NULL) == 0)
				outb(xioport + com_mcr, 0);
		}
		kfree(devs, M_TEMP);
		already_init = TRUE;
	}
#endif

	if (COM_LLCONSOLE(flags)) {
		kprintf("sio%d: reserved for low-level i/o\n",
		       device_get_unit(dev));
		bus_release_resource(dev, SYS_RES_IOPORT, rid, port);
		lwkt_reltoken(&tty_token);
		return (ENXIO);
	}

	/*
	 * If the device is on a multiport card and has an AST/4
	 * compatible interrupt control register, initialize this
	 * register and prepare to leave MCR_IENABLE clear in the mcr.
	 * Otherwise, prepare to set MCR_IENABLE in the mcr.
	 * Point idev to the device struct giving the correct id_irq.
	 * This is the struct for the master device if there is one.
	 */
	idev = dev;
	mcr_image = MCR_IENABLE;
#ifdef COM_MULTIPORT
	if (COM_ISMULTIPORT(flags)) {
		Port_t xiobase;
		u_long io;

		idev = devclass_get_device(sio_devclass, COM_MPMASTER(flags));
		if (idev == NULL) {
			kprintf("sio%d: master device %d not configured\n",
			       device_get_unit(dev), COM_MPMASTER(flags));
			idev = dev;
		}
		if (!COM_NOTAST4(flags)) {
			if (bus_get_resource(idev, SYS_RES_IOPORT, 0, &io,
					     NULL) == 0) {
				xiobase = io;
				if (bus_get_resource(idev, SYS_RES_IRQ, 0,
				    NULL, NULL) == 0)
					outb(xiobase + com_scr, 0x80);
				else
					outb(xiobase + com_scr, 0);
			}
			mcr_image = 0;
		}
	}
#endif /* COM_MULTIPORT */
	if (bus_get_resource(idev, SYS_RES_IRQ, 0, NULL, NULL) != 0)
		mcr_image = 0;

	bzero(failures, sizeof failures);
	iobase = rman_get_start(port);

	/*
	 * We don't want to get actual interrupts, just masked ones.
	 * Interrupts from this line should already be masked in the ICU,
	 * but mask them in the processor as well in case there are some
	 * (misconfigured) shared interrupts.
	 */
	com_lock();
/* EXTRA DELAY? */

	/*
	 * For the TI16754 chips, set prescaler to 1 (4 is often the
	 * default after-reset value) as otherwise it's impossible to
	 * get highest baudrates.
	 */
	if (COM_TI16754(flags)) {
		u_char cfcr, efr;

		cfcr = sio_getreg(com, com_cfcr);
		sio_setreg(com, com_cfcr, CFCR_EFR_ENABLE);
		efr = sio_getreg(com, com_efr);
		/* Unlock extended features to turn off prescaler. */
		sio_setreg(com, com_efr, efr | EFR_EFE);
		/* Disable EFR. */
		sio_setreg(com, com_cfcr, (cfcr != CFCR_EFR_ENABLE) ? cfcr : 0);
		/* Turn off prescaler. */
		sio_setreg(com, com_mcr,
			   sio_getreg(com, com_mcr) & ~MCR_PRESCALE);
		sio_setreg(com, com_cfcr, CFCR_EFR_ENABLE);
		sio_setreg(com, com_efr, efr);
		sio_setreg(com, com_cfcr, cfcr);
	}

	/*
	 * Initialize the speed and the word size and wait long enough to
	 * drain the maximum of 16 bytes of junk in device output queues.
	 * The speed is undefined after a master reset and must be set
	 * before relying on anything related to output.  There may be
	 * junk after a (very fast) soft reboot and (apparently) after
	 * master reset.
	 * XXX what about the UART bug avoided by waiting in comparam()?
	 * We don't want to to wait long enough to drain at 2 bps.
	 */
	if (iobase == siocniobase) {
		DELAY((16 + 1) * 1000000 / (comdefaultrate / 10));
	} else {
		sio_setreg(com, com_cfcr, CFCR_DLAB | CFCR_8BITS);
		divisor = siodivisor(rclk, SIO_TEST_SPEED);
		sio_setreg(com, com_dlbl, divisor & 0xff);
		sio_setreg(com, com_dlbh, divisor >> 8);
		sio_setreg(com, com_cfcr, CFCR_8BITS);
		DELAY((16 + 1) * 1000000 / (SIO_TEST_SPEED / 10));
	}

	/*
	 * Make sure we can drain the receiver.  If we can't, the serial
	 * port may not exist.
	 */
	for (fn = 0; fn < 256; ++fn) {
		if ((sio_getreg(com, com_lsr) & LSR_RXRDY) == 0)
			break;
		(void)sio_getreg(com, com_data);
	}
	if (fn == 256) {
		com_unlock();
		lwkt_reltoken(&tty_token);
		kprintf("sio%d: can't drain, serial port might "
			"not exist, disabling\n", device_get_unit(dev));
		return (ENXIO);
	}

	/*
	 * Enable the interrupt gate and disable device interupts.  This
	 * should leave the device driving the interrupt line low and
	 * guarantee an edge trigger if an interrupt can be generated.
	 */
/* EXTRA DELAY? */
	sio_setreg(com, com_mcr, mcr_image);
	sio_setreg(com, com_ier, 0);
	DELAY(1000);		/* XXX */
	irqmap[0] = isa_irq_pending();

	/*
	 * Attempt to set loopback mode so that we can send a null byte
	 * without annoying any external device.
	 */
/* EXTRA DELAY? */
	sio_setreg(com, com_mcr, mcr_image | MCR_LOOPBACK);

	/*
	 * Attempt to generate an output interrupt.  On 8250's, setting
	 * IER_ETXRDY generates an interrupt independent of the current
	 * setting and independent of whether the THR is empty.  On 16450's,
	 * setting IER_ETXRDY generates an interrupt independent of the
	 * current setting.  On 16550A's, setting IER_ETXRDY only
	 * generates an interrupt when IER_ETXRDY is not already set.
	 */
	sio_setreg(com, com_ier, IER_ETXRDY);

	/*
	 * On some 16x50 incompatibles, setting IER_ETXRDY doesn't generate
	 * an interrupt.  They'd better generate one for actually doing
	 * output.  Loopback may be broken on the same incompatibles but
	 * it's unlikely to do more than allow the null byte out.
	 */
	sio_setreg(com, com_data, 0);
	DELAY((1 + 2) * 1000000 / (SIO_TEST_SPEED / 10));

	/*
	 * Turn off loopback mode so that the interrupt gate works again
	 * (MCR_IENABLE was hidden).  This should leave the device driving
	 * an interrupt line high.  It doesn't matter if the interrupt
	 * line oscillates while we are not looking at it, since interrupts
	 * are disabled.
	 */
/* EXTRA DELAY? */
	sio_setreg(com, com_mcr, mcr_image);

	/*
	 * Some pcmcia cards have the "TXRDY bug", so we check everyone
	 * for IIR_TXRDY implementation ( Palido 321s, DC-1S... )
	 */
	if (COM_NOPROBE(flags)) {
		/* Reading IIR register twice */
		for (fn = 0; fn < 2; fn ++) {
			DELAY(10000);
			failures[6] = sio_getreg(com, com_iir);
		}
		/* Check IIR_TXRDY clear ? */
		result = 0;
		if (failures[6] & IIR_TXRDY) {
			/* Nop, Double check with clearing IER */
			sio_setreg(com, com_ier, 0);
			if (sio_getreg(com, com_iir) & IIR_NOPEND) {
				/* Ok. we're familia this gang */
				SET_FLAG(dev, COM_C_IIR_TXRDYBUG);
			} else {
				/* Unknown, Just omit this chip.. XXX */
				result = ENXIO;
				sio_setreg(com, com_mcr, 0);
			}
		} else {
			/* OK. this is well-known guys */
			CLR_FLAG(dev, COM_C_IIR_TXRDYBUG);
		}
		sio_setreg(com, com_ier, 0);
		sio_setreg(com, com_cfcr, CFCR_8BITS);
		com_unlock();
		bus_release_resource(dev, SYS_RES_IOPORT, rid, port);
		lwkt_reltoken(&tty_token);
		return (iobase == siocniobase ? 0 : result);
	}

	/*
	 * Check that
	 *	o the CFCR, IER and MCR in UART hold the values written to them
	 *	  (the values happen to be all distinct - this is good for
	 *	  avoiding false positive tests from bus echoes).
	 *	o an output interrupt is generated and its vector is correct.
	 *	o the interrupt goes away when the IIR in the UART is read.
	 */
/* EXTRA DELAY? */
	failures[0] = sio_getreg(com, com_cfcr) - CFCR_8BITS;
	failures[1] = sio_getreg(com, com_ier) - IER_ETXRDY;
	failures[2] = sio_getreg(com, com_mcr) - mcr_image;
	DELAY(10000);		/* Some internal modems need this time */
	irqmap[1] = isa_irq_pending();
	failures[4] = (sio_getreg(com, com_iir) & IIR_IMASK) - IIR_TXRDY;
	DELAY(1000);		/* XXX */
	irqmap[2] = isa_irq_pending();
	failures[6] = (sio_getreg(com, com_iir) & IIR_IMASK) - IIR_NOPEND;

	/*
	 * Turn off all device interrupts and check that they go off properly.
	 * Leave MCR_IENABLE alone.  For ports without a master port, it gates
	 * the OUT2 output of the UART to
	 * the ICU input.  Closing the gate would give a floating ICU input
	 * (unless there is another device driving it) and spurious interrupts.
	 * (On the system that this was first tested on, the input floats high
	 * and gives a (masked) interrupt as soon as the gate is closed.)
	 */
	sio_setreg(com, com_ier, 0);
	sio_setreg(com, com_cfcr, CFCR_8BITS);	/* dummy to avoid bus echo */
	failures[7] = sio_getreg(com, com_ier);
	DELAY(1000);		/* XXX */
	irqmap[3] = isa_irq_pending();
	failures[9] = (sio_getreg(com, com_iir) & IIR_IMASK) - IIR_NOPEND;

	com_unlock();

	irqs = irqmap[1] & ~irqmap[0];
	if (bus_get_resource(idev, SYS_RES_IRQ, 0, &xirq, NULL) == 0 &&
	    ((1 << xirq) & irqs) == 0)
		kprintf(
		"sio%d: configured irq %ld not in bitmap of probed irqs %#x\n",
		    device_get_unit(dev), xirq, irqs);
	if (bootverbose)
		kprintf("sio%d: irq maps: %#x %#x %#x %#x\n",
		    device_get_unit(dev),
		    irqmap[0], irqmap[1], irqmap[2], irqmap[3]);

	result = 0;
	for (fn = 0; fn < sizeof failures; ++fn)
		if (failures[fn]) {
			sio_setreg(com, com_mcr, 0);
			result = ENXIO;
			if (bootverbose) {
				kprintf("sio%d: probe failed test(s):",
				    device_get_unit(dev));
				for (fn = 0; fn < sizeof failures; ++fn)
					if (failures[fn])
						kprintf(" %d", fn);
				kprintf("\n");
			}
			break;
		}
	bus_release_resource(dev, SYS_RES_IOPORT, rid, port);
	lwkt_reltoken(&tty_token);
	return (iobase == siocniobase ? 0 : result);
}

#ifdef COM_ESP
static int
espattach(struct com_s *com, Port_t esp_port)
{
	u_char	dips;
	u_char	val;

	/*
	 * Check the ESP-specific I/O port to see if we're an ESP
	 * card.  If not, return failure immediately.
	 */
	if ((inb(esp_port) & 0xf3) == 0) {
		kprintf(" port 0x%x is not an ESP board?\n", esp_port);
		return (0);
	}

	lwkt_gettoken(&tty_token);
	/*
	 * We've got something that claims to be a Hayes ESP card.
	 * Let's hope so.
	 */

	/* Get the dip-switch configuration */
	outb(esp_port + ESP_CMD1, ESP_GETDIPS);
	dips = inb(esp_port + ESP_STATUS1);

	/*
	 * Bits 0,1 of dips say which COM port we are.
	 */
	if (rman_get_start(com->ioportres) == likely_com_ports[dips & 0x03])
		kprintf(" : ESP");
	else {
		kprintf(" esp_port has com %d\n", dips & 0x03);
		lwkt_reltoken(&tty_token);
		return (0);
	}

	/*
	 * Check for ESP version 2.0 or later:  bits 4,5,6 = 010.
	 */
	outb(esp_port + ESP_CMD1, ESP_GETTEST);
	val = inb(esp_port + ESP_STATUS1);	/* clear reg 1 */
	val = inb(esp_port + ESP_STATUS2);
	if ((val & 0x70) < 0x20) {
		kprintf("-old (%o)", val & 0x70);
		lwkt_reltoken(&tty_token);
		return (0);
	}

	/*
	 * Check for ability to emulate 16550:  bit 7 == 1
	 */
	if ((dips & 0x80) == 0) {
		kprintf(" slave");
		lwkt_reltoken(&tty_token);
		return (0);
	}

	/*
	 * Okay, we seem to be a Hayes ESP card.  Whee.
	 */
	com->esp = TRUE;
	com->esp_port = esp_port;
	lwkt_reltoken(&tty_token);
	return (1);
}
#endif /* COM_ESP */

static int
sio_isa_attach(device_t dev)
{
	return (sioattach(dev, 0, 0UL));
}

int
sioattach(device_t dev, int xrid, u_long rclk)
{
	struct com_s	*com;
#ifdef COM_ESP
	Port_t		*espp;
#endif
	Port_t		iobase;
	int		minorbase;
	int		unit;
	u_int		flags;
	int		rid;
	struct resource *port;
	int		ret;
	static int	did_init;

	lwkt_gettoken(&tty_token);
	if (did_init == 0) {
		did_init = 1;
		callout_init_mp(&sio_timeout_handle);
	}

	rid = xrid;
	port = bus_alloc_resource(dev, SYS_RES_IOPORT, &rid,
				  0, ~0, IO_COMSIZE, RF_ACTIVE);
	if (!port) {
		lwkt_reltoken(&tty_token);
		return (ENXIO);
	}

	iobase = rman_get_start(port);
	unit = device_get_unit(dev);
	com = device_get_softc(dev);
	flags = device_get_flags(dev);

	if (unit >= sio_numunits)
		sio_numunits = unit + 1;
	/*
	 * sioprobe() has initialized the device registers as follows:
	 *	o cfcr = CFCR_8BITS.
	 *	  It is most important that CFCR_DLAB is off, so that the
	 *	  data port is not hidden when we enable interrupts.
	 *	o ier = 0.
	 *	  Interrupts are only enabled when the line is open.
	 *	o mcr = MCR_IENABLE, or 0 if the port has AST/4 compatible
	 *	  interrupt control register or the config specifies no irq.
	 *	  Keeping MCR_DTR and MCR_RTS off might stop the external
	 *	  device from sending before we are ready.
	 */
	bzero(com, sizeof *com);
	com->unit = unit;
	com->ioportres = port;
	com->bst = rman_get_bustag(port);
	com->bsh = rman_get_bushandle(port);
	com->cfcr_image = CFCR_8BITS;
	com->dtr_wait = 3 * hz;
	callout_init_mp(&com->dtr_ch);
	callout_init_mp(&com->busy_ch);
	com->loses_outints = COM_LOSESOUTINTS(flags) != 0;
	com->no_irq = bus_get_resource(dev, SYS_RES_IRQ, 0, NULL, NULL) != 0;
	com->tx_fifo_size = 1;
	com->obufs[0].l_head = com->obuf1;
	com->obufs[1].l_head = com->obuf2;

	com->data_port = iobase + com_data;
	com->int_id_port = iobase + com_iir;
	com->modem_ctl_port = iobase + com_mcr;
	com->mcr_image = inb(com->modem_ctl_port);
	com->line_status_port = iobase + com_lsr;
	com->modem_status_port = iobase + com_msr;
	com->intr_ctl_port = iobase + com_ier;

	if (rclk == 0)
		rclk = DEFAULT_RCLK;
	com->rclk = rclk;

	/*
	 * We don't use all the flags from <sys/ttydefaults.h> since they
	 * are only relevant for logins.  It's important to have echo off
	 * initially so that the line doesn't start blathering before the
	 * echo flag can be turned off.
	 */
	com->it_in.c_iflag = 0;
	com->it_in.c_oflag = 0;
	com->it_in.c_cflag = TTYDEF_CFLAG;
	com->it_in.c_lflag = 0;
	if (unit == comconsole) {
		com->it_in.c_iflag = TTYDEF_IFLAG;
		com->it_in.c_oflag = TTYDEF_OFLAG;
		com->it_in.c_cflag = TTYDEF_CFLAG | CLOCAL;
		com->it_in.c_lflag = TTYDEF_LFLAG;
		com->lt_out.c_cflag = com->lt_in.c_cflag = CLOCAL;
		com->lt_out.c_ispeed = com->lt_out.c_ospeed =
		com->lt_in.c_ispeed = com->lt_in.c_ospeed =
		com->it_in.c_ispeed = com->it_in.c_ospeed = comdefaultrate;
	} else
		com->it_in.c_ispeed = com->it_in.c_ospeed = TTYDEF_SPEED;
	if (siosetwater(com, com->it_in.c_ispeed) != 0) {
		/*
		 * Leave i/o resources allocated if this is a `cn'-level
		 * console, so that other devices can't snarf them.
		 */
		if (iobase != siocniobase)
			bus_release_resource(dev, SYS_RES_IOPORT, rid, port);
		lwkt_reltoken(&tty_token);
		return (ENOMEM);
	}
	termioschars(&com->it_in);
	com->it_out = com->it_in;

	/* attempt to determine UART type */
	kprintf("sio%d: type", unit);


#ifdef COM_MULTIPORT
	if (!COM_ISMULTIPORT(flags) && !COM_IIR_TXRDYBUG(flags))
#else
	if (!COM_IIR_TXRDYBUG(flags))
#endif
	{
		u_char	scr;
		u_char	scr1;
		u_char	scr2;

		scr = sio_getreg(com, com_scr);
		sio_setreg(com, com_scr, 0xa5);
		scr1 = sio_getreg(com, com_scr);
		sio_setreg(com, com_scr, 0x5a);
		scr2 = sio_getreg(com, com_scr);
		sio_setreg(com, com_scr, scr);
		if (scr1 != 0xa5 || scr2 != 0x5a) {
			kprintf(" 8250");
			goto determined_type;
		}
	}
	sio_setreg(com, com_fifo, FIFO_ENABLE | FIFO_RX_HIGH);
	DELAY(100);
	com->st16650a = 0;
	switch (inb(com->int_id_port) & IIR_FIFO_MASK) {
	case FIFO_RX_LOW:
		kprintf(" 16450");
		break;
	case FIFO_RX_MEDL:
		kprintf(" 16450?");
		break;
	case FIFO_RX_MEDH:
		kprintf(" 16550?");
		break;
	case FIFO_RX_HIGH:
		if (COM_NOFIFO(flags)) {
			kprintf(" 16550A fifo disabled");
		} else {
			com->hasfifo = TRUE;
			if (COM_ST16650A(flags)) {
				com->st16650a = 1;
				com->tx_fifo_size = 32;
				kprintf(" ST16650A");
			} else if (COM_TI16754(flags)) {
				com->tx_fifo_size = 64;
				kprintf(" TI16754");
			} else {
				com->tx_fifo_size = COM_FIFOSIZE(flags);
				kprintf(" 16550A");
			}
		}
#ifdef COM_ESP
		for (espp = likely_esp_ports; *espp != 0; espp++)
			if (espattach(com, *espp)) {
				com->tx_fifo_size = 1024;
				break;
			}
#endif
		if (!com->st16650a && !COM_TI16754(flags)) {
			if (!com->tx_fifo_size)
				com->tx_fifo_size = 16;
			else
				kprintf(" lookalike with %d bytes FIFO",
				    com->tx_fifo_size);
		}

		break;
	}
	
#ifdef COM_ESP
	if (com->esp) {
		/*
		 * Set 16550 compatibility mode.
		 * We don't use the ESP_MODE_SCALE bit to increase the
		 * fifo trigger levels because we can't handle large
		 * bursts of input.
		 * XXX flow control should be set in comparam(), not here.
		 */
		outb(com->esp_port + ESP_CMD1, ESP_SETMODE);
		outb(com->esp_port + ESP_CMD2, ESP_MODE_RTS | ESP_MODE_FIFO);

		/* Set RTS/CTS flow control. */
		outb(com->esp_port + ESP_CMD1, ESP_SETFLOWTYPE);
		outb(com->esp_port + ESP_CMD2, ESP_FLOW_RTS);
		outb(com->esp_port + ESP_CMD2, ESP_FLOW_CTS);

		/* Set flow-control levels. */
		outb(com->esp_port + ESP_CMD1, ESP_SETRXFLOW);
		outb(com->esp_port + ESP_CMD2, HIBYTE(768));
		outb(com->esp_port + ESP_CMD2, LOBYTE(768));
		outb(com->esp_port + ESP_CMD2, HIBYTE(512));
		outb(com->esp_port + ESP_CMD2, LOBYTE(512));
	}
#endif /* COM_ESP */
	sio_setreg(com, com_fifo, 0);
determined_type: ;

#ifdef COM_MULTIPORT
	if (COM_ISMULTIPORT(flags)) {
		device_t masterdev;

		com->multiport = TRUE;
		kprintf(" (multiport");
		if (unit == COM_MPMASTER(flags))
			kprintf(" master");
		kprintf(")");
		masterdev = devclass_get_device(sio_devclass,
		    COM_MPMASTER(flags));
		com->no_irq = (masterdev == NULL || bus_get_resource(masterdev,
		    SYS_RES_IRQ, 0, NULL, NULL) != 0);
	 }
#endif /* COM_MULTIPORT */
	if (unit == comconsole)
		kprintf(", console");
	if (COM_IIR_TXRDYBUG(flags))
		kprintf(" with a bogus IIR_TXRDY register");
	kprintf("\n");

	if (!sio_registered) {
		register_swi(SWI_TTY, siopoll, NULL ,"swi_siopoll", NULL, -1);
		sio_registered = TRUE;
	}
	minorbase = UNIT_TO_MINOR(unit);
	make_dev(&sio_ops, minorbase,
	    UID_ROOT, GID_WHEEL, 0600, "ttyd%r", unit);
	make_dev(&sio_ops, minorbase | CONTROL_INIT_STATE,
	    UID_ROOT, GID_WHEEL, 0600, "ttyid%r", unit);
	make_dev(&sio_ops, minorbase | CONTROL_LOCK_STATE,
	    UID_ROOT, GID_WHEEL, 0600, "ttyld%r", unit);
	make_dev(&sio_ops, minorbase | CALLOUT_MASK,
	    UID_UUCP, GID_DIALER, 0660, "cuaa%r", unit);
	make_dev(&sio_ops, minorbase | CALLOUT_MASK | CONTROL_INIT_STATE,
	    UID_UUCP, GID_DIALER, 0660, "cuaia%r", unit);
	make_dev(&sio_ops, minorbase | CALLOUT_MASK | CONTROL_LOCK_STATE,
	    UID_UUCP, GID_DIALER, 0660, "cuala%r", unit);
	com->flags = flags;
	com->pps.ppscap = PPS_CAPTUREASSERT | PPS_CAPTURECLEAR;
	pps_init(&com->pps);

	rid = 0;
	com->irqres = bus_alloc_resource(dev, SYS_RES_IRQ, &rid, 0ul, ~0ul, 1,
	    RF_ACTIVE);
	if (com->irqres) {
		ret = BUS_SETUP_INTR(device_get_parent(dev), dev,
				     com->irqres, INTR_MPSAFE, siointr, com,
				     &com->cookie, NULL, NULL);
		if (ret)
			device_printf(dev, "could not activate interrupt\n");
#if defined(DDB)
		/*
		 * Enable interrupts for early break-to-debugger support
		 * on the console.
		 */
		if (ret == 0 && unit == comconsole &&
		    (break_to_debugger || alt_break_to_debugger)) {
			inb(siocniobase + com_data);
			outb(siocniobase + com_ier,
			     IER_ERXRDY | IER_ERLS | IER_EMSC);
		}
#endif
	}

	lwkt_reltoken(&tty_token);
	return (0);
}

static int
sioopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct com_s	*com;
	int		error;
	int		mynor;
	struct tty	*tp;
	int		unit;

	mynor = minor(dev);
	unit = MINOR_TO_UNIT(mynor);
	com = com_addr(unit);
	if (com == NULL)
		return (ENXIO);
	if (com->gone)
		return (ENXIO);
	if (mynor & CONTROL_MASK)
		return (0);
	lwkt_gettoken(&tty_token);
	tp = dev->si_tty = com->tp = ttymalloc(com->tp);
	crit_enter();
	/*
	 * We jump to this label after all non-interrupted sleeps to pick
	 * up any changes of the device state.
	 */
open_top:
	while (com->state & CS_DTR_OFF) {
		error = tsleep(&com->dtr_wait, PCATCH, "siodtr", 0);
		if (com_addr(unit) == NULL) {
			crit_exit();
			lwkt_reltoken(&tty_token);
			return (ENXIO);
		}
		if (error != 0 || com->gone)
			goto out;
	}
	if (tp->t_state & TS_ISOPEN) {
		/*
		 * The device is open, so everything has been initialized.
		 * Handle conflicts.
		 */
		if (mynor & CALLOUT_MASK) {
			if (!com->active_out) {
				error = EBUSY;
				goto out;
			}
		} else {
			if (com->active_out) {
				if (ap->a_oflags & O_NONBLOCK) {
					error = EBUSY;
					goto out;
				}
				error =	tsleep(&com->active_out,
					       PCATCH, "siobi", 0);
				if (com_addr(unit) == NULL) {
					crit_exit();
					lwkt_reltoken(&tty_token);
					return (ENXIO);
				}
				if (error != 0 || com->gone)
					goto out;
				goto open_top;
			}
		}
		if (tp->t_state & TS_XCLUDE && priv_check_cred(ap->a_cred, PRIV_ROOT, 0)) {
			error = EBUSY;
			goto out;
		}
	} else {
		/*
		 * The device isn't open, so there are no conflicts.
		 * Initialize it.  Initialization is done twice in many
		 * cases: to preempt sleeping callin opens if we are
		 * callout, and to complete a callin open after DCD rises.
		 */
		tp->t_oproc = comstart;
		tp->t_param = comparam;
		tp->t_stop = comstop;
		tp->t_dev = dev;
		tp->t_termios = mynor & CALLOUT_MASK
				? com->it_out : com->it_in;
		(void)commctl(com, TIOCM_DTR | TIOCM_RTS, DMSET);
		com->poll = com->no_irq;
		com->poll_output = com->loses_outints;
		++com->wopeners;
		error = comparam(tp, &tp->t_termios);
		--com->wopeners;
		if (error != 0)
			goto out;
		/*
		 * XXX we should goto open_top if comparam() slept.
		 */
		if (com->hasfifo) {
			/*
			 * (Re)enable and drain fifos.
			 *
			 * Certain SMC chips cause problems if the fifos
			 * are enabled while input is ready.  Turn off the
			 * fifo if necessary to clear the input.  We test
			 * the input ready bit after enabling the fifos
			 * since we've already enabled them in comparam()
			 * and to handle races between enabling and fresh
			 * input.
			 */
			while (TRUE) {
				sio_setreg(com, com_fifo,
					   FIFO_RCV_RST | FIFO_XMT_RST
					   | com->fifo_image);
				/*
				 * XXX the delays are for superstitious
				 * historical reasons.  It must be less than
				 * the character time at the maximum
				 * supported speed (87 usec at 115200 bps
				 * 8N1).  Otherwise we might loop endlessly
				 * if data is streaming in.  We used to use
				 * delays of 100.  That usually worked
				 * because DELAY(100) used to usually delay
				 * for about 85 usec instead of 100.
				 */
				DELAY(50);
				if (!(inb(com->line_status_port) & LSR_RXRDY))
					break;
				sio_setreg(com, com_fifo, 0);
				DELAY(50);
				(void) inb(com->data_port);
			}
		}

		com_lock();
		(void) inb(com->line_status_port);
		(void) inb(com->data_port);
		com->prev_modem_status = com->last_modem_status
		    = inb(com->modem_status_port);
		if (COM_IIR_TXRDYBUG(com->flags)) {
			outb(com->intr_ctl_port, IER_ERXRDY | IER_ERLS
						| IER_EMSC);
		} else {
			outb(com->intr_ctl_port, IER_ERXRDY | IER_ETXRDY
						| IER_ERLS | IER_EMSC);
		}
		com_unlock();
		/*
		 * Handle initial DCD.  Callout devices get a fake initial
		 * DCD (trapdoor DCD).  If we are callout, then any sleeping
		 * callin opens get woken up and resume sleeping on "siobi"
		 * instead of "siodcd".
		 */
		/*
		 * XXX `mynor & CALLOUT_MASK' should be
		 * `tp->t_cflag & (SOFT_CARRIER | TRAPDOOR_CARRIER) where
		 * TRAPDOOR_CARRIER is the default initial state for callout
		 * devices and SOFT_CARRIER is like CLOCAL except it hides
		 * the true carrier.
		 */
		if (com->prev_modem_status & MSR_DCD || mynor & CALLOUT_MASK)
			(*linesw[tp->t_line].l_modem)(tp, 1);
	}
	/*
	 * Wait for DCD if necessary.
	 */
	if (!(tp->t_state & TS_CARR_ON) && !(mynor & CALLOUT_MASK)
	    && !(tp->t_cflag & CLOCAL) && !(ap->a_oflags & O_NONBLOCK)) {
		++com->wopeners;
		error = tsleep(TSA_CARR_ON(tp), PCATCH, "siodcd", 0);
		if (com_addr(unit) == NULL) {
			crit_exit();
			lwkt_reltoken(&tty_token);
			return (ENXIO);
		}
		--com->wopeners;
		if (error != 0 || com->gone)
			goto out;
		goto open_top;
	}
	error =	(*linesw[tp->t_line].l_open)(dev, tp);
	disc_optim(tp, &tp->t_termios, com);
	if (tp->t_state & TS_ISOPEN && mynor & CALLOUT_MASK)
		com->active_out = TRUE;
	siosettimeout();
out:
	crit_exit();
	if (!(tp->t_state & TS_ISOPEN) && com->wopeners == 0)
		comhardclose(com);
	lwkt_reltoken(&tty_token);
	return (error);
}

static int
sioclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct com_s	*com;
	int		mynor;
	struct tty	*tp;

	mynor = minor(dev);
	if (mynor & CONTROL_MASK)
		return (0);
	lwkt_gettoken(&tty_token);
	com = com_addr(MINOR_TO_UNIT(mynor));
	if (com == NULL) {
		lwkt_reltoken(&tty_token);
		return (ENODEV);
	}
	tp = com->tp;
	crit_enter();
	(*linesw[tp->t_line].l_close)(tp, ap->a_fflag);
	disc_optim(tp, &tp->t_termios, com);
	comstop(tp, FREAD | FWRITE);
	comhardclose(com);
	ttyclose(tp);
	siosettimeout();
	crit_exit();
	if (com->gone) {
		kprintf("sio%d: gone\n", com->unit);
		crit_enter();
		if (com->ibuf != NULL)
			kfree(com->ibuf, M_DEVBUF);
		bzero(tp, sizeof *tp);
		crit_exit();
	}
	lwkt_reltoken(&tty_token);
	return (0);
}

static void
comhardclose(struct com_s *com)
{
	struct tty	*tp;
	crit_enter();
	lwkt_gettoken(&tty_token);
	com->poll = FALSE;
	com->poll_output = FALSE;
	com->do_timestamp = FALSE;
	com->do_dcd_timestamp = FALSE;
	com->pps.ppsparam.mode = 0;
	sio_setreg(com, com_cfcr, com->cfcr_image &= ~CFCR_SBREAK);
	tp = com->tp;

#if defined(DDB)
	/*
	 * Leave interrupts enabled and don't clear DTR if this is the
	 * console. This allows us to detect break-to-debugger events
	 * while the console device is closed.
	 */
	if (com->unit != comconsole ||
	    (break_to_debugger == 0 && alt_break_to_debugger == 0))
#endif
	{
		sio_setreg(com, com_ier, 0);
		if (tp->t_cflag & HUPCL
		    /*
		     * XXX we will miss any carrier drop between here and the
		     * next open.  Perhaps we should watch DCD even when the
		     * port is closed; it is not sufficient to check it at
		     * the next open because it might go up and down while
		     * we're not watching.
		     */
		    || (!com->active_out
		        && !(com->prev_modem_status & MSR_DCD)
		        && !(com->it_in.c_cflag & CLOCAL))
		    || !(tp->t_state & TS_ISOPEN)) {
			(void)commctl(com, TIOCM_DTR, DMBIC);
			if (com->dtr_wait != 0 && !(com->state & CS_DTR_OFF)) {
				callout_reset(&com->dtr_ch, com->dtr_wait,
						siodtrwakeup, com);
				com->state |= CS_DTR_OFF;
			}
		}
	}
	if (com->hasfifo) {
		/*
		 * Disable fifos so that they are off after controlled
		 * reboots.  Some BIOSes fail to detect 16550s when the
		 * fifos are enabled.
		 */
		sio_setreg(com, com_fifo, 0);
	}
	com->active_out = FALSE;
	wakeup(&com->active_out);
	wakeup(TSA_CARR_ON(tp));	/* restart any wopeners */
	lwkt_reltoken(&tty_token);
	crit_exit();
}

static int
sioread(struct dev_read_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int		mynor, ret;
	struct com_s	*com;

	lwkt_gettoken(&tty_token);
	mynor = minor(dev);
	if (mynor & CONTROL_MASK) {
		lwkt_reltoken(&tty_token);
		return (ENODEV);
	}
	com = com_addr(MINOR_TO_UNIT(mynor));
	if (com == NULL || com->gone) {
		lwkt_reltoken(&tty_token);
		return (ENODEV);
	}
	ret = ((*linesw[com->tp->t_line].l_read)(com->tp, ap->a_uio, ap->a_ioflag));
	lwkt_reltoken(&tty_token);
	return ret;
}

static int
siowrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int		mynor;
	struct com_s	*com;
	int		unit, ret;

	lwkt_gettoken(&tty_token);
	mynor = minor(dev);
	if (mynor & CONTROL_MASK) {
		lwkt_reltoken(&tty_token);
		return (ENODEV);
	}

	unit = MINOR_TO_UNIT(mynor);
	com = com_addr(unit);
	if (com == NULL || com->gone) {
		lwkt_reltoken(&tty_token);
		return (ENODEV);
	}
	/*
	 * (XXX) We disallow virtual consoles if the physical console is
	 * a serial port.  This is in case there is a display attached that
	 * is not the console.  In that situation we don't need/want the X
	 * server taking over the console.
	 */
	if (constty != NULL && unit == comconsole)
		constty = NULL;
	ret = ((*linesw[com->tp->t_line].l_write)(com->tp, ap->a_uio, ap->a_ioflag));
	lwkt_reltoken(&tty_token);
	return ret;
}

static void
siobusycheck(void *chan)
{
	struct com_s	*com;

	lwkt_gettoken(&tty_token);
	com = (struct com_s *)chan;

	/*
	 * Clear TS_BUSY if low-level output is complete.
	 * spl locking is sufficient because siointr1() does not set CS_BUSY.
	 * If siointr1() clears CS_BUSY after we look at it, then we'll get
	 * called again.  Reading the line status port outside of siointr1()
	 * is safe because CS_BUSY is clear so there are no output interrupts
	 * to lose.
	 */
	crit_enter();
	if (com->state & CS_BUSY)
		com->extra_state &= ~CSE_BUSYCHECK;	/* False alarm. */
	else if ((inb(com->line_status_port) & (LSR_TSRE | LSR_TXRDY))
	    == (LSR_TSRE | LSR_TXRDY)) {
		com->tp->t_state &= ~TS_BUSY;
		ttwwakeup(com->tp);
		com->extra_state &= ~CSE_BUSYCHECK;
	} else {
		callout_reset(&com->busy_ch, hz / 100, siobusycheck, com);
	}
	crit_exit();
	lwkt_reltoken(&tty_token);
}

static u_int
siodivisor(u_long rclk, speed_t speed)
{
	long	actual_speed;
	u_int	divisor;
	int	error;

	if (speed == 0 || speed > ((speed_t)-1 - 1) / 8)
		return (0);
	divisor = (rclk / (8UL * speed) + 1) / 2;
	if (divisor == 0 || divisor >= 65536)
		return (0);
	actual_speed = rclk / (16UL * divisor);

	/* 10 times error in percent: */
	error = ((actual_speed - (long)speed) * 2000 / (long)speed + 1) / 2;

	/* 3.0% maximum error tolerance: */
	if (error < -30 || error > 30)
		return (0);

	return (divisor);
}

static void
siodtrwakeup(void *chan)
{
	struct com_s	*com;

	lwkt_gettoken(&tty_token);
	com = (struct com_s *)chan;
	com->state &= ~CS_DTR_OFF;
	wakeup(&com->dtr_wait);
	lwkt_reltoken(&tty_token);
}

/*
 * NOTE: Normally called with tty_token held but might not be when
 *	 operating as the console.
 *
 *	 Must be called with com_lock
 */
static void
sioinput(struct com_s *com)
{
	u_char		*buf;
	int		incc;
	u_char		line_status;
	int		recv_data;
	struct tty	*tp;

	buf = com->ibuf;
	tp = com->tp;
	if (!(tp->t_state & TS_ISOPEN) || !(tp->t_cflag & CREAD)) {
		com_events -= (com->iptr - com->ibuf);
		com->iptr = com->ibuf;
		return;
	}
	if (tp->t_state & TS_CAN_BYPASS_L_RINT) {
		/*
		 * Avoid the grotesquely inefficient lineswitch routine
		 * (ttyinput) in "raw" mode.  It usually takes about 450
		 * instructions (that's without canonical processing or echo!).
		 * slinput is reasonably fast (usually 40 instructions plus
		 * call overhead).
		 */
		do {
			com_unlock();
			incc = com->iptr - buf;
			if (tp->t_rawq.c_cc + incc > tp->t_ihiwat
			    && (com->state & CS_RTS_IFLOW
				|| tp->t_iflag & IXOFF)
			    && !(tp->t_state & TS_TBLOCK))
				ttyblock(tp);
			com->delta_error_counts[CE_TTY_BUF_OVERFLOW]
				+= b_to_q((char *)buf, incc, &tp->t_rawq);
			buf += incc;
			tk_nin += incc;
			tk_rawcc += incc;
			tp->t_rawcc += incc;
			ttwakeup(tp);
			if (tp->t_state & TS_TTSTOP
			    && (tp->t_iflag & IXANY
				|| tp->t_cc[VSTART] == tp->t_cc[VSTOP])) {
				tp->t_state &= ~TS_TTSTOP;
				tp->t_lflag &= ~FLUSHO;
				comstart(tp);
			}
			com_lock();
		} while (buf < com->iptr);
	} else {
		do {
			com_unlock();
			line_status = buf[com->ierroff];
			recv_data = *buf++;
			if (line_status
			    & (LSR_BI | LSR_FE | LSR_OE | LSR_PE)) {
				if (line_status & LSR_BI)
					recv_data |= TTY_BI;
				if (line_status & LSR_FE)
					recv_data |= TTY_FE;
				if (line_status & LSR_OE)
					recv_data |= TTY_OE;
				if (line_status & LSR_PE)
					recv_data |= TTY_PE;
			}
			(*linesw[tp->t_line].l_rint)(recv_data, tp);
			com_lock();
		} while (buf < com->iptr);
	}
	com_events -= (com->iptr - com->ibuf);
	com->iptr = com->ibuf;

	/*
	 * There is now room for another low-level buffer full of input,
	 * so enable RTS if it is now disabled and there is room in the
	 * high-level buffer.
	 */
	if ((com->state & CS_RTS_IFLOW) && !(com->mcr_image & MCR_RTS) &&
	    !(tp->t_state & TS_TBLOCK))
		outb(com->modem_ctl_port, com->mcr_image |= MCR_RTS);
}

void
siointr(void *arg)
{
	lwkt_gettoken(&tty_token);
#ifndef COM_MULTIPORT
	com_lock();
	siointr1((struct com_s *) arg);
	com_unlock();
#else /* COM_MULTIPORT */
	bool_t		possibly_more_intrs;
	int		unit;
	struct com_s	*com;

	/*
	 * Loop until there is no activity on any port.  This is necessary
	 * to get an interrupt edge more than to avoid another interrupt.
	 * If the IRQ signal is just an OR of the IRQ signals from several
	 * devices, then the edge from one may be lost because another is
	 * on.
	 */
	com_lock();
	do {
		possibly_more_intrs = FALSE;
		for (unit = 0; unit < sio_numunits; ++unit) {
			com = com_addr(unit);
			/*
			 * XXX com_lock();
			 * would it work here, or be counter-productive?
			 */
			if (com != NULL 
			    && !com->gone
			    && (inb(com->int_id_port) & IIR_IMASK)
			       != IIR_NOPEND) {
				siointr1(com);
				possibly_more_intrs = TRUE;
			}
			/* XXX com_unlock(); */
		}
	} while (possibly_more_intrs);
	com_unlock();
#endif /* COM_MULTIPORT */
	lwkt_reltoken(&tty_token);
}

/*
 * Called with tty_token held and com_lock held.
 */
static void
siointr1(struct com_s *com)
{
	u_char	line_status;
	u_char	modem_status;
	u_char	*ioptr;
	u_char	recv_data;
	u_char	int_ctl;
	u_char	int_ctl_new;
	sysclock_t count;

	int_ctl = inb(com->intr_ctl_port);
	int_ctl_new = int_ctl;

	while (!com->gone) {
		if (com->pps.ppsparam.mode & PPS_CAPTUREBOTH) {
			modem_status = inb(com->modem_status_port);
		        if ((modem_status ^ com->last_modem_status) & MSR_DCD) {
				count = sys_cputimer->count();
				pps_event(&com->pps, count, 
				    (modem_status & MSR_DCD) ? 
				    PPS_CAPTUREASSERT : PPS_CAPTURECLEAR);
			}
		}
		line_status = inb(com->line_status_port);

		/* input event? (check first to help avoid overruns) */
		while (line_status & LSR_RCV_MASK) {
			/* break/unnattached error bits or real input? */
			if (!(line_status & LSR_RXRDY))
				recv_data = 0;
			else
				recv_data = inb(com->data_port);
#if defined(DDB)
			/*
			 * Solaris implements a new BREAK which is initiated
			 * by a character sequence CR ~ ^b which is similar
			 * to a familiar pattern used on Sun servers by the
			 * Remote Console.
			 */
#define	KEY_CRTLB	2	/* ^B */
#define	KEY_CR		13	/* CR '\r' */
#define	KEY_TILDE	126	/* ~ */

			if (com->unit == comconsole && alt_break_to_debugger) {
				static int brk_state1 = 0, brk_state2 = 0;
				if (recv_data == KEY_CR) {
					brk_state1 = recv_data;
					brk_state2 = 0;
				} else if (brk_state1 == KEY_CR && (recv_data == KEY_TILDE || recv_data == KEY_CRTLB)) {
					if (recv_data == KEY_TILDE)
						brk_state2 = recv_data;
					else if (brk_state2 == KEY_TILDE && recv_data == KEY_CRTLB) {
							com_unlock();
							breakpoint();
							com_lock();
							brk_state1 = brk_state2 = 0;
							goto cont;
					} else
						brk_state2 = 0;
				} else
					brk_state1 = 0;
			}
#endif
			if (line_status & (LSR_BI | LSR_FE | LSR_PE)) {
				/*
				 * Don't store BI if IGNBRK or FE/PE if IGNPAR.
				 * Otherwise, push the work to a higher level
				 * (to handle PARMRK) if we're bypassing.
				 * Otherwise, convert BI/FE and PE+INPCK to 0.
				 *
				 * This makes bypassing work right in the
				 * usual "raw" case (IGNBRK set, and IGNPAR
				 * and INPCK clear).
				 *
				 * Note: BI together with FE/PE means just BI.
				 */
				if (line_status & LSR_BI) {
#if defined(DDB)
					if (com->unit == comconsole &&
					    break_to_debugger) {
						com_unlock();
						breakpoint();
						com_lock();
						goto cont;
					}
#endif
					if (com->tp == NULL
					    || com->tp->t_iflag & IGNBRK)
						goto cont;
				} else {
					if (com->tp == NULL
					    || com->tp->t_iflag & IGNPAR)
						goto cont;
				}
				if (com->tp->t_state & TS_CAN_BYPASS_L_RINT
				    && (line_status & (LSR_BI | LSR_FE)
					|| com->tp->t_iflag & INPCK))
					recv_data = 0;
			}
			++com->bytes_in;
			if (com->hotchar != 0 && recv_data == com->hotchar)
				setsofttty();
			ioptr = com->iptr;
			if (ioptr >= com->ibufend)
				CE_RECORD(com, CE_INTERRUPT_BUF_OVERFLOW);
			else {
				if (com->do_timestamp)
					microtime(&com->timestamp);
				++com_events;
				schedsofttty();
#if 0 /* for testing input latency vs efficiency */
if (com->iptr - com->ibuf == 8)
	setsofttty();
#endif
				ioptr[0] = recv_data;
				ioptr[com->ierroff] = line_status;
				com->iptr = ++ioptr;
				if (ioptr == com->ihighwater
				    && com->state & CS_RTS_IFLOW)
					outb(com->modem_ctl_port,
					     com->mcr_image &= ~MCR_RTS);
				if (line_status & LSR_OE)
					CE_RECORD(com, CE_OVERRUN);
			}
cont:
			/*
			 * "& 0x7F" is to avoid the gcc-1.40 generating a slow
			 * jump from the top of the loop to here
			 */
			line_status = inb(com->line_status_port) & 0x7F;
		}

		/* modem status change? (always check before doing output) */
		modem_status = inb(com->modem_status_port);
		if (modem_status != com->last_modem_status) {
			if (com->do_dcd_timestamp
			    && !(com->last_modem_status & MSR_DCD)
			    && modem_status & MSR_DCD)
				microtime(&com->dcd_timestamp);

			/*
			 * Schedule high level to handle DCD changes.  Note
			 * that we don't use the delta bits anywhere.  Some
			 * UARTs mess them up, and it's easy to remember the
			 * previous bits and calculate the delta.
			 */
			com->last_modem_status = modem_status;
			if (!(com->state & CS_CHECKMSR)) {
				com_events += LOTS_OF_EVENTS;
				com->state |= CS_CHECKMSR;
				setsofttty();
			}

			/* handle CTS change immediately for crisp flow ctl */
			if (com->state & CS_CTS_OFLOW) {
				if (modem_status & MSR_CTS)
					com->state |= CS_ODEVREADY;
				else
					com->state &= ~CS_ODEVREADY;
			}
		}

		/* output queued and everything ready? */
		if ((line_status & LSR_TXRDY)
		    && com->state >= (CS_BUSY | CS_TTGO | CS_ODEVREADY)) {
			ioptr = com->obufq.l_head;
			if (com->tx_fifo_size > 1) {
				u_int	ocount;

				ocount = com->obufq.l_tail - ioptr;
				if (ocount > com->tx_fifo_size)
					ocount = com->tx_fifo_size;
				com->bytes_out += ocount;
				do
					outb(com->data_port, *ioptr++);
				while (--ocount != 0);
			} else {
				outb(com->data_port, *ioptr++);
				++com->bytes_out;
			}
			com->obufq.l_head = ioptr;
			if (COM_IIR_TXRDYBUG(com->flags)) {
				int_ctl_new = int_ctl | IER_ETXRDY;
			}
			if (ioptr >= com->obufq.l_tail) {
				struct lbq	*qp;

				qp = com->obufq.l_next;
				qp->l_queued = FALSE;
				qp = qp->l_next;
				if (qp != NULL) {
					com->obufq.l_head = qp->l_head;
					com->obufq.l_tail = qp->l_tail;
					com->obufq.l_next = qp;
				} else {
					/* output just completed */
					if (COM_IIR_TXRDYBUG(com->flags)) {
						int_ctl_new = int_ctl & ~IER_ETXRDY;
					}
					com->state &= ~CS_BUSY;
				}
				if (!(com->state & CS_ODONE)) {
					com_events += LOTS_OF_EVENTS;
					com->state |= CS_ODONE;
					setsofttty();	/* handle at high level ASAP */
				}
			}
			if (COM_IIR_TXRDYBUG(com->flags) && (int_ctl != int_ctl_new)) {
				outb(com->intr_ctl_port, int_ctl_new);
			}
		}

		/* finished? */
#ifdef COM_MULTIPORT
		return;
#else
		if (inb(com->int_id_port) & IIR_NOPEND) 
			return;
#endif
	}
}

static int
sioioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	caddr_t data = ap->a_data;
	struct com_s	*com;
	int		error;
	int		mynor;
	struct tty	*tp;
#if defined(COMPAT_43)
	u_long		oldcmd;
	struct termios	term;
#endif
	lwkt_gettoken(&tty_token);
	mynor = minor(dev);

	com = com_addr(MINOR_TO_UNIT(mynor));
	if (com == NULL || com->gone) {
		lwkt_reltoken(&tty_token);
		return (ENODEV);
	}
	if (mynor & CONTROL_MASK) {
		struct termios	*ct;

		switch (mynor & CONTROL_MASK) {
		case CONTROL_INIT_STATE:
			ct = mynor & CALLOUT_MASK ? &com->it_out : &com->it_in;
			break;
		case CONTROL_LOCK_STATE:
			ct = mynor & CALLOUT_MASK ? &com->lt_out : &com->lt_in;
			break;
		default:
			lwkt_reltoken(&tty_token);
			return (ENODEV);	/* /dev/nodev */
		}
		switch (ap->a_cmd) {
		case TIOCSETA:
			error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0);
			if (error != 0)
				return (error);
			*ct = *(struct termios *)data;
			lwkt_reltoken(&tty_token);
			return (0);
		case TIOCGETA:
			*(struct termios *)data = *ct;
			lwkt_reltoken(&tty_token);
			return (0);
		case TIOCGETD:
			*(int *)data = TTYDISC;
			lwkt_reltoken(&tty_token);
			return (0);
		case TIOCGWINSZ:
			bzero(data, sizeof(struct winsize));
			lwkt_reltoken(&tty_token);
			return (0);
		default:
			lwkt_reltoken(&tty_token);
			return (ENOTTY);
		}
	}
	tp = com->tp;
#if defined(COMPAT_43)
	term = tp->t_termios;
	oldcmd = ap->a_cmd;
	error = ttsetcompat(tp, &ap->a_cmd, data, &term);
	if (error != 0) {
		lwkt_reltoken(&tty_token);
		return (error);
	}
	if (ap->a_cmd != oldcmd)
		data = (caddr_t)&term;
#endif
	if (ap->a_cmd == TIOCSETA || ap->a_cmd == TIOCSETAW ||
	    ap->a_cmd == TIOCSETAF) {
		int	cc;
		struct termios *dt = (struct termios *)data;
		struct termios *lt = mynor & CALLOUT_MASK
				     ? &com->lt_out : &com->lt_in;

		dt->c_iflag = (tp->t_iflag & lt->c_iflag)
			      | (dt->c_iflag & ~lt->c_iflag);
		dt->c_oflag = (tp->t_oflag & lt->c_oflag)
			      | (dt->c_oflag & ~lt->c_oflag);
		dt->c_cflag = (tp->t_cflag & lt->c_cflag)
			      | (dt->c_cflag & ~lt->c_cflag);
		dt->c_lflag = (tp->t_lflag & lt->c_lflag)
			      | (dt->c_lflag & ~lt->c_lflag);
		for (cc = 0; cc < NCCS; ++cc)
			if (lt->c_cc[cc] != 0)
				dt->c_cc[cc] = tp->t_cc[cc];
		if (lt->c_ispeed != 0)
			dt->c_ispeed = tp->t_ispeed;
		if (lt->c_ospeed != 0)
			dt->c_ospeed = tp->t_ospeed;
	}
	error = (*linesw[tp->t_line].l_ioctl)(tp, ap->a_cmd, data, ap->a_fflag, ap->a_cred);
	if (error != ENOIOCTL) {
		lwkt_reltoken(&tty_token);
		return (error);
	}
	crit_enter();
	error = ttioctl(tp, ap->a_cmd, data, ap->a_fflag);
	disc_optim(tp, &tp->t_termios, com);
	if (error != ENOIOCTL) {
		crit_exit();
		lwkt_reltoken(&tty_token);
		return (error);
	}
	switch (ap->a_cmd) {
	case TIOCSBRK:
		sio_setreg(com, com_cfcr, com->cfcr_image |= CFCR_SBREAK);
		break;
	case TIOCCBRK:
		sio_setreg(com, com_cfcr, com->cfcr_image &= ~CFCR_SBREAK);
		break;
	case TIOCSDTR:
		(void)commctl(com, TIOCM_DTR, DMBIS);
		break;
	case TIOCCDTR:
		(void)commctl(com, TIOCM_DTR, DMBIC);
		break;
	/*
	 * XXX should disallow changing MCR_RTS if CS_RTS_IFLOW is set.  The
	 * changes get undone on the next call to comparam().
	 */
	case TIOCMSET:
		(void)commctl(com, *(int *)data, DMSET);
		break;
	case TIOCMBIS:
		(void)commctl(com, *(int *)data, DMBIS);
		break;
	case TIOCMBIC:
		(void)commctl(com, *(int *)data, DMBIC);
		break;
	case TIOCMGET:
		*(int *)data = commctl(com, 0, DMGET);
		break;
	case TIOCMSDTRWAIT:
		/* must be root since the wait applies to following logins */
		error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0);
		if (error != 0) {
			crit_exit();
			lwkt_reltoken(&tty_token);
			return (error);
		}
		com->dtr_wait = *(int *)data * hz / 100;
		break;
	case TIOCMGDTRWAIT:
		*(int *)data = com->dtr_wait * 100 / hz;
		break;
	case TIOCTIMESTAMP:
		com->do_timestamp = TRUE;
		*(struct timeval *)data = com->timestamp;
		break;
	case TIOCDCDTIMESTAMP:
		com->do_dcd_timestamp = TRUE;
		*(struct timeval *)data = com->dcd_timestamp;
		break;
	default:
		crit_exit();
		error = pps_ioctl(ap->a_cmd, data, &com->pps);
		if (error == ENODEV)
			error = ENOTTY;
		lwkt_reltoken(&tty_token);
		return (error);
	}
	crit_exit();
	lwkt_reltoken(&tty_token);
	return (0);
}

static void
siopoll(void *dummy, void *frame)
{
	int		unit;

	lwkt_gettoken(&tty_token);
	if (com_events == 0) {
		lwkt_reltoken(&tty_token);
		return;
	}

repeat:
	for (unit = 0; unit < sio_numunits; ++unit) {
		struct com_s	*com;
		int		incc;
		struct tty	*tp;

		com = com_addr(unit);
		if (com == NULL)
			continue;
		tp = com->tp;
		if (tp == NULL || com->gone) {
			/*
			 * Discard any events related to never-opened or
			 * going-away devices.
			 */
			com_lock();
			incc = com->iptr - com->ibuf;
			com->iptr = com->ibuf;
			if (com->state & CS_CHECKMSR) {
				incc += LOTS_OF_EVENTS;
				com->state &= ~CS_CHECKMSR;
			}
			com_events -= incc;
			com_unlock();
			continue;
		}
		if (com->iptr != com->ibuf) {
			com_lock();
			sioinput(com);
			com_unlock();
		}
		if (com->state & CS_CHECKMSR) {
			u_char	delta_modem_status;

			com_lock();
			delta_modem_status = com->last_modem_status
					     ^ com->prev_modem_status;
			com->prev_modem_status = com->last_modem_status;
			com_events -= LOTS_OF_EVENTS;
			com->state &= ~CS_CHECKMSR;
			com_unlock();
			if (delta_modem_status & MSR_DCD)
				(*linesw[tp->t_line].l_modem)
					(tp, com->prev_modem_status & MSR_DCD);
		}
		if (com->state & CS_ODONE) {
			com_lock();
			com_events -= LOTS_OF_EVENTS;
			com->state &= ~CS_ODONE;
			com_unlock();
			if (!(com->state & CS_BUSY)
			    && !(com->extra_state & CSE_BUSYCHECK)) {
				callout_reset(&com->busy_ch, hz / 100,
						siobusycheck, com);
				com->extra_state |= CSE_BUSYCHECK;
			}
			(*linesw[tp->t_line].l_start)(tp);
		}
		if (com_events == 0)
			break;
	}
	if (com_events >= LOTS_OF_EVENTS)
		goto repeat;
	lwkt_reltoken(&tty_token);
}

/*
 * Called with tty_token held but no com_lock
 */
static int
comparam(struct tty *tp, struct termios *t)
{
	u_int		cfcr;
	int		cflag;
	struct com_s	*com;
	u_int		divisor;
	u_char		dlbh;
	u_char		dlbl;
	int		unit;

	unit = DEV_TO_UNIT(tp->t_dev);
	com = com_addr(unit);
	if (com == NULL) {
		return (ENODEV);
	}

	/* do historical conversions */
	if (t->c_ispeed == 0)
		t->c_ispeed = t->c_ospeed;

	/* check requested parameters */
	if (t->c_ospeed == 0)
		divisor = 0;
	else {
		if (t->c_ispeed != t->c_ospeed)
			return (EINVAL);
		divisor = siodivisor(com->rclk, t->c_ispeed);
		if (divisor == 0)
			return (EINVAL);
	}

	/* parameters are OK, convert them to the com struct and the device */
	crit_enter();
	if (divisor == 0)
		(void)commctl(com, TIOCM_DTR, DMBIC);	/* hang up line */
	else
		(void)commctl(com, TIOCM_DTR, DMBIS);
	cflag = t->c_cflag;
	switch (cflag & CSIZE) {
	case CS5:
		cfcr = CFCR_5BITS;
		break;
	case CS6:
		cfcr = CFCR_6BITS;
		break;
	case CS7:
		cfcr = CFCR_7BITS;
		break;
	default:
		cfcr = CFCR_8BITS;
		break;
	}
	if (cflag & PARENB) {
		cfcr |= CFCR_PENAB;
		if (!(cflag & PARODD))
			cfcr |= CFCR_PEVEN;
	}
	if (cflag & CSTOPB)
		cfcr |= CFCR_STOPB;

	if (com->hasfifo && divisor != 0) {
		/*
		 * Use a fifo trigger level low enough so that the input
		 * latency from the fifo is less than about 16 msec and
		 * the total latency is less than about 30 msec.  These
		 * latencies are reasonable for humans.  Serial comms
		 * protocols shouldn't expect anything better since modem
		 * latencies are larger.
		 *
		 * Interrupts can be held up for long periods of time
		 * due to inefficiencies in other parts of the kernel,
		 * certain video cards, etc.  Setting the FIFO trigger
		 * point to MEDH instead of HIGH gives us 694uS of slop
		 * (8 character times) instead of 173uS (2 character times)
		 * @ 115200 bps.
		 */
		com->fifo_image = t->c_ospeed <= 4800
				  ? FIFO_ENABLE : FIFO_ENABLE | FIFO_RX_MEDH;
#ifdef COM_ESP
		/*
		 * The Hayes ESP card needs the fifo DMA mode bit set
		 * in compatibility mode.  If not, it will interrupt
		 * for each character received.
		 */
		if (com->esp)
			com->fifo_image |= FIFO_DMA_MODE;
#endif
		sio_setreg(com, com_fifo, com->fifo_image);
	}

	/*
	 * This returns with interrupts disabled so that we can complete
	 * the speed change atomically.  Keeping interrupts disabled is
	 * especially important while com_data is hidden.
	 */
	(void) siosetwater(com, t->c_ispeed);

	if (divisor != 0) {
		sio_setreg(com, com_cfcr, cfcr | CFCR_DLAB);
		/*
		 * Only set the divisor registers if they would change,
		 * since on some 16550 incompatibles (UMC8669F), setting
		 * them while input is arriving them loses sync until
		 * data stops arriving.
		 */
		dlbl = divisor & 0xFF;
		if (sio_getreg(com, com_dlbl) != dlbl)
			sio_setreg(com, com_dlbl, dlbl);
		dlbh = divisor >> 8;
		if (sio_getreg(com, com_dlbh) != dlbh)
			sio_setreg(com, com_dlbh, dlbh);
	}

	sio_setreg(com, com_cfcr, com->cfcr_image = cfcr);

	if (!(tp->t_state & TS_TTSTOP))
		com->state |= CS_TTGO;

	if (cflag & CRTS_IFLOW) {
		if (com->st16650a) {
			sio_setreg(com, com_cfcr, 0xbf);
			sio_setreg(com, com_fifo,
				   sio_getreg(com, com_fifo) | 0x40);
		}
		com->state |= CS_RTS_IFLOW;
		/*
		 * If CS_RTS_IFLOW just changed from off to on, the change
		 * needs to be propagated to MCR_RTS.  This isn't urgent,
		 * so do it later by calling comstart() instead of repeating
		 * a lot of code from comstart() here.
		 */
	} else if (com->state & CS_RTS_IFLOW) {
		com->state &= ~CS_RTS_IFLOW;
		/*
		 * CS_RTS_IFLOW just changed from on to off.  Force MCR_RTS
		 * on here, since comstart() won't do it later.
		 */
		outb(com->modem_ctl_port, com->mcr_image |= MCR_RTS);
		if (com->st16650a) {
			sio_setreg(com, com_cfcr, 0xbf);
			sio_setreg(com, com_fifo,
				   sio_getreg(com, com_fifo) & ~0x40);
		}
	}


	/*
	 * Set up state to handle output flow control.
	 * XXX - worth handling MDMBUF (DCD) flow control at the lowest level?
	 * Now has 10+ msec latency, while CTS flow has 50- usec latency.
	 */
	com->state |= CS_ODEVREADY;
	com->state &= ~CS_CTS_OFLOW;
	if (cflag & CCTS_OFLOW) {
		com->state |= CS_CTS_OFLOW;
		if (!(com->last_modem_status & MSR_CTS))
			com->state &= ~CS_ODEVREADY;
		if (com->st16650a) {
			sio_setreg(com, com_cfcr, 0xbf);
			sio_setreg(com, com_fifo,
				   sio_getreg(com, com_fifo) | 0x80);
		}
	} else {
		if (com->st16650a) {
			sio_setreg(com, com_cfcr, 0xbf);
			sio_setreg(com, com_fifo,
				   sio_getreg(com, com_fifo) & ~0x80);
		}
	}

	sio_setreg(com, com_cfcr, com->cfcr_image);

	/* XXX shouldn't call functions while intrs are disabled. */
	disc_optim(tp, t, com);
	/*
	 * Recover from fiddling with CS_TTGO.  We used to call siointr1()
	 * unconditionally, but that defeated the careful discarding of
	 * stale input in sioopen().
	 */
	if (com->state >= (CS_BUSY | CS_TTGO)) {
		com_lock();
		siointr1(com);
		com_unlock();
	}
	crit_exit();
	comstart(tp);
	if (com->ibufold != NULL) {
		kfree(com->ibufold, M_DEVBUF);
		com->ibufold = NULL;
	}
	return (0);
}

/*
 * called with tty_token held
 */
static int
siosetwater(struct com_s *com, speed_t speed)
{
	int		cp4ticks;
	u_char		*ibuf;
	int		ibufsize;
	struct tty	*tp;

	/*
	 * Make the buffer size large enough to handle a softtty interrupt
	 * latency of about 2 ticks without loss of throughput or data
	 * (about 3 ticks if input flow control is not used or not honoured,
	 * but a bit less for CS5-CS7 modes).
	 */
	cp4ticks = speed / 10 / hz * 4;
	for (ibufsize = 128; ibufsize < cp4ticks;)
		ibufsize <<= 1;
	if (ibufsize == com->ibufsize)
		return (0);

	/*
	 * Allocate input buffer.  The extra factor of 2 in the size is
	 * to allow for an error byte for each input byte.
	 */
	ibuf = kmalloc(2 * ibufsize, M_DEVBUF, M_WAITOK | M_ZERO);

	/* Initialize non-critical variables. */
	com->ibufold = com->ibuf;
	com->ibufsize = ibufsize;
	tp = com->tp;
	if (tp != NULL) {
		tp->t_ififosize = 2 * ibufsize;
		tp->t_ispeedwat = (speed_t)-1;
		tp->t_ospeedwat = (speed_t)-1;
	}

	/*
	 * Read current input buffer.
	 */
	com_lock();
	if (com->iptr != com->ibuf)
		sioinput(com);

	/*-
	 * Initialize critical variables, including input buffer watermarks.
	 * The external device is asked to stop sending when the buffer
	 * exactly reaches high water, or when the high level requests it.
	 * The high level is notified immediately (rather than at a later
	 * clock tick) when this watermark is reached.
	 * The buffer size is chosen so the watermark should almost never
	 * be reached.
	 * The low watermark is invisibly 0 since the buffer is always
	 * emptied all at once.
	 */
	com->iptr = com->ibuf = ibuf;
	com->ibufend = ibuf + ibufsize;
	com->ierroff = ibufsize;
	com->ihighwater = ibuf + 3 * ibufsize / 4;
	com_unlock();
	return (0);
}

static void
comstart(struct tty *tp)
{
	struct com_s	*com;
	int		unit;

	lwkt_gettoken(&tty_token);
	unit = DEV_TO_UNIT(tp->t_dev);
	com = com_addr(unit);
	if (com == NULL) {
		lwkt_reltoken(&tty_token);
		return;
	}
	crit_enter();
	com_lock();
	if (tp->t_state & TS_TTSTOP)
		com->state &= ~CS_TTGO;
	else
		com->state |= CS_TTGO;
	if (tp->t_state & TS_TBLOCK) {
		if (com->mcr_image & MCR_RTS && com->state & CS_RTS_IFLOW)
			outb(com->modem_ctl_port, com->mcr_image &= ~MCR_RTS);
	} else {
		if (!(com->mcr_image & MCR_RTS) && com->iptr < com->ihighwater
		    && com->state & CS_RTS_IFLOW)
			outb(com->modem_ctl_port, com->mcr_image |= MCR_RTS);
	}
	com_unlock();
	if (tp->t_state & (TS_TIMEOUT | TS_TTSTOP)) {
		ttwwakeup(tp);
		crit_exit();
		lwkt_reltoken(&tty_token);
		return;
	}
	if (tp->t_outq.c_cc != 0) {
		struct lbq	*qp;
		struct lbq	*next;

		if (!com->obufs[0].l_queued) {
			com->obufs[0].l_tail
			    = com->obuf1 + q_to_b(&tp->t_outq, com->obuf1,
						  sizeof com->obuf1);
			com->obufs[0].l_next = NULL;
			com->obufs[0].l_queued = TRUE;
			com_lock();
			if (com->state & CS_BUSY) {
				qp = com->obufq.l_next;
				while ((next = qp->l_next) != NULL)
					qp = next;
				qp->l_next = &com->obufs[0];
			} else {
				com->obufq.l_head = com->obufs[0].l_head;
				com->obufq.l_tail = com->obufs[0].l_tail;
				com->obufq.l_next = &com->obufs[0];
				com->state |= CS_BUSY;
			}
			com_unlock();
		}
		if (tp->t_outq.c_cc != 0 && !com->obufs[1].l_queued) {
			com->obufs[1].l_tail
			    = com->obuf2 + q_to_b(&tp->t_outq, com->obuf2,
						  sizeof com->obuf2);
			com->obufs[1].l_next = NULL;
			com->obufs[1].l_queued = TRUE;
			com_lock();
			if (com->state & CS_BUSY) {
				qp = com->obufq.l_next;
				while ((next = qp->l_next) != NULL)
					qp = next;
				qp->l_next = &com->obufs[1];
			} else {
				com->obufq.l_head = com->obufs[1].l_head;
				com->obufq.l_tail = com->obufs[1].l_tail;
				com->obufq.l_next = &com->obufs[1];
				com->state |= CS_BUSY;
			}
			com_unlock();
		}
		tp->t_state |= TS_BUSY;
	}
	com_lock();
	if (com->state >= (CS_BUSY | CS_TTGO))
		siointr1(com);	/* fake interrupt to start output */
	com_unlock();
	ttwwakeup(tp);
	crit_exit();
	lwkt_reltoken(&tty_token);
}

static void
comstop(struct tty *tp, int rw)
{
	struct com_s	*com;

	lwkt_gettoken(&tty_token);
	com = com_addr(DEV_TO_UNIT(tp->t_dev));
	if (com == NULL || com->gone) {
		lwkt_reltoken(&tty_token);
		return;
	}
	com_lock();
	if (rw & FWRITE) {
		if (com->hasfifo)
#ifdef COM_ESP
		    /* XXX avoid h/w bug. */
		    if (!com->esp)
#endif
			sio_setreg(com, com_fifo,
				   FIFO_XMT_RST | com->fifo_image);
		com->obufs[0].l_queued = FALSE;
		com->obufs[1].l_queued = FALSE;
		if (com->state & CS_ODONE)
			com_events -= LOTS_OF_EVENTS;
		com->state &= ~(CS_ODONE | CS_BUSY);
		com->tp->t_state &= ~TS_BUSY;
	}
	if (rw & FREAD) {
		if (com->hasfifo)
#ifdef COM_ESP
		    /* XXX avoid h/w bug. */
		    if (!com->esp)
#endif
			sio_setreg(com, com_fifo,
				   FIFO_RCV_RST | com->fifo_image);
		com_events -= (com->iptr - com->ibuf);
		com->iptr = com->ibuf;
	}
	com_unlock();
	comstart(tp);
	lwkt_reltoken(&tty_token);
}

static int
commctl(struct com_s *com, int bits, int how)
{
	int	mcr;
	int	msr;

	lwkt_gettoken(&tty_token);
	if (how == DMGET) {
		bits = TIOCM_LE;	/* XXX - always enabled while open */
		mcr = com->mcr_image;
		if (mcr & MCR_DTR)
			bits |= TIOCM_DTR;
		if (mcr & MCR_RTS)
			bits |= TIOCM_RTS;
		msr = com->prev_modem_status;
		if (msr & MSR_CTS)
			bits |= TIOCM_CTS;
		if (msr & MSR_DCD)
			bits |= TIOCM_CD;
		if (msr & MSR_DSR)
			bits |= TIOCM_DSR;
		/*
		 * XXX - MSR_RI is naturally volatile, and we make MSR_TERI
		 * more volatile by reading the modem status a lot.  Perhaps
		 * we should latch both bits until the status is read here.
		 */
		if (msr & (MSR_RI | MSR_TERI))
			bits |= TIOCM_RI;
		lwkt_reltoken(&tty_token);
		return (bits);
	}
	mcr = 0;
	if (bits & TIOCM_DTR)
		mcr |= MCR_DTR;
	if (bits & TIOCM_RTS)
		mcr |= MCR_RTS;
	if (com->gone) {
		lwkt_reltoken(&tty_token);
		return(0);
	}
	com_lock();
	switch (how) {
	case DMSET:
		outb(com->modem_ctl_port,
		     com->mcr_image = mcr | (com->mcr_image & MCR_IENABLE));
		break;
	case DMBIS:
		outb(com->modem_ctl_port, com->mcr_image |= mcr);
		break;
	case DMBIC:
		outb(com->modem_ctl_port, com->mcr_image &= ~mcr);
		break;
	}
	com_unlock();
	lwkt_reltoken(&tty_token);
	return (0);
}

/*
 * NOTE: Must be called with tty_token held
 */
static void
siosettimeout(void)
{
	struct com_s	*com;
	bool_t		someopen;
	int		unit;

	ASSERT_LWKT_TOKEN_HELD(&tty_token);
	/*
	 * Set our timeout period to 1 second if no polled devices are open.
	 * Otherwise set it to max(1/200, 1/hz).
	 * Enable timeouts iff some device is open.
	 */
	callout_stop(&sio_timeout_handle);
	sio_timeout = hz;
	someopen = FALSE;
	for (unit = 0; unit < sio_numunits; ++unit) {
		com = com_addr(unit);
		if (com != NULL && com->tp != NULL
		    && com->tp->t_state & TS_ISOPEN && !com->gone) {
			someopen = TRUE;
			if (com->poll || com->poll_output) {
				sio_timeout = hz > 200 ? hz / 200 : 1;
				break;
			}
		}
	}
	if (someopen) {
		sio_timeouts_until_log = hz / sio_timeout;
		callout_reset(&sio_timeout_handle, sio_timeout,
				comwakeup, NULL);
	} else {
		/* Flush error messages, if any. */
		sio_timeouts_until_log = 1;
		comwakeup(NULL);
		callout_stop(&sio_timeout_handle);
	}
}

/*
 * NOTE: Must be called with tty_token held
 */
static void
comwakeup(void *chan)
{
	struct com_s	*com;
	int		unit;

	/*
	 * Can be called from a callout too so just get the token
	 */
	lwkt_gettoken(&tty_token);
	callout_reset(&sio_timeout_handle, sio_timeout, comwakeup, NULL);

	/*
	 * Recover from lost output interrupts.
	 * Poll any lines that don't use interrupts.
	 */
	for (unit = 0; unit < sio_numunits; ++unit) {
		com = com_addr(unit);
		if (com != NULL && !com->gone
		    && (com->state >= (CS_BUSY | CS_TTGO) || com->poll)) {
			com_lock();
			siointr1(com);
			com_unlock();
		}
	}

	/*
	 * Check for and log errors, but not too often.
	 */
	if (--sio_timeouts_until_log > 0) {
		lwkt_reltoken(&tty_token);
		return;
	}
	sio_timeouts_until_log = hz / sio_timeout;
	for (unit = 0; unit < sio_numunits; ++unit) {
		int	errnum;

		com = com_addr(unit);
		if (com == NULL)
			continue;
		if (com->gone)
			continue;
		for (errnum = 0; errnum < CE_NTYPES; ++errnum) {
			u_int	delta;
			u_long	total;

			com_lock();
			delta = com->delta_error_counts[errnum];
			com->delta_error_counts[errnum] = 0;
			com_unlock();
			if (delta == 0)
				continue;
			total = com->error_counts[errnum] += delta;
			log(LOG_ERR, "sio%d: %u more %s%s (total %lu)\n",
			    unit, delta, error_desc[errnum],
			    delta == 1 ? "" : "s", total);
		}
	}
	lwkt_reltoken(&tty_token);
}

static void
disc_optim(struct tty *tp, struct termios *t, struct com_s *com)
{
	lwkt_gettoken(&tty_token);
	if (!(t->c_iflag & (ICRNL | IGNCR | IMAXBEL | INLCR | ISTRIP | IXON))
	    && (!(t->c_iflag & BRKINT) || (t->c_iflag & IGNBRK))
	    && (!(t->c_iflag & PARMRK)
		|| (t->c_iflag & (IGNPAR | IGNBRK)) == (IGNPAR | IGNBRK))
	    && !(t->c_lflag & (ECHO | ICANON | IEXTEN | ISIG | PENDIN))
	    && linesw[tp->t_line].l_rint == ttyinput)
		tp->t_state |= TS_CAN_BYPASS_L_RINT;
	else
		tp->t_state &= ~TS_CAN_BYPASS_L_RINT;
	com->hotchar = linesw[tp->t_line].l_hotchar;
	lwkt_reltoken(&tty_token);
}

/*
 * Following are all routines needed for SIO to act as console
 */
#include <sys/cons.h>

struct siocnstate {
	u_char	dlbl;
	u_char	dlbh;
	u_char	ier;
	u_char	cfcr;
	u_char	mcr;
};

static speed_t siocngetspeed (Port_t, u_long rclk);
#if 0
static void siocnclose	(struct siocnstate *sp, Port_t iobase);
#endif
static void siocnopen	(struct siocnstate *sp, Port_t iobase, int speed);
static void siocntxwait	(Port_t iobase);

static cn_probe_t siocnprobe;
static cn_init_t siocninit;
static cn_init_fini_t siocninit_fini;
static cn_checkc_t siocncheckc;
static cn_getc_t siocngetc;
static cn_putc_t siocnputc;

#if defined(__i386__) || defined(__x86_64__)
CONS_DRIVER(sio, siocnprobe, siocninit, siocninit_fini,
	    NULL, siocngetc, siocncheckc, siocnputc, NULL);
#endif

/* To get the GDB related variables */
#if DDB > 0
#include <ddb/ddb.h>
#endif

static void
siocntxwait(Port_t iobase)
{
	int	timo;

	/*
	 * Wait for any pending transmission to finish.  Required to avoid
	 * the UART lockup bug when the speed is changed, and for normal
	 * transmits.
	 */
	timo = 100000;
	while ((inb(iobase + com_lsr) & (LSR_TSRE | LSR_TXRDY))
	       != (LSR_TSRE | LSR_TXRDY) && --timo != 0)
		;
}

/*
 * Read the serial port specified and try to figure out what speed
 * it's currently running at.  We're assuming the serial port has
 * been initialized and is basicly idle.  This routine is only intended
 * to be run at system startup.
 *
 * If the value read from the serial port doesn't make sense, return 0.
 */
/*
 * NOTE: Must be called with tty_token held
 */
static speed_t
siocngetspeed(Port_t iobase, u_long rclk)
{
	u_int	divisor;
	u_char	dlbh;
	u_char	dlbl;
	u_char  cfcr;

	cfcr = inb(iobase + com_cfcr);
	outb(iobase + com_cfcr, CFCR_DLAB | cfcr);

	dlbl = inb(iobase + com_dlbl);
	dlbh = inb(iobase + com_dlbh);

	outb(iobase + com_cfcr, cfcr);

	divisor = dlbh << 8 | dlbl;

	/* XXX there should be more sanity checking. */
	if (divisor == 0)
		return (CONSPEED);
	return (rclk / (16UL * divisor));
}

static void
siocnopen(struct siocnstate *sp, Port_t iobase, int speed)
{
	u_int	divisor;
	u_char	dlbh;
	u_char	dlbl;

	/*
	 * Save all the device control registers except the fifo register
	 * and set our default ones (cs8 -parenb speed=comdefaultrate).
	 * We can't save the fifo register since it is read-only.
	 */
	sp->ier = inb(iobase + com_ier);
	outb(iobase + com_ier, 0);	/* spltty() doesn't stop siointr() */
	siocntxwait(iobase);
	sp->cfcr = inb(iobase + com_cfcr);
	outb(iobase + com_cfcr, CFCR_DLAB | CFCR_8BITS);
	sp->dlbl = inb(iobase + com_dlbl);
	sp->dlbh = inb(iobase + com_dlbh);
	/*
	 * Only set the divisor registers if they would change, since on
	 * some 16550 incompatibles (Startech), setting them clears the
	 * data input register.  This also reduces the effects of the
	 * UMC8669F bug.
	 */
	divisor = siodivisor(comdefaultrclk, speed);
	dlbl = divisor & 0xFF;
	if (sp->dlbl != dlbl)
		outb(iobase + com_dlbl, dlbl);
	dlbh = divisor >> 8;
	if (sp->dlbh != dlbh)
		outb(iobase + com_dlbh, dlbh);
	outb(iobase + com_cfcr, CFCR_8BITS);
	sp->mcr = inb(iobase + com_mcr);
	/*
	 * We don't want interrupts, but must be careful not to "disable"
	 * them by clearing the MCR_IENABLE bit, since that might cause
	 * an interrupt by floating the IRQ line.
	 */
	outb(iobase + com_mcr, (sp->mcr & MCR_IENABLE) | MCR_DTR | MCR_RTS);
}

#if 0
static void
siocnclose(struct siocnstate *sp, Port_t iobase)
{
	/*
	 * Restore the device control registers.
	 */
	siocntxwait(iobase);
	outb(iobase + com_cfcr, CFCR_DLAB | CFCR_8BITS);
	if (sp->dlbl != inb(iobase + com_dlbl))
		outb(iobase + com_dlbl, sp->dlbl);
	if (sp->dlbh != inb(iobase + com_dlbh))
		outb(iobase + com_dlbh, sp->dlbh);
	outb(iobase + com_cfcr, sp->cfcr);
	/*
	 * XXX damp oscillations of MCR_DTR and MCR_RTS by not restoring them.
	 */
	outb(iobase + com_mcr, sp->mcr | MCR_DTR | MCR_RTS);
	outb(iobase + com_ier, sp->ier);
}
#endif

static void
siocnprobe(struct consdev *cp)
{
	u_char			cfcr;
	u_int			divisor;
	int			unit;
	struct siocnstate	sp;

	/*
	 * Find our first enabled console, if any.  If it is a high-level
	 * console device, then initialize it and return successfully.
	 * If it is a low-level console device, then initialize it and
	 * return unsuccessfully.  It must be initialized in both cases
	 * for early use by console drivers and debuggers.  Initializing
	 * the hardware is not necessary in all cases, since the i/o
	 * routines initialize it on the fly, but it is necessary if
	 * input might arrive while the hardware is switched back to an
	 * uninitialized state.  We can't handle multiple console devices
	 * yet because our low-level routines don't take a device arg.
	 * We trust the user to set the console flags properly so that we
	 * don't need to probe.
	 */
	cp->cn_pri = CN_DEAD;

	for (unit = 0; unit < 16; unit++) { /* XXX need to know how many */
		int flags;
		int disabled;
		if (resource_int_value("sio", unit, "disabled", &disabled) == 0) {
			if (disabled)
				continue;
		}
		if (resource_int_value("sio", unit, "flags", &flags))
			continue;
		if (COM_CONSOLE(flags) || COM_DEBUGGER(flags)) {
			int port;
			int baud;
			Port_t iobase;
			speed_t boot_speed;

			if (resource_int_value("sio", unit, "port", &port))
				continue;
			if (resource_int_value("sio", unit, "baud", &baud) == 0)
				boot_speed = baud;
			else
				boot_speed = 0;
			iobase = port;
			crit_enter();
			if (boothowto & RB_SERIAL) {
				if (boot_speed == 0) {
					boot_speed = siocngetspeed(iobase,
								comdefaultrclk);
				}
				if (boot_speed)
					comdefaultrate = boot_speed;
			}

			/*
			 * Initialize the divisor latch.  We can't rely on
			 * siocnopen() to do this the first time, since it 
			 * avoids writing to the latch if the latch appears
			 * to have the correct value.  Also, if we didn't
			 * just read the speed from the hardware, then we
			 * need to set the speed in hardware so that
			 * switching it later is null.
			 */
			com_lock();
			cfcr = inb(iobase + com_cfcr);
			outb(iobase + com_cfcr, CFCR_DLAB | cfcr);
			divisor = siodivisor(comdefaultrclk, comdefaultrate);
			outb(iobase + com_dlbl, divisor & 0xff);
			outb(iobase + com_dlbh, divisor >> 8);
			outb(iobase + com_cfcr, cfcr);

			siocnopen(&sp, iobase, comdefaultrate);
			com_unlock();

			crit_exit();
			if (COM_CONSOLE(flags) && !COM_LLCONSOLE(flags)) {
				cp->cn_probegood = 1;
				cp->cn_private = (void *)(intptr_t)unit;
				cp->cn_pri = COM_FORCECONSOLE(flags)
					     || boothowto & RB_SERIAL
					     ? CN_REMOTE : CN_NORMAL;
				siocniobase = iobase;
				siocnunit = unit;
			}
			if (COM_DEBUGGER(flags) && gdb_tab == NULL) {
				kprintf("sio%d: gdb debugging port\n", unit);
				siogdbiobase = iobase;
				siogdbunit = unit;
#if DDB > 0
				cp->cn_gdbprivate = (void *)(intptr_t)unit;
				gdb_tab = cp;
#endif
			}
		}
	}
#if defined(__i386__) || defined(__x86_64__)
#if DDB > 0
	/*
	 * XXX Ugly Compatability.
	 * If no gdb port has been specified, set it to be the console
	 * as some configuration files don't specify the gdb port.
	 */
	if (gdb_tab == NULL && (boothowto & RB_GDB)) {
		kprintf("Warning: no GDB port specified. Defaulting to sio%d.\n",
			siocnunit);
		kprintf("Set flag 0x80 on desired GDB port in your\n");
		kprintf("configuration file (currently sio only).\n");
		siogdbiobase = siocniobase;
		siogdbunit = siocnunit;
		cp->cn_gdbprivate = (void *)(intptr_t)siocnunit;
		gdb_tab = cp;
	}
#endif
#endif
}

static void
siocninit(struct consdev *cp)
{
	comconsole = (int)(intptr_t)cp->cn_private;
}

static void
siocninit_fini(struct consdev *cp)
{
	cdev_t dev;
	int unit;

	if (cp->cn_probegood) {
		unit = (int)(intptr_t)cp->cn_private;
		/*
		 * Call devfs_find_device_by_name on ttydX to find the correct device,
		 * as it should have been created already at this point by the
		 * attach routine.
		 * If it isn't found, the serial port was not attached at all and we
		 * shouldn't be here, so assert this case.
		 */
		dev = devfs_find_device_by_name("ttyd%r", unit);

		KKASSERT(dev != NULL);
		cp->cn_dev = dev;
	}
}

static int
siocncheckc(void *private)
{
	int	c;
	int	unit = (int)(intptr_t)private;
	Port_t	iobase;
#if 0
	struct siocnstate	sp;
#endif

	if (unit == siogdbunit)
		iobase = siogdbiobase;
	else
		iobase = siocniobase;
	com_lock();
	crit_enter();
#if 0
	siocnopen(&sp, iobase, comdefaultrate);
#endif
	if (inb(iobase + com_lsr) & LSR_RXRDY)
		c = inb(iobase + com_data);
	else
		c = -1;
#if 0
	siocnclose(&sp, iobase);
#endif
	crit_exit();
	com_unlock();
	return (c);
}


int
siocngetc(void *private)
{
	int	c;
	int	unit = (int)(intptr_t)private;
	Port_t	iobase;
#if 0
	struct siocnstate	sp;
#endif

	if (unit == siogdbunit)
		iobase = siogdbiobase;
	else
		iobase = siocniobase;
	com_lock();
	crit_enter();
#if 0
	siocnopen(&sp, iobase, comdefaultrate);
#endif
	while (!(inb(iobase + com_lsr) & LSR_RXRDY))
		;
	c = inb(iobase + com_data);
#if 0
	siocnclose(&sp, iobase);
#endif
	crit_exit();
	com_unlock();
	return (c);
}

void
siocnputc(void *private, int c)
{
	int	unit = (int)(intptr_t)private;
#if 0
	struct siocnstate	sp;
#endif
	Port_t	iobase;

	if (unit == siogdbunit)
		iobase = siogdbiobase;
	else
		iobase = siocniobase;
	com_lock();
	crit_enter();
#if 0
	siocnopen(&sp, iobase, comdefaultrate);
#endif
	siocntxwait(iobase);
	outb(iobase + com_data, c);
#if 0
	siocnclose(&sp, iobase);
#endif
	crit_exit();
	com_unlock();
}

DRIVER_MODULE(sio, isa, sio_isa_driver, sio_devclass, NULL, NULL);
DRIVER_MODULE(sio, acpi, sio_isa_driver, sio_devclass, NULL, NULL);
#if NPCI > 0
DRIVER_MODULE(sio, pci, sio_pci_driver, sio_devclass, NULL, NULL);
#endif
#if NPUC > 0
DRIVER_MODULE(sio, puc, sio_puc_driver, sio_devclass, NULL, NULL);
#endif
