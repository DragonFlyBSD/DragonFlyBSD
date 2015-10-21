/*
 * (MPSAFE)
 *
 * Device driver for Specialix range (SI/XIO) of serial line multiplexors.
 *
 * Copyright (C) 1990, 1992, 1998 Specialix International,
 * Copyright (C) 1993, Andy Rutter <andy@acronym.co.uk>
 * Copyright (C) 2000, Peter Wemm <peter@netplex.com.au>
 *
 * Originally derived from:	SunOS 4.x version
 * Ported from BSDI version to FreeBSD by Peter Wemm.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notices, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notices, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Andy Rutter of
 *	Advanced Methods and Tools Ltd. based on original information
 *	from Specialix International.
 * 4. Neither the name of Advanced Methods and Tools, nor Specialix
 *    International may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHORS BE LIABLE.
 *
 * $FreeBSD: src/sys/dev/si/si.c,v 1.101.2.1 2001/02/26 04:23:06 jlemon Exp $
 */

#ifndef lint
static const char si_copyright1[] =  "@(#) Copyright (C) Specialix International, 1990,1992,1998",
		  si_copyright2[] =  "@(#) Copyright (C) Andy Rutter 1993",
		  si_copyright3[] =  "@(#) Copyright (C) Peter Wemm 2000";
#endif	/* not lint */

#include "opt_compat.h"
#include "opt_debug_si.h"

#include <sys/param.h>
#include <sys/systm.h>
#if defined(COMPAT_43)
#include <sys/ioctl_compat.h>
#endif
#include <sys/tty.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/dkstat.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/thread2.h>

#include <machine/clock.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/stdarg.h>

#include "sireg.h"
#include "sivar.h"
#include "si.h"

/*
 * This device driver is designed to interface the Specialix International
 * SI, XIO and SX range of serial multiplexor cards to FreeBSD on an ISA,
 * EISA or PCI bus machine.
 *
 * The controller is interfaced to the host via dual port RAM
 * and an interrupt.
 *
 * The code for the Host 1 (very old ISA cards) has not been tested.
 */

#define	POLL		/* turn on poller to scan for lost interrupts */
#define REALPOLL	/* on each poll, scan for work regardless */
#define POLLHZ	(hz/10)	/* 10 times per second */
#define SI_I_HIGH_WATER	(TTYHOG - 2 * SI_BUFFERSIZE)
#define INT_COUNT 25000		/* max of 125 ints per second */
#define JET_INT_COUNT 100	/* max of 100 ints per second */
#define RXINT_COUNT 1	/* one rxint per 10 milliseconds */

enum si_mctl { GET, SET, BIS, BIC };

static void si_command(struct si_port *, int, int);
static int si_modem(struct si_port *, enum si_mctl, int);
static void si_write_enable(struct si_port *, int);
static int si_Sioctl(cdev_t, u_long, caddr_t, int, struct ucred *);
static void si_start(struct tty *);
static void si_stop(struct tty *, int);
static timeout_t si_lstart;
static void si_disc_optim(struct tty *tp, struct termios *t,struct si_port *pp);
static void sihardclose(struct si_port *pp);
static void sidtrwakeup(void *chan);

#ifdef SI_DEBUG
static char	*si_mctl2str(enum si_mctl cmd);
#endif

static int	siparam(struct tty *, struct termios *);

static void	si_modem_state(struct si_port *pp, struct tty *tp, int hi_ip);
static char *	si_modulename(int host_type, int uart_type);

static	d_open_t	siopen;
static	d_close_t	siclose;
static	d_write_t	siwrite;
static	d_ioctl_t	siioctl;

static struct dev_ops si_ops = {
	{ "si", 0, D_TTY },
	.d_open =	siopen,
	.d_close =	siclose,
	.d_read =	ttyread,
	.d_write =	siwrite,
	.d_ioctl =	siioctl,
	.d_kqfilter =	ttykqfilter,
	.d_revoke =	ttyrevoke
};

static int si_Nports;
static int si_Nmodules;
static int si_debug = 0;	/* data, not bss, so it's patchable */

SYSCTL_INT(_machdep, OID_AUTO, si_debug, CTLFLAG_RW, &si_debug, 0, "");

static struct tty *si__tty;

static int si_numunits;
static struct callout poll_ch;

devclass_t si_devclass;

#ifndef B2000	/* not standard, but the hardware knows it. */
# define B2000 2000
#endif
static struct speedtab bdrates[] = {
	{ B75,		CLK75, },	/* 0x0 */
	{ B110,		CLK110, },	/* 0x1 */
	{ B150,		CLK150, },	/* 0x3 */
	{ B300,		CLK300, },	/* 0x4 */
	{ B600,		CLK600, },	/* 0x5 */
	{ B1200,	CLK1200, },	/* 0x6 */
	{ B2000,	CLK2000, },	/* 0x7 */
	{ B2400,	CLK2400, },	/* 0x8 */
	{ B4800,	CLK4800, },	/* 0x9 */
	{ B9600,	CLK9600, },	/* 0xb */
	{ B19200,	CLK19200, },	/* 0xc */
	{ B38400,	CLK38400, },	/* 0x2 (out of order!) */
	{ B57600,	CLK57600, },	/* 0xd */
	{ B115200,	CLK110, },	/* 0x1 (dupe!, 110 baud on "si") */
	{ -1,		-1 },
};


/* populated with approx character/sec rates - translated at card
 * initialisation time to chars per tick of the clock */
static int done_chartimes = 0;
static struct speedtab chartimes[] = {
	{ B75,		8, },
	{ B110,		11, },
	{ B150,		15, },
	{ B300,		30, },
	{ B600,		60, },
	{ B1200,	120, },
	{ B2000,	200, },
	{ B2400,	240, },
	{ B4800,	480, },
	{ B9600,	960, },
	{ B19200,	1920, },
	{ B38400,	3840, },
	{ B57600,	5760, },
	{ B115200,	11520, },
	{ -1,		-1 },
};
static volatile int in_intr = 0;	/* Inside interrupt handler? */

#ifdef POLL
static int si_pollrate;			/* in addition to irq */
static int si_realpoll = 0;		/* poll HW on timer */

SYSCTL_INT(_machdep, OID_AUTO, si_pollrate, CTLFLAG_RW, &si_pollrate, 0, "");
SYSCTL_INT(_machdep, OID_AUTO, si_realpoll, CTLFLAG_RW, &si_realpoll, 0, "");

static int init_finished = 0;
static void si_poll(void *);
#endif

/*
 * Array of adapter types and the corresponding RAM size. The order of
 * entries here MUST match the ordinal of the adapter type.
 */
static char *si_type[] = {
	"EMPTY",
	"SIHOST",
	"SIMCA",		/* FreeBSD does not support Microchannel */
	"SIHOST2",
	"SIEISA",
	"SIPCI",
	"SXPCI",
	"SXISA",
};

/*
 * We have to make an 8 bit version of bcopy, since some cards can't
 * deal with 32 bit I/O
 */
static __inline void
si_bcopy(const void *src, void *dst, size_t len)
{
	const uint8_t *src_byte = (const uint8_t *)src;
	uint8_t *dst_byte = (uint8_t *)dst;

	while (len--)
		*dst_byte++ = *src_byte++;
}

static __inline void
si_vbcopy(const volatile void *src, void *dst, size_t len)
{
	volatile const uint8_t *src_byte = (volatile const uint8_t *)src;
	uint8_t *dst_byte = (uint8_t *)dst;

	while (len--)
		*dst_byte++ = *src_byte++;
}

static __inline void
si_bcopyv(const void *src, volatile void *dst, size_t len)
{
	const uint8_t *src_byte = (const uint8_t *)src;
	volatile uint8_t *dst_byte = (volatile uint8_t *)dst;

	while (len--)
		*dst_byte++ = *src_byte++;
}

/*
 * Attach the device.  Initialize the card.
 */
