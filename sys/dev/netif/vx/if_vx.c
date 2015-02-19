/*
 * Copyright (c) 1994 Herb Peyerl <hpeyerl@novatel.ca>
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
 *      This product includes software developed by Herb Peyerl.
 * 4. The name of Herb Peyerl may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/vx/if_vx.c,v 1.25.2.6 2002/02/13 00:43:10 dillon Exp $
 */

/*
 * Created from if_ep.c driver by Fred Gray (fgray@rice.edu) to support
 * the 3c590 family.
 */

/*
 *	Modified from the FreeBSD 1.1.5.1 version by:
 *		 	Andres Vega Garcia
 *			INRIA - Sophia Antipolis, France
 *			avega@sophia.inria.fr
 */

/*
 *  Promiscuous mode added and interrupt logic slightly changed
 *  to reduce the number of adapter failures. Transceiver select
 *  logic changed to use value from EEPROM. Autoconfiguration
 *  features added.
 *  Done by:
 *          Serge Babkin
 *          Chelindbank (Chelyabinsk, Russia)
 *          babkin@hq.icb.chel.su
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/linker_set.h>
#include <sys/module.h>
#include <sys/serialize.h>
#include <sys/thread2.h>

#include <net/if.h>
#include <net/ifq_var.h>
#include <net/ethernet.h>
#include <net/if_arp.h>

#include <net/bpf.h>

#include "if_vxreg.h"

DECLARE_DUMMY_MODULE(if_vx);

static struct connector_entry {
  int bit;
  const char *name;
} conn_tab[VX_CONNECTORS] = {
#define CONNECTOR_UTP	0
  { 0x08, "utp"},
#define CONNECTOR_AUI	1
  { 0x20, "aui"},
/* dummy */
  { 0, "???"},
#define CONNECTOR_BNC	3
  { 0x10, "bnc"},
#define CONNECTOR_TX	4
  { 0x02, "tx"},
#define CONNECTOR_FX	5
  { 0x04, "fx"},
#define CONNECTOR_MII	6
  { 0x40, "mii"},
  { 0, "???"}
};

/* int vxattach (struct vx_softc *); */
static void vxtxstat (struct vx_softc *);
static int vxstatus (struct vx_softc *);
static void vxinit (void *);
static int vxioctl (struct ifnet *, u_long, caddr_t, struct ucred *);
static void vxstart (struct ifnet *ifp, struct ifaltq_subque *);
static void vxwatchdog (struct ifnet *);
static void vxreset (struct vx_softc *);
/* void vxstop (struct vx_softc *); */
static void vxread (struct vx_softc *);
static struct mbuf *vxget (struct vx_softc *, u_int);
static void vxmbuffill (void *);
static void vxmbuffill_serialized (void *);
static void vxmbufempty (struct vx_softc *);
static void vxsetfilter (struct vx_softc *);
static void vxgetlink (struct vx_softc *);
static void vxsetlink (struct vx_softc *);
/* int vxbusyeeprom (struct vx_softc *); */

int
vxattach(device_t dev)
{
    struct vx_softc *sc;
    struct ifnet *ifp;
    uint8_t eaddr[ETHER_ADDR_LEN];
    int i;

    sc = device_get_softc(dev);

    callout_init(&sc->vx_timer);
    GO_WINDOW(0);
    CSR_WRITE_2(sc, VX_COMMAND, GLOBAL_RESET);
    VX_BUSY_WAIT;

    ifp = &sc->arpcom.ac_if;
    if_initname(ifp, device_get_name(dev), device_get_unit(dev));

    vxgetlink(sc);

    /*
     * Read the station address from the eeprom
     */
    GO_WINDOW(0);
    for (i = 0; i < 3; i++) {
        int x;
        if (vxbusyeeprom(sc))
            return 0;
	CSR_WRITE_2(sc, VX_W0_EEPROM_COMMAND, EEPROM_CMD_RD
	     | (EEPROM_OEM_ADDR_0 + i));
        if (vxbusyeeprom(sc))
            return 0;
        x = CSR_READ_2(sc, VX_W0_EEPROM_DATA);
        eaddr[(i << 1)] = x >> 8;
        eaddr[(i << 1) + 1] = x;
    }

    ifp->if_mtu = ETHERMTU;
    ifq_set_maxlen(&ifp->if_snd, IFQ_MAXLEN);
    ifq_set_ready(&ifp->if_snd);
    ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
    ifp->if_start = vxstart;
    ifp->if_ioctl = vxioctl;
    ifp->if_init = vxinit;
    ifp->if_watchdog = vxwatchdog;
    ifp->if_softc = sc;

    ether_ifattach(ifp, eaddr, NULL);

    sc->tx_start_thresh = 20;	/* probably a good starting point. */

    vxstop(sc);

    return 1;
}



