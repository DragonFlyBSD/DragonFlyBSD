/*-
 * Copyright (c) 1997 Poul-Henning Kamp
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
 *	From Id: lpt.c,v 1.55.2.1 1996/11/12 09:08:38 phk Exp
 * $FreeBSD: src/sys/dev/ppbus/if_plip.c,v 1.19.2.1 2000/05/24 00:20:57 n_hibma Exp $
 */

/*
 * Parallel port TCP/IP interfaces added.  I looked at the driver from
 * MACH but this is a complete rewrite, and btw. incompatible, and it
 * should perform better too.  I have never run the MACH driver though.
 *
 * This driver sends two bytes (0x08, 0x00) in front of each packet,
 * to allow us to distinguish another format later.
 *
 * Now added an Linux/Crynwr compatibility mode which is enabled using
 * IF_LINK0 - Tim Wilkinson.
 *
 * TODO:
 *    Make HDLC/PPP mode, use IF_LLC1 to enable.
 *
 * Connect the two computers using a Laplink parallel cable to use this
 * feature:
 *
 *      +----------------------------------------+
 * 	|A-name	A-End	B-End	Descr.	Port/Bit |
 *      +----------------------------------------+
 *	|DATA0	2	15	Data	0/0x01   |
 *	|-ERROR	15	2	   	1/0x08   |
 *      +----------------------------------------+
 *	|DATA1	3	13	Data	0/0x02	 |
 *	|+SLCT	13	3	   	1/0x10   |
 *      +----------------------------------------+
 *	|DATA2	4	12	Data	0/0x04   |
 *	|+PE	12	4	   	1/0x20   |
 *      +----------------------------------------+
 *	|DATA3	5	10	Strobe	0/0x08   |
 *	|-ACK	10	5	   	1/0x40   |
 *      +----------------------------------------+
 *	|DATA4	6	11	Data	0/0x10   |
 *	|BUSY	11	6	   	1/~0x80  |
 *      +----------------------------------------+
 *	|GND	18-25	18-25	GND	-        |
 *      +----------------------------------------+
 *
 * Expect transfer-rates up to 75 kbyte/sec.
 *
 * If GCC could correctly grok
 *	register int port asm("edx")
 * the code would be cleaner
 *
 * Poul-Henning Kamp <phk@freebsd.org>
 */

/*
 * Update for ppbus, PLIP support only - Nicolas Souchu
 */ 

#include "opt_plip.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/thread2.h>

#include <machine/clock.h>

#include <net/if.h>
#include <net/ifq_var.h>
#include <net/if_types.h>
#include <net/netisr.h>

#include <netinet/in.h>
#include <netinet/in_var.h>

#include <net/bpf.h>

#include <bus/ppbus/ppbconf.h>
#include "ppbus_if.h"
#include <bus/ppbus/ppbio.h>

#ifndef LPMTU			/* MTU for the lp# interfaces */
#define	LPMTU	1500
#endif

#ifndef LPMAXSPIN1		/* DELAY factor for the lp# interfaces */
#define	LPMAXSPIN1	8000   /* Spinning for remote intr to happen */
#endif

#ifndef LPMAXSPIN2		/* DELAY factor for the lp# interfaces */
#define	LPMAXSPIN2	500	/* Spinning for remote handshake to happen */
#endif

#ifndef LPMAXERRS		/* Max errors before !RUNNING */
#define	LPMAXERRS	100
#endif

#define CLPIPHDRLEN	14	/* We send dummy ethernet addresses (two) + packet type in front of packet */
#define	CLPIP_SHAKE	0x80	/* This bit toggles between nibble reception */
#define MLPIPHDRLEN	CLPIPHDRLEN

#define LPIPHDRLEN	2	/* We send 0x08, 0x00 in front of packet */
#define	LPIP_SHAKE	0x40	/* This bit toggles between nibble reception */
#if !defined(MLPIPHDRLEN) || LPIPHDRLEN > MLPIPHDRLEN
#define MLPIPHDRLEN	LPIPHDRLEN
#endif