int
siattach(device_t dev)
{
	int unit;
	struct si_softc *sc;
	struct si_port *pp;
	volatile struct si_channel *ccbp;
	volatile struct si_reg *regp;
	volatile caddr_t maddr;
	struct si_module *modp;
	struct tty *tp;
	struct speedtab *spt;
	int nmodule, nport, x, y;
	int uart_type;
	int n;

	if ((poll_ch.c_flags & CALLOUT_DID_INIT) == 0)
		callout_init_mp(&poll_ch);

	lwkt_gettoken(&tty_token);
	sc = device_get_softc(dev);
	unit = device_get_unit(dev);

	sc->sc_typename = si_type[sc->sc_type];
	if (si_numunits < unit + 1)
		si_numunits = unit + 1;

	DPRINT((0, DBG_AUTOBOOT, "si%d: siattach\n", unit));

#ifdef POLL
	if (si_pollrate == 0) {
		si_pollrate = POLLHZ;		/* in addition to irq */
#ifdef REALPOLL
		si_realpoll = 1;		/* scan always */
#endif
	}
#endif

	DPRINT((0, DBG_AUTOBOOT, "si%d: type: %s paddr: %p maddr: %p\n", unit,
		sc->sc_typename, sc->sc_paddr, sc->sc_maddr));

	sc->sc_ports = NULL;			/* mark as uninitialised */

	maddr = sc->sc_maddr;

	/* Stop the CPU first so it won't stomp around while we load */

	switch (sc->sc_type) {
		case SIEISA:
			outb(sc->sc_iobase + 2, sc->sc_irq << 4);
		break;
		case SIPCI:
			*(maddr+SIPCIRESET) = 0;
		break;
		case SIJETPCI: /* fall through to JET ISA */
		case SIJETISA:
			*(maddr+SIJETCONFIG) = 0;
		break;
		case SIHOST2:
			*(maddr+SIPLRESET) = 0;
		break;
		case SIHOST:
			*(maddr+SIRESET) = 0;
		break;
		default: /* this should never happen */
			kprintf("si%d: unsupported configuration\n", unit);
			lwkt_reltoken(&tty_token);
			return EINVAL;
		break;
	}

	/* OK, now lets download the download code */

	if (SI_ISJET(sc->sc_type)) {
		DPRINT((0, DBG_DOWNLOAD, "si%d: jet_download: nbytes %d\n",
			unit, si3_t225_dsize));
		si_bcopy(si3_t225_download, maddr + si3_t225_downloadaddr,
			si3_t225_dsize);
		DPRINT((0, DBG_DOWNLOAD,
			"si%d: jet_bootstrap: nbytes %d -> %x\n",
			unit, si3_t225_bsize, si3_t225_bootloadaddr));
		si_bcopy(si3_t225_bootstrap, maddr + si3_t225_bootloadaddr,
			si3_t225_bsize);
	} else {
		DPRINT((0, DBG_DOWNLOAD, "si%d: si_download: nbytes %d\n",
			unit, si2_z280_dsize));
		si_bcopy(si2_z280_download, maddr + si2_z280_downloadaddr,
			si2_z280_dsize);
	}

	/* Now start the CPU */

	switch (sc->sc_type) {
	case SIEISA:
		/* modify the download code to tell it that it's on an EISA */
		*(maddr + 0x42) = 1;
		outb(sc->sc_iobase + 2, (sc->sc_irq << 4) | 4);
		(void)inb(sc->sc_iobase + 3); /* reset interrupt */
		break;
	case SIPCI:
		/* modify the download code to tell it that it's on a PCI */
		*(maddr+0x42) = 1;
		*(maddr+SIPCIRESET) = 1;
		*(maddr+SIPCIINTCL) = 0;
		break;
	case SIJETPCI:
		*(maddr+SIJETRESET) = 0;
		*(maddr+SIJETCONFIG) = SIJETBUSEN|SIJETIRQEN;
		break;
	case SIJETISA:
		*(maddr+SIJETRESET) = 0;
		switch (sc->sc_irq) {
		case 9:
			*(maddr+SIJETCONFIG) = SIJETBUSEN|SIJETIRQEN|0x90;
			break;
		case 10:
			*(maddr+SIJETCONFIG) = SIJETBUSEN|SIJETIRQEN|0xa0;
			break;
		case 11:
			*(maddr+SIJETCONFIG) = SIJETBUSEN|SIJETIRQEN|0xb0;
			break;
		case 12:
			*(maddr+SIJETCONFIG) = SIJETBUSEN|SIJETIRQEN|0xc0;
			break;
		case 15:
			*(maddr+SIJETCONFIG) = SIJETBUSEN|SIJETIRQEN|0xf0;
			break;
		}
		break;
	case SIHOST:
		*(maddr+SIRESET_CL) = 0;
		*(maddr+SIINTCL_CL) = 0;
		break;
	case SIHOST2:
		*(maddr+SIPLRESET) = 0x10;
		switch (sc->sc_irq) {
		case 11:
			*(maddr+SIPLIRQ11) = 0x10;
			break;
		case 12:
			*(maddr+SIPLIRQ12) = 0x10;
			break;
		case 15:
			*(maddr+SIPLIRQ15) = 0x10;
			break;
		}
		*(maddr+SIPLIRQCLR) = 0x10;
		break;
	default: /* this should _REALLY_ never happen */
		kprintf("si%d: Uh, it was supported a second ago...\n", unit);
		lwkt_reltoken(&tty_token);
		return EINVAL;
	}

	DELAY(1000000);			/* wait around for a second */

	regp = (struct si_reg *)maddr;
	y = 0;
					/* wait max of 5 sec for init OK */
	while (regp->initstat == 0 && y++ < 10) {
		DELAY(500000);
	}
	switch (regp->initstat) {
	case 0:
		kprintf("si%d: startup timeout - aborting\n", unit);
		sc->sc_type = SIEMPTY;
		lwkt_reltoken(&tty_token);
		return EINVAL;
	case 1:
		if (SI_ISJET(sc->sc_type)) {
			/* set throttle to 100 times per second */
			regp->int_count = JET_INT_COUNT;
			/* rx_intr_count is a NOP in Jet */
		} else {
			/* set throttle to 125 times per second */
			regp->int_count = INT_COUNT;
			/* rx intr max of 25 times per second */
			regp->rx_int_count = RXINT_COUNT;
		}
		regp->int_pending = 0;		/* no intr pending */
		regp->int_scounter = 0;	/* reset counter */
		break;
	case 0xff:
		/*
		 * No modules found, so give up on this one.
		 */
		kprintf("si%d: %s - no ports found\n", unit,
			si_type[sc->sc_type]);
		lwkt_reltoken(&tty_token);
		return 0;
	default:
		kprintf("si%d: download code version error - initstat %x\n",
			unit, regp->initstat);
		lwkt_reltoken(&tty_token);
		return EINVAL;
	}

	/*
	 * First time around the ports just count them in order
	 * to allocate some memory.
	 */
	nport = 0;
	modp = (struct si_module *)(maddr + 0x80);
	for (;;) {
		DPRINT((0, DBG_DOWNLOAD, "si%d: ccb addr %p\n", unit, modp));
		switch (modp->sm_type) {
		case TA4:
			DPRINT((0, DBG_DOWNLOAD,
				"si%d: Found old TA4 module, 4 ports\n",
				unit));
			x = 4;
			break;
		case TA8:
			DPRINT((0, DBG_DOWNLOAD,
				"si%d: Found old TA8 module, 8 ports\n",
				unit));
			x = 8;
			break;
		case TA4_ASIC:
			DPRINT((0, DBG_DOWNLOAD,
				"si%d: Found ASIC TA4 module, 4 ports\n",
				unit));
			x = 4;
			break;
		case TA8_ASIC:
			DPRINT((0, DBG_DOWNLOAD,
				"si%d: Found ASIC TA8 module, 8 ports\n",
				unit));
			x = 8;
			break;
		case MTA:
			DPRINT((0, DBG_DOWNLOAD,
				"si%d: Found CD1400 module, 8 ports\n",
				unit));
			x = 8;
			break;
		case SXDC:
			DPRINT((0, DBG_DOWNLOAD,
				"si%d: Found SXDC module, 8 ports\n",
				unit));
			x = 8;
			break;
		default:
			kprintf("si%d: unknown module type %d\n",
				unit, modp->sm_type);
			goto try_next;
		}

		/* this was limited in firmware and is also a driver issue */
		if ((nport + x) > SI_MAXPORTPERCARD) {
			kprintf("si%d: extra ports ignored\n", unit);
			goto try_next;
		}

		nport += x;
		si_Nports += x;
		si_Nmodules++;

try_next:
		if (modp->sm_next == 0)
			break;
		modp = (struct si_module *)
			(maddr + (unsigned)(modp->sm_next & 0x7fff));
	}
	sc->sc_ports = kmalloc(sizeof(struct si_port) * nport,
				M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_nport = nport;
	for (n = 0; n < nport; ++n) {
		callout_init_mp(&sc->sc_ports[n].lstart_ch);
		callout_init_mp(&sc->sc_ports[n].dtr_ch);
	}

	/*
	 * allocate tty structures for ports
	 */
	tp = kmalloc(sizeof(*tp) * nport, M_DEVBUF, M_WAITOK | M_ZERO);
	si__tty = tp;

	/*
	 * Scan round the ports again, this time initialising.
	 */
	pp = sc->sc_ports;
	nmodule = 0;
	modp = (struct si_module *)(maddr + 0x80);
	uart_type = 1000;	/* arbitary, > uchar_max */
	for (;;) {
		switch (modp->sm_type) {
		case TA4:
			nport = 4;
			break;
		case TA8:
			nport = 8;
			break;
		case TA4_ASIC:
			nport = 4;
			break;
		case TA8_ASIC:
			nport = 8;
			break;
		case MTA:
			nport = 8;
			break;
		case SXDC:
			nport = 8;
			break;
		default:
			goto try_next2;
		}
		nmodule++;
		ccbp = (struct si_channel *)((char *)modp + 0x100);
		if (uart_type == 1000)
			uart_type = ccbp->type;
		else if (uart_type != ccbp->type)
			kprintf("si%d: Warning: module %d mismatch! (%d%s != %d%s)\n",
			    unit, nmodule,
			    ccbp->type, si_modulename(sc->sc_type, ccbp->type),
			    uart_type, si_modulename(sc->sc_type, uart_type));

		for (x = 0; x < nport; x++, pp++, ccbp++) {
			pp->sp_ccb = ccbp;	/* save the address */
			pp->sp_tty = tp++;
			pp->sp_pend = IDLE_CLOSE;
			pp->sp_state = 0;	/* internal flag */
			pp->sp_dtr_wait = 3 * hz;
			pp->sp_iin.c_iflag = TTYDEF_IFLAG;
			pp->sp_iin.c_oflag = TTYDEF_OFLAG;
			pp->sp_iin.c_cflag = TTYDEF_CFLAG;
			pp->sp_iin.c_lflag = TTYDEF_LFLAG;
			termioschars(&pp->sp_iin);
			pp->sp_iin.c_ispeed = pp->sp_iin.c_ospeed =
				TTYDEF_SPEED;
			pp->sp_iout = pp->sp_iin;
		}
try_next2:
		if (modp->sm_next == 0) {
			kprintf("si%d: card: %s, ports: %d, modules: %d, type: %d%s\n",
				unit,
				sc->sc_typename,
				sc->sc_nport,
				nmodule,
				uart_type,
				si_modulename(sc->sc_type, uart_type));
			break;
		}
		modp = (struct si_module *)
			(maddr + (unsigned)(modp->sm_next & 0x7fff));
	}
	if (done_chartimes == 0) {
		for (spt = chartimes ; spt->sp_speed != -1; spt++) {
			if ((spt->sp_code /= hz) == 0)
				spt->sp_code = 1;
		}
		done_chartimes = 1;
	}

	for (x = 0; x < sc->sc_nport; x++) {
		/* sync with the manuals that start at 1 */
		y = x + 1 + unit * (1 << SI_CARDSHIFT);
		make_dev(&si_ops, x, 0, 0, 0600, "ttyA%02d", y);
		make_dev(&si_ops, x + 0x00080, 0, 0, 0600, "cuaA%02d", y);
		make_dev(&si_ops, x + 0x10000, 0, 0, 0600, "ttyiA%02d", y);
		make_dev(&si_ops, x + 0x10080, 0, 0, 0600, "cuaiA%02d", y);
		make_dev(&si_ops, x + 0x20000, 0, 0, 0600, "ttylA%02d", y);
		make_dev(&si_ops, x + 0x20080, 0, 0, 0600, "cualA%02d", y);
	}
	make_dev(&si_ops, 0x40000, 0, 0, 0600, "si_control");
	lwkt_reltoken(&tty_token);
	return (0);
}

static	int
siopen(struct dev_open_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	int error;
	int card, port;
	struct si_softc *sc;
	struct tty *tp;
	volatile struct si_channel *ccbp;
	struct si_port *pp;
	int mynor = minor(dev);

	lwkt_gettoken(&tty_token);
	/* quickly let in /dev/si_control */
	if (IS_CONTROLDEV(mynor)) {
		if ((error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0))) {
			lwkt_reltoken(&tty_token);
			return(error);
		}
		lwkt_reltoken(&tty_token);
		return(0);
	}

	card = SI_CARD(mynor);
	sc = devclass_get_softc(si_devclass, card);
	if (sc == NULL) {
		lwkt_reltoken(&tty_token);
		return (ENXIO);
	}

	if (sc->sc_type == SIEMPTY) {
		DPRINT((0, DBG_OPEN|DBG_FAIL, "si%d: type %s??\n",
			card, sc->sc_typename));
		lwkt_reltoken(&tty_token);
		return(ENXIO);
	}

	port = SI_PORT(mynor);
	if (port >= sc->sc_nport) {
		DPRINT((0, DBG_OPEN|DBG_FAIL, "si%d: nports %d\n",
			card, sc->sc_nport));
		lwkt_reltoken(&tty_token);
		return(ENXIO);
	}

#ifdef	POLL
	/*
	 * We've now got a device, so start the poller.
	 */
	if (init_finished == 0) {
		callout_reset(&poll_ch, si_pollrate, si_poll, NULL);
		init_finished = 1;
	}
#endif

	/* initial/lock device */
	if (IS_STATE(mynor)) {
		lwkt_reltoken(&tty_token);
		return(0);
	}

	pp = sc->sc_ports + port;
	tp = pp->sp_tty;			/* the "real" tty */
	dev->si_tty = tp;
	ccbp = pp->sp_ccb;			/* Find control block */
	DPRINT((pp, DBG_ENTRY|DBG_OPEN, "siopen(%s,%x,%x)\n",
		devtoname(dev), ap->a_oflags, ap->a_devtype));

	crit_enter();			/* Keep others out */
	error = 0;

open_top:
	while (pp->sp_state & SS_DTR_OFF) {
		error = tsleep(&pp->sp_dtr_wait, PCATCH, "sidtr", 0);
		if (error != 0)
			goto out;
	}

	if (tp->t_state & TS_ISOPEN) {
		/*
		 * The device is open, so everything has been initialised.
		 * handle conflicts.
		 */
		if (IS_CALLOUT(mynor)) {
			if (!pp->sp_active_out) {
				error = EBUSY;
				goto out;
			}
		} else {
			if (pp->sp_active_out) {
				if (ap->a_oflags & O_NONBLOCK) {
					error = EBUSY;
					goto out;
				}
				error = tsleep(&pp->sp_active_out,
						PCATCH, "sibi", 0);
				if (error != 0)
					goto out;
				goto open_top;
			}
		}
		if (tp->t_state & TS_XCLUDE &&
		    priv_check_cred(ap->a_cred, PRIV_ROOT, 0)) {
			DPRINT((pp, DBG_OPEN|DBG_FAIL,
				"already open and EXCLUSIVE set\n"));
			error = EBUSY;
			goto out;
		}
	} else {
		/*
		 * The device isn't open, so there are no conflicts.
		 * Initialize it. Avoid sleep... :-)
		 */
		DPRINT((pp, DBG_OPEN, "first open\n"));
		tp->t_oproc = si_start;
		tp->t_stop = si_stop;
		tp->t_param = siparam;
		tp->t_dev = dev;
		tp->t_termios = mynor & SI_CALLOUT_MASK
				? pp->sp_iout : pp->sp_iin;

		(void) si_modem(pp, SET, TIOCM_DTR|TIOCM_RTS);

		++pp->sp_wopeners;	/* in case of sleep in siparam */

		error = siparam(tp, &tp->t_termios);

		--pp->sp_wopeners;
		if (error != 0)
			goto out;
		/* XXX: we should goto_top if siparam slept */

		/* set initial DCD state */
		pp->sp_last_hi_ip = ccbp->hi_ip;
		if ((pp->sp_last_hi_ip & IP_DCD) || IS_CALLOUT(mynor)) {
			(*linesw[tp->t_line].l_modem)(tp, 1);
		}
	}

	/* whoops! we beat the close! */
	if (pp->sp_state & SS_CLOSING) {
		/* try and stop it from proceeding to bash the hardware */
		pp->sp_state &= ~SS_CLOSING;
	}

	/*
	 * Wait for DCD if necessary
	 */
	if (!(tp->t_state & TS_CARR_ON) &&
	    !IS_CALLOUT(mynor) &&
	    !(tp->t_cflag & CLOCAL) &&
	    !(ap->a_oflags & O_NONBLOCK)) {
		++pp->sp_wopeners;
		DPRINT((pp, DBG_OPEN, "sleeping for carrier\n"));
		error = tsleep(TSA_CARR_ON(tp), PCATCH, "sidcd", 0);
		--pp->sp_wopeners;
		if (error != 0)
			goto out;
		goto open_top;
	}

	error = (*linesw[tp->t_line].l_open)(dev, tp);
	si_disc_optim(tp, &tp->t_termios, pp);
	if (tp->t_state & TS_ISOPEN && IS_CALLOUT(mynor))
		pp->sp_active_out = TRUE;

	pp->sp_state |= SS_OPEN;	/* made it! */

out:
	crit_exit();

	DPRINT((pp, DBG_OPEN, "leaving siopen\n"));

	if (!(tp->t_state & TS_ISOPEN) && pp->sp_wopeners == 0)
		sihardclose(pp);

	lwkt_reltoken(&tty_token);
	return(error);
}