/*
 * The order in here seems important. Otherwise we may not receive
 * interrupts. ?!
 */
static void
vxinit(void *xsc)
{
    struct vx_softc *sc = (struct vx_softc *) xsc;
    struct ifnet *ifp = &sc->arpcom.ac_if;
    int i;

    VX_BUSY_WAIT;

    GO_WINDOW(2);

    for (i = 0; i < 6; i++) /* Reload the ether_addr. */
	CSR_WRITE_1(sc, VX_W2_ADDR_0 + i, sc->arpcom.ac_enaddr[i]);

    CSR_WRITE_2(sc, VX_COMMAND, RX_RESET);
    VX_BUSY_WAIT;
    CSR_WRITE_2(sc, VX_COMMAND, TX_RESET);
    VX_BUSY_WAIT;

    GO_WINDOW(1);	/* Window 1 is operating window */
    for (i = 0; i < 31; i++)
	CSR_READ_1(sc, VX_W1_TX_STATUS);

    CSR_WRITE_2(sc, VX_COMMAND,SET_RD_0_MASK | S_CARD_FAILURE |
			S_RX_COMPLETE | S_TX_COMPLETE | S_TX_AVAIL);
    CSR_WRITE_2(sc, VX_COMMAND,SET_INTR_MASK | S_CARD_FAILURE |
			S_RX_COMPLETE | S_TX_COMPLETE | S_TX_AVAIL);

    /*
     * Attempt to get rid of any stray interrupts that occured during
     * configuration.  On the i386 this isn't possible because one may
     * already be queued.  However, a single stray interrupt is
     * unimportant.
     */
    CSR_WRITE_2(sc, VX_COMMAND, ACK_INTR | 0xff);

    vxsetfilter(sc);
    vxsetlink(sc);

    CSR_WRITE_2(sc, VX_COMMAND, RX_ENABLE);
    CSR_WRITE_2(sc, VX_COMMAND, TX_ENABLE);

    vxmbuffill_serialized((caddr_t) sc);

    /* Interface is now `running', with no output active. */
    ifp->if_flags |= IFF_RUNNING;
    ifq_clr_oactive(&ifp->if_snd);

    /* Attempt to start output, if any. */
    if_devstart(ifp);
}

static void
vxsetfilter(struct vx_softc *sc)
{
    struct ifnet *ifp = &sc->arpcom.ac_if;  
    
    GO_WINDOW(1);           /* Window 1 is operating window */
    CSR_WRITE_2(sc, VX_COMMAND, SET_RX_FILTER | FIL_INDIVIDUAL | FIL_BRDCST |
	 FIL_MULTICAST |
	 ((ifp->if_flags & IFF_PROMISC) ? FIL_PROMISC : 0 ));
}               

static void            
vxgetlink(struct vx_softc *sc)
{
    int n, k;

    GO_WINDOW(3);
    sc->vx_connectors = CSR_READ_2(sc, VX_W3_RESET_OPT) & 0x7f;
    for (n = 0, k = 0; k < VX_CONNECTORS; k++) {
      if (sc->vx_connectors & conn_tab[k].bit) {
	if (n == 0)
	  if_printf(&sc->arpcom.ac_if, "%s", conn_tab[k].name);
	else
	  kprintf("/%s", conn_tab[k].name);
	n++;
      }
    }
    if (n == 0) {
	if_printf(&sc->arpcom.ac_if, "no connectors!\n");
	return;
    }
    GO_WINDOW(3);
    sc->vx_connector = (CSR_READ_4(sc, VX_W3_INTERNAL_CFG) 
			& INTERNAL_CONNECTOR_MASK) 
			>> INTERNAL_CONNECTOR_BITS;
    if (sc->vx_connector & 0x10) {
	sc->vx_connector &= 0x0f;
	kprintf("[*%s*]", conn_tab[(int)sc->vx_connector].name);
	kprintf(": disable 'auto select' with DOS util!\n");
    } else {
	kprintf("[*%s*]\n", conn_tab[(int)sc->vx_connector].name);
    }
}