#define	LPIPTBLSIZE	256	/* Size of octet translation table */

#define lprintf		if (lptflag) kprintf

#ifdef PLIP_DEBUG
static int volatile lptflag = 1;
#else
static int volatile lptflag = 0;
#endif

struct lp_data {
	unsigned short lp_unit;

	struct  ifnet	sc_if;
	u_char		*sc_ifbuf;
	int		sc_iferrs;

	struct resource *res_irq;
};

/* Tables for the lp# interface */
static u_char *txmith;
#define txmitl (txmith+(1*LPIPTBLSIZE))
#define trecvh (txmith+(2*LPIPTBLSIZE))
#define trecvl (txmith+(3*LPIPTBLSIZE))

static u_char *ctxmith;
#define ctxmitl (ctxmith+(1*LPIPTBLSIZE))
#define ctrecvh (ctxmith+(2*LPIPTBLSIZE))
#define ctrecvl (ctxmith+(3*LPIPTBLSIZE))

/* Functions for the lp# interface */
static int lpinittables(void);
static int lpioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static int lpoutput(struct ifnet *, struct mbuf *, struct sockaddr *,
	struct rtentry *);
static void lp_intr(void *);

#define DEVTOSOFTC(dev) \
	((struct lp_data *)device_get_softc(dev))
#define UNITOSOFTC(unit) \
	((struct lp_data *)devclass_get_softc(lp_devclass, (unit)))
#define UNITODEVICE(unit) \
	(devclass_get_device(lp_devclass, (unit)))

static devclass_t lp_devclass;

/*
 * lpprobe()
 */
static int
lp_probe(device_t dev)
{
	device_t ppbus = device_get_parent(dev);
	struct lp_data *lp;
	int zero = 0;
	uintptr_t irq;

	lp = DEVTOSOFTC(dev);

	/* retrieve the ppbus irq */
	BUS_READ_IVAR(ppbus, dev, PPBUS_IVAR_IRQ, &irq);

	/* if we haven't interrupts, the probe fails */
	if (irq == -1) {
		device_printf(dev, "not an interrupt driven port, failed.\n");
		return (ENXIO);
	}

	/* reserve the interrupt resource, expecting irq is available to continue */
	lp->res_irq = bus_alloc_legacy_irq_resource(dev, &zero, irq,
	    RF_SHAREABLE);
	if (lp->res_irq == NULL) {
		device_printf(dev, "cannot reserve interrupt, failed.\n");
		return (ENXIO);
	}

	/*
	 * lp dependent initialisation.
	 */
	lp->lp_unit = device_get_unit(dev);

	device_set_desc(dev, "PLIP network interface");

	return (0);
}

static int
lp_attach (device_t dev)
{
	struct lp_data *lp = DEVTOSOFTC(dev);
	struct ifnet *ifp = &lp->sc_if;

	ifp->if_softc = lp;
	if_initname(ifp, "lp", device_get_unit(dev));
	ifp->if_mtu = LPMTU;
	ifp->if_flags = IFF_SIMPLEX | IFF_POINTOPOINT | IFF_MULTICAST;
	ifp->if_ioctl = lpioctl;
	ifp->if_output = lpoutput;
	ifp->if_type = IFT_PARA;
	ifp->if_hdrlen = 0;
	ifp->if_addrlen = 0;
	ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
	if_attach(ifp, NULL);

	bpfattach(ifp, DLT_NULL, sizeof(u_int32_t));

	return (0);
}
/*
 * Build the translation tables for the LPIP (BSD unix) protocol.
 * We don't want to calculate these nasties in our tight loop, so we
 * precalculate them when we initialize.
 */