static	int
siclose(struct dev_close_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct si_port *pp;
	struct tty *tp;
	int error = 0;
	int mynor = minor(dev);

	if (IS_SPECIAL(mynor))
		return(0);

	lwkt_gettoken(&tty_token);
	crit_enter();

	pp = MINOR2PP(mynor);
	tp = pp->sp_tty;

	DPRINT((pp, DBG_ENTRY|DBG_CLOSE, "siclose(%s,%x,%x) sp_state:%x\n",
		devtoname(dev), ap->a_fflag, ap->a_devtype, pp->sp_state));

	/* did we sleep and loose a race? */
	if (pp->sp_state & SS_CLOSING) {
		/* error = ESOMETING? */
		goto out;
	}

	/* begin race detection.. */
	pp->sp_state |= SS_CLOSING;

	si_write_enable(pp, 0);		/* block writes for ttywait() */

	/* THIS MAY SLEEP IN TTYWAIT!!! */
	(*linesw[tp->t_line].l_close)(tp, ap->a_fflag);

	si_write_enable(pp, 1);

	/* did we sleep and somebody started another open? */
	if (!(pp->sp_state & SS_CLOSING)) {
		/* error = ESOMETING? */
		goto out;
	}
	/* ok. we are now still on the right track.. nuke the hardware */

	if (pp->sp_state & SS_LSTART) {
		callout_stop(&pp->lstart_ch);
		pp->sp_state &= ~SS_LSTART;
	}

	si_stop(tp, FREAD | FWRITE);

	sihardclose(pp);
	ttyclose(tp);
	pp->sp_state &= ~SS_OPEN;

out:
	DPRINT((pp, DBG_CLOSE|DBG_EXIT, "close done, returning\n"));
	crit_exit();
	lwkt_reltoken(&tty_token);
	return(error);
}