static void            
vxsetlink(struct vx_softc *sc)
{       
    struct ifnet *ifp = &sc->arpcom.ac_if;  
    int i, j, k;
    const char *reason, *warning;
    static short prev_flags;
    static char prev_conn = -1;

    if (prev_conn == -1) {
	prev_conn = sc->vx_connector;
    }

    /*
     * S.B.
     *
     * Now behavior was slightly changed:
     *
     * if any of flags link[0-2] is used and its connector is
     * physically present the following connectors are used:
     *
     *   link0 - AUI * highest precedence
     *   link1 - BNC
     *   link2 - UTP * lowest precedence
     *
     * If none of them is specified then
     * connector specified in the EEPROM is used
     * (if present on card or UTP if not).
     */

    i = sc->vx_connector;	/* default in EEPROM */
    reason = "default";
    warning = NULL;

    if (ifp->if_flags & IFF_LINK0) {
	if (sc->vx_connectors & conn_tab[CONNECTOR_AUI].bit) {
	    i = CONNECTOR_AUI;
	    reason = "link0";
	} else {
	    warning = "aui not present! (link0)";
	}
    } else if (ifp->if_flags & IFF_LINK1) {
	if (sc->vx_connectors & conn_tab[CONNECTOR_BNC].bit) {
	    i = CONNECTOR_BNC;
	    reason = "link1";
	} else {
	    warning = "bnc not present! (link1)";
	}
    } else if (ifp->if_flags & IFF_LINK2) {
	if (sc->vx_connectors & conn_tab[CONNECTOR_UTP].bit) {
	    i = CONNECTOR_UTP;
	    reason = "link2";
	} else {
	    warning = "utp not present! (link2)";
	}
    } else if ((sc->vx_connectors & conn_tab[(int)sc->vx_connector].bit) == 0) {
	warning = "strange connector type in EEPROM.";
	reason = "forced";
	i = CONNECTOR_UTP;
    }

    /* Avoid unnecessary message. */
    k = (prev_flags ^ ifp->if_flags) & (IFF_LINK0 | IFF_LINK1 | IFF_LINK2);
    if ((k != 0) || (prev_conn != i)) {
	if (warning != NULL) {
	    if_printf(ifp, "warning: %s\n", warning);
	}
	if_printf(ifp, "selected %s. (%s)\n", conn_tab[i].name, reason);
    }

    /* Set the selected connector. */
    GO_WINDOW(3);
    j = CSR_READ_4(sc, VX_W3_INTERNAL_CFG) & ~INTERNAL_CONNECTOR_MASK;
    CSR_WRITE_4(sc, VX_W3_INTERNAL_CFG, j | (i <<INTERNAL_CONNECTOR_BITS));

    /* First, disable all. */
    CSR_WRITE_2(sc,VX_COMMAND, STOP_TRANSCEIVER);
    DELAY(800);
    GO_WINDOW(4);
    CSR_WRITE_2(sc, VX_W4_MEDIA_TYPE, 0);

    /* Second, enable the selected one. */
    switch(i) {
      case CONNECTOR_UTP:
	GO_WINDOW(4);
	CSR_WRITE_2(sc, VX_W4_MEDIA_TYPE, ENABLE_UTP);
	break;
      case CONNECTOR_BNC:
	CSR_WRITE_2(sc, VX_COMMAND, START_TRANSCEIVER);
	DELAY(800);
	break;
      case CONNECTOR_TX:
      case CONNECTOR_FX:
	GO_WINDOW(4);
	CSR_WRITE_2(sc, VX_W4_MEDIA_TYPE, LINKBEAT_ENABLE);
	break;
      default:	/* AUI and MII fall here */
	break;
    }
    GO_WINDOW(1); 

    prev_flags = ifp->if_flags;
    prev_conn = i;
}

