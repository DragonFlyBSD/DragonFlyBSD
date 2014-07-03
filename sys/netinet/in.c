/*
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)in.c	8.4 (Berkeley) 1/9/95
 * $FreeBSD: src/sys/netinet/in.c,v 1.44.2.14 2002/11/08 00:45:50 suz Exp $
 */

#include "opt_bootp.h"
#include "opt_carp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/msgport.h>
#include <sys/socket.h>

#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/thread2.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/netmsg2.h>
#include <net/netisr2.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_pcb.h>
#include <netinet/udp_var.h>

#include <netinet/igmp_var.h>

MALLOC_DEFINE(M_IPMADDR, "in_multi", "internet multicast address");

static int in_mask2len (struct in_addr *);
static void in_len2mask (struct in_addr *, int);
static int in_lifaddr_ioctl (struct socket *, u_long, caddr_t,
	struct ifnet *, struct thread *);

static void	in_socktrim (struct sockaddr_in *);
static int	in_ifinit(struct ifnet *, struct in_ifaddr *,
		    const struct sockaddr_in *, int);

static int	in_control_internal(u_long, caddr_t, struct ifnet *,
		    struct thread *);
static int	in_control_redispatch(u_long, caddr_t, struct ifnet *,
		    struct thread *);

static int	in_addprefix(struct in_ifaddr *, int);
static void	in_scrubprefix(struct in_ifaddr *);

static int subnetsarelocal = 0;
SYSCTL_INT(_net_inet_ip, OID_AUTO, subnets_are_local, CTLFLAG_RW,
    &subnetsarelocal, 0,
    "Count all internet addresses of subnets of the local net as local");

struct in_multihead in_multihead; /* XXX BSS initialization */

extern struct inpcbinfo ripcbinfo;

/*
 * Return 1 if an internet address is for a ``local'' host
 * (one to which we have a connection).  If subnetsarelocal
 * is true, this includes other subnets of the local net.
 * Otherwise, it includes only the directly-connected (sub)nets.
 */
int
in_localaddr(struct in_addr in)
{
	u_long i = ntohl(in.s_addr);
	struct in_ifaddr_container *iac;
	struct in_ifaddr *ia;

	if (subnetsarelocal) {
		TAILQ_FOREACH(iac, &in_ifaddrheads[mycpuid], ia_link) {
			ia = iac->ia;

			if ((i & ia->ia_netmask) == ia->ia_net)
				return (1);
		}
	} else {
		TAILQ_FOREACH(iac, &in_ifaddrheads[mycpuid], ia_link) {
			ia = iac->ia;

			if ((i & ia->ia_subnetmask) == ia->ia_subnet)
				return (1);
		}
	}
	return (0);
}

/*
 * Determine whether an IP address is in a reserved set of addresses
 * that may not be forwarded, or whether datagrams to that destination
 * may be forwarded.
 */
int
in_canforward(struct in_addr in)
{
	u_long i = ntohl(in.s_addr);
	u_long net;

	if (IN_EXPERIMENTAL(i) || IN_MULTICAST(i))
		return (0);
	if (IN_CLASSA(i)) {
		net = i & IN_CLASSA_NET;
		if (net == 0 || net == (IN_LOOPBACKNET << IN_CLASSA_NSHIFT))
			return (0);
	}
	return (1);
}

/*
 * Trim a mask in a sockaddr
 */
static void
in_socktrim(struct sockaddr_in *ap)
{
    char *cplim = (char *) &ap->sin_addr;
    char *cp = (char *) (&ap->sin_addr + 1);

    ap->sin_len = 0;
    while (--cp >= cplim)
	if (*cp) {
	    (ap)->sin_len = cp - (char *) (ap) + 1;
	    break;
	}
}

static int
in_mask2len(struct in_addr *mask)
{
	int x, y;
	u_char *p;

	p = (u_char *)mask;
	for (x = 0; x < sizeof *mask; x++) {
		if (p[x] != 0xff)
			break;
	}
	y = 0;
	if (x < sizeof *mask) {
		for (y = 0; y < 8; y++) {
			if ((p[x] & (0x80 >> y)) == 0)
				break;
		}
	}
	return x * 8 + y;
}

static void
in_len2mask(struct in_addr *mask, int len)
{
	int i;
	u_char *p;

	p = (u_char *)mask;
	bzero(mask, sizeof *mask);
	for (i = 0; i < len / 8; i++)
		p[i] = 0xff;
	if (len % 8)
		p[i] = (0xff00 >> (len % 8)) & 0xff;
}

static int in_interfaces;	/* number of external internet interfaces */

void
in_control_dispatch(netmsg_t msg)
{
	int error;

	error = in_control_redispatch(msg->control.nm_cmd,
				      msg->control.nm_data,
				      msg->control.nm_ifp,
				      msg->control.nm_td);
	lwkt_replymsg(&msg->lmsg, error);
}

static void
in_control_internal_dispatch(netmsg_t msg)
{
	int error;

	error = in_control_internal(msg->control.nm_cmd,
				    msg->control.nm_data,
				    msg->control.nm_ifp,
				    msg->control.nm_td);
	lwkt_replymsg(&msg->lmsg, error);
}

static int
in_control_redispatch(u_long cmd, caddr_t data, struct ifnet *ifp,
		      struct thread *td)
{
	struct netmsg_pru_control msg;
	int error;

	/*
	 * IFADDR alterations are serialized by netisr0
	 */
	switch (cmd) {
	case SIOCSIFDSTADDR:
	case SIOCSIFBRDADDR:
	case SIOCSIFADDR:
	case SIOCSIFNETMASK:
	case SIOCAIFADDR:
	case SIOCDIFADDR:
		netmsg_init(&msg.base, NULL, &curthread->td_msgport,
			    0, in_control_internal_dispatch);
		msg.nm_cmd = cmd;
		msg.nm_data = data;
		msg.nm_ifp = ifp;
		msg.nm_td = td;
		lwkt_domsg(netisr_cpuport(0), &msg.base.lmsg, 0);
		error = msg.base.lmsg.ms_error;
		break;

	default:
		error = in_control_internal(cmd, data, ifp, td);
		break;
	}
	return error;
}

