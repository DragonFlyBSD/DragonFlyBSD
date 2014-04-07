/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/net/if_gif.c,v 1.4.2.15 2002/11/08 16:57:13 ume Exp $
 * $DragonFly: src/sys/net/gif/if_gif.c,v 1.21 2008/05/14 11:59:23 sephe Exp $
 * $KAME: if_gif.c,v 1.87 2001/10/19 08:50:27 itojun Exp $
 */

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/protosw.h>
#include <sys/conf.h>
#include <sys/thread2.h>

#include <machine/cpu.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/ifq_var.h>
#include <net/netisr.h>
#include <net/route.h>
#include <net/bpf.h>
#include <net/if_clone.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#ifdef	INET
#include <netinet/in_var.h>
#include <netinet/in_gif.h>
#include <netinet/ip_var.h>
#endif	/* INET */

#ifdef INET6
#ifndef INET
#include <netinet/in.h>
#endif
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_gif.h>
#include <netinet6/ip6protosw.h>
#endif /* INET6 */

#include <netinet/ip_encap.h>
#include "if_gif.h"

#include <net/net_osdep.h>

#define GIFNAME		"gif"

static MALLOC_DEFINE(M_GIF, "gif", "Generic Tunnel Interface");
LIST_HEAD(, gif_softc) gif_softc_list;

int	gif_clone_create (struct if_clone *, int, caddr_t);
int	gif_clone_destroy (struct ifnet *);

struct if_clone gif_cloner = IF_CLONE_INITIALIZER("gif", gif_clone_create,
    gif_clone_destroy, 0, IF_MAXUNIT);

static int gifmodevent (module_t, int, void *);
static void gif_clear_cache(struct gif_softc *sc);

SYSCTL_DECL(_net_link);
SYSCTL_NODE(_net_link, IFT_GIF, gif, CTLFLAG_RW, 0,
    "Generic Tunnel Interface");
#ifndef MAX_GIF_NEST
/*
 * This macro controls the default upper limitation on nesting of gif tunnels.
 * Since, setting a large value to this macro with a careless configuration
 * may introduce system crash, we don't allow any nestings by default.
 * If you need to configure nested gif tunnels, you can define this macro
 * in your kernel configuration file.  However, if you do so, please be
 * careful to configure the tunnels so that it won't make a loop.
 */
#define MAX_GIF_NEST 1
#endif
static int max_gif_nesting = MAX_GIF_NEST;
SYSCTL_INT(_net_link_gif, OID_AUTO, max_nesting, CTLFLAG_RW,
    &max_gif_nesting, 0, "Max nested tunnels");

/*
 * By default, we disallow creation of multiple tunnels between the same
 * pair of addresses.  Some applications require this functionality so
 * we allow control over this check here.
 */
#ifdef XBONEHACK
static int parallel_tunnels = 1;
#else
static int parallel_tunnels = 0;
#endif
SYSCTL_INT(_net_link_gif, OID_AUTO, parallel_tunnels, CTLFLAG_RW,
    &parallel_tunnels, 0, "Allow parallel tunnels?");

int
gif_clone_create(struct if_clone *ifc, int unit, caddr_t params)
{
	struct gif_softc *sc;
	
	sc = kmalloc (sizeof(struct gif_softc), M_GIF, M_WAITOK | M_ZERO);

	sc->gif_if.if_softc = sc;
	if_initname(&(sc->gif_if), GIFNAME, unit);

	gifattach0(sc);

	LIST_INSERT_HEAD(&gif_softc_list, sc, gif_list);
	return (0);
}

