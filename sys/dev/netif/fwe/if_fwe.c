/*
 * Copyright (c) 2002-2003
 * 	Hidetoshi Shimokawa. All rights reserved.
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
 *
 *	This product includes software developed by Hidetoshi Shimokawa.
 *
 * 4. Neither the name of the author nor the names of its contributors
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
 * $FreeBSD: src/sys/dev/firewire/if_fwe.c,v 1.27 2004/01/08 14:58:09 simokawa Exp $
 */

#include "opt_inet.h"
#include "opt_ifpoll.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/thread2.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#ifdef __DragonFly__
#include <net/ifq_var.h>
#include <net/if_poll.h>
#include <net/vlan/if_vlan_var.h>
#include <bus/firewire/firewire.h>
#include <bus/firewire/firewirereg.h>
#include "if_fwevar.h"
#else
#include <net/if_vlan_var.h>

#include <dev/firewire/firewire.h>
#include <dev/firewire/firewirereg.h>
#include <dev/firewire/if_fwevar.h>
#endif

#define FWEDEBUG	if (fwedebug) if_printf
#define TX_MAX_QUEUE	(FWMAXQUEUE - 1)

/* network interface */
static void fwe_start (struct ifnet *, struct ifaltq_subque *);
static int fwe_ioctl (struct ifnet *, u_long, caddr_t, struct ucred *);
static void fwe_init (void *);

static void fwe_output_callback (struct fw_xfer *);
static void fwe_as_output (struct fwe_softc *, struct ifnet *);
static void fwe_as_input (struct fw_xferq *);

static int fwedebug = 0;
static int stream_ch = 1;
static int tx_speed = 2;
static int rx_queue_len = FWMAXQUEUE;

MALLOC_DEFINE(M_FWE, "if_fwe", "Ethernet over FireWire interface");
SYSCTL_INT(_debug, OID_AUTO, if_fwe_debug, CTLFLAG_RW, &fwedebug, 0,
	"Enable debug output");
SYSCTL_DECL(_hw_firewire);
SYSCTL_NODE(_hw_firewire, OID_AUTO, fwe, CTLFLAG_RD, 0,
	"Ethernet emulation subsystem");
SYSCTL_INT(_hw_firewire_fwe, OID_AUTO, stream_ch, CTLFLAG_RW, &stream_ch, 0,
	"Stream channel to use");
SYSCTL_INT(_hw_firewire_fwe, OID_AUTO, tx_speed, CTLFLAG_RW, &tx_speed, 0,
	"Transmission speed");
SYSCTL_INT(_hw_firewire_fwe, OID_AUTO, rx_queue_len, CTLFLAG_RW, &rx_queue_len,
	0, "Length of the receive queue");

TUNABLE_INT("hw.firewire.fwe.stream_ch", &stream_ch);
TUNABLE_INT("hw.firewire.fwe.tx_speed", &tx_speed);
TUNABLE_INT("hw.firewire.fwe.rx_queue_len", &rx_queue_len);

#ifdef IFPOLL_ENABLE

static void
fwe_npoll_compat(struct ifnet *ifp, void *arg __unused, int count)
{
	struct fwe_softc *fwe;
	struct firewire_comm *fc;
	int check_status = 0;

	ASSERT_SERIALIZED(ifp->if_serializer);

	fwe = ((struct fwe_eth_softc *)ifp->if_softc)->fwe;
	fc = fwe->fd.fc;

	if (fwe->npoll.ifpc_stcount-- == 0) {
		fwe->npoll.ifpc_stcount = fwe->npoll.ifpc_stfrac;
		check_status = 1;
	}
	fc->poll(fc, check_status?0:1, count);
}