/*
 * Generic internet control operations (ioctl's).
 * Ifp is 0 if not an interface-specific ioctl.
 *
 * NOTE! td might be NULL.
 */
/* ARGSUSED */
int
in_control(struct socket *so, u_long cmd, caddr_t data, struct ifnet *ifp,
	   struct thread *td)
{
	int error;

	switch (cmd) {
	case SIOCALIFADDR:
	case SIOCDLIFADDR:
		if (td && (error = priv_check(td, PRIV_ROOT)) != 0)
			return error;
		/* FALLTHROUGH */
	case SIOCGLIFADDR:
		if (!ifp)
			return EINVAL;
		return in_lifaddr_ioctl(so, cmd, data, ifp, td);
	}

	KASSERT(cmd != SIOCALIFADDR && cmd != SIOCDLIFADDR,
		("recursive SIOC%cLIFADDR!",
		 cmd == SIOCDLIFADDR ? 'D' : 'A'));

	return in_control_redispatch(cmd, data, ifp, td);
}

static void
in_ialink_dispatch(netmsg_t msg)
{
	struct in_ifaddr *ia = msg->lmsg.u.ms_resultp;
	struct ifaddr_container *ifac;
	struct in_ifaddr_container *iac;
	int cpu = mycpuid;

	crit_enter();

	ifac = &ia->ia_ifa.ifa_containers[cpu];
	ASSERT_IFAC_VALID(ifac);
	KASSERT((ifac->ifa_listmask & IFA_LIST_IN_IFADDRHEAD) == 0,
		("ia is on in_ifaddrheads"));

	ifac->ifa_listmask |= IFA_LIST_IN_IFADDRHEAD;
	iac = &ifac->ifa_proto_u.u_in_ifac;
	TAILQ_INSERT_TAIL(&in_ifaddrheads[cpu], iac, ia_link);

	crit_exit();

	ifa_forwardmsg(&msg->lmsg, cpu + 1);
}

static void
in_iaunlink_dispatch(netmsg_t msg)
{
	struct in_ifaddr *ia = msg->lmsg.u.ms_resultp;
	struct ifaddr_container *ifac;
	struct in_ifaddr_container *iac;
	int cpu = mycpuid;

	crit_enter();

	ifac = &ia->ia_ifa.ifa_containers[cpu];
	ASSERT_IFAC_VALID(ifac);
	KASSERT(ifac->ifa_listmask & IFA_LIST_IN_IFADDRHEAD,
		("ia is not on in_ifaddrheads"));

	iac = &ifac->ifa_proto_u.u_in_ifac;
	TAILQ_REMOVE(&in_ifaddrheads[cpu], iac, ia_link);
	ifac->ifa_listmask &= ~IFA_LIST_IN_IFADDRHEAD;

	crit_exit();

	ifa_forwardmsg(&msg->lmsg, cpu + 1);
}

static void
in_iahashins_dispatch(netmsg_t msg)
{
	struct in_ifaddr *ia = msg->lmsg.u.ms_resultp;
	struct ifaddr_container *ifac;
	struct in_ifaddr_container *iac;
	int cpu = mycpuid;

	crit_enter();

	ifac = &ia->ia_ifa.ifa_containers[cpu];
	ASSERT_IFAC_VALID(ifac);
	KASSERT((ifac->ifa_listmask & IFA_LIST_IN_IFADDRHASH) == 0,
		("ia is on in_ifaddrhashtbls"));

	ifac->ifa_listmask |= IFA_LIST_IN_IFADDRHASH;
	iac = &ifac->ifa_proto_u.u_in_ifac;
	LIST_INSERT_HEAD(INADDR_HASH(ia->ia_addr.sin_addr.s_addr),
			 iac, ia_hash);

	crit_exit();

	ifa_forwardmsg(&msg->lmsg, cpu + 1);
}

static void
in_iahashrem_dispatch(netmsg_t msg)
{
	struct in_ifaddr *ia = msg->lmsg.u.ms_resultp;
	struct ifaddr_container *ifac;
	struct in_ifaddr_container *iac;
	int cpu = mycpuid;

	crit_enter();

	ifac = &ia->ia_ifa.ifa_containers[cpu];
	ASSERT_IFAC_VALID(ifac);
	KASSERT(ifac->ifa_listmask & IFA_LIST_IN_IFADDRHASH,
		("ia is not on in_ifaddrhashtbls"));

	iac = &ifac->ifa_proto_u.u_in_ifac;
	LIST_REMOVE(iac, ia_hash);
	ifac->ifa_listmask &= ~IFA_LIST_IN_IFADDRHASH;

	crit_exit();

	ifa_forwardmsg(&msg->lmsg, cpu + 1);
}

static void
in_ialink(struct in_ifaddr *ia)
{
	struct netmsg_base msg;

	netmsg_init(&msg, NULL, &curthread->td_msgport,
		    0, in_ialink_dispatch);
	msg.lmsg.u.ms_resultp = ia;

	ifa_domsg(&msg.lmsg, 0);
}

void
in_iaunlink(struct in_ifaddr *ia)
{
	struct netmsg_base msg;

	netmsg_init(&msg, NULL, &curthread->td_msgport,
		    0, in_iaunlink_dispatch);
	msg.lmsg.u.ms_resultp = ia;

	ifa_domsg(&msg.lmsg, 0);
}

void
in_iahash_insert(struct in_ifaddr *ia)
{
	struct netmsg_base msg;

	netmsg_init(&msg, NULL, &curthread->td_msgport,
		    0, in_iahashins_dispatch);
	msg.lmsg.u.ms_resultp = ia;

	ifa_domsg(&msg.lmsg, 0);
}