void
gifattach0(struct gif_softc *sc)
{

	sc->encap_cookie4 = sc->encap_cookie6 = NULL;

	sc->gif_if.if_addrlen = 0;
	sc->gif_if.if_mtu    = GIF_MTU;
	sc->gif_if.if_flags  = IFF_POINTOPOINT | IFF_MULTICAST;
#if 0
	/* turn off ingress filter */
	sc->gif_if.if_flags  |= IFF_LINK2;
#endif
	sc->gif_if.if_ioctl  = gif_ioctl;
	sc->gif_if.if_output = gif_output;
	sc->gif_if.if_type   = IFT_GIF;
	ifq_set_maxlen(&sc->gif_if.if_snd, IFQ_MAXLEN);
	if_attach(&sc->gif_if, NULL);
	bpfattach(&sc->gif_if, DLT_NULL, sizeof(u_int));
}

int
gif_clone_destroy(struct ifnet *ifp)
{
	struct gif_softc *sc = ifp->if_softc;
	int err;

	gif_delete_tunnel(&sc->gif_if);
	LIST_REMOVE(sc, gif_list);
#ifdef INET6
	if (sc->encap_cookie6 != NULL) {
		err = encap_detach(sc->encap_cookie6);
		KASSERT(err == 0, ("Unexpected error detaching encap_cookie6"));
	}
#endif
#ifdef INET
	if (sc->encap_cookie4 != NULL) {
		err = encap_detach(sc->encap_cookie4);
		KASSERT(err == 0, ("Unexpected error detaching encap_cookie4"));
	}
#endif
	gif_clear_cache(sc);

	bpfdetach(ifp);
	if_detach(ifp);

	kfree(sc, M_GIF);

	return 0;
}

static void
gif_clear_cache(struct gif_softc *sc)
{
	struct rtentry *rt;
	int origcpu;
	int n;

	for (n = 0; n < ncpus; ++n) {
		rt = sc->gif_ro[n].ro_rt;
		/*
		 * Routes need to be cleaned up in their CPU so migrate
		 * to it and return to the original CPU after completion.
		 */
		origcpu = mycpuid;
		if (rt && rt->rt_cpuid != mycpuid)
			lwkt_migratecpu(rt->rt_cpuid);
		else
			origcpu = -1;

		if (sc->gif_ro[n].ro_rt) {
			RTFREE(sc->gif_ro[n].ro_rt);
			sc->gif_ro[n].ro_rt = NULL;
		}
#ifdef INET6
		if (sc->gif_ro6[n].ro_rt) {
			RTFREE(sc->gif_ro6[n].ro_rt);
			sc->gif_ro6[n].ro_rt = NULL;
		}
#endif
		if (origcpu >= 0)
			lwkt_migratecpu(origcpu);
	}
}

static int
gifmodevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		LIST_INIT(&gif_softc_list);
		if_clone_attach(&gif_cloner);

#ifdef INET6
		ip6_gif_hlim = GIF_HLIM;
#endif

		break;
	case MOD_UNLOAD:
		if_clone_detach(&gif_cloner);

		while (!LIST_EMPTY(&gif_softc_list))
			gif_clone_destroy(&LIST_FIRST(&gif_softc_list)->gif_if);

#ifdef INET6
		ip6_gif_hlim = 0;
#endif
		break;
	}
	return 0;
}

static moduledata_t gif_mod = {
	"if_gif",
	gifmodevent,
	0
};

DECLARE_MODULE(if_gif, gif_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);

int
gif_encapcheck(const struct mbuf *m, int off, int proto, void *arg)
{
	struct ip ip;
	struct gif_softc *sc;

	sc = (struct gif_softc *)arg;
	if (sc == NULL)
		return 0;

	if ((sc->gif_if.if_flags & IFF_UP) == 0)
		return 0;

	/* no physical address */
	if (!sc->gif_psrc || !sc->gif_pdst)
		return 0;

	switch (proto) {
#ifdef INET
	case IPPROTO_IPV4:
		break;
#endif
#ifdef INET6
	case IPPROTO_IPV6:
		break;
#endif
	default:
		return 0;
	}

	/* Bail on short packets */
	if (m->m_pkthdr.len < sizeof(ip))
		return 0;

	m_copydata(m, 0, sizeof(ip), (caddr_t)&ip);

	switch (ip.ip_v) {
#ifdef INET
	case 4:
		if (sc->gif_psrc->sa_family != AF_INET ||
		    sc->gif_pdst->sa_family != AF_INET)
			return 0;
		return gif_encapcheck4(m, off, proto, arg);
#endif
#ifdef INET6
	case 6:
		if (m->m_pkthdr.len < sizeof(struct ip6_hdr))
			return 0;
		if (sc->gif_psrc->sa_family != AF_INET6 ||
		    sc->gif_pdst->sa_family != AF_INET6)
			return 0;
		return gif_encapcheck6(m, off, proto, arg);
#endif
	default:
		return 0;
	}
}