static void
fwe_npoll(struct ifnet *ifp, struct ifpoll_info *info)
{
	struct fwe_softc *fwe;
	struct firewire_comm *fc;

	ASSERT_SERIALIZED(ifp->if_serializer);

	fwe = ((struct fwe_eth_softc *)ifp->if_softc)->fwe;
	fc = fwe->fd.fc;

	if (info != NULL) {
		int cpuid = fwe->npoll.ifpc_cpuid;

		info->ifpi_rx[cpuid].poll_func = fwe_npoll_compat;
		info->ifpi_rx[cpuid].arg = NULL;
		info->ifpi_rx[cpuid].serializer = ifp->if_serializer;

		if (ifp->if_flags & IFF_RUNNING) {
			/* disable interrupts */
			fc->set_intr(fc, 0);
			fwe->npoll.ifpc_stcount = 0;
		}
		ifq_set_cpuid(&ifp->if_snd, cpuid);
	} else {
		if (ifp->if_flags & IFF_RUNNING) {
			/* enable interrupts */
			fc->set_intr(fc, 1);
		}
		ifq_set_cpuid(&ifp->if_snd, 0 /* XXX */);
	}
}

#endif	/* IFPOLL_ENABLE */

static int
fwe_probe(device_t dev)
{
	device_t pa;

	pa = device_get_parent(dev);
	if(device_get_unit(dev) != device_get_unit(pa)){
		return(ENXIO);
	}

	device_set_desc(dev, "Ethernet over FireWire");
	return (0);
}

static int
fwe_attach(device_t dev)
{
	struct fwe_softc *fwe;
	struct ifnet *ifp;
	uint8_t eaddr[ETHER_ADDR_LEN];
	struct fw_eui64 *eui;

	fwe = ((struct fwe_softc *)device_get_softc(dev));

	/* XXX */
	fwe->stream_ch = stream_ch;
	fwe->dma_ch = -1;

	fwe->fd.fc = device_get_ivars(dev);
	if (tx_speed < 0)
		tx_speed = fwe->fd.fc->speed;

	fwe->fd.dev = dev;
	fwe->fd.post_explore = NULL;
	fwe->eth_softc.fwe = fwe;

	fwe->pkt_hdr.mode.stream.tcode = FWTCODE_STREAM;
	fwe->pkt_hdr.mode.stream.sy = 0;
	fwe->pkt_hdr.mode.stream.chtag = fwe->stream_ch;

	/* generate fake MAC address: first and last 3bytes from eui64 */
#define LOCAL (0x02)
#define GROUP (0x01)

	eui = &fwe->fd.fc->eui;
	eaddr[0] = (FW_EUI64_BYTE(eui, 0) | LOCAL) & ~GROUP;
	eaddr[1] = FW_EUI64_BYTE(eui, 1);
	eaddr[2] = FW_EUI64_BYTE(eui, 2);
	eaddr[3] = FW_EUI64_BYTE(eui, 5);
	eaddr[4] = FW_EUI64_BYTE(eui, 6);
	eaddr[5] = FW_EUI64_BYTE(eui, 7);

	/* fill the rest and attach interface */	
	ifp = &fwe->fwe_if;
	ifp->if_softc = &fwe->eth_softc;

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_init = fwe_init;
	ifp->if_start = fwe_start;
	ifp->if_ioctl = fwe_ioctl;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
#ifdef IFPOLL_ENABLE
	ifp->if_npoll = fwe_npoll;
#endif
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = (IFF_BROADCAST|IFF_SIMPLEX|IFF_MULTICAST);
	ifq_set_maxlen(&ifp->if_snd, TX_MAX_QUEUE);
	ifq_set_ready(&ifp->if_snd);

	ether_ifattach(ifp, eaddr, NULL);

        /* Tell the upper layer(s) we support long frames. */
	ifp->if_data.ifi_hdrlen = sizeof(struct ether_vlan_header);

#ifdef IFPOLL_ENABLE
	ifpoll_compat_setup(&fwe->npoll, NULL, NULL, device_get_unit(dev),
	    ifp->if_serializer);
#endif

	FWEDEBUG(ifp, "interface created\n");
	return 0;
}