static void
vxstart(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
    struct vx_softc *sc = ifp->if_softc;
    struct mbuf *m0;
    int len, pad;

    ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);

    /* Don't transmit if interface is busy or not running */
    if ((ifp->if_flags & IFF_RUNNING) == 0 || ifq_is_oactive(&ifp->if_snd))
	return;

startagain:
    /* Sneak a peek at the next packet */
    m0 = ifq_dequeue(&ifp->if_snd);
    if (m0 == NULL)
	return;

    /* We need to use m->m_pkthdr.len, so require the header */
    M_ASSERTPKTHDR(m0);
    len = m0->m_pkthdr.len;

    pad = (4 - len) & 3;

    /*
     * The 3c509 automatically pads short packets to minimum ethernet length,
     * but we drop packets that are too large. Perhaps we should truncate
     * them instead?
     */
    if (len + pad > ETHER_MAX_LEN) {
	/* packet is obviously too large: toss it */
	IFNET_STAT_INC(ifp, oerrors, 1);
	m_freem(m0);
	goto readcheck;
    }
    VX_BUSY_WAIT;
    if (CSR_READ_2(sc, VX_W1_FREE_TX) < len + pad + 4) {
	CSR_WRITE_2(sc, VX_COMMAND, SET_TX_AVAIL_THRESH | ((len + pad + 4) >> 2));
	/* not enough room in FIFO */
	if (CSR_READ_2(sc, VX_W1_FREE_TX) < len + pad + 4) { /* make sure */
	    ifq_set_oactive(&ifp->if_snd);
	    ifp->if_timer = 1;
	    ifq_prepend(&ifp->if_snd, m0);
	    return;
	}
    }
    CSR_WRITE_2(sc, VX_COMMAND, SET_TX_AVAIL_THRESH | (8188 >> 2));

    VX_BUSY_WAIT;
    CSR_WRITE_2(sc, VX_COMMAND, SET_TX_START_THRESH |
	((len / 4 + sc->tx_start_thresh) >> 2));

    BPF_MTAP(ifp, m0);

    /*
     * Do the output in a critical section so that an interrupt from
     * another device won't cause a FIFO underrun.
     */
    crit_enter();

    CSR_WRITE_4(sc, VX_W1_TX_PIO_WR_1, len | TX_INDICATE);

    while (m0) {
        if (m0->m_len > 3)
	    bus_space_write_multi_4(sc->vx_btag, sc->vx_bhandle,
	        VX_W1_TX_PIO_WR_1,
		(u_int32_t *)mtod(m0, caddr_t), m0->m_len / 4);
        if (m0->m_len & 3)
	    bus_space_write_multi_1(sc->vx_btag, sc->vx_bhandle,
	        VX_W1_TX_PIO_WR_1,
		mtod(m0, caddr_t) + (m0->m_len & ~3), m0->m_len & 3);
	m0 = m_free(m0);
    }
    while (pad--)
	CSR_WRITE_1(sc, VX_W1_TX_PIO_WR_1, 0);	/* Padding */

    crit_exit();

    IFNET_STAT_INC(ifp, opackets, 1);
    ifp->if_timer = 1;

readcheck:
    if ((CSR_READ_2(sc, VX_W1_RX_STATUS) & ERR_INCOMPLETE) == 0) {
	/* We received a complete packet. */
	
	if ((CSR_READ_2(sc, VX_STATUS) & S_INTR_LATCH) != 0) {
	    /* Got an interrupt, return so that it gets serviced. */
	    return;
	}
	/*
	 * No interrupt, read the packet and continue
	 * Is  this supposed to happen? Is my motherboard
	 * completely busted?
	 */
	vxread(sc);
    } else {
	/* Check if we are stuck and reset [see XXX comment] */
	if (vxstatus(sc)) {
	    if (ifp->if_flags & IFF_DEBUG)
	       if_printf(ifp, "adapter reset\n");
	    vxreset(sc);
	}
    }

    goto startagain;
}