void
in_iahash_remove(struct in_ifaddr *ia)
{
	struct netmsg_base msg;

	netmsg_init(&msg, NULL, &curthread->td_msgport,
		    0, in_iahashrem_dispatch);
	msg.lmsg.u.ms_resultp = ia;

	ifa_domsg(&msg.lmsg, 0);
}

static __inline struct in_ifaddr *
in_ianext(struct in_ifaddr *oia)
{
	struct ifaddr_container *ifac;
	struct in_ifaddr_container *iac;

	ifac = &oia->ia_ifa.ifa_containers[mycpuid];
	ASSERT_IFAC_VALID(ifac);
	KASSERT(ifac->ifa_listmask & IFA_LIST_IN_IFADDRHEAD,
		("ia is not on in_ifaddrheads"));

	iac = &ifac->ifa_proto_u.u_in_ifac;
	iac = TAILQ_NEXT(iac, ia_link);
	if (iac != NULL)
		return iac->ia;
	else
		return NULL;
}

static int
in_control_internal(u_long cmd, caddr_t data, struct ifnet *ifp,
		    struct thread *td)
{
	struct ifreq *ifr = (struct ifreq *)data;
	struct in_ifaddr *ia = NULL;
	struct in_addr dst;
	struct in_aliasreq *ifra = (struct in_aliasreq *)data;
	struct ifaddr_container *ifac;
	struct in_ifaddr_container *iac;
	struct sockaddr_in oldaddr;
	int hostIsNew, iaIsNew, maskIsNew, ifpWasUp;
	int error = 0;

	iaIsNew = 0;
	ifpWasUp = 0;

