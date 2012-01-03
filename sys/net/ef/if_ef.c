/*-
 * Copyright (c) 1999, 2000 Boris Popov
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
 * $FreeBSD: src/sys/net/if_ef.c,v 1.2.2.4 2001/02/22 09:27:04 bp Exp $
 */

#include "opt_inet.h"
#include "opt_ipx.h"
#include "opt_ef.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/thread2.h>

#include <net/ethernet.h>
#include <net/if_llc.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/netisr.h>
#include <net/route.h>
#include <net/bpf.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#endif

#ifdef IPX
#include <netproto/ipx/ipx.h>
#include <netproto/ipx/ipx_if.h>
#endif

/* internal frame types */
#define ETHER_FT_EII		0	/* Ethernet_II - default */
#define	ETHER_FT_8023		1	/* 802.3 (Novell) */
#define	ETHER_FT_8022		2	/* 802.2 */
#define	ETHER_FT_SNAP		3	/* SNAP */
#define	EF_NFT			4	/* total number of frame types */

#ifdef EF_DEBUG
#define EFDEBUG(format, args...) kprintf("%s: "format, __func__ ,## args)
#else
#define EFDEBUG(format, args...)
#endif

#define EFERROR(format, args...) kprintf("%s: "format, __func__ ,## args)

struct efnet {
	struct arpcom	ef_ac;
	struct ifnet *  ef_ifp;
	int		ef_frametype;
};

struct ef_link {
	SLIST_ENTRY(ef_link) el_next;
	struct ifnet	*el_ifp;		/* raw device for this clones */
	struct efnet	*el_units[EF_NFT];	/* our clones */
};

static SLIST_HEAD(ef_link_head, ef_link) efdev = {NULL};
static int efcount;

extern int (*ef_inputp)(struct ifnet*, const struct ether_header *eh,
		struct mbuf *m);
extern int (*ef_outputp)(struct ifnet *ifp, struct mbuf **mp,
		struct sockaddr *dst, short *tp, int *hlen);

/*
static void ef_reset (struct ifnet *);
*/
static int ef_attach(struct efnet *sc);
static int ef_detach(struct efnet *sc);
static void ef_init(void *);
static int ef_ioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
static void ef_start(struct ifnet *);
static int ef_input(struct ifnet*, const struct ether_header *, struct mbuf *);
static int ef_output(struct ifnet *ifp, struct mbuf **mp,
		struct sockaddr *dst, short *tp, int *hlen);

static int ef_load(void);
static int ef_unload(void);

/*
 * Install the interface, most of structure initialization done in ef_clone()
 */
static int
ef_attach(struct efnet *sc)
{
	struct ifnet *ifp = (struct ifnet*)&sc->ef_ac.ac_if;

	ifp->if_start = ef_start;
	ifp->if_watchdog = NULL;
	ifp->if_init = ef_init;
	ifp->if_snd.ifq_maxlen = IFQ_MAXLEN;
	ifp->if_flags = (IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
	/*
	 * Attach the interface
	 */
	ether_ifattach(ifp, IF_LLADDR(sc->ef_ifp), NULL);

	ifp->if_resolvemulti = 0;
	ifp->if_type = IFT_XETHER;
	ifp->if_flags |= IFF_RUNNING;

	EFDEBUG("%s: attached\n", ifp->if_xname);
	return 1;
}

/*
 * This is for _testing_only_, just removes interface from interfaces list
 */
static int
ef_detach(struct efnet *sc)
{
	ether_ifdetach(&sc->ef_ac.ac_if);
	return 0;
}

static void
ef_init(void *foo __unused)
{
}

static int
ef_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct efnet *sc = ifp->if_softc;
	struct ifaddr *ifa = (struct ifaddr*)data;
	int error;

	EFDEBUG("IOCTL %ld for %s\n", cmd, ifp->if_xname);
	error = 0;
	crit_enter();
	switch (cmd) {
	    case SIOCSIFADDR:
		if (sc->ef_frametype == ETHER_FT_8023 && 
		    ifa->ifa_addr->sa_family != AF_IPX) {
			error = EAFNOSUPPORT;
			break;
		}
		ifp->if_flags |= IFF_UP; 
		/* FALL THROUGH */
	    case SIOCGIFADDR:
	    case SIOCSIFMTU:
		error = ether_ioctl(ifp, cmd, data);
		break;
	    case SIOCSIFFLAGS:
		error = 0;
		break;
	    default:
		error = EINVAL;
	}
	crit_exit();
	return error;
}