static void
fwe_stop(struct fwe_softc *fwe)
{
	struct firewire_comm *fc;
	struct fw_xferq *xferq;
	struct ifnet *ifp = &fwe->fwe_if;
	struct fw_xfer *xfer, *next;
	int i;

	fc = fwe->fd.fc;

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (fwe->dma_ch >= 0) {
		xferq = fc->ir[fwe->dma_ch];

		if (xferq->flag & FWXFERQ_RUNNING)
			fc->irx_disable(fc, fwe->dma_ch);
		xferq->flag &= 
			~(FWXFERQ_MODEMASK | FWXFERQ_OPEN | FWXFERQ_STREAM |
			FWXFERQ_EXTBUF | FWXFERQ_HANDLER | FWXFERQ_CHTAGMASK);
		xferq->hand =  NULL;

		for (i = 0; i < xferq->bnchunk; i ++)
			m_freem(xferq->bulkxfer[i].mbuf);
		kfree(xferq->bulkxfer, M_FWE);

		for (xfer = STAILQ_FIRST(&fwe->xferlist); xfer != NULL;
					xfer = next) {
			next = STAILQ_NEXT(xfer, link);
			fw_xfer_free(xfer);
		}
		STAILQ_INIT(&fwe->xferlist);

		xferq->bulkxfer =  NULL;
		fwe->dma_ch = -1;
	}
}

static int
fwe_detach(device_t dev)
{
	struct fwe_softc *fwe = device_get_softc(dev);

	lwkt_serialize_enter(fwe->fwe_if.if_serializer);
	fwe_stop(fwe);
	lwkt_serialize_exit(fwe->fwe_if.if_serializer);

	ether_ifdetach(&fwe->fwe_if);
	return 0;
}

static void
fwe_init(void *arg)
{
	struct fwe_softc *fwe = ((struct fwe_eth_softc *)arg)->fwe;
	struct firewire_comm *fc;
	struct ifnet *ifp = &fwe->fwe_if;
	struct fw_xferq *xferq;
	struct fw_xfer *xfer;
	struct mbuf *m;
	int i;

	FWEDEBUG(ifp, "initializing\n");

	/* XXX keep promiscoud mode */
	ifp->if_flags |= IFF_PROMISC;

	fc = fwe->fd.fc;
#define START 0
	if (fwe->dma_ch < 0) {
		for (i = START; i < fc->nisodma; i ++) {
			xferq = fc->ir[i];
			if ((xferq->flag & FWXFERQ_OPEN) == 0)
				goto found;
		}
		if_printf(ifp, "no free dma channel\n");
		return;
found:
		fwe->dma_ch = i;
		fwe->stream_ch = stream_ch;
		fwe->pkt_hdr.mode.stream.chtag = fwe->stream_ch;
		/* allocate DMA channel and init packet mode */
		xferq->flag |= FWXFERQ_OPEN | FWXFERQ_EXTBUF |
				FWXFERQ_HANDLER | FWXFERQ_STREAM;
		xferq->flag &= ~0xff;
		xferq->flag |= fwe->stream_ch & 0xff;
		/* register fwe_input handler */
		xferq->sc = (caddr_t) fwe;
		xferq->hand = fwe_as_input;
		xferq->bnchunk = rx_queue_len;
		xferq->bnpacket = 1;
		xferq->psize = MCLBYTES;
		xferq->queued = 0;
		xferq->buf = NULL;
		xferq->bulkxfer = (struct fw_bulkxfer *) kmalloc(
			sizeof(struct fw_bulkxfer) * xferq->bnchunk,
							M_FWE, M_WAITOK);
		STAILQ_INIT(&xferq->stvalid);
		STAILQ_INIT(&xferq->stfree);
		STAILQ_INIT(&xferq->stdma);
		xferq->stproc = NULL;
		for (i = 0; i < xferq->bnchunk; i ++) {
			m = m_getcl(MB_WAIT, MT_DATA, M_PKTHDR);
			xferq->bulkxfer[i].mbuf = m;
			if (m != NULL) {
				m->m_len = m->m_pkthdr.len = m->m_ext.ext_size;
				STAILQ_INSERT_TAIL(&xferq->stfree,
						&xferq->bulkxfer[i], link);
			} else {
				if_printf(ifp, "fwe_init: m_getcl failed\n");
			}
		}
		STAILQ_INIT(&fwe->xferlist);
		for (i = 0; i < TX_MAX_QUEUE; i++) {
			xfer = fw_xfer_alloc(M_FWE);
			if (xfer == NULL)
				break;
			xfer->send.spd = tx_speed;
			xfer->fc = fwe->fd.fc;
			xfer->retry_req = fw_asybusy;
			xfer->sc = (caddr_t)fwe;
			xfer->act.hand = fwe_output_callback;
			STAILQ_INSERT_TAIL(&fwe->xferlist, xfer, link);
		}
	} else
		xferq = fc->ir[fwe->dma_ch];

#ifdef IFPOLL_ENABLE
	/* Disable interrupt, if polling(4) is enabled */
	if (ifp->if_flags & IFF_NPOLLING) {
		fc->set_intr(fc, 0);
		fwe->npoll.ifpc_stcount = 0;
	} else
#endif
	fc->set_intr(fc, 1);

	/* start dma */
	if ((xferq->flag & FWXFERQ_RUNNING) == 0)
		fc->irx_enable(fc, fwe->dma_ch);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
}