/*
 * Parameters:
 *	rt:	added in net2
 */
static int
gif_output_serialized(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
		      struct rtentry *rt)
{
	struct gif_softc *sc = (struct gif_softc*)ifp;
	int error = 0;
	static int called = 0;	/* XXX: MUTEX */

	/*
	 * gif may cause infinite recursion calls when misconfigured.
	 * We'll prevent this by introducing upper limit.
	 * XXX: this mechanism may introduce another problem about
	 *      mutual exclusion of the variable CALLED, especially if we
	 *      use kernel thread.
	 */
	if (++called > max_gif_nesting) {
		log(LOG_NOTICE,
		    "gif_output: recursively called too many times(%d)\n",
		    called);
		m_freem(m);
		error = EIO;	/* is there better errno? */
		goto end;
	}

	m->m_flags &= ~(M_BCAST|M_MCAST);
	if (!(ifp->if_flags & IFF_UP) ||
	    sc->gif_psrc == NULL || sc->gif_pdst == NULL) {
		m_freem(m);
		error = ENETDOWN;
		goto end;
	}

	if (ifp->if_bpf) {
		bpf_gettoken();
		if (ifp->if_bpf) {
			/*
			 * We need to prepend the address family as
			 * a four byte field.
			 */
			uint32_t af = dst->sa_family;

			bpf_ptap(ifp->if_bpf, m, &af, sizeof(af));
		}
		bpf_reltoken();
	}
	IFNET_STAT_INC(ifp, opackets, 1);	
	IFNET_STAT_INC(ifp, obytes, m->m_pkthdr.len);

	/* inner AF-specific encapsulation */

	/* XXX should we check if our outer source is legal? */

	/* dispatch to output logic based on outer AF */
	switch (sc->gif_psrc->sa_family) {
#ifdef INET
	case AF_INET:
		error = in_gif_output(ifp, dst->sa_family, m);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		error = in6_gif_output(ifp, dst->sa_family, m);
		break;
#endif
	default:
		m_freem(m);		
		error = ENETDOWN;
		goto end;
	}

  end:
	called = 0;		/* reset recursion counter */
	if (error)
		IFNET_STAT_INC(ifp, oerrors, 1);
	return error;
}

int
gif_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
	   struct rtentry *rt)
{
	struct ifaltq_subque *ifsq = ifq_get_subq_default(&ifp->if_snd);
	int error;

	ifsq_serialize_hw(ifsq);
	error = gif_output_serialized(ifp, m, dst, rt);
	ifsq_deserialize_hw(ifsq);
	return error;
}