static void
sihardclose(struct si_port *pp)
{
	struct tty *tp;
	volatile struct si_channel *ccbp;

	crit_enter();
	lwkt_gettoken(&tty_token);

	tp = pp->sp_tty;
	ccbp = pp->sp_ccb;			/* Find control block */
	if (tp->t_cflag & HUPCL ||
	    (!pp->sp_active_out &&
	     !(ccbp->hi_ip & IP_DCD) &&
	     !(pp->sp_iin.c_cflag & CLOCAL)) ||
	    !(tp->t_state & TS_ISOPEN)) {

		(void) si_modem(pp, BIC, TIOCM_DTR|TIOCM_RTS);
		(void) si_command(pp, FCLOSE, SI_NOWAIT);

		if (pp->sp_dtr_wait != 0) {
			callout_reset(&pp->dtr_ch, pp->sp_dtr_wait,
					sidtrwakeup, pp);
			pp->sp_state |= SS_DTR_OFF;
		}

	}
	pp->sp_active_out = FALSE;
	wakeup((caddr_t)&pp->sp_active_out);
	wakeup(TSA_CARR_ON(tp));

	lwkt_reltoken(&tty_token);
	crit_exit();
}


/*
 * called at splsoftclock()...
 */
static void
sidtrwakeup(void *chan)
{
	struct si_port *pp;

	crit_enter();
	lwkt_gettoken(&tty_token);

	pp = (struct si_port *)chan;
	pp->sp_state &= ~SS_DTR_OFF;
	wakeup(&pp->sp_dtr_wait);

	lwkt_reltoken(&tty_token);
	crit_exit();
}

static	int
siwrite(struct dev_write_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	struct si_port *pp;
	struct tty *tp;
	int error = 0;
	int mynor = minor(dev);

	lwkt_gettoken(&tty_token);
	if (IS_SPECIAL(mynor)) {
		DPRINT((0, DBG_ENTRY|DBG_FAIL|DBG_WRITE, "siwrite(CONTROLDEV!!)\n"));
		lwkt_reltoken(&tty_token);
		return(ENODEV);
	}
	pp = MINOR2PP(mynor);
	tp = pp->sp_tty;
	DPRINT((pp, DBG_WRITE, "siwrite(%s,%p,%x)\n", devtoname(dev), ap->a_uio, ap->a_ioflag));

	crit_enter();
	/*
	 * If writes are currently blocked, wait on the "real" tty
	 */
	while (pp->sp_state & SS_BLOCKWRITE) {
		pp->sp_state |= SS_WAITWRITE;
		DPRINT((pp, DBG_WRITE, "in siwrite, wait for SS_BLOCKWRITE to clear\n"));
		if ((error = ttysleep(tp, (caddr_t)pp, PCATCH,
				     "siwrite", tp->t_timeout))) {
			if (error == EWOULDBLOCK)
				error = EIO;
			goto out;
		}
	}

	error = (*linesw[tp->t_line].l_write)(tp, ap->a_uio, ap->a_ioflag);
out:
	crit_exit();
	lwkt_reltoken(&tty_token);
	return (error);
}


static	int
siioctl(struct dev_ioctl_args *ap)
{
	cdev_t dev = ap->a_head.a_dev;
	caddr_t data = ap->a_data;
	u_long cmd = ap->a_cmd;
	struct si_port *pp;
	struct tty *tp;
	int error;
	int mynor = minor(dev);
	int blocked = 0;
#if defined(COMPAT_43)
	u_long oldcmd;
	struct termios term;
#endif

	if (IS_SI_IOCTL(cmd))
		return(si_Sioctl(dev, cmd, data, ap->a_fflag, ap->a_cred));

	lwkt_gettoken(&tty_token);
	pp = MINOR2PP(mynor);
	tp = pp->sp_tty;

	DPRINT((pp, DBG_ENTRY|DBG_IOCTL, "siioctl(%s,%lx,%p,%x)\n",
		devtoname(dev), cmd, data, ap->a_fflag));
	if (IS_STATE(mynor)) {
		struct termios *ct;

		switch (mynor & SI_STATE_MASK) {
		case SI_INIT_STATE_MASK:
			ct = IS_CALLOUT(mynor) ? &pp->sp_iout : &pp->sp_iin;
			break;
		case SI_LOCK_STATE_MASK:
			ct = IS_CALLOUT(mynor) ? &pp->sp_lout : &pp->sp_lin;
			break;
		default:
			lwkt_reltoken(&tty_token);
			return (ENODEV);
		}
		switch (cmd) {
		case TIOCSETA:
			error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0);
			if (error != 0) {
				lwkt_reltoken(&tty_token);
				return (error);
			}
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
	/*
	 * Do the old-style ioctl compat routines...
	 */
#if defined(COMPAT_43)
	term = tp->t_termios;
	oldcmd = cmd;
	error = ttsetcompat(tp, &cmd, data, &term);
	if (error != 0) {
		lwkt_reltoken(&tty_token);
		return (error);
	}
	if (cmd != oldcmd)
		data = (caddr_t)&term;
#endif
	/*
	 * Do the initial / lock state business
	 */
	if (cmd == TIOCSETA || cmd == TIOCSETAW || cmd == TIOCSETAF) {
		int     cc;
		struct termios *dt = (struct termios *)data;
		struct termios *lt = mynor & SI_CALLOUT_MASK
				     ? &pp->sp_lout : &pp->sp_lin;

		dt->c_iflag = (tp->t_iflag & lt->c_iflag) |
			(dt->c_iflag & ~lt->c_iflag);
		dt->c_oflag = (tp->t_oflag & lt->c_oflag) |
			(dt->c_oflag & ~lt->c_oflag);
		dt->c_cflag = (tp->t_cflag & lt->c_cflag) |
			(dt->c_cflag & ~lt->c_cflag);
		dt->c_lflag = (tp->t_lflag & lt->c_lflag) |
			(dt->c_lflag & ~lt->c_lflag);
		for (cc = 0; cc < NCCS; ++cc)
			if (lt->c_cc[cc] != 0)
				dt->c_cc[cc] = tp->t_cc[cc];
		if (lt->c_ispeed != 0)
			dt->c_ispeed = tp->t_ispeed;
		if (lt->c_ospeed != 0)
			dt->c_ospeed = tp->t_ospeed;
	}

	/*
	 * Block user-level writes to give the ttywait()
	 * a chance to completely drain for commands
	 * that require the port to be in a quiescent state.
	 */
	switch (cmd) {
	case TIOCSETAW:
	case TIOCSETAF:
	case TIOCDRAIN:
#ifdef COMPAT_43
	case TIOCSETP:
#endif
		blocked++;	/* block writes for ttywait() and siparam() */
		si_write_enable(pp, 0);
	}

	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data,
					      ap->a_fflag, ap->a_cred);
	if (error != ENOIOCTL)
		goto out;

	crit_enter();

	error = ttioctl(tp, cmd, data, ap->a_fflag);
	si_disc_optim(tp, &tp->t_termios, pp);
	if (error != ENOIOCTL) {
		crit_exit();
		goto out;
	}

	error = 0;
	switch (cmd) {
	case TIOCSBRK:
		si_command(pp, SBREAK, SI_WAIT);
		break;
	case TIOCCBRK:
		si_command(pp, EBREAK, SI_WAIT);
		break;
	case TIOCSDTR:
		(void) si_modem(pp, SET, TIOCM_DTR|TIOCM_RTS);
		break;
	case TIOCCDTR:
		(void) si_modem(pp, SET, 0);
		break;
	case TIOCMSET:
		(void) si_modem(pp, SET, *(int *)data);
		break;
	case TIOCMBIS:
		(void) si_modem(pp, BIS, *(int *)data);
		break;
	case TIOCMBIC:
		(void) si_modem(pp, BIC, *(int *)data);
		break;
	case TIOCMGET:
		*(int *)data = si_modem(pp, GET, 0);
		break;
	case TIOCMSDTRWAIT:
		/* must be root since the wait applies to following logins */
		error = priv_check_cred(ap->a_cred, PRIV_ROOT, 0);
		if (error == 0)
			pp->sp_dtr_wait = *(int *)data * hz / 100;
		break;
	case TIOCMGDTRWAIT:
		*(int *)data = pp->sp_dtr_wait * 100 / hz;
		break;
	default:
		error = ENOTTY;
	}
	crit_exit();

out:
	DPRINT((pp, DBG_IOCTL|DBG_EXIT, "siioctl ret %d\n", error));
	if (blocked)
		si_write_enable(pp, 1);
	lwkt_reltoken(&tty_token);
	return(error);
}

/*
 * Handle the Specialix ioctls. All MUST be called via the CONTROL device
 */