static int
fwe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct fwe_softc *fwe = ((struct fwe_eth_softc *)ifp->if_softc)->fwe;
	struct ifstat *ifs = NULL;
	int error = 0, len;

	switch (cmd) {
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_flags & IFF_RUNNING))
				fwe_init(&fwe->eth_softc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				fwe_stop(fwe);
		}
		/* XXX keep promiscoud mode */
		ifp->if_flags |= IFF_PROMISC;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

	case SIOCGIFSTATUS:
		ifs = (struct ifstat *)data;
		len = strlen(ifs->ascii);
		if (len < sizeof(ifs->ascii))
			ksnprintf(ifs->ascii + len,
				sizeof(ifs->ascii) - len,
				"\tch %d dma %d\n",
					fwe->stream_ch, fwe->dma_ch);
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return (error);
}

static void
fwe_output_callback(struct fw_xfer *xfer)
{
	struct fwe_softc *fwe;
	struct ifnet *ifp;

	fwe = (struct fwe_softc *)xfer->sc;
	ifp = &fwe->fwe_if;
	lwkt_serialize_enter(ifp->if_serializer);
	/* XXX error check */
	FWEDEBUG(ifp, "resp = %d\n", xfer->resp);
	if (xfer->resp != 0)
		IFNET_STAT_INC(ifp, oerrors, 1);
		
	m_freem(xfer->mbuf);
	fw_xfer_unload(xfer);

	STAILQ_INSERT_TAIL(&fwe->xferlist, xfer, link);

	/* for queue full */
	if (!ifq_is_empty(&ifp->if_snd))
		if_devstart(ifp);
	lwkt_serialize_exit(ifp->if_serializer);
}

static void
fwe_start(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	struct fwe_softc *fwe = ((struct fwe_eth_softc *)ifp->if_softc)->fwe;

	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	FWEDEBUG(ifp, "starting\n");

	if (fwe->dma_ch < 0) {
		FWEDEBUG(ifp, "not ready\n");

		ifq_purge(&ifp->if_snd);
	} else {
		ifq_set_oactive(&ifp->if_snd);

		if (!ifq_is_empty(&ifp->if_snd))
			fwe_as_output(fwe, ifp);

		ifq_clr_oactive(&ifp->if_snd);
	}
}

#define HDR_LEN 4
#ifndef ETHER_ALIGN
#define ETHER_ALIGN 2
#endif
/* Async. stream output */
static void
fwe_as_output(struct fwe_softc *fwe, struct ifnet *ifp)
{
	struct mbuf *m;
	struct fw_xfer *xfer;
	struct fw_xferq *xferq;
	struct fw_pkt *fp;
	int i = 0;

	xfer = NULL;
	xferq = fwe->fd.fc->atq;
	while (xferq->queued < xferq->maxq - 1) {
		xfer = STAILQ_FIRST(&fwe->xferlist);
		if (xfer == NULL) {
			if_printf(ifp, "lack of xfer\n");
			return;
		}
		m = ifq_dequeue(&ifp->if_snd);
		if (m == NULL)
			break;
		STAILQ_REMOVE_HEAD(&fwe->xferlist, link);
		BPF_MTAP(ifp, m);

		/* keep ip packet alignment for alpha */
		M_PREPEND(m, ETHER_ALIGN, MB_DONTWAIT);
		fp = &xfer->send.hdr;
		*(u_int32_t *)&xfer->send.hdr = *(int32_t *)&fwe->pkt_hdr;
		fp->mode.stream.len = m->m_pkthdr.len;
		xfer->mbuf = m;
		xfer->send.pay_len = m->m_pkthdr.len;

		if (fw_asyreq(fwe->fd.fc, -1, xfer) != 0) {
			/* error */
			IFNET_STAT_INC(ifp, oerrors, 1);
			/* XXX set error code */
			fwe_output_callback(xfer);
		} else {
			IFNET_STAT_INC(ifp, opackets, 1);
			i++;
		}
	}
#if 0
	if (i > 1)
		if_printf(ifp, "%d queued\n", i);
#endif
	if (i > 0)
		xferq->start(fwe->fd.fc);
}