void
gif_input(struct mbuf *m, int af, struct ifnet *ifp)
{
	int isr;

	if (ifp == NULL) {
		/* just in case */
		m_freem(m);
		return;
	}

	m->m_pkthdr.rcvif = ifp;
	
	if (ifp->if_bpf) {
		bpf_gettoken();
		if (ifp->if_bpf) {
			/*
			 * We need to prepend the address family as
			 * a four byte field.
			 */
			uint32_t af1 = af;

			bpf_ptap(ifp->if_bpf, m, &af1, sizeof(af1));
		}
		bpf_reltoken();
	}

	/*
	 * Put the packet to the network layer input queue according to the
	 * specified address family.
	 * Note: older versions of gif_input directly called network layer
	 * input functions, e.g. ip6_input, here.  We changed the policy to
	 * prevent too many recursive calls of such input functions, which
	 * might cause kernel panic.  But the change may introduce another
	 * problem; if the input queue is full, packets are discarded.
	 * The kernel stack overflow really happened, and we believed
	 * queue-full rarely occurs, so we changed the policy.
	 */
	switch (af) {
#ifdef INET
	case AF_INET:
		isr = NETISR_IP;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		isr = NETISR_IPV6;
		break;
#endif
	default:
		m_freem(m);
		return;
	}

	IFNET_STAT_INC(ifp, ipackets, 1);
	IFNET_STAT_INC(ifp, ibytes, m->m_pkthdr.len);
	m->m_flags &= ~M_HASH;
	netisr_queue(isr, m);

	return;
}

/* XXX how should we handle IPv6 scope on SIOC[GS]IFPHYADDR? */
int
gif_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct gif_softc *sc  = (struct gif_softc*)ifp;
	struct ifreq     *ifr = (struct ifreq*)data;
	int error = 0, size;
	struct sockaddr *dst, *src;
#ifdef	SIOCSIFMTU /* xxx */
	u_long mtu;
#endif

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		break;
		
	case SIOCSIFDSTADDR:
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		break;

#ifdef	SIOCSIFMTU /* xxx */
	case SIOCGIFMTU:
		break;

	case SIOCSIFMTU:
		mtu = ifr->ifr_mtu;
		if (mtu < GIF_MTU_MIN || mtu > GIF_MTU_MAX)
			return (EINVAL);
		ifp->if_mtu = mtu;
		break;
#endif /* SIOCSIFMTU */

#ifdef INET
	case SIOCSIFPHYADDR:
#endif
#ifdef INET6
	case SIOCSIFPHYADDR_IN6:
#endif /* INET6 */
	case SIOCSLIFPHYADDR:
		switch (cmd) {
#ifdef INET
		case SIOCSIFPHYADDR:
			src = (struct sockaddr *)
				&(((struct in_aliasreq *)data)->ifra_addr);
			dst = (struct sockaddr *)
				&(((struct in_aliasreq *)data)->ifra_dstaddr);
			break;
#endif
#ifdef INET6
		case SIOCSIFPHYADDR_IN6:
			src = (struct sockaddr *)
				&(((struct in6_aliasreq *)data)->ifra_addr);
			dst = (struct sockaddr *)
				&(((struct in6_aliasreq *)data)->ifra_dstaddr);
			break;
#endif
		case SIOCSLIFPHYADDR:
			src = (struct sockaddr *)
				&(((struct if_laddrreq *)data)->addr);
			dst = (struct sockaddr *)
				&(((struct if_laddrreq *)data)->dstaddr);
			break;
		default:
			return EINVAL;
		}

		/* sa_family must be equal */
		if (src->sa_family != dst->sa_family)
			return EINVAL;

		/* validate sa_len */
		switch (src->sa_family) {
#ifdef INET
		case AF_INET:
			if (src->sa_len != sizeof(struct sockaddr_in))
				return EINVAL;
			break;
#endif
#ifdef INET6
		case AF_INET6:
			if (src->sa_len != sizeof(struct sockaddr_in6))
				return EINVAL;
			break;
#endif
		default:
			return EAFNOSUPPORT;
		}
		switch (dst->sa_family) {
#ifdef INET
		case AF_INET:
			if (dst->sa_len != sizeof(struct sockaddr_in))
				return EINVAL;
			break;
#endif
#ifdef INET6
		case AF_INET6:
			if (dst->sa_len != sizeof(struct sockaddr_in6))
				return EINVAL;
			break;
#endif
		default:
			return EAFNOSUPPORT;
		}

		/* check sa_family looks sane for the cmd */
		switch (cmd) {
		case SIOCSIFPHYADDR:
			if (src->sa_family == AF_INET)
				break;
			return EAFNOSUPPORT;
#ifdef INET6
		case SIOCSIFPHYADDR_IN6:
			if (src->sa_family == AF_INET6)
				break;
			return EAFNOSUPPORT;
#endif /* INET6 */
		case SIOCSLIFPHYADDR:
			/* checks done in the above */
			break;
		}

		error = gif_set_tunnel(&sc->gif_if, src, dst);
		break;