static int
lpinittables (void)
{
    int i;

    if (!txmith)
	txmith = kmalloc(4*LPIPTBLSIZE, M_DEVBUF, M_WAITOK);

    if (!ctxmith)
	ctxmith = kmalloc(4*LPIPTBLSIZE, M_DEVBUF, M_WAITOK);

    for (i=0; i < LPIPTBLSIZE; i++) {
	ctxmith[i] = (i & 0xF0) >> 4;
	ctxmitl[i] = 0x10 | (i & 0x0F);
	ctrecvh[i] = (i & 0x78) << 1;
	ctrecvl[i] = (i & 0x78) >> 3;
    }

    for (i=0; i < LPIPTBLSIZE; i++) {
	txmith[i] = ((i & 0x80) >> 3) | ((i & 0x70) >> 4) | 0x08;
	txmitl[i] = ((i & 0x08) << 1) | (i & 0x07);
	trecvh[i] = ((~i) & 0x80) | ((i & 0x38) << 1);
	trecvl[i] = (((~i) & 0x80) >> 4) | ((i & 0x38) >> 3);
    }

    return 0;
}

/*
 * Process an ioctl request.
 */

static int
lpioctl (struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
    device_t dev = UNITODEVICE(ifp->if_dunit);
    device_t ppbus = device_get_parent(dev);
    struct lp_data *sc = DEVTOSOFTC(dev);
    struct ifaddr *ifa = (struct ifaddr *)data;
    struct ifreq *ifr = (struct ifreq *)data;
    u_char *ptr;
    void *ih;
    int error;

    switch (cmd) {

    case SIOCSIFDSTADDR:
    case SIOCAIFADDR:
    case SIOCSIFADDR:
	if (ifa->ifa_addr->sa_family != AF_INET)
	    return EAFNOSUPPORT;

	ifp->if_flags |= IFF_UP;
	/* FALLTHROUGH */
    case SIOCSIFFLAGS:
	if ((!(ifp->if_flags & IFF_UP)) && (ifp->if_flags & IFF_RUNNING)) {

	    ppb_wctr(ppbus, 0x00);
	    ifp->if_flags &= ~IFF_RUNNING;

	    /* IFF_UP is not set, try to release the bus anyway */
	    ppb_release_bus(ppbus, dev);
	    break;
	}
	if (((ifp->if_flags & IFF_UP)) && (!(ifp->if_flags & IFF_RUNNING))) {

	    /* XXX
	     * Should the request be interruptible?
	     */
	    if ((error = ppb_request_bus(ppbus, dev, PPB_WAIT|PPB_INTR)))
		return (error);

	    /* Now IFF_UP means that we own the bus */

	    ppb_set_mode(ppbus, PPB_COMPATIBLE);

	    if (lpinittables()) {
		ppb_release_bus(ppbus, dev);
		return ENOBUFS;
	    }

	    sc->sc_ifbuf = kmalloc(sc->sc_if.if_mtu + MLPIPHDRLEN,
				  M_DEVBUF, M_WAITOK);

	    /* attach our interrupt handler, later detached when the bus is released */
	    error = BUS_SETUP_INTR(ppbus, dev, sc->res_irq, 0,
				   lp_intr, dev, &ih, NULL, NULL);
	    if (error) {
		ppb_release_bus(ppbus, dev);
		return (error);
	    }

	    ppb_wctr(ppbus, IRQENABLE);
	    ifp->if_flags |= IFF_RUNNING;
	}
	break;

    case SIOCSIFMTU:
	ptr = sc->sc_ifbuf;
	sc->sc_ifbuf = kmalloc(ifr->ifr_mtu+MLPIPHDRLEN, M_DEVBUF, M_WAITOK);
	if (ptr)
	    kfree(ptr,M_DEVBUF);
	sc->sc_if.if_mtu = ifr->ifr_mtu;
	break;

    case SIOCGIFMTU:
	ifr->ifr_mtu = sc->sc_if.if_mtu;
	break;

    case SIOCADDMULTI:
    case SIOCDELMULTI:
	if (ifr == NULL) {
	    return EAFNOSUPPORT;		/* XXX */
	}
	switch (ifr->ifr_addr.sa_family) {

	case AF_INET:
	    break;

	default:
	    return EAFNOSUPPORT;
	}
	break;

    case SIOCGIFMEDIA:
	/*
	 * No ifmedia support at this stage; maybe use it
	 * in future for eg. protocol selection.
	 */
	return EINVAL;

    default:
	lprintf("LP:ioctl(0x%lx)\n", cmd);
	return EINVAL;
    }
    return 0;
}