static int
si_Sioctl(cdev_t dev, u_long cmd, caddr_t data, int flag, struct ucred *cred)
{
	struct si_softc *xsc;
	struct si_port *xpp;
	volatile struct si_reg *regp;
	struct si_tcsi *dp;
	struct si_pstat *sps;
	int *ip, error = 0;
	int card, port;
	int mynor = minor(dev);

	DPRINT((0, DBG_ENTRY|DBG_IOCTL, "si_Sioctl(%s,%lx,%p,%x)\n",
		devtoname(dev), cmd, data, flag));

#if 1
	DPRINT((0, DBG_IOCTL, "TCSI_PORT=%lx\n", TCSI_PORT));
	DPRINT((0, DBG_IOCTL, "TCSI_CCB=%lx\n", TCSI_CCB));
	DPRINT((0, DBG_IOCTL, "TCSI_TTY=%lx\n", TCSI_TTY));
#endif

	if (!IS_CONTROLDEV(mynor)) {
		DPRINT((0, DBG_IOCTL|DBG_FAIL, "not called from control device!\n"));
		return(ENODEV);
	}

	crit_enter();	/* better safe than sorry */
	lwkt_gettoken(&tty_token);

	ip = (int *)data;

#define SUCHECK if ((error = priv_check_cred(cred, PRIV_ROOT, 0))) goto out

	switch (cmd) {
	case TCSIPORTS:
		*ip = si_Nports;
		goto out;
	case TCSIMODULES:
		*ip = si_Nmodules;
		goto out;
	case TCSISDBG_ALL:
		SUCHECK;
		si_debug = *ip;
		goto out;
	case TCSIGDBG_ALL:
		*ip = si_debug;
		goto out;
	default:
		/*
		 * Check that a controller for this port exists
		 */

		/* may also be a struct si_pstat, a superset of si_tcsi */

		dp = (struct si_tcsi *)data;
		sps = (struct si_pstat *)data;
		card = dp->tc_card;
		xsc = devclass_get_softc(si_devclass, card);	/* check.. */
		if (xsc == NULL || xsc->sc_type == SIEMPTY) {
			error = ENOENT;
			goto out;
		}
		/*
		 * And check that a port exists
		 */
		port = dp->tc_port;
		if (port < 0 || port >= xsc->sc_nport) {
			error = ENOENT;
			goto out;
		}
		xpp = xsc->sc_ports + port;
		regp = (struct si_reg *)xsc->sc_maddr;
	}

	switch (cmd) {
	case TCSIDEBUG:
#ifdef	SI_DEBUG
		SUCHECK;
		if (xpp->sp_debug)
			xpp->sp_debug = 0;
		else {
			xpp->sp_debug = DBG_ALL;
			DPRINT((xpp, DBG_IOCTL, "debug toggled %s\n",
				(xpp->sp_debug&DBG_ALL)?"ON":"OFF"));
		}
		break;
#else
		error = ENODEV;
		goto out;
#endif
	case TCSISDBG_LEVEL:
	case TCSIGDBG_LEVEL:
#ifdef	SI_DEBUG
		if (cmd == TCSIGDBG_LEVEL) {
			dp->tc_dbglvl = xpp->sp_debug;
		} else {
			SUCHECK;
			xpp->sp_debug = dp->tc_dbglvl;
		}
		break;
#else
		error = ENODEV;
		goto out;
#endif
	case TCSIGRXIT:
		dp->tc_int = regp->rx_int_count;
		break;
	case TCSIRXIT:
		SUCHECK;
		regp->rx_int_count = dp->tc_int;
		break;
	case TCSIGIT:
		dp->tc_int = regp->int_count;
		break;
	case TCSIIT:
		SUCHECK;
		regp->int_count = dp->tc_int;
		break;
	case TCSISTATE:
		dp->tc_int = xpp->sp_ccb->hi_ip;
		break;
	/* these next three use a different structure */
	case TCSI_PORT:
		SUCHECK;
		si_bcopy(xpp, &sps->tc_siport, sizeof(sps->tc_siport));
		break;
	case TCSI_CCB:
		SUCHECK;
		si_vbcopy(xpp->sp_ccb, &sps->tc_ccb, sizeof(sps->tc_ccb));
		break;
	case TCSI_TTY:
		SUCHECK;
		si_bcopy(xpp->sp_tty, &sps->tc_tty, sizeof(sps->tc_tty));
		break;
	default:
		error = EINVAL;
		goto out;
	}
out:
	lwkt_reltoken(&tty_token);
	crit_exit();
	return(error);		/* success */
}

/*
 *	siparam()	: Configure line params
 *	called at spltty();
 *	this may sleep, does not flush, nor wait for drain, nor block writes
 *	caller must arrange this if it's important..
 */
static int
siparam(struct tty *tp, struct termios *t)
{
	struct si_port *pp = TP2PP(tp);
	volatile struct si_channel *ccbp;
	int cflag, iflag, oflag, lflag;
	int error = 0;		/* shutup gcc */
	int ispeed = 0;		/* shutup gcc */
	int ospeed = 0;		/* shutup gcc */
	BYTE val;

	lwkt_gettoken(&tty_token);
	DPRINT((pp, DBG_ENTRY|DBG_PARAM, "siparam(%p,%p)\n", tp, t));
	cflag = t->c_cflag;
	iflag = t->c_iflag;
	oflag = t->c_oflag;
	lflag = t->c_lflag;
	DPRINT((pp, DBG_PARAM, "OFLAG 0x%x CFLAG 0x%x IFLAG 0x%x LFLAG 0x%x\n",
		oflag, cflag, iflag, lflag));

	/* XXX - if Jet host and SXDC module, use extended baud rates */

	/* if not hung up.. */
	if (t->c_ospeed != 0) {
		/* translate baud rate to firmware values */
		ospeed = ttspeedtab(t->c_ospeed, bdrates);
		ispeed = t->c_ispeed ?
			 ttspeedtab(t->c_ispeed, bdrates) : ospeed;

		/* enforce legit baud rate */
		if (ospeed < 0 || ispeed < 0) {
			lwkt_reltoken(&tty_token);
			return (EINVAL);
		}
	}

	crit_enter();

	ccbp = pp->sp_ccb;

	/* ========== set hi_break ========== */
	val = 0;
	if (iflag & IGNBRK)		/* Breaks */
		val |= BR_IGN;
	if (iflag & BRKINT)		/* Interrupt on break? */
		val |= BR_INT;
	if (iflag & PARMRK)		/* Parity mark? */
		val |= BR_PARMRK;
	if (iflag & IGNPAR)		/* Ignore chars with parity errors? */
		val |= BR_PARIGN;
	ccbp->hi_break = val;

	/* ========== set hi_csr ========== */
	/* if not hung up.. */
	if (t->c_ospeed != 0) {
		/* Set I/O speeds */
		 val = (ispeed << 4) | ospeed;
	}
	ccbp->hi_csr = val;

	/* ========== set hi_mr2 ========== */
	val = 0;
	if (cflag & CSTOPB)				/* Stop bits */
		val |= MR2_2_STOP;
	else
		val |= MR2_1_STOP;
	/*
	 * Enable H/W RTS/CTS handshaking. The default TA/MTA is
	 * a DCE, hence the reverse sense of RTS and CTS
	 */
	/* Output Flow - RTS must be raised before data can be sent */
	if (cflag & CCTS_OFLOW)
		val |= MR2_RTSCONT;

	ccbp->hi_mr2 = val;

	/* ========== set hi_mr1 ========== */
	val = 0;
	if (!(cflag & PARENB))				/* Parity */
		val |= MR1_NONE;
	else
		val |= MR1_WITH;
	if (cflag & PARODD)
		val |= MR1_ODD;

	if ((cflag & CS8) == CS8) {			/* 8 data bits? */
		val |= MR1_8_BITS;
	} else if ((cflag & CS7) == CS7) {		/* 7 data bits? */
		val |= MR1_7_BITS;
	} else if ((cflag & CS6) == CS6) {		/* 6 data bits? */
		val |= MR1_6_BITS;
	} else {					/* Must be 5 */
		val |= MR1_5_BITS;
	}
	/*
	 * Enable H/W RTS/CTS handshaking. The default TA/MTA is
	 * a DCE, hence the reverse sense of RTS and CTS
	 */
	/* Input Flow - CTS is raised when port is ready to receive data */
	if (cflag & CRTS_IFLOW)
		val |= MR1_CTSCONT;

	ccbp->hi_mr1 = val;

	/* ========== set hi_mask ========== */
	val = 0xff;
	if ((cflag & CS8) == CS8) {			/* 8 data bits? */
		val &= 0xFF;
	} else if ((cflag & CS7) == CS7) {		/* 7 data bits? */
		val &= 0x7F;
	} else if ((cflag & CS6) == CS6) {		/* 6 data bits? */
		val &= 0x3F;
	} else {					/* Must be 5 */
		val &= 0x1F;
	}
	if (iflag & ISTRIP)
		val &= 0x7F;

	ccbp->hi_mask = val;

	/* ========== set hi_prtcl ========== */
	val = SP_DCEN;		/* Monitor DCD always, or TIOCMGET misses it */
	if (iflag & IXANY)
		val |= SP_TANY;
	if (iflag & IXON)
		val |= SP_TXEN;
	if (iflag & IXOFF)
		val |= SP_RXEN;
	if (iflag & INPCK)
		val |= SP_PAEN;

	ccbp->hi_prtcl = val;


	/* ========== set hi_{rx|tx}{on|off} ========== */
	/* XXX: the card TOTALLY shields us from the flow control... */
	ccbp->hi_txon = t->c_cc[VSTART];
	ccbp->hi_txoff = t->c_cc[VSTOP];

	ccbp->hi_rxon = t->c_cc[VSTART];
	ccbp->hi_rxoff = t->c_cc[VSTOP];

	/* ========== send settings to the card ========== */
	/* potential sleep here */
	if (ccbp->hi_stat == IDLE_CLOSE)		/* Not yet open */
		si_command(pp, LOPEN, SI_WAIT);		/* open it */
	else
		si_command(pp, CONFIG, SI_WAIT);	/* change params */

	/* ========== set DTR etc ========== */
	/* Hangup if ospeed == 0 */
	if (t->c_ospeed == 0) {
		(void) si_modem(pp, BIC, TIOCM_DTR|TIOCM_RTS);
	} else {
		/*
		 * If the previous speed was 0, may need to re-enable
		 * the modem signals
		 */
		(void) si_modem(pp, SET, TIOCM_DTR|TIOCM_RTS);
	}

	DPRINT((pp, DBG_PARAM, "siparam, complete: MR1 %x MR2 %x HI_MASK %x PRTCL %x HI_BREAK %x\n",
		ccbp->hi_mr1, ccbp->hi_mr2, ccbp->hi_mask, ccbp->hi_prtcl, ccbp->hi_break));

	crit_exit();
	lwkt_reltoken(&tty_token);
	return(error);
}