#ifdef SIOCDIFPHYADDR
	case SIOCDIFPHYADDR:
		gif_delete_tunnel(&sc->gif_if);
		break;
#endif
			
	case SIOCGIFPSRCADDR:
#ifdef INET6
	case SIOCGIFPSRCADDR_IN6:
#endif /* INET6 */
		if (sc->gif_psrc == NULL) {
			error = EADDRNOTAVAIL;
			goto bad;
		}
		src = sc->gif_psrc;
		switch (cmd) {
#ifdef INET
		case SIOCGIFPSRCADDR:
			dst = &ifr->ifr_addr;
			size = sizeof(ifr->ifr_addr);
			break;
#endif /* INET */
#ifdef INET6
		case SIOCGIFPSRCADDR_IN6:
			dst = (struct sockaddr *)
				&(((struct in6_ifreq *)data)->ifr_addr);
			size = sizeof(((struct in6_ifreq *)data)->ifr_addr);
			break;
#endif /* INET6 */
		default:
			error = EADDRNOTAVAIL;
			goto bad;
		}
		if (src->sa_len > size)
			return EINVAL;
		bcopy((caddr_t)src, (caddr_t)dst, src->sa_len);
		break;
			
	case SIOCGIFPDSTADDR:
#ifdef INET6
	case SIOCGIFPDSTADDR_IN6:
#endif /* INET6 */
		if (sc->gif_pdst == NULL) {
			error = EADDRNOTAVAIL;
			goto bad;
		}
		src = sc->gif_pdst;
		switch (cmd) {
#ifdef INET
		case SIOCGIFPDSTADDR:
			dst = &ifr->ifr_addr;
			size = sizeof(ifr->ifr_addr);
			break;
#endif /* INET */
#ifdef INET6
		case SIOCGIFPDSTADDR_IN6:
			dst = (struct sockaddr *)
				&(((struct in6_ifreq *)data)->ifr_addr);
			size = sizeof(((struct in6_ifreq *)data)->ifr_addr);
			break;
#endif /* INET6 */
		default:
			error = EADDRNOTAVAIL;
			goto bad;
		}
		if (src->sa_len > size)
			return EINVAL;
		bcopy((caddr_t)src, (caddr_t)dst, src->sa_len);
		break;

	case SIOCGLIFPHYADDR:
		if (sc->gif_psrc == NULL || sc->gif_pdst == NULL) {
			error = EADDRNOTAVAIL;
			goto bad;
		}

		/* copy src */
		src = sc->gif_psrc;
		dst = (struct sockaddr *)
			&(((struct if_laddrreq *)data)->addr);
		size = sizeof(((struct if_laddrreq *)data)->addr);
		if (src->sa_len > size)
			return EINVAL;
		bcopy((caddr_t)src, (caddr_t)dst, src->sa_len);

		/* copy dst */
		src = sc->gif_pdst;
		dst = (struct sockaddr *)
			&(((struct if_laddrreq *)data)->dstaddr);
		size = sizeof(((struct if_laddrreq *)data)->dstaddr);
		if (src->sa_len > size)
			return EINVAL;
		bcopy((caddr_t)src, (caddr_t)dst, src->sa_len);
		break;

	case SIOCSIFFLAGS:
		/* if_ioctl() takes care of it */
		break;

	default:
		error = EINVAL;
		break;
	}
 bad:
	return error;
}

