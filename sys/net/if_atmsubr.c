/*      $NetBSD: if_atmsubr.c,v 1.10 1997/03/11 23:19:51 chuck Exp $       */

/*
 *
 * Copyright (c) 1996 Charles D. Cranor and Washington University.
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
 *      This product includes software developed by Charles D. Cranor and 
 *	Washington University.
 * 4. The name of the author may not be used to endorse or promote products
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
 * $FreeBSD: src/sys/net/if_atmsubr.c,v 1.10.2.1 2001/03/06 00:29:26 obrien Exp $
 * $DragonFly: src/sys/net/if_atmsubr.c,v 1.20 2008/06/05 18:06:32 swildner Exp $
 */

/*
 * if_atmsubr.c
 */

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_natm.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/errno.h>
#include <sys/serialize.h>

#include <sys/thread2.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/ifq_var.h>
#include <net/bpf.h>
#include <net/netisr.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_atm.h>

#include <netinet/in.h>
#include <netinet/if_atm.h>
#include <netinet/if_ether.h> /* XXX: for ETHERTYPE_* */
#if defined(INET) || defined(INET6)
#include <netinet/in_var.h>
#endif
#ifdef NATM
#include <netproto/natm/natm.h>
#endif

#ifndef ETHERTYPE_IPV6
#define ETHERTYPE_IPV6	0x86dd
#endif

#define gotoerr(e) { error = (e); goto bad;}

/*
 * atm_output: ATM output routine
 *   inputs:
 *     "ifp" = ATM interface to output to
 *     "m0" = the packet to output
 *     "dst" = the sockaddr to send to (either IP addr, or raw VPI/VCI)
 *     "rt0" = the route to use
 *   returns: error code   [0 == ok]
 *
 *   note: special semantic: if (dst == NULL) then we assume "m" already
 *		has an atm_pseudohdr on it and just send it directly.
 *		[for native mode ATM output]   if dst is null, then
 *		rt0 must also be NULL.
 */

int
atm_output(struct ifnet *ifp, struct mbuf *m0, struct sockaddr *dst,
	   struct rtentry *rt0)
{
	u_int16_t etype = 0;			/* if using LLC/SNAP */
	int error = 0, sz;
	struct atm_pseudohdr atmdst, *ad;
	struct mbuf *m = m0;
	struct rtentry *rt;
	struct atmllc *atmllc;
	struct atmllc *llc_hdr = NULL;
	u_int32_t atm_flags;
	struct altq_pktattr pktattr;

	if ((ifp->if_flags & (IFF_UP|IFF_RUNNING)) != (IFF_UP|IFF_RUNNING))
		gotoerr(ENETDOWN);

	/*
	 * if the queueing discipline needs packet classification,
	 * do it before prepending link headers.
	 */
	ifq_classify(&ifp->if_snd, m,
		     (dst != NULL ? dst->sa_family : AF_UNSPEC), &pktattr);

	/*
	 * check route
	 */
	if ((rt = rt0) != NULL) {

		if (!(rt->rt_flags & RTF_UP)) { /* route went down! */
			if ((rt0 = rt = RTALLOC1(dst, 0)) != NULL)
				rt->rt_refcnt--;
			else 
				gotoerr(EHOSTUNREACH);
		}

		if (rt->rt_flags & RTF_GATEWAY) {
			if (rt->rt_gwroute == NULL)
				goto lookup;
			if (((rt = rt->rt_gwroute)->rt_flags & RTF_UP) == 0) {
				rtfree(rt); rt = rt0;
			lookup: rt->rt_gwroute = RTALLOC1(rt->rt_gateway, 0);
				if ((rt = rt->rt_gwroute) == NULL)
					gotoerr(EHOSTUNREACH);
			}
		}

		/* XXX: put RTF_REJECT code here if doing ATMARP */

	}

	/*
	 * check for non-native ATM traffic   (dst != NULL)
	 */
	if (dst) {
		switch (dst->sa_family) {
#if defined(INET) || defined(INET6)
		case AF_INET:
		case AF_INET6:
			if (dst->sa_family == AF_INET6)
			        etype = htons(ETHERTYPE_IPV6);
			else
			        etype = htons(ETHERTYPE_IP);
			if (!atmresolve(rt, m, dst, &atmdst)) {
				m = NULL; 
				/* XXX: atmresolve already free'd it */
				gotoerr(EHOSTUNREACH);
				/* XXX: put ATMARP stuff here */
				/* XXX: watch who frees m on failure */
			}
			break;
#endif /* INET || INET6 */

		case AF_UNSPEC:
			/*
			 * XXX: bpfwrite. assuming dst contains 12 bytes
			 * (atm pseudo header (4) + LLC/SNAP (8))
			 */
			bcopy(dst->sa_data, &atmdst, sizeof(atmdst));
			llc_hdr = (struct atmllc *)(dst->sa_data + sizeof(atmdst));
			break;
			
		default:
#if defined(__NetBSD__) || defined(__OpenBSD__)
			kprintf("%s: can't handle af%d\n", ifp->if_xname, 
			    dst->sa_family);
#elif defined(__DragonFly__) || defined(__FreeBSD__) || defined(__bsdi__)
			kprintf("%s: can't handle af%d\n", ifp->if_xname, 
			    dst->sa_family);
#endif
			gotoerr(EAFNOSUPPORT);
		}

		/*
		 * must add atm_pseudohdr to data
		 */
		sz = sizeof(atmdst);
		atm_flags = ATM_PH_FLAGS(&atmdst);
		if (atm_flags & ATM_PH_LLCSNAP) sz += 8; /* sizeof snap == 8 */
		M_PREPEND(m, sz, MB_DONTWAIT);
		if (m == NULL)
			gotoerr(ENOBUFS);
		ad = mtod(m, struct atm_pseudohdr *);
		*ad = atmdst;
		if (atm_flags & ATM_PH_LLCSNAP) {
			atmllc = (struct atmllc *)(ad + 1);
			if (llc_hdr == NULL) {
			        bcopy(ATMLLC_HDR, atmllc->llchdr, 
				      sizeof(atmllc->llchdr));
				ATM_LLC_SETTYPE(atmllc, etype); 
					/* note: already in network order */
			}
			else
			        bcopy(llc_hdr, atmllc, sizeof(struct atmllc));
		}
	}