/*
 * Enable or Disable the writes to this channel...
 * "state" ->  enabled = 1; disabled = 0;
 * NOTE: Must be called with tty_token held
 */
static void
si_write_enable(struct si_port *pp, int state)
{

	ASSERT_LWKT_TOKEN_HELD(&tty_token);
	crit_enter();

	if (state) {
		pp->sp_state &= ~SS_BLOCKWRITE;
		if (pp->sp_state & SS_WAITWRITE) {
			pp->sp_state &= ~SS_WAITWRITE;
			/* thunder away! */
			wakeup((caddr_t)pp);
		}
	} else {
		pp->sp_state |= SS_BLOCKWRITE;
	}

	crit_exit();
}

/*
 * Set/Get state of modem control lines.
 * Due to DCE-like behaviour of the adapter, some signals need translation:
 *	TIOCM_DTR	DSR
 *	TIOCM_RTS	CTS
 * NOTE: Must be called with tty_token held
 */
static int
si_modem(struct si_port *pp, enum si_mctl cmd, int bits)
{
	volatile struct si_channel *ccbp;
	int x;

	ASSERT_LWKT_TOKEN_HELD(&tty_token);
	DPRINT((pp, DBG_ENTRY|DBG_MODEM, "si_modem(%p,%s,%x)\n", pp, si_mctl2str(cmd), bits));
	ccbp = pp->sp_ccb;		/* Find channel address */
	switch (cmd) {
	case GET:
		x = ccbp->hi_ip;
		bits = TIOCM_LE;
		if (x & IP_DCD)		bits |= TIOCM_CAR;
		if (x & IP_DTR)		bits |= TIOCM_DTR;
		if (x & IP_RTS)		bits |= TIOCM_RTS;
		if (x & IP_RI)		bits |= TIOCM_RI;
		return(bits);
	case SET:
		ccbp->hi_op &= ~(OP_DSR|OP_CTS);
		/* fall through */
	case BIS:
		x = 0;
		if (bits & TIOCM_DTR)
			x |= OP_DSR;
		if (bits & TIOCM_RTS)
			x |= OP_CTS;
		ccbp->hi_op |= x;
		break;
	case BIC:
		if (bits & TIOCM_DTR)
			ccbp->hi_op &= ~OP_DSR;
		if (bits & TIOCM_RTS)
			ccbp->hi_op &= ~OP_CTS;
	}
	return 0;
}

/*
 * Handle change of modem state
 * NOTE: Must be called with tty_token held
 */
static void
si_modem_state(struct si_port *pp, struct tty *tp, int hi_ip)
{
	ASSERT_LWKT_TOKEN_HELD(&tty_token);
							/* if a modem dev */
	if (hi_ip & IP_DCD) {
		if (!(pp->sp_last_hi_ip & IP_DCD)) {
			DPRINT((pp, DBG_INTR, "modem carr on t_line %d\n",
				tp->t_line));
			(void)(*linesw[tp->t_line].l_modem)(tp, 1);
		}
	} else {
		if (pp->sp_last_hi_ip & IP_DCD) {
			DPRINT((pp, DBG_INTR, "modem carr off\n"));
			if ((*linesw[tp->t_line].l_modem)(tp, 0))
				(void) si_modem(pp, SET, 0);
		}
	}
	pp->sp_last_hi_ip = hi_ip;

}

/*
 * Poller to catch missed interrupts.
 *
 * Note that the SYSV Specialix drivers poll at 100 times per second to get
 * better response.  We could really use a "periodic" version timeout(). :-)
 */
#ifdef POLL
static void
si_poll(void *nothing)
{
	struct si_softc *sc;
	int i;
	volatile struct si_reg *regp;
	struct si_port *pp;
	int lost, port;

	DPRINT((0, DBG_POLL, "si_poll()\n"));
	crit_enter();
	lwkt_gettoken(&tty_token);
	if (in_intr)
		goto out;
	lost = 0;
	for (i = 0; i < si_numunits; i++) {
		sc = devclass_get_softc(si_devclass, i);
		if (sc == NULL || sc->sc_type == SIEMPTY)
			continue;
		regp = (struct si_reg *)sc->sc_maddr;

		/*
		 * See if there has been a pending interrupt for 2 seconds
		 * or so. The test (int_scounter >= 200) won't correspond
		 * to 2 seconds if int_count gets changed.
		 */
		if (regp->int_pending != 0) {
			if (regp->int_scounter >= 200 &&
			    regp->initstat == 1) {
				kprintf("si%d: lost intr\n", i);
				lost++;
			}
		} else {
			regp->int_scounter = 0;
		}

		/*
		 * gripe about no input flow control..
		 */
		pp = sc->sc_ports;
		for (port = 0; port < sc->sc_nport; pp++, port++) {
			if (pp->sp_delta_overflows > 0) {
				kprintf("si%d: %d tty level buffer overflows\n",
					i, pp->sp_delta_overflows);
				pp->sp_delta_overflows = 0;
			}
		}
	}
	if (lost || si_realpoll)
		si_intr(NULL);	/* call intr with fake vector */
out:
	lwkt_reltoken(&tty_token);
	crit_exit();

	callout_reset(&poll_ch, si_pollrate, si_poll, NULL);
}
#endif	/* ifdef POLL */

/*
 * The interrupt handler polls ALL ports on ALL adapters each time
 * it is called.
 */

static BYTE si_rxbuf[SI_BUFFERSIZE];	/* input staging area */
static BYTE si_txbuf[SI_BUFFERSIZE];	/* output staging area */