	/*
	 * Find address for this interface, if it exists.
	 *
	 * If an alias address was specified, find that one instead of
	 * the first one on the interface, if possible
	 */
	if (ifp) {
		struct in_ifaddr *iap;

		dst = ((struct sockaddr_in *)&ifr->ifr_addr)->sin_addr;
		LIST_FOREACH(iac, INADDR_HASH(dst.s_addr), ia_hash) {
			iap = iac->ia;
			if (iap->ia_ifp == ifp &&
			    iap->ia_addr.sin_addr.s_addr == dst.s_addr) {
				ia = iap;
				break;
			}
		}
		if (ia == NULL) {
			TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid],
				      ifa_link) {
				iap = ifatoia(ifac->ifa);
				if (iap->ia_addr.sin_family == AF_INET) {
					ia = iap;
					break;
				}
			}
		}

		if (ifp->if_flags & IFF_UP)
			ifpWasUp = 1;
	}

	switch (cmd) {
	case SIOCAIFADDR:
	case SIOCDIFADDR:
		if (ifp == NULL)
			return (EADDRNOTAVAIL);
		if (ifra->ifra_addr.sin_family == AF_INET) {
			while (ia != NULL) {
				if (ia->ia_ifp == ifp  &&
				    ia->ia_addr.sin_addr.s_addr ==
				    ifra->ifra_addr.sin_addr.s_addr)
					break;
				ia = in_ianext(ia);
			}
			if ((ifp->if_flags & IFF_POINTOPOINT) &&
			    cmd == SIOCAIFADDR &&
			    ifra->ifra_dstaddr.sin_addr.s_addr == INADDR_ANY) {
				return EDESTADDRREQ;
			}
		}
		if (cmd == SIOCDIFADDR && ia == NULL)
			return (EADDRNOTAVAIL);
		/* FALLTHROUGH */
	case SIOCSIFADDR:
	case SIOCSIFNETMASK:
	case SIOCSIFDSTADDR:
		if (td && (error = priv_check(td, PRIV_ROOT)) != 0)
			return error;

		if (ifp == NULL)
			return (EADDRNOTAVAIL);

		if (cmd == SIOCSIFDSTADDR &&
		    (ifp->if_flags & IFF_POINTOPOINT) == 0)
			return (EINVAL);

		if (ia == NULL) {
			struct ifaddr *ifa;
			int i;

			ia = ifa_create(sizeof(*ia), M_WAITOK);
			ifa = &ia->ia_ifa;

			/*
			 * Setup per-CPU information
			 */
			for (i = 0; i < ncpus; ++i) {
				ifac = &ifa->ifa_containers[i];
				iac = &ifac->ifa_proto_u.u_in_ifac;
				iac->ia = ia;
				iac->ia_ifac = ifac;
			}

			/*
			 * Protect from NETISR_IP traversing address list
			 * while we're modifying it.
			 */
			crit_enter();

			in_ialink(ia);
			ifa_iflink(ifa, ifp, 1);

			ifa->ifa_addr = (struct sockaddr *)&ia->ia_addr;
			ifa->ifa_dstaddr = (struct sockaddr *)&ia->ia_dstaddr;
			ifa->ifa_netmask = (struct sockaddr *)&ia->ia_sockmask;
			ia->ia_sockmask.sin_len = 8;
			ia->ia_sockmask.sin_family = AF_INET;
			if (ifp->if_flags & IFF_BROADCAST) {
				ia->ia_broadaddr.sin_len = sizeof ia->ia_addr;
				ia->ia_broadaddr.sin_family = AF_INET;
			}
			ia->ia_ifp = ifp;
			if (!(ifp->if_flags & IFF_LOOPBACK))
				in_interfaces++;
			iaIsNew = 1;

			crit_exit();
		}
		break;

	case SIOCSIFBRDADDR:
		if (td && (error = priv_check(td, PRIV_ROOT)) != 0)
			return error;
		/* FALLTHROUGH */

	case SIOCGIFADDR:
	case SIOCGIFNETMASK:
	case SIOCGIFDSTADDR:
	case SIOCGIFBRDADDR:
		if (ia == NULL)
			return (EADDRNOTAVAIL);
		break;
	}

	switch (cmd) {
	case SIOCGIFADDR:
		*((struct sockaddr_in *)&ifr->ifr_addr) = ia->ia_addr;
		return (0);

	case SIOCGIFBRDADDR:
		if ((ifp->if_flags & IFF_BROADCAST) == 0)
			return (EINVAL);
		*((struct sockaddr_in *)&ifr->ifr_dstaddr) = ia->ia_broadaddr;
		return (0);

	case SIOCGIFDSTADDR:
		if ((ifp->if_flags & IFF_POINTOPOINT) == 0)
			return (EINVAL);
		*((struct sockaddr_in *)&ifr->ifr_dstaddr) = ia->ia_dstaddr;
		return (0);

	case SIOCGIFNETMASK:
		*((struct sockaddr_in *)&ifr->ifr_addr) = ia->ia_sockmask;
		return (0);

	case SIOCSIFDSTADDR:
		KKASSERT(ifp->if_flags & IFF_POINTOPOINT);

		oldaddr = ia->ia_dstaddr;
		ia->ia_dstaddr = *(struct sockaddr_in *)&ifr->ifr_dstaddr;
		if (ifp->if_ioctl != NULL) {
			ifnet_serialize_all(ifp);
			error = ifp->if_ioctl(ifp, SIOCSIFDSTADDR, (caddr_t)ia,
					      td->td_proc->p_ucred);
			ifnet_deserialize_all(ifp);
			if (error) {
				ia->ia_dstaddr = oldaddr;
				return (error);
			}
		}
		if (ia->ia_flags & IFA_ROUTE) {
			ia->ia_ifa.ifa_dstaddr = (struct sockaddr *)&oldaddr;
			rtinit(&ia->ia_ifa, RTM_DELETE, RTF_HOST);
			ia->ia_ifa.ifa_dstaddr =
					(struct sockaddr *)&ia->ia_dstaddr;
			rtinit(&ia->ia_ifa, RTM_ADD, RTF_HOST | RTF_UP);
		}
		return (0);

	case SIOCSIFBRDADDR:
		if ((ifp->if_flags & IFF_BROADCAST) == 0)
			return (EINVAL);
		ia->ia_broadaddr = *(struct sockaddr_in *)&ifr->ifr_broadaddr;
		return (0);

	case SIOCSIFADDR:
		error = in_ifinit(ifp, ia,
		    (const struct sockaddr_in *)&ifr->ifr_addr, 1);
		if (error != 0 && iaIsNew)
			break;
		if (error == 0) {
			EVENTHANDLER_INVOKE(ifaddr_event, ifp,
			iaIsNew ? IFADDR_EVENT_ADD : IFADDR_EVENT_CHANGE,
			&ia->ia_ifa);
		}
		if (!ifpWasUp && (ifp->if_flags & IFF_UP)) {
			/*
			 * Interface is brought up by in_ifinit()
			 * (via ifp->if_ioctl).  We act as if the
			 * interface got IFF_UP flag turned on.
			 */
			if_up(ifp);
		}
		return (0);

	case SIOCSIFNETMASK:
		ia->ia_sockmask.sin_addr = ifra->ifra_addr.sin_addr;
		ia->ia_subnetmask = ntohl(ia->ia_sockmask.sin_addr.s_addr);
		return (0);

	case SIOCAIFADDR:
		maskIsNew = 0;
		hostIsNew = 1;
		error = 0;
		if (ia->ia_addr.sin_family == AF_INET) {
			if (ifra->ifra_addr.sin_len == 0) {
				ifra->ifra_addr = ia->ia_addr;
				hostIsNew = 0;
			} else if (ifra->ifra_addr.sin_addr.s_addr ==
				   ia->ia_addr.sin_addr.s_addr) {
				hostIsNew = 0;
			}
		}
		if (ifra->ifra_mask.sin_len) {
			in_ifscrub(ifp, ia);
			ia->ia_sockmask = ifra->ifra_mask;
			ia->ia_sockmask.sin_family = AF_INET;
			ia->ia_subnetmask =
			    ntohl(ia->ia_sockmask.sin_addr.s_addr);
			maskIsNew = 1;
		}
		if ((ifp->if_flags & IFF_POINTOPOINT) &&
		    ifra->ifra_dstaddr.sin_family == AF_INET) {
			in_ifscrub(ifp, ia);
			ia->ia_dstaddr = ifra->ifra_dstaddr;
			maskIsNew  = 1; /* We lie; but the effect's the same */
		}
		if (ifra->ifra_addr.sin_family == AF_INET &&
		    (hostIsNew || maskIsNew))
			error = in_ifinit(ifp, ia, &ifra->ifra_addr, 0);

		if (error != 0 && iaIsNew)
			break;

		if ((ifp->if_flags & IFF_BROADCAST) &&
		    ifra->ifra_broadaddr.sin_family == AF_INET)
			ia->ia_broadaddr = ifra->ifra_broadaddr;
		if (error == 0) {
			EVENTHANDLER_INVOKE(ifaddr_event, ifp,
			iaIsNew ? IFADDR_EVENT_ADD : IFADDR_EVENT_CHANGE,
			&ia->ia_ifa);
		}
		if (!ifpWasUp && (ifp->if_flags & IFF_UP)) {
			/* See the comment in SIOCSIFADDR */
			if_up(ifp);
		}
		return (error);

	case SIOCDIFADDR:
		/*
		 * in_ifscrub kills the interface route.
		 */
		in_ifscrub(ifp, ia);
		/*
		 * in_ifadown gets rid of all the rest of
		 * the routes.  This is not quite the right
		 * thing to do, but at least if we are running
		 * a routing process they will come back.
		 */
		in_ifadown(&ia->ia_ifa, 1);
		EVENTHANDLER_INVOKE(ifaddr_event, ifp, IFADDR_EVENT_DELETE,
				    &ia->ia_ifa);
		error = 0;
		break;

	default:
		if (ifp == NULL || ifp->if_ioctl == NULL)
			return (EOPNOTSUPP);
		ifnet_serialize_all(ifp);
		error = ifp->if_ioctl(ifp, cmd, data, td->td_proc->p_ucred);
		ifnet_deserialize_all(ifp);
		return (error);
	}

	KKASSERT(cmd == SIOCDIFADDR ||
		 ((cmd == SIOCAIFADDR || cmd == SIOCSIFADDR) && iaIsNew));

	ifa_ifunlink(&ia->ia_ifa, ifp);
	in_iaunlink(ia);

	if (cmd == SIOCDIFADDR) {
		ifac = &ia->ia_ifa.ifa_containers[mycpuid];
		if (ifac->ifa_listmask & IFA_LIST_IN_IFADDRHASH)
			in_iahash_remove(ia);
	}