	/*
	 * Dispatch message to the interface.
	 */
	ifnet_serialize_tx(ifp);
	error = ifq_handoff(ifp, m, &pktattr);
	ifnet_deserialize_tx(ifp);
	return error;

bad:
	if (m != NULL)
		m_freem(m);
	return (error);
}

/*
 * Process a received ATM packet;
 * the packet is in the mbuf chain m.
 */
void
atm_input(struct ifnet *ifp, struct atm_pseudohdr *ah, struct mbuf *m,
	  void *rxhand)
{
	u_int16_t etype = ETHERTYPE_IP; /* default */
	int isr;

	if (!(ifp->if_flags & IFF_UP)) {
		m_freem(m);
		return;
	}
	ifp->if_ibytes += m->m_pkthdr.len;

	if (rxhand) {
#ifdef NATM
		struct natmpcb *npcb = rxhand;
		crit_enter();		/* in case 2 atm cards @ diff lvls */
		npcb->npcb_inq++;	/* count # in queue */
		crit_exit();
		isr = NETISR_NATM;
		m->m_pkthdr.rcvif = rxhand; /* XXX: overload */
#else
		kprintf("atm_input: NATM detected but not configured in kernel\n");
		m_freem(m);
		return;
#endif
	} else {
		/*
		 * handle LLC/SNAP header, if present
		 */
		if (ATM_PH_FLAGS(ah) & ATM_PH_LLCSNAP) {
			struct atmllc *alc;
			if (m->m_len < sizeof(*alc) &&
			    (m = m_pullup(m, sizeof(*alc))) == NULL)
				return; /* failed */
			alc = mtod(m, struct atmllc *);
			if (bcmp(alc, ATMLLC_HDR, 6)) {
#if defined(__NetBSD__) || defined(__OpenBSD__)
				kprintf("%s: recv'd invalid LLC/SNAP frame [vp=%d,vc=%d]\n",
				       ifp->if_xname, ATM_PH_VPI(ah), ATM_PH_VCI(ah));
#elif defined(__DragonFly__) || defined(__FreeBSD__) || defined(__bsdi__)
				kprintf("%s: recv'd invalid LLC/SNAP frame [vp=%d,vc=%d]\n",
				       ifp->if_xname, ATM_PH_VPI(ah), ATM_PH_VCI(ah));
#endif
				m_freem(m);
				return;
			}
			etype = ATM_LLC_TYPE(alc);
			m_adj(m, sizeof(*alc));
		}

		switch (etype) {
#ifdef INET
		case ETHERTYPE_IP:
			isr = NETISR_IP;
			break;
#endif
#ifdef INET6
		case ETHERTYPE_IPV6:
			isr = NETISR_IPV6;
			break;
#endif
		default:
			m_freem(m);
			return;
		}
	}

	m->m_flags &= ~M_HASH;
	netisr_queue(isr, m);
}

/*
 * Perform common duties while attaching to interface list
 */
void
atm_ifattach(struct ifnet *ifp, lwkt_serialize_t serializer)
{
	struct ifaddr_container *ifac;
	struct sockaddr_dl *sdl;

	ifp->if_type = IFT_ATM;
	ifp->if_addrlen = 0;
	ifp->if_hdrlen = 0;
	ifp->if_mtu = ATMMTU;
#if 0 /* XXX */
	ifp->if_input = atm_input;
#endif
	ifp->if_output = atm_output;
	ifq_set_maxlen(&ifp->if_snd, 50);
	if_attach(ifp, serializer);

	TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
		struct ifaddr *ifa = ifac->ifa;

		if ((sdl = (struct sockaddr_dl *)ifa->ifa_addr) &&
		    sdl->sdl_family == AF_LINK) {
			sdl->sdl_type = IFT_ATM;
			sdl->sdl_alen = ifp->if_addrlen;
#ifdef notyet /* if using ATMARP, store hardware address using the next line */
			bcopy(ifp->hw_addr, LLADDR(sdl), ifp->if_addrlen);
#endif
			break;
		}
	}
	bpfattach(ifp, DLT_ATM_RFC1483, sizeof(struct atmllc));
}