/* Async. stream output */
static void
fwe_as_input(struct fw_xferq *xferq)
{
	struct mbuf *m, *m0;
	struct ifnet *ifp;
	struct fwe_softc *fwe;
	struct fw_bulkxfer *sxfer;
	struct fw_pkt *fp;

	fwe = (struct fwe_softc *)xferq->sc;
	ifp = &fwe->fwe_if;
	lwkt_serialize_enter(ifp->if_serializer);
	while ((sxfer = STAILQ_FIRST(&xferq->stvalid)) != NULL) {
		STAILQ_REMOVE_HEAD(&xferq->stvalid, link);
		fp = mtod(sxfer->mbuf, struct fw_pkt *);
		if (fwe->fd.fc->irx_post != NULL)
			fwe->fd.fc->irx_post(fwe->fd.fc, fp->mode.ld);
		m = sxfer->mbuf;

		/* insert new rbuf */
		sxfer->mbuf = m0 = m_getcl(MB_DONTWAIT, MT_DATA, M_PKTHDR);
		if (m0 != NULL) {
			m0->m_len = m0->m_pkthdr.len = m0->m_ext.ext_size;
			STAILQ_INSERT_TAIL(&xferq->stfree, sxfer, link);
		} else {
			if_printf(ifp, "fwe_as_input: m_getcl failed\n");
		}

		if (sxfer->resp != 0 || fp->mode.stream.len <
		    ETHER_ALIGN + sizeof(struct ether_header)) {
			m_freem(m);
			IFNET_STAT_INC(ifp, ierrors, 1);
			continue;
		}

		m->m_data += HDR_LEN + ETHER_ALIGN;
		m->m_len = m->m_pkthdr.len =
				fp->mode.stream.len - ETHER_ALIGN;
		m->m_pkthdr.rcvif = ifp;
#if 0
		FWEDEBUG(ifp, "%02x %02x %02x %02x %02x %02x\n"
			 "%02x %02x %02x %02x %02x %02x\n"
			 "%02x %02x %02x %02x\n"
			 "%02x %02x %02x %02x\n"
			 "%02x %02x %02x %02x\n"
			 "%02x %02x %02x %02x\n",
			 c[0], c[1], c[2], c[3], c[4], c[5],
			 c[6], c[7], c[8], c[9], c[10], c[11],
			 c[12], c[13], c[14], c[15],
			 c[16], c[17], c[18], c[19],
			 c[20], c[21], c[22], c[23],
			 c[20], c[21], c[22], c[23]
		 );
#endif
		ifp->if_input(ifp, m, NULL, -1);
		IFNET_STAT_INC(ifp, ipackets, 1);
	}
	if (STAILQ_FIRST(&xferq->stfree) != NULL)
		fwe->fd.fc->irx_enable(fwe->fd.fc, fwe->dma_ch);
	lwkt_serialize_exit(ifp->if_serializer);
}


static devclass_t fwe_devclass;

/*
 * Because fwe is a static device that always exists under any attached
 * firewire device, and not scanned by the firewire device, we need an 
 * identify function to install the device.
 */
static device_method_t fwe_methods[] = {
	/* device interface */
	DEVMETHOD(device_identify,	bus_generic_identify_sameunit),
	DEVMETHOD(device_probe,		fwe_probe),
	DEVMETHOD(device_attach,	fwe_attach),
	DEVMETHOD(device_detach,	fwe_detach),
	DEVMETHOD_END
};

static driver_t fwe_driver = {
        "fwe",
	fwe_methods,
	sizeof(struct fwe_softc),
};


DECLARE_DUMMY_MODULE(fwe);
DRIVER_MODULE(fwe, firewire, fwe_driver, fwe_devclass, NULL, NULL);
MODULE_VERSION(fwe, 1);
MODULE_DEPEND(fwe, firewire, 1, 1, 1);