/*
 * Currently packet prepared in the ether_output(), but this can be a better
 * place.
 */
static void
ef_start(struct ifnet *ifp)
{
	struct efnet *sc = (struct efnet*)ifp->if_softc;
	struct ifnet *p;
	struct mbuf *m;

	ifp->if_flags |= IFF_OACTIVE;
	p = sc->ef_ifp;

	EFDEBUG("\n");
	for (;;) {
		IF_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;
		BPF_MTAP(ifp, m);
		if (IF_QFULL(&p->if_snd)) {
			IF_DROP(&p->if_snd);
			ifp->if_oerrors++;
			m_freem(m);
			continue;
		}
		IF_ENQUEUE(&p->if_snd, m);
		if ((p->if_flags & IFF_OACTIVE) == 0) {
			p->if_start(p);
			ifp->if_opackets++;
		}
	}
	ifp->if_flags &= ~IFF_OACTIVE;
	return;
}

/*
 * Inline functions do not put additional overhead to procedure call or
 * parameter passing but simplify the code
 */
static __inline int
ef_inputEII(struct mbuf *m, struct llc* l, u_short ether_type)
{
	int isr;

	switch(ether_type) {
#ifdef IPX
	case ETHERTYPE_IPX:
		isr = NETISR_IPX;
		break;
#endif
#ifdef INET
	case ETHERTYPE_IP:
#ifdef notyet
		if (ipflow_fastforward(m))
			return (0);
#endif
		isr = NETISR_IP;
		break;
	case ETHERTYPE_ARP:
		isr = NETISR_ARP;
		break;
#endif
	default:
		return (EPROTONOSUPPORT);
	}
	netisr_queue(isr, m);
	return (0);
}

static __inline int
ef_inputSNAP(struct mbuf *m, struct llc* l, u_short ether_type)
{
	int isr;

	switch(ether_type) {
#ifdef IPX
	case ETHERTYPE_IPX:
		m_adj(m, 8);
		isr = NETISR_IPX;
		break;
#endif
	default:
		return (EPROTONOSUPPORT);
	}
	netisr_queue(isr, m);
	return (0);
}

static __inline int
ef_input8022(struct mbuf *m, struct llc* l, u_short ether_type)
{
	int isr;

	switch(ether_type) {
#ifdef IPX
	case 0xe0:
		m_adj(m, 3);
		isr = NETISR_IPX;
		break;
#endif
	default:
		return (EPROTONOSUPPORT);
	}
	netisr_queue(isr, m);
	return (0);
}

/*
 * Called from ether_input()
 */