static __inline int
clpoutbyte (u_char byte, int spin, device_t ppbus)
{
	ppb_wdtr(ppbus, ctxmitl[byte]);
	while (ppb_rstr(ppbus) & CLPIP_SHAKE)
		if (--spin == 0) {
			return 1;
		}
	ppb_wdtr(ppbus, ctxmith[byte]);
	while (!(ppb_rstr(ppbus) & CLPIP_SHAKE))
		if (--spin == 0) {
			return 1;
		}
	return 0;
}

static __inline int
clpinbyte (int spin, device_t ppbus)
{
	u_char c, cl;

	while((ppb_rstr(ppbus) & CLPIP_SHAKE))
	    if(!--spin) {
		return -1;
	    }
	cl = ppb_rstr(ppbus);
	ppb_wdtr(ppbus, 0x10);

	while(!(ppb_rstr(ppbus) & CLPIP_SHAKE))
	    if(!--spin) {
		return -1;
	    }
	c = ppb_rstr(ppbus);
	ppb_wdtr(ppbus, 0x00);

	return (ctrecvl[cl] | ctrecvh[c]);
}

static void
lptap(struct ifnet *ifp, struct mbuf *m)
{
	/*
	 * We need to prepend the address family as a four byte field.
	 */
	static const uint32_t af = AF_INET;

	if (ifp->if_bpf) {
		bpf_gettoken();
		if (ifp->if_bpf)
			bpf_ptap(ifp->if_bpf, m, &af, sizeof(af));
		bpf_reltoken();
	}
}