#ifdef INVARIANTS
	else {
		/*
		 * If cmd is SIOCSIFADDR or SIOCAIFADDR, in_ifinit() has
		 * already taken care of the deletion from hash table
		 */
		ifac = &ia->ia_ifa.ifa_containers[mycpuid];
		KASSERT((ifac->ifa_listmask & IFA_LIST_IN_IFADDRHASH) == 0,
			("SIOC%cIFADDR failed on new ia, "
			 "but the new ia is still in hash table",
			 cmd == SIOCSIFADDR ? 'S' : 'A'));
	}
#endif

	ifa_destroy(&ia->ia_ifa);

	if ((cmd == SIOCAIFADDR || cmd == SIOCSIFADDR) &&
	    !ifpWasUp && (ifp->if_flags & IFF_UP)) {
		/*
		 * Though the address assignment failed, the
		 * interface is brought up by in_ifinit()
		 * (via ifp->if_ioctl).  With the hope that
		 * the interface has some valid addresses, we
		 * act as if IFF_UP flag was just set on the
		 * interface.
		 *
		 * NOTE:
		 * This could only be done after the failed
		 * address is unlinked from the global address
		 * list.
		 */
		if_up(ifp);
	}

	return (error);
}

/*
 * SIOC[GAD]LIFADDR.
 *	SIOCGLIFADDR: get first address. (?!?)
 *	SIOCGLIFADDR with IFLR_PREFIX:
 *		get first address that matches the specified prefix.
 *	SIOCALIFADDR: add the specified address.
 *	SIOCALIFADDR with IFLR_PREFIX:
 *		EINVAL since we can't deduce hostid part of the address.
 *	SIOCDLIFADDR: delete the specified address.
 *	SIOCDLIFADDR with IFLR_PREFIX:
 *		delete the first address that matches the specified prefix.
 * return values:
 *	EINVAL on invalid parameters
 *	EADDRNOTAVAIL on prefix match failed/specified address not found
 *	other values may be returned from in_ioctl()
 *
 * NOTE! td might be NULL.
 */
static int
in_lifaddr_ioctl(struct socket *so, u_long cmd, caddr_t data, struct ifnet *ifp,
		 struct thread *td)
{
	struct if_laddrreq *iflr = (struct if_laddrreq *)data;

	/* sanity checks */
	if (!data || !ifp) {
		panic("invalid argument to in_lifaddr_ioctl");
		/*NOTRECHED*/
	}

	switch (cmd) {
	case SIOCGLIFADDR:
		/* address must be specified on GET with IFLR_PREFIX */
		if ((iflr->flags & IFLR_PREFIX) == 0)
			break;
		/*FALLTHROUGH*/
	case SIOCALIFADDR:
	case SIOCDLIFADDR:
		/* address must be specified on ADD and DELETE */
		if (iflr->addr.ss_family != AF_INET)
			return EINVAL;
		if (iflr->addr.ss_len != sizeof(struct sockaddr_in))
			return EINVAL;
		/* XXX need improvement */
		if (iflr->dstaddr.ss_family
		 && iflr->dstaddr.ss_family != AF_INET)
			return EINVAL;
		if (iflr->dstaddr.ss_family
		 && iflr->dstaddr.ss_len != sizeof(struct sockaddr_in))
			return EINVAL;
		break;
	default: /*shouldn't happen*/
		return EOPNOTSUPP;
	}
	if (sizeof(struct in_addr) * 8 < iflr->prefixlen)
		return EINVAL;