void
si_intr(void *arg)
{
	struct si_softc *sc;
	struct si_port *pp;
	volatile struct si_channel *ccbp;
	struct tty *tp;
	volatile caddr_t maddr;
	BYTE op, ip;
	int x, card, port, n, i, isopen;
	volatile BYTE *z;
	BYTE c;

	sc = arg;

	DPRINT((0, arg == NULL ? DBG_POLL:DBG_INTR, "si_intr\n"));
	if (in_intr)
		return;
	in_intr = 1;

	lwkt_gettoken(&tty_token);

	/*
	 * When we get an int we poll all the channels and do ALL pending
	 * work, not just the first one we find. This allows all cards to
	 * share the same vector.
	 *
	 * XXX - But if we're sharing the vector with something that's NOT
	 * a SI/XIO/SX card, we may be making more work for ourselves.
	 */
	for (card = 0; card < si_numunits; card++) {
		sc = devclass_get_softc(si_devclass, card);
		if (sc == NULL || sc->sc_type == SIEMPTY)
			continue;

		/*
		 * First, clear the interrupt
		 */
		switch(sc->sc_type) {
		case SIHOST:
			maddr = sc->sc_maddr;
			((volatile struct si_reg *)maddr)->int_pending = 0;
							/* flag nothing pending */
			*(maddr+SIINTCL) = 0x00;	/* Set IRQ clear */
			*(maddr+SIINTCL_CL) = 0x00;	/* Clear IRQ clear */
			break;
		case SIHOST2:
			maddr = sc->sc_maddr;
			((volatile struct si_reg *)maddr)->int_pending = 0;
			*(maddr+SIPLIRQCLR) = 0x00;
			*(maddr+SIPLIRQCLR) = 0x10;
			break;
		case SIPCI:
			maddr = sc->sc_maddr;
			((volatile struct si_reg *)maddr)->int_pending = 0;
			*(maddr+SIPCIINTCL) = 0x0;
			break;
		case SIJETPCI:	/* fall through to JETISA case */
		case SIJETISA:
			maddr = sc->sc_maddr;
			((volatile struct si_reg *)maddr)->int_pending = 0;
			*(maddr+SIJETINTCL) = 0x0;
			break;
		case SIEISA:
			maddr = sc->sc_maddr;
			((volatile struct si_reg *)maddr)->int_pending = 0;
			(void)inb(sc->sc_iobase + 3);
			break;
		case SIEMPTY:
		default:
			continue;
		}
		((volatile struct si_reg *)maddr)->int_scounter = 0;

		/*
		 * check each port
		 */
		for (pp = sc->sc_ports, port = 0; port < sc->sc_nport;
		     pp++, port++) {
			ccbp = pp->sp_ccb;
			tp = pp->sp_tty;

			/*
			 * See if a command has completed ?
			 */
			if (ccbp->hi_stat != pp->sp_pend) {
				DPRINT((pp, DBG_INTR,
					"si_intr hi_stat = 0x%x, pend = %d\n",
					ccbp->hi_stat, pp->sp_pend));
				switch(pp->sp_pend) {
				case LOPEN:
				case MPEND:
				case MOPEN:
				case CONFIG:
				case SBREAK:
				case EBREAK:
					pp->sp_pend = ccbp->hi_stat;
						/* sleeping in si_command */
					wakeup(&pp->sp_state);
					break;
				default:
					pp->sp_pend = ccbp->hi_stat;
				}
			}

			/*
			 * Continue on if it's closed
			 */
			if (ccbp->hi_stat == IDLE_CLOSE) {
				continue;
			}

			/*
			 * Do modem state change if not a local device
			 */
			si_modem_state(pp, tp, ccbp->hi_ip);

			/*
			 * Check to see if we should 'receive' characters.
			 */
			if (tp->t_state & TS_CONNECTED &&
			    tp->t_state & TS_ISOPEN)
				isopen = 1;
			else
				isopen = 0;

			/*
			 * Do input break processing
			 */
			if (ccbp->hi_state & ST_BREAK) {
				if (isopen) {
				    (*linesw[tp->t_line].l_rint)(TTY_BI, tp);
				}
				ccbp->hi_state &= ~ST_BREAK;   /* A Bit iffy this */
				DPRINT((pp, DBG_INTR, "si_intr break\n"));
			}

			/*
			 * Do RX stuff - if not open then dump any characters.
			 * XXX: This is VERY messy and needs to be cleaned up.
			 *
			 * XXX: can we leave data in the host adapter buffer
			 * when the clists are full?  That may be dangerous
			 * if the user cannot get an interrupt signal through.
			 */

	more_rx:	/* XXX Sorry. the nesting was driving me bats! :-( */

			if (!isopen) {
				ccbp->hi_rxopos = ccbp->hi_rxipos;
				goto end_rx;
			}

			/*
			 * If the tty input buffers are blocked, stop emptying
			 * the incoming buffers and let the auto flow control
			 * assert..
			 */
			if (tp->t_state & TS_TBLOCK) {
				goto end_rx;
			}

			/*
			 * Process read characters if not skipped above
			 */
			op = ccbp->hi_rxopos;
			ip = ccbp->hi_rxipos;
			c = ip - op;
			if (c == 0) {
				goto end_rx;
			}

			n = c & 0xff;
			if (n > 250)
				n = 250;

			DPRINT((pp, DBG_INTR, "n = %d, op = %d, ip = %d\n",
						n, op, ip));

			/*
			 * Suck characters out of host card buffer into the
			 * "input staging buffer" - so that we dont leave the
			 * host card in limbo while we're possibly echoing
			 * characters and possibly flushing input inside the
			 * ldisc l_rint() routine.
			 */
			if (n <= SI_BUFFERSIZE - op) {

				DPRINT((pp, DBG_INTR, "\tsingle copy\n"));
				z = ccbp->hi_rxbuf + op;
				si_vbcopy(z, si_rxbuf, n);

				op += n;
			} else {
				x = SI_BUFFERSIZE - op;

				DPRINT((pp, DBG_INTR, "\tdouble part 1 %d\n", x));
				z = ccbp->hi_rxbuf + op;
				si_vbcopy(z, si_rxbuf, x);

				DPRINT((pp, DBG_INTR, "\tdouble part 2 %d\n",
					n - x));
				z = ccbp->hi_rxbuf;
				si_vbcopy(z, si_rxbuf + x, n - x);

				op += n;
			}

			/* clear collected characters from buffer */
			ccbp->hi_rxopos = op;

			DPRINT((pp, DBG_INTR, "n = %d, op = %d, ip = %d\n",
						n, op, ip));

			/*
			 * at this point...
			 * n = number of chars placed in si_rxbuf
			 */

			/*
			 * Avoid the grotesquely inefficient lineswitch
			 * routine (ttyinput) in "raw" mode. It usually
			 * takes about 450 instructions (that's without
			 * canonical processing or echo!). slinput is
			 * reasonably fast (usually 40 instructions
			 * plus call overhead).
			 */
			if (tp->t_state & TS_CAN_BYPASS_L_RINT) {

				/* block if the driver supports it */
				if (tp->t_rawq.c_cc + n >= SI_I_HIGH_WATER &&
				    (tp->t_cflag & CRTS_IFLOW ||
				     tp->t_iflag & IXOFF) &&
				    !(tp->t_state & TS_TBLOCK))
					ttyblock(tp);

				tk_nin += n;
				tk_rawcc += n;
				tp->t_rawcc += n;

				pp->sp_delta_overflows +=
				    b_to_q((char *)si_rxbuf, n, &tp->t_rawq);

				ttwakeup(tp);
				if (tp->t_state & TS_TTSTOP &&
				    (tp->t_iflag & IXANY ||
				     tp->t_cc[VSTART] == tp->t_cc[VSTOP])) {
					tp->t_state &= ~TS_TTSTOP;
					tp->t_lflag &= ~FLUSHO;
					si_start(tp);
				}
			} else {
				/*
				 * It'd be nice to not have to go through the
				 * function call overhead for each char here.
				 * It'd be nice to block input it, saving a
				 * loop here and the call/return overhead.
				 */
				for(x = 0; x < n; x++) {
					i = si_rxbuf[x];
					if ((*linesw[tp->t_line].l_rint)(i, tp)
					     == -1) {
						pp->sp_delta_overflows++;
					}
					/*
					 * doesn't seem to be much point doing
					 * this here.. this driver has no
					 * softtty processing! ??
					 */
					if (pp->sp_hotchar && i == pp->sp_hotchar) {
						setsofttty();
					}
				}
			}
			goto more_rx;	/* try for more until RXbuf is empty */

	end_rx:		/* XXX: Again, sorry about the gotos.. :-) */

			/*
			 * Do TX stuff
			 */
			(*linesw[tp->t_line].l_start)(tp);

		} /* end of for (all ports on this controller) */
	} /* end of for (all controllers) */

	in_intr = 0;
	lwkt_reltoken(&tty_token);
	DPRINT((0, arg == NULL ? DBG_POLL:DBG_INTR, "end si_intr\n"));
}

/*
 * Nudge the transmitter...
 *
 * XXX: I inherited some funny code here.  It implies the host card only
 * interrupts when the transmit buffer reaches the low-water-mark, and does
 * not interrupt when it's actually hits empty.  In some cases, we have
 * processes waiting for complete drain, and we need to simulate an interrupt
 * about when we think the buffer is going to be empty (and retry if not).
 * I really am not certain about this...  I *need* the hardware manuals.
 */
static void
si_start(struct tty *tp)
{
	struct si_port *pp;
	volatile struct si_channel *ccbp;
	struct clist *qp;
	BYTE ipos;
	int nchar;
	int count, n, amount, buffer_full;

	crit_enter();
	lwkt_gettoken(&tty_token);

	qp = &tp->t_outq;
	pp = TP2PP(tp);

	DPRINT((pp, DBG_ENTRY|DBG_START,
		"si_start(%p) t_state %x sp_state %x t_outq.c_cc %d\n",
		tp, tp->t_state, pp->sp_state, qp->c_cc));

	if (tp->t_state & (TS_TIMEOUT|TS_TTSTOP))
		goto out;

	buffer_full = 0;
	ccbp = pp->sp_ccb;

	count = (int)ccbp->hi_txipos - (int)ccbp->hi_txopos;
	DPRINT((pp, DBG_START, "count %d\n", (BYTE)count));

	while ((nchar = qp->c_cc) > 0) {
		if ((BYTE)count >= 255) {
			buffer_full++;
			break;
		}
		amount = min(nchar, (255 - (BYTE)count));
		ipos = (unsigned int)ccbp->hi_txipos;
		n = q_to_b(&tp->t_outq, si_txbuf, amount);
		/* will it fit in one lump? */
		if ((SI_BUFFERSIZE - ipos) >= n) {
			si_bcopyv(si_txbuf, &ccbp->hi_txbuf[ipos], n);
		} else {
			si_bcopyv(si_txbuf, &ccbp->hi_txbuf[ipos],
				SI_BUFFERSIZE - ipos);
			si_bcopyv(si_txbuf + (SI_BUFFERSIZE - ipos),
				&ccbp->hi_txbuf[0], n - (SI_BUFFERSIZE - ipos));
		}
		ccbp->hi_txipos += n;
		count = (int)ccbp->hi_txipos - (int)ccbp->hi_txopos;
	}

	if (count != 0 && nchar == 0) {
		tp->t_state |= TS_BUSY;
	} else {
		tp->t_state &= ~TS_BUSY;
	}

	/* wakeup time? */
	ttwwakeup(tp);

	DPRINT((pp, DBG_START, "count %d, nchar %d, tp->t_state 0x%x\n",
		(BYTE)count, nchar, tp->t_state));

	if (tp->t_state & TS_BUSY)
	{
		int time;

		time = ttspeedtab(tp->t_ospeed, chartimes);

		if (time > 0) {
			if (time < nchar)
				time = nchar / time;
			else
				time = 2;
		} else {
			DPRINT((pp, DBG_START,
				"bad char time value! %d\n", time));
			time = hz/10;
		}

		if ((pp->sp_state & (SS_LSTART|SS_INLSTART)) == SS_LSTART) {
			callout_stop(&pp->lstart_ch);
		} else {
			pp->sp_state |= SS_LSTART;
		}
		DPRINT((pp, DBG_START, "arming lstart, time=%d\n", time));
		callout_reset(&pp->lstart_ch, time, si_lstart, (void *)pp);
	}

out:
	lwkt_reltoken(&tty_token);
	crit_exit();
	DPRINT((pp, DBG_EXIT|DBG_START, "leave si_start()\n"));
}