static void
lp_intr (void *arg)
{
	device_t dev = (device_t)arg;
	device_t ppbus = device_get_parent(dev);
	struct lp_data *sc = DEVTOSOFTC(dev);
	int len, j;
	u_char *bp;
	u_char c, cl;
	struct mbuf *top;

	crit_enter();

	if (sc->sc_if.if_flags & IFF_LINK0) {

	    /* Ack. the request */
	    ppb_wdtr(ppbus, 0x01);

	    /* Get the packet length */
	    j = clpinbyte(LPMAXSPIN2, ppbus);
	    if (j == -1)
		goto err;
	    len = j;
	    j = clpinbyte(LPMAXSPIN2, ppbus);
	    if (j == -1)
		goto err;
	    len = len + (j << 8);
	    if (len > sc->sc_if.if_mtu + MLPIPHDRLEN)
		goto err;

	    bp  = sc->sc_ifbuf;
	
	    while (len--) {
	        j = clpinbyte(LPMAXSPIN2, ppbus);
	        if (j == -1) {
		    goto err;
	        }
	        *bp++ = j;
	    }
	    /* Get and ignore checksum */
	    j = clpinbyte(LPMAXSPIN2, ppbus);
	    if (j == -1) {
	        goto err;
	    }

	    len = bp - sc->sc_ifbuf;
	    if (len <= CLPIPHDRLEN)
	        goto err;

	    sc->sc_iferrs = 0;

	    len -= CLPIPHDRLEN;
	    IFNET_STAT_INC(&sc->sc_if, ipackets, 1);
	    IFNET_STAT_INC(&sc->sc_if, ibytes, len);
	    top = m_devget(sc->sc_ifbuf + CLPIPHDRLEN, len, 0, &sc->sc_if, 0);
	    if (top) {
		if (sc->sc_if.if_bpf)
		    lptap(&sc->sc_if, top);
		netisr_queue(NETISR_IP, top);
	    }
	    goto done;
	}
	while ((ppb_rstr(ppbus) & LPIP_SHAKE)) {
	    len = sc->sc_if.if_mtu + LPIPHDRLEN;
	    bp  = sc->sc_ifbuf;
	    while (len--) {

		cl = ppb_rstr(ppbus);
		ppb_wdtr(ppbus, 8);

		j = LPMAXSPIN2;
		while((ppb_rstr(ppbus) & LPIP_SHAKE))
		    if(!--j) goto err;

		c = ppb_rstr(ppbus);
		ppb_wdtr(ppbus, 0);

		*bp++= trecvh[cl] | trecvl[c];

		j = LPMAXSPIN2;
		while (!((cl=ppb_rstr(ppbus)) & LPIP_SHAKE)) {
		    if (cl != c &&
			(((cl = ppb_rstr(ppbus)) ^ 0xb8) & 0xf8) ==
			  (c & 0xf8))
			goto end;
		    if (!--j) goto err;
		}
	    }

	end:
	    len = bp - sc->sc_ifbuf;
	    if (len <= LPIPHDRLEN)
		goto err;

	    sc->sc_iferrs = 0;

	    len -= LPIPHDRLEN;
	    IFNET_STAT_INC(&sc->sc_if, ipackets, 1);
	    IFNET_STAT_INC(&sc->sc_if, ibytes, len);
	    top = m_devget(sc->sc_ifbuf + LPIPHDRLEN, len, 0, &sc->sc_if, 0);
	    if (top) {
		if (sc->sc_if.if_bpf)
		    lptap(&sc->sc_if, top);
		netisr_queue(NETISR_IP, top);
	    }
	}
	goto done;

    err:
	ppb_wdtr(ppbus, 0);
	lprintf("R");
	IFNET_STAT_INC(&sc->sc_if, ierrors, 1);
	sc->sc_iferrs++;

	/*
	 * We are not able to send receive anything for now,
	 * so stop wasting our time
	 */
	if (sc->sc_iferrs > LPMAXERRS) {
	    kprintf("lp%d: Too many errors, Going off-line.\n", device_get_unit(dev));
	    ppb_wctr(ppbus, 0x00);
	    sc->sc_if.if_flags &= ~IFF_RUNNING;
	    sc->sc_iferrs=0;
	}

    done:
	crit_exit();
	return;
}

static __inline int
lpoutbyte (u_char byte, int spin, device_t ppbus)
{
    ppb_wdtr(ppbus, txmith[byte]);
    while (!(ppb_rstr(ppbus) & LPIP_SHAKE))
	if (--spin == 0)
		return 1;
    ppb_wdtr(ppbus, txmitl[byte]);
    while (ppb_rstr(ppbus) & LPIP_SHAKE)
	if (--spin == 0)
		return 1;
    return 0;
}

static int
lpoutput (struct ifnet *ifp, struct mbuf *m,
	  struct sockaddr *dst, struct rtentry *rt)
{
    device_t dev = UNITODEVICE(ifp->if_dunit);
    device_t ppbus = device_get_parent(dev);
    int err;
    struct mbuf *mm;
    u_char *cp = "\0\0";
    u_char chksum = 0;
    int count = 0;
    int i, len, spin;

    /* We need a sensible value if we abort */
    cp++;
    ifp->if_flags |= IFF_RUNNING;

    err = 1;			/* assume we're aborting because of an error */

    crit_enter();

    /* Suspend (on laptops) or receive-errors might have taken us offline */
    ppb_wctr(ppbus, IRQENABLE);