static int
ef_input(struct ifnet *ifp, const struct ether_header *eh, struct mbuf *m)
{
	u_short ether_type;
	int ft = -1;
	struct efnet *efp;
	struct ifnet *eifp;
	struct llc *l;
	struct ef_link *efl;
	int isr;

	ether_type = ntohs(eh->ether_type);
	if (ether_type < ETHERMTU) {
		l = mtod(m, struct llc*);
		if (l->llc_dsap == 0xff && l->llc_ssap == 0xff) {
			/* 
			 * Novell's "802.3" frame
			 */
			ft = ETHER_FT_8023;
		} else if (l->llc_dsap == 0xaa && l->llc_ssap == 0xaa) {
			/*
			 * 802.2/SNAP
			 */
			ft = ETHER_FT_SNAP;
			ether_type = ntohs(l->llc_un.type_snap.ether_type);
		} else if (l->llc_dsap == l->llc_ssap) {
			/*
			 * 802.3/802.2
			 */
			ft = ETHER_FT_8022;
			ether_type = l->llc_ssap;
		}
	} else
		ft = ETHER_FT_EII;

	if (ft == -1) {
		EFDEBUG("Unrecognised ether_type %x\n", ether_type);
		return EPROTONOSUPPORT;
	}

	/*
	 * Check if interface configured for the given frame
	 */
	efp = NULL;
	SLIST_FOREACH(efl, &efdev, el_next) {
		if (efl->el_ifp == ifp) {
			efp = efl->el_units[ft];
			break;
		}
	}
	if (efp == NULL) {
		EFDEBUG("Can't find if for %d\n", ft);
		return EPROTONOSUPPORT;
	}
	eifp = &efp->ef_ac.ac_if;
	if ((eifp->if_flags & IFF_UP) == 0)
		return EPROTONOSUPPORT;
	eifp->if_ibytes += m->m_pkthdr.len + sizeof (*eh);
	m->m_pkthdr.rcvif = eifp;

	if (eifp->if_bpf)
		bpf_ptap(eifp->if_bpf, m, eh, ETHER_HDR_LEN);

	/*
	 * Now we ready to adjust mbufs and pass them to protocol intr's
	 */
	switch(ft) {
	case ETHER_FT_EII:
		return (ef_inputEII(m, l, ether_type));
		break;
#ifdef IPX
	case ETHER_FT_8023:		/* only IPX can be here */
		isr = NETISR_IPX;
		break;
#endif
	case ETHER_FT_SNAP:
		return (ef_inputSNAP(m, l, ether_type));
		break;
	case ETHER_FT_8022:
		return (ef_input8022(m, l, ether_type));
		break;
	default:
		EFDEBUG("No support for frame %d and proto %04x\n",
			ft, ether_type);
		return (EPROTONOSUPPORT);
	}
	netisr_queue(isr, m);
	return (0);
}

static int
ef_output(struct ifnet *ifp, struct mbuf **mp, struct sockaddr *dst, short *tp,
	int *hlen)
{
	struct efnet *sc = (struct efnet*)ifp->if_softc;
	struct mbuf *m = *mp;
	u_char *cp;
	short type;

	if (ifp->if_type != IFT_XETHER)
		return ENETDOWN;
	switch (sc->ef_frametype) {
	    case ETHER_FT_EII:
#ifdef IPX
		type = htons(ETHERTYPE_IPX);
#else
		return EPFNOSUPPORT;
#endif
		break;
	    case ETHER_FT_8023:
		type = htons(m->m_pkthdr.len);
		break;
	    case ETHER_FT_8022:
		M_PREPEND(m, ETHER_HDR_LEN + 3, MB_WAIT);
		if (m == NULL) {
			*mp = NULL;
			return ENOBUFS;
		}
		/*
		 * Ensure that ethernet header and next three bytes
		 * will fit into single mbuf
		 */
		m = m_pullup(m, ETHER_HDR_LEN + 3);
		if (m == NULL) {
			*mp = NULL;
			return ENOBUFS;
		}
		m_adj(m, ETHER_HDR_LEN);
		type = htons(m->m_pkthdr.len);
		cp = mtod(m, u_char *);
		*cp++ = 0xE0;
		*cp++ = 0xE0;
		*cp++ = 0x03;
		*hlen += 3;
		break;
	    case ETHER_FT_SNAP:
		M_PREPEND(m, 8, MB_WAIT);
		if (m == NULL) {
			*mp = NULL;
			return ENOBUFS;
		}
		type = htons(m->m_pkthdr.len);
		cp = mtod(m, u_char *);
		bcopy("\xAA\xAA\x03\x00\x00\x00\x81\x37", cp, 8);
		*hlen += 8;
		break;
	    default:
		return EPFNOSUPPORT;
	}
	*mp = m;
	*tp = type;
	return 0;
}