/*
 * XXX: The 3c509 card can get in a mode where both the fifo status bit
 *      FIFOS_RX_OVERRUN and the status bit ERR_INCOMPLETE are set
 *      We detect this situation and we reset the adapter.
 *      It happens at times when there is a lot of broadcast traffic
 *      on the cable (once in a blue moon).
 */
static int
vxstatus(struct vx_softc *sc)
{
    int fifost;
    struct ifnet *ifp = &sc->arpcom.ac_if;

    /*
     * Check the FIFO status and act accordingly
     */
    GO_WINDOW(4);
    fifost = CSR_READ_2(sc, VX_W4_FIFO_DIAG);
    GO_WINDOW(1);

    if (fifost & FIFOS_RX_UNDERRUN) {
	if (ifp->if_flags & IFF_DEBUG)
	    if_printf(ifp, "RX underrun\n");
	vxreset(sc);
	return 0;
    }

    if (fifost & FIFOS_RX_STATUS_OVERRUN) {
	if (ifp->if_flags & IFF_DEBUG)
	    if_printf(ifp, "RX Status overrun\n");
	return 1;
    }

    if (fifost & FIFOS_RX_OVERRUN) {
	if (ifp->if_flags & IFF_DEBUG)
	    if_printf(ifp, "RX overrun\n");
	return 1;
    }

    if (fifost & FIFOS_TX_OVERRUN) {
	if (ifp->if_flags & IFF_DEBUG)
	    if_printf(ifp, "TX overrun\n");
	vxreset(sc);
    }

    return 0;
}

static void     
vxtxstat(struct vx_softc *sc)
{
	int i;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	/*
	 * We need to read+write TX_STATUS until we get a 0 status
	 * in order to turn off the interrupt flag.
	 */
	while ((i = CSR_READ_1(sc, VX_W1_TX_STATUS)) & TXS_COMPLETE) {
		CSR_WRITE_1(sc, VX_W1_TX_STATUS, 0x0);

		if (i & TXS_JABBER) {
			IFNET_STAT_INC(ifp, oerrors, 1);
			if (ifp->if_flags & IFF_DEBUG)
				if_printf(ifp, "jabber (%x)\n", i);
			vxreset(sc);
		} else if (i & TXS_UNDERRUN) {
			IFNET_STAT_INC(ifp, oerrors, 1);
			if (ifp->if_flags & IFF_DEBUG) {
				if_printf(ifp, "fifo underrun (%x) @%d\n",
					  i, sc->tx_start_thresh);
			}
			if (sc->tx_succ_ok < 100) {
				sc->tx_start_thresh = min(
				    ETHER_MAX_LEN, sc->tx_start_thresh + 20);
			}
			sc->tx_succ_ok = 0;
			vxreset(sc);
		} else if (i & TXS_MAX_COLLISION) {
			IFNET_STAT_INC(ifp, collisions, 1);
			CSR_WRITE_2(sc, VX_COMMAND, TX_ENABLE);
			ifq_clr_oactive(&ifp->if_snd);
		} else {
			sc->tx_succ_ok = (sc->tx_succ_ok+1) & 127;
		}
	}
}

void
vxintr(void *voidsc)
{
    short status;
    struct vx_softc *sc = voidsc;
    struct ifnet *ifp = &sc->arpcom.ac_if;

    for (;;) {
	CSR_WRITE_2(sc, VX_COMMAND, C_INTR_LATCH);

	status = CSR_READ_2(sc, VX_STATUS);

	if ((status & (S_TX_COMPLETE | S_TX_AVAIL |
		S_RX_COMPLETE | S_CARD_FAILURE)) == 0)
	    break;

	/*
	 * Acknowledge any interrupts.  It's important that we do this
	 * first, since there would otherwise be a race condition.
	 * Due to the i386 interrupt queueing, we may get spurious
	 * interrupts occasionally.
	 */
	CSR_WRITE_2(sc, VX_COMMAND, ACK_INTR | status);

	if (status & S_RX_COMPLETE)
	    vxread(sc);
	if (status & S_TX_AVAIL) {
	    ifp->if_timer = 0;
	    ifq_clr_oactive(&ifp->if_snd);
	    if_devstart(ifp);
	}
	if (status & S_CARD_FAILURE) {
	    if_printf(ifp, "adapter failure (%x)\n", status);
	    ifp->if_timer = 0;
	    vxreset(sc);
	    return;
	}
	if (status & S_TX_COMPLETE) {
	    ifp->if_timer = 0;
	    vxtxstat(sc);
	    if_devstart(ifp);
	}
    }
    /* no more interrupts */
}