    if (ifp->if_flags & IFF_LINK0) {

	if (!(ppb_rstr(ppbus) & CLPIP_SHAKE)) {
	    lprintf("&");
	    lp_intr(dev);
	}

	/* Alert other end to pending packet */
	spin = LPMAXSPIN1;
	ppb_wdtr(ppbus, 0x08);
	while ((ppb_rstr(ppbus) & 0x08) == 0)
		if (--spin == 0) {
			goto nend;
		}

	/* Calculate length of packet, then send that */

	count += 14;		/* Ethernet header len */

	mm = m;
	for (mm = m; mm; mm = mm->m_next) {
		count += mm->m_len;
	}
	if (clpoutbyte(count & 0xFF, LPMAXSPIN1, ppbus))
		goto nend;
	if (clpoutbyte((count >> 8) & 0xFF, LPMAXSPIN1, ppbus))
		goto nend;

	/* Send dummy ethernet header */
	for (i = 0; i < 12; i++) {
		if (clpoutbyte(i, LPMAXSPIN1, ppbus))
			goto nend;
		chksum += i;
	}

	if (clpoutbyte(0x08, LPMAXSPIN1, ppbus))
		goto nend;
	if (clpoutbyte(0x00, LPMAXSPIN1, ppbus))
		goto nend;
	chksum += 0x08 + 0x00;		/* Add into checksum */

	mm = m;
	do {
		cp = mtod(mm, u_char *);
		len = mm->m_len;
		while (len--) {
			chksum += *cp;
			if (clpoutbyte(*cp++, LPMAXSPIN2, ppbus))
				goto nend;
		}
	} while ((mm = mm->m_next));

	/* Send checksum */
	if (clpoutbyte(chksum, LPMAXSPIN2, ppbus))
		goto nend;

	/* Go quiescent */
	ppb_wdtr(ppbus, 0);

	err = 0;			/* No errors */

	nend:
	if (err)  {				/* if we didn't timeout... */
		IFNET_STAT_INC(ifp, oerrors, 1);
		lprintf("X");
	} else {
		IFNET_STAT_INC(ifp, opackets, 1);
		IFNET_STAT_INC(ifp, obytes, m->m_pkthdr.len);
		if (ifp->if_bpf)
		    lptap(ifp, m);
	}

	m_freem(m);

	if (!(ppb_rstr(ppbus) & CLPIP_SHAKE)) {
		lprintf("^");
		lp_intr(dev);
	}
	crit_exit();
	return 0;
    }

    if (ppb_rstr(ppbus) & LPIP_SHAKE) {
        lprintf("&");
        lp_intr(dev);
    }

    if (lpoutbyte(0x08, LPMAXSPIN1, ppbus))
        goto end;
    if (lpoutbyte(0x00, LPMAXSPIN2, ppbus))
        goto end;

    mm = m;
    do {
        cp = mtod(mm,u_char *);
	len = mm->m_len;
        while (len--)
	    if (lpoutbyte(*cp++, LPMAXSPIN2, ppbus))
	        goto end;
    } while ((mm = mm->m_next));

    err = 0;				/* no errors were encountered */

    end:
    --cp;
    ppb_wdtr(ppbus, txmitl[*cp] ^ 0x17);

    if (err)  {				/* if we didn't timeout... */
	IFNET_STAT_INC(ifp, oerrors, 1);
        lprintf("X");
    } else {
	IFNET_STAT_INC(ifp, opackets, 1);
	IFNET_STAT_INC(ifp, obytes, m->m_pkthdr.len);
	if (ifp->if_bpf)
	    lptap(ifp, m);
    }

    m_freem(m);

    if (ppb_rstr(ppbus) & LPIP_SHAKE) {
	lprintf("^");
	lp_intr(dev);
    }

    crit_exit();
    return 0;
}

/*
 * Because plip is a static device that always exists under any attached
 * ppbus device, and not scanned by the ppbus device, we need an identify
 * function to install the device.
 */
static device_method_t lp_methods[] = {
  	/* device interface */
	DEVMETHOD(device_identify,	bus_generic_identify),
	DEVMETHOD(device_probe,		lp_probe),
	DEVMETHOD(device_attach,	lp_attach),

	DEVMETHOD_END
};

static driver_t lp_driver = {
  "plip",
  lp_methods,
  sizeof(struct lp_data),
};

DECLARE_DUMMY_MODULE(if_plip);
DRIVER_MODULE(if_plip, ppbus, lp_driver, lp_devclass, NULL, NULL);