/*
 * Create clone from the given interface
 */
static int
ef_clone(struct ef_link *efl, int ft)
{
	struct efnet *efp;
	struct ifnet *eifp;
	struct ifnet *ifp = efl->el_ifp;

	efp = (struct efnet*)kmalloc(sizeof(struct efnet), M_IFADDR,
	    M_WAITOK | M_ZERO);
	efp->ef_ifp = ifp;
	efp->ef_frametype = ft;
	eifp = &efp->ef_ac.ac_if;
	ksnprintf(eifp->if_xname, IFNAMSIZ,
	    "%sf%d", ifp->if_xname, efp->ef_frametype);
	eifp->if_dname = "ef";
	eifp->if_dunit = IF_DUNIT_NONE;
	eifp->if_softc = efp;
	if (ifp->if_ioctl)
		eifp->if_ioctl = ef_ioctl;
	efl->el_units[ft] = efp;
	return 0;
}

static int
ef_load(void)
{
	struct ifnet *ifp;
	struct efnet *efp;
	struct ef_link *efl = NULL;
	int error = 0, d;

	TAILQ_FOREACH(ifp, &ifnet, if_link) {
		if (ifp->if_type != IFT_ETHER) continue;
		EFDEBUG("Found interface %s\n", ifp->if_xname);
		efl = (struct ef_link*)kmalloc(sizeof(struct ef_link), 
		    M_IFADDR, M_WAITOK | M_ZERO);

		efl->el_ifp = ifp;
#ifdef ETHER_II
		error = ef_clone(efl, ETHER_FT_EII);
		if (error) break;
#endif
#ifdef ETHER_8023
		error = ef_clone(efl, ETHER_FT_8023);
		if (error) break;
#endif
#ifdef ETHER_8022
		error = ef_clone(efl, ETHER_FT_8022);
		if (error) break;
#endif
#ifdef ETHER_SNAP
		error = ef_clone(efl, ETHER_FT_SNAP);
		if (error) break;
#endif
		efcount++;
		SLIST_INSERT_HEAD(&efdev, efl, el_next);
	}
	if (error) {
		if (efl)
			SLIST_INSERT_HEAD(&efdev, efl, el_next);
		SLIST_FOREACH(efl, &efdev, el_next) {
			for (d = 0; d < EF_NFT; d++)
				if (efl->el_units[d])
					kfree(efl->el_units[d], M_IFADDR);
			kfree(efl, M_IFADDR);
		}
		return error;
	}
	SLIST_FOREACH(efl, &efdev, el_next) {
		for (d = 0; d < EF_NFT; d++) {
			efp = efl->el_units[d];
			if (efp)
				ef_attach(efp);
		}
	}
	ef_inputp = ef_input;
	ef_outputp = ef_output;
	EFDEBUG("Loaded\n");
	return 0;
}

static int
ef_unload(void)
{
	struct efnet *efp;
	struct ef_link *efl;
	int d;

	ef_inputp = NULL;
	ef_outputp = NULL;
	SLIST_FOREACH(efl, &efdev, el_next) {
		for (d = 0; d < EF_NFT; d++) {
			efp = efl->el_units[d];
			if (efp) {
				ef_detach(efp);
			}
		}
	}
	EFDEBUG("Unloaded\n");
	return 0;
}

static int 
if_ef_modevent(module_t mod, int type, void *data)
{
	switch ((modeventtype_t)type) {
	    case MOD_LOAD:
		return ef_load();
	    case MOD_UNLOAD:
		return ef_unload();
	    default:
		break;
	}
	return 0;
}

static moduledata_t if_ef_mod = {
	"if_ef", if_ef_modevent, NULL
};

DECLARE_MODULE(if_ef, if_ef_mod, SI_SUB_PSEUDO, SI_ORDER_MIDDLE);