int
gif_set_tunnel(struct ifnet *ifp, struct sockaddr *src, struct sockaddr *dst)
{
	struct gif_softc *sc = (struct gif_softc *)ifp;
	struct gif_softc *sc2;
	struct sockaddr *osrc, *odst, *sa;
	int error = 0; 

	crit_enter();

	LIST_FOREACH(sc2, &gif_softc_list, gif_list) {
		if (sc2 == sc)
			continue;
		if (!sc2->gif_pdst || !sc2->gif_psrc)
			continue;
		if (sc2->gif_pdst->sa_family != dst->sa_family ||
		    sc2->gif_pdst->sa_len != dst->sa_len ||
		    sc2->gif_psrc->sa_family != src->sa_family ||
		    sc2->gif_psrc->sa_len != src->sa_len)
			continue;

		/*
		 * Disallow parallel tunnels unless instructed
		 * otherwise.
		 */
		if (!parallel_tunnels &&
		    bcmp(sc2->gif_pdst, dst, dst->sa_len) == 0 &&
		    bcmp(sc2->gif_psrc, src, src->sa_len) == 0) {
			error = EADDRNOTAVAIL;
			goto bad;
		}

		/* XXX both end must be valid? (I mean, not 0.0.0.0) */
	}

	/* XXX we can detach from both, but be polite just in case */
	if (sc->gif_psrc) {
		switch (sc->gif_psrc->sa_family) {
#ifdef INET
		case AF_INET:
			in_gif_detach(sc);
			break;
#endif
#ifdef INET6
		case AF_INET6:
			in6_gif_detach(sc);
			break;
#endif
		}
		gif_clear_cache(sc);
	}

	osrc = sc->gif_psrc;
	sa = (struct sockaddr *)kmalloc(src->sa_len, M_IFADDR, M_WAITOK);
	bcopy((caddr_t)src, (caddr_t)sa, src->sa_len);
	sc->gif_psrc = sa;

	odst = sc->gif_pdst;
	sa = (struct sockaddr *)kmalloc(dst->sa_len, M_IFADDR, M_WAITOK);
	bcopy((caddr_t)dst, (caddr_t)sa, dst->sa_len);
	sc->gif_pdst = sa;

	switch (sc->gif_psrc->sa_family) {
#ifdef INET
	case AF_INET:
		error = in_gif_attach(sc);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		error = in6_gif_attach(sc);
		break;
#endif
	}
	if (error) {
		/* rollback */
		kfree((caddr_t)sc->gif_psrc, M_IFADDR);
		kfree((caddr_t)sc->gif_pdst, M_IFADDR);
		sc->gif_psrc = osrc;
		sc->gif_pdst = odst;
		goto bad;
	}

	if (osrc)
		kfree((caddr_t)osrc, M_IFADDR);
	if (odst)
		kfree((caddr_t)odst, M_IFADDR);

	if (sc->gif_psrc && sc->gif_pdst)
		ifp->if_flags |= IFF_RUNNING;
	else
		ifp->if_flags &= ~IFF_RUNNING;
	crit_exit();

	return 0;

 bad:
	if (sc->gif_psrc && sc->gif_pdst)
		ifp->if_flags |= IFF_RUNNING;
	else
		ifp->if_flags &= ~IFF_RUNNING;
	crit_exit();

	return error;
}

void
gif_delete_tunnel(struct ifnet *ifp)
{
	struct gif_softc *sc = (struct gif_softc *)ifp;

	crit_enter();

	if (sc->gif_psrc) {
		kfree((caddr_t)sc->gif_psrc, M_IFADDR);
		sc->gif_psrc = NULL;
	}
	if (sc->gif_pdst) {
		kfree((caddr_t)sc->gif_pdst, M_IFADDR);
		sc->gif_pdst = NULL;
	}
	/* it is safe to detach from both */
#ifdef INET
	in_gif_detach(sc);
#endif
#ifdef INET6
	in6_gif_detach(sc);
#endif
	gif_clear_cache(sc);

	if (sc->gif_psrc && sc->gif_pdst)
		ifp->if_flags |= IFF_RUNNING;
	else
		ifp->if_flags &= ~IFF_RUNNING;
	crit_exit();
}