	switch (cmd) {
	case SIOCALIFADDR:
	    {
		struct in_aliasreq ifra;

		if (iflr->flags & IFLR_PREFIX)
			return EINVAL;

		/* copy args to in_aliasreq, perform ioctl(SIOCAIFADDR_IN6). */
		bzero(&ifra, sizeof ifra);
		bcopy(iflr->iflr_name, ifra.ifra_name, sizeof ifra.ifra_name);

		bcopy(&iflr->addr, &ifra.ifra_addr, iflr->addr.ss_len);

		if (iflr->dstaddr.ss_family) {	/*XXX*/
			bcopy(&iflr->dstaddr, &ifra.ifra_dstaddr,
				iflr->dstaddr.ss_len);
		}

		ifra.ifra_mask.sin_family = AF_INET;
		ifra.ifra_mask.sin_len = sizeof(struct sockaddr_in);
		in_len2mask(&ifra.ifra_mask.sin_addr, iflr->prefixlen);

		return in_control(so, SIOCAIFADDR, (caddr_t)&ifra, ifp, td);
	    }
	case SIOCGLIFADDR:
	case SIOCDLIFADDR:
	    {
		struct ifaddr_container *ifac;
		struct in_ifaddr *ia;
		struct in_addr mask, candidate, match;
		struct sockaddr_in *sin;
		int cmp;

		bzero(&mask, sizeof mask);
		if (iflr->flags & IFLR_PREFIX) {
			/* lookup a prefix rather than address. */
			in_len2mask(&mask, iflr->prefixlen);

			sin = (struct sockaddr_in *)&iflr->addr;
			match.s_addr = sin->sin_addr.s_addr;
			match.s_addr &= mask.s_addr;

			/* if you set extra bits, that's wrong */
			if (match.s_addr != sin->sin_addr.s_addr)
				return EINVAL;

			cmp = 1;
		} else {
			if (cmd == SIOCGLIFADDR) {
				/* on getting an address, take the 1st match */
				match.s_addr = 0; /* gcc4 warning */
				cmp = 0;	/*XXX*/
			} else {
				/* on deleting an address, do exact match */
				in_len2mask(&mask, 32);
				sin = (struct sockaddr_in *)&iflr->addr;
				match.s_addr = sin->sin_addr.s_addr;

				cmp = 1;
			}
		}

		TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
			struct ifaddr *ifa = ifac->ifa;

			if (ifa->ifa_addr->sa_family != AF_INET6)
				continue;
			if (!cmp)
				break;
			candidate.s_addr =
			((struct sockaddr_in *)&ifa->ifa_addr)->sin_addr.s_addr;
			candidate.s_addr &= mask.s_addr;
			if (candidate.s_addr == match.s_addr)
				break;
		}
		if (ifac == NULL)
			return EADDRNOTAVAIL;
		ia = (struct in_ifaddr *)(ifac->ifa);

		if (cmd == SIOCGLIFADDR) {
			/* fill in the if_laddrreq structure */
			bcopy(&ia->ia_addr, &iflr->addr, ia->ia_addr.sin_len);

			if ((ifp->if_flags & IFF_POINTOPOINT) != 0) {
				bcopy(&ia->ia_dstaddr, &iflr->dstaddr,
					ia->ia_dstaddr.sin_len);
			} else
				bzero(&iflr->dstaddr, sizeof iflr->dstaddr);

			iflr->prefixlen =
				in_mask2len(&ia->ia_sockmask.sin_addr);

			iflr->flags = 0;	/*XXX*/

			return 0;
		} else {
			struct in_aliasreq ifra;

			/* fill in_aliasreq and do ioctl(SIOCDIFADDR_IN6) */
			bzero(&ifra, sizeof ifra);
			bcopy(iflr->iflr_name, ifra.ifra_name,
				sizeof ifra.ifra_name);

			bcopy(&ia->ia_addr, &ifra.ifra_addr,
				ia->ia_addr.sin_len);
			if ((ifp->if_flags & IFF_POINTOPOINT) != 0) {
				bcopy(&ia->ia_dstaddr, &ifra.ifra_dstaddr,
					ia->ia_dstaddr.sin_len);
			}
			bcopy(&ia->ia_sockmask, &ifra.ifra_dstaddr,
				ia->ia_sockmask.sin_len);

			return in_control(so, SIOCDIFADDR, (caddr_t)&ifra,
					  ifp, td);
		}
	    }
	}

	return EOPNOTSUPP;	/*just for safety*/
}

/*
 * Delete any existing route for an interface.
 */
void
in_ifscrub(struct ifnet *ifp __unused, struct in_ifaddr *ia)
{
	in_scrubprefix(ia);
}

/*
 * Initialize an interface's internet address
 * and routing table entry.
 */
static int
in_ifinit(struct ifnet *ifp, struct in_ifaddr *ia,
	  const struct sockaddr_in *sin, int scrub)
{
	u_long i = ntohl(sin->sin_addr.s_addr);
	struct sockaddr_in oldaddr;
	struct ifaddr_container *ifac;
	int flags = RTF_UP, error = 0;
	int was_hash = 0;

	ifac = &ia->ia_ifa.ifa_containers[mycpuid];
	oldaddr = ia->ia_addr;

	if (ifac->ifa_listmask & IFA_LIST_IN_IFADDRHASH) {
		was_hash = 1;
		in_iahash_remove(ia);
	}

	ia->ia_addr = *sin;
	if (ia->ia_addr.sin_family == AF_INET)
		in_iahash_insert(ia);

	/*
	 * Give the interface a chance to initialize
	 * if this is its first address,
	 * and to validate the address if necessary.
	 */
	if (ifp->if_ioctl != NULL) {
		ifnet_serialize_all(ifp);
		error = ifp->if_ioctl(ifp, SIOCSIFADDR, (caddr_t)ia, NULL);
		ifnet_deserialize_all(ifp);
		if (error)
			goto fail;
	}

	/*
	 * Delete old route, if requested.
	 */
	if (scrub) {
		ia->ia_ifa.ifa_addr = (struct sockaddr *)&oldaddr;
		in_ifscrub(ifp, ia);
		ia->ia_ifa.ifa_addr = (struct sockaddr *)&ia->ia_addr;
	}

	/*
	 * Calculate netmask/subnetmask.
	 */
	if (IN_CLASSA(i))
		ia->ia_netmask = IN_CLASSA_NET;
	else if (IN_CLASSB(i))
		ia->ia_netmask = IN_CLASSB_NET;
	else
		ia->ia_netmask = IN_CLASSC_NET;
	/*
	 * The subnet mask usually includes at least the standard network part,
	 * but may may be smaller in the case of supernetting.
	 * If it is set, we believe it.
	 */
	if (ia->ia_subnetmask == 0) {
		ia->ia_subnetmask = ia->ia_netmask;
		ia->ia_sockmask.sin_addr.s_addr = htonl(ia->ia_subnetmask);
	} else {
		ia->ia_netmask &= ia->ia_subnetmask;
	}
	ia->ia_net = i & ia->ia_netmask;
	ia->ia_subnet = i & ia->ia_subnetmask;
	in_socktrim(&ia->ia_sockmask);

	/*
	 * Add route for the network.
	 */
	ia->ia_ifa.ifa_metric = ifp->if_metric;
	if (ifp->if_flags & IFF_BROADCAST) {
		ia->ia_broadaddr.sin_addr.s_addr =
			htonl(ia->ia_subnet | ~ia->ia_subnetmask);
		ia->ia_netbroadcast.s_addr =
			htonl(ia->ia_net | ~ ia->ia_netmask);
	} else if (ifp->if_flags & IFF_LOOPBACK) {
		ia->ia_dstaddr = ia->ia_addr;
		flags |= RTF_HOST;
	} else if (ifp->if_flags & IFF_POINTOPOINT) {
		if (ia->ia_dstaddr.sin_family != AF_INET)
			return (0);
		flags |= RTF_HOST;
	}

	/*-
	 * Don't add host routes for interface addresses of
	 * 0.0.0.0 --> 0.255.255.255 netmask 255.0.0.0.  This makes it
	 * possible to assign several such address pairs with consistent
	 * results (no host route) and is required by BOOTP.
	 *
	 * XXX: This is ugly !  There should be a way for the caller to
	 *      say that they don't want a host route.
	 */
	if (ia->ia_addr.sin_addr.s_addr != INADDR_ANY ||
	    ia->ia_netmask != IN_CLASSA_NET ||
	    ia->ia_dstaddr.sin_addr.s_addr != htonl(IN_CLASSA_HOST)) {
		error = in_addprefix(ia, flags);
		if (error)
			goto fail;
	}

	/*
	 * If the interface supports multicast, join the "all hosts"
	 * multicast group on that interface.
	 */
	if (ifp->if_flags & IFF_MULTICAST) {
		struct in_addr addr;

		addr.s_addr = htonl(INADDR_ALLHOSTS_GROUP);
		in_addmulti(&addr, ifp);
	}
	return (0);
fail:
	if (ifac->ifa_listmask & IFA_LIST_IN_IFADDRHASH)
		in_iahash_remove(ia);

	ia->ia_addr = oldaddr;
	if (was_hash)
		in_iahash_insert(ia);
	return (error);
}