static void
vxread(struct vx_softc *sc)
{
    struct ifnet *ifp = &sc->arpcom.ac_if;
    struct mbuf *m;
    struct ether_header *eh;
    u_int len;

    len = CSR_READ_2(sc, VX_W1_RX_STATUS);

again:

    if (ifp->if_flags & IFF_DEBUG) {
	int err = len & ERR_MASK;
	const char *s = NULL;

	if (len & ERR_INCOMPLETE)
	    s = "incomplete packet";
	else if (err == ERR_OVERRUN)
	    s = "packet overrun";
	else if (err == ERR_RUNT)
	    s = "runt packet";
	else if (err == ERR_ALIGNMENT)
	    s = "bad alignment";
	else if (err == ERR_CRC)
	    s = "bad crc";
	else if (err == ERR_OVERSIZE)
	    s = "oversized packet";
	else if (err == ERR_DRIBBLE)
	    s = "dribble bits";

	if (s)
	    if_printf(ifp, "%s\n", s);
    }

    if (len & ERR_INCOMPLETE)
	return;

    if (len & ERR_RX) {
	IFNET_STAT_INC(ifp, ierrors, 1);
	goto abort;
    }

    len &= RX_BYTES_MASK;	/* Lower 11 bits = RX bytes. */

    /* Pull packet off interface. */
    m = vxget(sc, len);
    if (m == NULL) {
	IFNET_STAT_INC(ifp, ierrors, 1);
	goto abort;
    }

    IFNET_STAT_INC(ifp, ipackets, 1);

    /* We assume the header fit entirely in one mbuf. */
    eh = mtod(m, struct ether_header *);

    /*
     * XXX: Some cards seem to be in promiscous mode all the time.
     * we need to make sure we only get our own stuff always.
     * bleah!
     */

    if ((eh->ether_dhost[0] & 1) == 0		/* !mcast and !bcast */
      && bcmp(eh->ether_dhost, sc->arpcom.ac_enaddr, ETHER_ADDR_LEN) != 0) {
	m_freem(m);
	return;
    }

    ifp->if_input(ifp, m, NULL, -1);

    /*
    * In periods of high traffic we can actually receive enough
    * packets so that the fifo overrun bit will be set at this point,
    * even though we just read a packet. In this case we
    * are not going to receive any more interrupts. We check for
    * this condition and read again until the fifo is not full.
    * We could simplify this test by not using vxstatus(), but
    * rechecking the RX_STATUS register directly. This test could
    * result in unnecessary looping in cases where there is a new
    * packet but the fifo is not full, but it will not fix the
    * stuck behavior.
    *
    * Even with this improvement, we still get packet overrun errors
    * which are hurting performance. Maybe when I get some more time
    * I'll modify vxread() so that it can handle RX_EARLY interrupts.
    */
    if (vxstatus(sc)) {
	len = CSR_READ_2(sc, VX_W1_RX_STATUS);
	/* Check if we are stuck and reset [see XXX comment] */
	if (len & ERR_INCOMPLETE) {
	    if (ifp->if_flags & IFF_DEBUG)
		if_printf(ifp, "adapter reset\n");
	    vxreset(sc);
	    return;
	}
	goto again;
    }

    return;

abort:
    CSR_WRITE_2(sc, VX_COMMAND, RX_DISCARD_TOP_PACK);
}