/*
 * Note: called at splsoftclock from the timeout code
 * This has to deal with two things...  cause wakeups while waiting for
 * tty drains on last process exit, and call l_start at about the right
 * time for protocols like ppp.
 */
static void
si_lstart(void *arg)
{
	struct si_port *pp = arg;
	struct tty *tp;

	DPRINT((pp, DBG_ENTRY|DBG_LSTART, "si_lstart(%p) sp_state %x\n",
		pp, pp->sp_state));

	crit_enter();
	lwkt_gettoken(&tty_token);

	if ((pp->sp_state & SS_OPEN) == 0 || (pp->sp_state & SS_LSTART) == 0) {
		lwkt_reltoken(&tty_token);
		crit_exit();
		return;
	}
	pp->sp_state &= ~SS_LSTART;
	pp->sp_state |= SS_INLSTART;

	tp = pp->sp_tty;

	/* deal with the process exit case */
	ttwwakeup(tp);

	/* nudge protocols - eg: ppp */
	(*linesw[tp->t_line].l_start)(tp);

	pp->sp_state &= ~SS_INLSTART;
	lwkt_reltoken(&tty_token);
	crit_exit();
}

/*
 * Stop output on a line. called at spltty();
 */
void
si_stop(struct tty *tp, int rw)
{
	volatile struct si_channel *ccbp;
	struct si_port *pp;

	lwkt_gettoken(&tty_token);
	pp = TP2PP(tp);
	ccbp = pp->sp_ccb;

	DPRINT((TP2PP(tp), DBG_ENTRY|DBG_STOP, "si_stop(%p,%x)\n", tp, rw));

	/* XXX: must check (rw & FWRITE | FREAD) etc flushing... */
	if (rw & FWRITE) {
		/* what level are we meant to be flushing anyway? */
		if (tp->t_state & TS_BUSY) {
			si_command(TP2PP(tp), WFLUSH, SI_NOWAIT);
			tp->t_state &= ~TS_BUSY;
			ttwwakeup(tp);	/* Bruce???? */
		}
	}
#if 1	/* XXX: this doesn't work right yet.. */
	/* XXX: this may have been failing because we used to call l_rint()
	 * while we were looping based on these two counters. Now, we collect
	 * the data and then loop stuffing it into l_rint(), making this
	 * useless.  Should we cause this to blow away the staging buffer?
	 */
	if (rw & FREAD) {
		ccbp->hi_rxopos = ccbp->hi_rxipos;
	}
#endif
	lwkt_reltoken(&tty_token);
}

/*
 * Issue a command to the host card CPU.
 */

static void
si_command(struct si_port *pp, int cmd, int waitflag)
{
	volatile struct si_channel *ccbp = pp->sp_ccb;
	int x;

	DPRINT((pp, DBG_ENTRY|DBG_PARAM, "si_command(%p,%x,%d): hi_stat 0x%x\n",
		pp, cmd, waitflag, ccbp->hi_stat));

	crit_enter();		/* Keep others out */
	lwkt_gettoken(&tty_token);

	/* wait until it's finished what it was doing.. */
	/* XXX: sits in IDLE_BREAK until something disturbs it or break
	 * is turned off. */
	while((x = ccbp->hi_stat) != IDLE_OPEN &&
			x != IDLE_CLOSE &&
			x != IDLE_BREAK &&
			x != cmd) {
		if (in_intr) {			/* Prevent sleep in intr */
			DPRINT((pp, DBG_PARAM,
				"cmd intr collision - completing %d\trequested %d\n",
				x, cmd));
			lwkt_reltoken(&tty_token);
			crit_exit();
			return;
		} else if (ttysleep(pp->sp_tty, (caddr_t)&pp->sp_state, PCATCH,
				"sicmd1", 1)) {
			lwkt_reltoken(&tty_token);
			crit_exit();
			return;
		}
	}
	/* it should now be in IDLE_{OPEN|CLOSE|BREAK}, or "cmd" */

	/* if there was a pending command, cause a state-change wakeup */
	switch(pp->sp_pend) {
	case LOPEN:
	case MPEND:
	case MOPEN:
	case CONFIG:
	case SBREAK:
	case EBREAK:
		wakeup(&pp->sp_state);
		break;
	default:
		break;
	}

	pp->sp_pend = cmd;		/* New command pending */
	ccbp->hi_stat = cmd;		/* Post it */

	if (waitflag) {
		if (in_intr) {		/* If in interrupt handler */
			DPRINT((pp, DBG_PARAM,
				"attempt to sleep in si_intr - cmd req %d\n",
				cmd));
			lwkt_reltoken(&tty_token);
			crit_exit();
			return;
		} else while(ccbp->hi_stat != IDLE_OPEN &&
			     ccbp->hi_stat != IDLE_BREAK) {
			if (ttysleep(pp->sp_tty, (caddr_t)&pp->sp_state, PCATCH,
			    "sicmd2", 0))
				break;
		}
	}
	lwkt_reltoken(&tty_token);
	crit_exit();
}

static void
si_disc_optim(struct tty *tp, struct termios *t, struct si_port *pp)
{
	lwkt_gettoken(&tty_token);
	/*
	 * XXX can skip a lot more cases if Smarts.  Maybe
	 * (IGNCR | ISTRIP | IXON) in c_iflag.  But perhaps we
	 * shouldn't skip if (TS_CNTTB | TS_LNCH) is set in t_state.
	 */
	if (!(t->c_iflag & (ICRNL | IGNCR | IMAXBEL | INLCR | ISTRIP | IXON)) &&
	    (!(t->c_iflag & BRKINT) || (t->c_iflag & IGNBRK)) &&
	    (!(t->c_iflag & PARMRK) ||
	     (t->c_iflag & (IGNPAR | IGNBRK)) == (IGNPAR | IGNBRK)) &&
	    !(t->c_lflag & (ECHO | ICANON | IEXTEN | ISIG | PENDIN)) &&
	    linesw[tp->t_line].l_rint == ttyinput)
		tp->t_state |= TS_CAN_BYPASS_L_RINT;
	else
		tp->t_state &= ~TS_CAN_BYPASS_L_RINT;
	pp->sp_hotchar = linesw[tp->t_line].l_hotchar;
	DPRINT((pp, DBG_OPTIM, "bypass: %s, hotchar: %x\n",
		(tp->t_state & TS_CAN_BYPASS_L_RINT) ? "on" : "off",
		pp->sp_hotchar));
	lwkt_reltoken(&tty_token);
}


#ifdef	SI_DEBUG

void
si_dprintf(struct si_port *pp, int flags, const char *fmt, ...)
{
	__va_list ap;

	if ((pp == NULL && (si_debug&flags)) ||
	    (pp != NULL && ((pp->sp_debug&flags) || (si_debug&flags)))) {
		if (pp != NULL)
			kprintf("%ci%d(%d): ", 's',
				(int)SI_CARD(minor(pp->sp_tty->t_dev)),
				(int)SI_PORT(minor(pp->sp_tty->t_dev)));
		__va_start(ap, fmt);
		kvprintf(fmt, ap);
		__va_end(ap);
	}
}

static char *
si_mctl2str(enum si_mctl cmd)
{
	switch (cmd) {
	case GET:
		return("GET");
	case SET:
		return("SET");
	case BIS:
		return("BIS");
	case BIC:
		return("BIC");
	}
	return("BAD");
}

#endif	/* DEBUG */

static char *
si_modulename(int host_type, int uart_type)
{
	switch (host_type) {
	/* Z280 based cards */
	case SIEISA:
	case SIHOST2:
	case SIHOST:
	case SIPCI:
		switch (uart_type) {
		case 0:
			return(" (XIO)");
		case 1:
			return(" (SI)");
		}
		break;
	/* T225 based hosts */
	case SIJETPCI:
	case SIJETISA:
		switch (uart_type) {
		case 0:
			return(" (SI)");
		case 40:
			return(" (XIO)");
		case 72:
			return(" (SXDC)");
		}
		break;
	}
	return("");
}