#define rtinitflags(x) \
	(((x)->ia_ifp->if_flags & (IFF_LOOPBACK | IFF_POINTOPOINT)) \
	 ? RTF_HOST : 0)

/*
 * Add a route to prefix ("connected route" in cisco terminology).
 * Do nothing, if there are some interface addresses with the same
 * prefix already.  This function assumes that the 'target' parent
 * interface is UP.
 */
static int
in_addprefix(struct in_ifaddr *target, int flags)
{
	struct in_ifaddr_container *iac;
	struct in_addr prefix, mask;
	int error;

#ifdef CARP
	/*
	 * Don't add prefix routes for CARP interfaces.
	 * Prefix routes creation is handled by CARP
	 * interfaces themselves.
	 */
	if (target->ia_ifp->if_type == IFT_CARP)
		return 0;
#endif

	mask = target->ia_sockmask.sin_addr;
	if (flags & RTF_HOST) {
		prefix = target->ia_dstaddr.sin_addr;
	} else {
		prefix = target->ia_addr.sin_addr;
		prefix.s_addr &= mask.s_addr;
	}

	TAILQ_FOREACH(iac, &in_ifaddrheads[mycpuid], ia_link) {
		struct in_ifaddr *ia = iac->ia;
		struct in_addr p;

		/* Don't test against self */
		if (ia == target)
			continue;

		/* The tested address does not own a route entry */
		if ((ia->ia_flags & IFA_ROUTE) == 0)
			continue;

		/* Prefix test */
		if (rtinitflags(ia)) {
			p = ia->ia_dstaddr.sin_addr;
		} else {
			p = ia->ia_addr.sin_addr;
			p.s_addr &= ia->ia_sockmask.sin_addr.s_addr;
		}
		if (prefix.s_addr != p.s_addr)
			continue;

		/*
		 * If the to-be-added address and the curretly being
		 * tested address are not host addresses, we need to
		 * take subnetmask into consideration.
		 */
		if (!(flags & RTF_HOST) && !rtinitflags(ia) &&
		    mask.s_addr != ia->ia_sockmask.sin_addr.s_addr)
			continue;

		/*
		 * If we got a matching prefix route inserted by other
		 * interface address, we don't need to bother.
		 */
		return 0;
	}

	/*
	 * No one seem to have prefix route; insert it.
	 */
	error = rtinit(&target->ia_ifa, RTM_ADD, flags);
	if (!error)
		target->ia_flags |= IFA_ROUTE;
	return error;
}

/*
 * Remove a route to prefix ("connected route" in cisco terminology).
 * Re-installs the route by using another interface address, if there's
 * one with the same prefix (otherwise we lose the route mistakenly).
 */
static void
in_scrubprefix(struct in_ifaddr *target)
{
	struct in_ifaddr_container *iac;
	struct in_addr prefix, mask;
	int error;

#ifdef CARP
	/*
	 * Don't scrub prefix routes for CARP interfaces.
	 * Prefix routes deletion is handled by CARP
	 * interfaces themselves.
	 */
	if (target->ia_ifp->if_type == IFT_CARP)
		return;
#endif

	if ((target->ia_flags & IFA_ROUTE) == 0)
		return;

	mask = target->ia_sockmask.sin_addr;
	if (rtinitflags(target)) {
		prefix = target->ia_dstaddr.sin_addr;
	} else {
		prefix = target->ia_addr.sin_addr;
		prefix.s_addr &= mask.s_addr;
	}

	TAILQ_FOREACH(iac, &in_ifaddrheads[mycpuid], ia_link) {
		struct in_ifaddr *ia = iac->ia;
		struct in_addr p;

		/* Don't test against self */
		if (ia == target)
			continue;

		/* The tested address already owns a route entry */
		if (ia->ia_flags & IFA_ROUTE)
			continue;

		/*
		 * The prefix route of the tested address should
		 * never be installed if its parent interface is
		 * not UP yet.
		 */
		if ((ia->ia_ifp->if_flags & IFF_UP) == 0)
			continue;

#ifdef CARP
		/*
		 * Don't add prefix routes for CARP interfaces.
		 * Prefix routes creation is handled by CARP
		 * interfaces themselves.
		 */
		if (ia->ia_ifp->if_type == IFT_CARP)
			continue;
#endif

		/* Prefix test */
		if (rtinitflags(ia)) {
			p = ia->ia_dstaddr.sin_addr;
		} else {
			p = ia->ia_addr.sin_addr;
			p.s_addr &= ia->ia_sockmask.sin_addr.s_addr;
		}
		if (prefix.s_addr != p.s_addr)
			continue;

		/*
		 * We don't need to test subnetmask here, as what we do
		 * in in_addprefix(), since if the the tested address's
		 * parent interface is UP, the tested address should own
		 * a prefix route entry and we would never reach here.
		 */

		/*
		 * If we got a matching prefix route, move IFA_ROUTE to him
		 */
		rtinit(&target->ia_ifa, RTM_DELETE, rtinitflags(target));
		target->ia_flags &= ~IFA_ROUTE;

		error = rtinit(&ia->ia_ifa, RTM_ADD, rtinitflags(ia) | RTF_UP);
		if (!error)
			ia->ia_flags |= IFA_ROUTE;
		return;
	}

	/*
	 * No candidates for this prefix route; just remove it.
	 */
	rtinit(&target->ia_ifa, RTM_DELETE, rtinitflags(target));
	target->ia_flags &= ~IFA_ROUTE;
}