static struct mbuf *
vxget(struct vx_softc *sc, u_int totlen)
{
    struct ifnet *ifp = &sc->arpcom.ac_if;
    struct mbuf *top, **mp, *m;
    int len;

    m = sc->mb[sc->next_mb];
    sc->mb[sc->next_mb] = NULL;
    if (m == NULL) {
        MGETHDR(m, M_NOWAIT, MT_DATA);
        if (m == NULL)
            return NULL;
    } else {
        /* If the queue is no longer full, refill. */
        if (sc->last_mb == sc->next_mb && sc->buffill_pending == 0) {
	    callout_reset(&sc->vx_timer, 1, vxmbuffill, sc);
	    sc->buffill_pending = 1;
	}
        /* Convert one of our saved mbuf's. */
        sc->next_mb = (sc->next_mb + 1) % MAX_MBS;
        m->m_data = m->m_pktdat;
        m->m_flags = M_PKTHDR;
	bzero(&m->m_pkthdr, sizeof(m->m_pkthdr));
    }
    m->m_pkthdr.rcvif = ifp;
    m->m_pkthdr.len = totlen;
    len = MHLEN;
    top = NULL;
    mp = &top;

    /*
     * We read the packet in a critical section so that an interrupt
     * from another device doesn't cause the card's buffer to overflow
     * while we're reading it.  We may still lose packets at other times.
     */
    crit_enter();

    /*
     * Since we don't set allowLargePackets bit in MacControl register,
     * we can assume that totlen <= 1500bytes.
     * The while loop will be performed if we have a packet with
     * MLEN < m_len < MINCLSIZE.
     */
    while (totlen > 0) {
        if (top) {
            m = sc->mb[sc->next_mb];
            sc->mb[sc->next_mb] = NULL;
            if (m == NULL) {
                MGET(m, M_NOWAIT, MT_DATA);
                if (m == NULL) {
                    crit_exit();
                    m_freem(top);
                    return NULL;
                }
            } else {
                sc->next_mb = (sc->next_mb + 1) % MAX_MBS;
            }
            len = MLEN;
        }
        if (totlen >= MINCLSIZE) {
	    MCLGET(m, M_NOWAIT);
	    if (m->m_flags & M_EXT)
		len = MCLBYTES;
        }
        len = min(totlen, len);
        if (len > 3)
	    bus_space_read_multi_4(sc->vx_btag, sc->vx_bhandle,
	        VX_W1_RX_PIO_RD_1, mtod(m, u_int32_t *), len / 4);
	if (len & 3) {
	    bus_space_read_multi_1(sc->vx_btag, sc->vx_bhandle,
	        VX_W1_RX_PIO_RD_1, mtod(m, u_int8_t *) + (len & ~3),
		len & 3);
	}
        m->m_len = len;
        totlen -= len;
        *mp = m;
        mp = &m->m_next;
    }

    CSR_WRITE_2(sc, VX_COMMAND, RX_DISCARD_TOP_PACK);

    crit_exit();

    return top;
}


static int
vxioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
    struct vx_softc *sc = ifp->if_softc;
    struct ifreq *ifr = (struct ifreq *) data;
    int error = 0;

    switch (cmd) {
    case SIOCSIFFLAGS:
	if ((ifp->if_flags & IFF_UP) == 0 &&
	    (ifp->if_flags & IFF_RUNNING) != 0) {
	    /*
             * If interface is marked up and it is stopped, then
             * start it.
             */
	    vxstop(sc);
	    ifp->if_flags &= ~IFF_RUNNING;
        } else if ((ifp->if_flags & IFF_UP) != 0 &&
                   (ifp->if_flags & IFF_RUNNING) == 0) {
            /*
             * If interface is marked up and it is stopped, then
             * start it.
             */
            vxinit(sc);
        } else {
            /*
             * deal with flags changes:
             * IFF_MULTICAST, IFF_PROMISC,
             * IFF_LINK0, IFF_LINK1,
             */
            vxsetfilter(sc);
            vxsetlink(sc);
        }
        break;

    case SIOCSIFMTU:
        /*
         * Set the interface MTU.
         */
        if (ifr->ifr_mtu > ETHERMTU) {
            error = EINVAL;
        } else {
            ifp->if_mtu = ifr->ifr_mtu;
        }
        break;

    case SIOCADDMULTI:
    case SIOCDELMULTI:
	/*
	 * Multicast list has changed; set the hardware filter
	 * accordingly.
	 */
	vxreset(sc);
	error = 0;
        break;


    default:
	ether_ioctl(ifp, cmd, data);
	break;
    }
    return (error);
}