#undef rtinitflags

/*
 * Return 1 if the address might be a local broadcast address.
 */
int
in_broadcast(struct in_addr in, struct ifnet *ifp)
{
	struct ifaddr_container *ifac;
	u_long t;

	if (in.s_addr == INADDR_BROADCAST ||
	    in.s_addr == INADDR_ANY)
		return 1;
	if (ifp == NULL || (ifp->if_flags & IFF_BROADCAST) == 0)
		return 0;
	t = ntohl(in.s_addr);
	/*
	 * Look through the list of addresses for a match
	 * with a broadcast address.
	 */
#define ia ((struct in_ifaddr *)ifa)
	TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
		struct ifaddr *ifa = ifac->ifa;

		if (ifa->ifa_addr->sa_family == AF_INET &&
		    (in.s_addr == ia->ia_broadaddr.sin_addr.s_addr ||
		     in.s_addr == ia->ia_netbroadcast.s_addr ||
		     /*
		      * Check for old-style (host 0) broadcast.
		      */
		     t == ia->ia_subnet || t == ia->ia_net) &&
		     /*
		      * Check for an all one subnetmask. These
		      * only exist when an interface gets a secondary
		      * address.
		      */
		     ia->ia_subnetmask != (u_long)0xffffffff)
			    return 1;
	}
	return (0);
#undef ia
}

/*
 * Add an address to the list of IP multicast addresses for a given interface.
 */
struct in_multi *
in_addmulti(struct in_addr *ap, struct ifnet *ifp)
{
	struct in_multi *inm;
	int error;
	struct sockaddr_in sin;
	struct ifmultiaddr *ifma;

	KASSERT(&curthread->td_msgport == netisr_cpuport(0),
	    ("in_addmulti is not called in netisr0"));

	/*
	 * Call generic routine to add membership or increment
	 * refcount.  It wants addresses in the form of a sockaddr,
	 * so we build one here (being careful to zero the unused bytes).
	 */
	bzero(&sin, sizeof sin);
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof sin;
	sin.sin_addr = *ap;
	error = if_addmulti(ifp, (struct sockaddr *)&sin, &ifma);
	if (error)
		return NULL;

	/*
	 * If ifma->ifma_protospec is null, then if_addmulti() created
	 * a new record.  Otherwise, we are done.
	 */
	if (ifma->ifma_protospec != NULL)
		return ifma->ifma_protospec;

	inm = kmalloc(sizeof *inm, M_IPMADDR, M_WAITOK | M_ZERO);
	inm->inm_addr = *ap;
	inm->inm_ifp = ifp;
	inm->inm_ifma = ifma;
	ifma->ifma_protospec = inm;
	LIST_INSERT_HEAD(&in_multihead, inm, inm_link);

	/*
	 * Let IGMP know that we have joined a new IP multicast group.
	 */
	igmp_joingroup(inm);
	return inm;
}

/*
 * Delete a multicast address record.
 */
void
in_delmulti(struct in_multi *inm)
{
	struct ifmultiaddr *ifma;
	struct in_multi my_inm;

	KASSERT(&curthread->td_msgport == netisr_cpuport(0),
	    ("in_delmulti is not called in netisr0"));

	ifma = inm->inm_ifma;
	my_inm.inm_ifp = NULL ; /* don't send the leave msg */
	if (ifma->ifma_refcount == 1) {
		/*
		 * No remaining claims to this record; let IGMP know that
		 * we are leaving the multicast group.
		 * But do it after the if_delmulti() which might reset
		 * the interface and nuke the packet.
		 */
		my_inm = *inm ;
		ifma->ifma_protospec = NULL;
		LIST_REMOVE(inm, inm_link);
		kfree(inm, M_IPMADDR);
	}
	/* XXX - should be separate API for when we have an ifma? */
	if_delmulti(ifma->ifma_ifp, ifma->ifma_addr);
	if (my_inm.inm_ifp != NULL)
		igmp_leavegroup(&my_inm);
}

static void
in_ifdetach_dispatch(netmsg_t nmsg)
{
	struct lwkt_msg *lmsg = &nmsg->lmsg;
	struct ifnet *ifp = lmsg->u.ms_resultp;
	int cpu;

	in_pcbpurgeif0(&ripcbinfo, ifp);
	for (cpu = 0; cpu < ncpus2; ++cpu)
		in_pcbpurgeif0(&udbinfo[cpu], ifp);

	lwkt_replymsg(lmsg, 0);
}

void
in_ifdetach(struct ifnet *ifp)
{
	struct netmsg_base nmsg;
	struct lwkt_msg *lmsg = &nmsg.lmsg;

	netmsg_init(&nmsg, NULL, &curthread->td_msgport, 0,
	    in_ifdetach_dispatch);
	lmsg->u.ms_resultp = ifp;

	lwkt_domsg(netisr_cpuport(0), lmsg, 0);
}