static void
vxreset(struct vx_softc *sc)
{
    vxstop(sc);
    vxinit(sc);
}

static void
vxwatchdog(struct ifnet *ifp)
{
    struct vx_softc *sc = ifp->if_softc;

    if (ifp->if_flags & IFF_DEBUG)
	if_printf(ifp, "device timeout\n");
    ifq_clr_oactive(&ifp->if_snd);
    if_devstart(ifp);
    vxintr(sc);
}

void
vxstop(struct vx_softc *sc)
{
    struct ifnet *ifp = &sc->arpcom.ac_if;

    ifp->if_timer = 0;

    CSR_WRITE_2(sc, VX_COMMAND, RX_DISABLE);
    CSR_WRITE_2(sc, VX_COMMAND, RX_DISCARD_TOP_PACK);
    VX_BUSY_WAIT;
    CSR_WRITE_2(sc, VX_COMMAND, TX_DISABLE);
    CSR_WRITE_2(sc, VX_COMMAND, STOP_TRANSCEIVER);
    DELAY(800);
    CSR_WRITE_2(sc, VX_COMMAND, RX_RESET);
    VX_BUSY_WAIT;
    CSR_WRITE_2(sc, VX_COMMAND, TX_RESET);
    VX_BUSY_WAIT;
    CSR_WRITE_2(sc, VX_COMMAND, C_INTR_LATCH);
    CSR_WRITE_2(sc, VX_COMMAND, SET_RD_0_MASK);
    CSR_WRITE_2(sc, VX_COMMAND, SET_INTR_MASK);
    CSR_WRITE_2(sc, VX_COMMAND, SET_RX_FILTER);

    vxmbufempty(sc);
}

int
vxbusyeeprom(struct vx_softc *sc)
{
    int j, i = 100;

    while (i--) {
        j = CSR_READ_2(sc, VX_W0_EEPROM_COMMAND);
        if (j & EEPROM_BUSY)
            DELAY(100);
        else
            break;
    }
    if (!i) {
        if_printf(&sc->arpcom.ac_if, "eeprom failed to come ready\n");
        return (1);
    }
    return (0);
}

static void
vxmbuffill(void *sp)
{
    struct vx_softc *sc = (struct vx_softc *) sp;
    struct ifnet *ifp = &sc->arpcom.ac_if;

    lwkt_serialize_enter(ifp->if_serializer);
    vxmbuffill_serialized(sp);
    lwkt_serialize_exit(ifp->if_serializer);
}

static void
vxmbuffill_serialized(void *sp)
{
    struct vx_softc *sc = (struct vx_softc *) sp;
    int	i;

    i = sc->last_mb;
    do {
	if (sc->mb[i] == NULL)
	    MGET(sc->mb[i], M_NOWAIT, MT_DATA);
	if (sc->mb[i] == NULL)
	    break;
	i = (i + 1) % MAX_MBS;
    } while (i != sc->next_mb);
    sc->last_mb = i;
    /* If the queue was not filled, try again. */
    if (sc->last_mb != sc->next_mb) {
	callout_reset(&sc->vx_timer, 1, vxmbuffill, sc);
	sc->buffill_pending = 1;
    } else {
	sc->buffill_pending = 0;
    }
}

static void
vxmbufempty(struct vx_softc *sc)
{
    int	i;

    for (i = 0; i < MAX_MBS; i++) {
	if (sc->mb[i]) {
	    m_freem(sc->mb[i]);
	    sc->mb[i] = NULL;
	}
    }
    sc->last_mb = sc->next_mb = 0;
    if (sc->buffill_pending != 0)
	callout_stop(&sc->vx_timer);
}
