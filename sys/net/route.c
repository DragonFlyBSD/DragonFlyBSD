/*
 * Copyright (c) 1980, 1986, 1991, 1993
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
 *	@(#)route.c	8.3 (Berkeley) 1/9/95
 * $FreeBSD: src/sys/net/route.c,v 1.59.2.10 2003/01/17 08:04:00 ru Exp $
 * $DragonFly: src/sys/net/route.c,v 1.11 2004/12/28 08:09:59 hsu Exp $
 */

#include "opt_inet.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/domain.h>
#include <sys/kernel.h>

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>
#include <net/ip_mroute/ip_mroute.h>

static struct rtstat rtstat;
struct radix_node_head *rt_tables[AF_MAX+1];

static void	rt_maskedcopy (struct sockaddr *, struct sockaddr *,
			       struct sockaddr *);
static void	rtable_init (void **);

static void
rtable_init(void **table)
{
	struct domain *dom;

	for (dom = domains; dom; dom = dom->dom_next)
		if (dom->dom_rtattach)
			dom->dom_rtattach(&table[dom->dom_family],
					  dom->dom_rtoffset);
}

void
route_init()
{
	rn_init();	/* initialize all zeroes, all ones, mask table */
	rtable_init((void **)rt_tables);
}

/*
 * Packet routing routines.
 */

/*
 * Lookup and fill in the "ro_rt" rtentry field in a route structure given
 * an address in the ro_dst field.  Always send a report and always
 * clone routes.
 */
void
rtalloc(struct route *ro)
{
	rtalloc_ign(ro, 0UL);
}

/*
 * Lookup and fill in the "ro_rt" rtentry field in a route structure given
 * an address in the ro_dst field.  Always send a report and optionally
 * clone routes when RTF_CLONING or RTF_PRCLONING are not being ignored.
 */
void
rtalloc_ign(struct route *ro, u_long ignore)
{
	if (ro->ro_rt != NULL) {
		if (ro->ro_rt->rt_ifp != NULL && ro->ro_rt->rt_flags & RTF_UP)
			return;
		rtfree(ro->ro_rt);
		ro->ro_rt = NULL;
	}
	ro->ro_rt = rtlookup(&ro->ro_dst, 1, ignore);
}

/*
 * Look up the route that matches the given "dst" address.
 *
 * Create and return a cloned route if "dst" matches a cloning route
 * and the RTF_CLONING and RTF_PRCLONING flags are not being ignored.
 *
 * Any route returned has its refcnt incremented.
 */
struct rtentry *
rtlookup(struct sockaddr *dst, int report, u_long ignflags)
{
	struct radix_node_head *rnh = rt_tables[dst->sa_family];
	struct rtentry *rt;
	struct radix_node *rn;
	struct rt_addrinfo info;
	u_long nflags;
	int s, err, msgtype;

	s = splnet();
	if (rnh != NULL && (rn = rnh->rnh_matchaddr((char *)dst, rnh))) {
		rt = (struct rtentry *)rn;
		nflags = rt->rt_flags & ~ignflags;
		if (report && (nflags & (RTF_CLONING | RTF_PRCLONING))) {
			struct rtentry *clonedroute;

			clonedroute = rt;  /* value used in rtrequest()! */
			err = rtrequest(RTM_RESOLVE, dst, NULL, NULL, 0,
					&clonedroute);
			if (err != 0) {
				/* use master cloning route on clone failure */
				rt->rt_refcnt++;
				goto reportmiss;
			}
			rt = clonedroute;  /* return cloned route to caller */
			if (clonedroute->rt_flags & RTF_XRESOLVE) {
				/* Cloned route needs external resolution. */
				msgtype = RTM_RESOLVE;
				goto reportmsg;
			}
			/* Inform listeners of the new route. */
			bzero(&info, sizeof(info));
			info.rti_info[RTAX_DST] = rt_key(clonedroute);
			info.rti_info[RTAX_NETMASK] = rt_mask(clonedroute);
			info.rti_info[RTAX_GATEWAY] = clonedroute->rt_gateway;
			if (clonedroute->rt_ifp != NULL) {
				info.rti_info[RTAX_IFP] =
				    TAILQ_FIRST(&clonedroute->rt_ifp
						->if_addrhead)->ifa_addr;
				info.rti_info[RTAX_IFA] =
				    clonedroute->rt_ifa->ifa_addr;
			}
			rt_missmsg(RTM_ADD, &info, clonedroute->rt_flags, 0);
		} else
			rt->rt_refcnt++;	/* most common case */
	} else {
		rt = NULL;
		rtstat.rts_unreach++;
		if (report) {
			err = 0;
reportmiss:
			msgtype = RTM_MISS;
reportmsg:
			bzero(&info, sizeof(info));
			info.rti_info[RTAX_DST] = dst;
			rt_missmsg(msgtype, &info, 0, err);
		}
	}
	splx(s);
	return (rt);
}

void
rtfree(struct rtentry *rt)
{
	struct radix_node_head *rnh = rt_tables[rt_key(rt)->sa_family];

	--rt->rt_refcnt;
	if (rnh->rnh_close && rt->rt_refcnt == 0)
		rnh->rnh_close((struct radix_node *)rt, rnh);
	if (rt->rt_refcnt <= 0 && !(rt->rt_flags & RTF_UP)) {
		KASSERT(!(rt->rt_nodes->rn_flags & (RNF_ACTIVE | RNF_ROOT)),
			("rtfree: rn_flags 0x%x ", rt->rt_nodes->rn_flags));
		KASSERT(rt->rt_refcnt == 0,
			("rtfree: rt_refcnt %ld", rt->rt_refcnt));
		if (rt->rt_ifa != NULL)
			IFAFREE(rt->rt_ifa);
		if (rt->rt_parent != NULL)
			RTFREE(rt->rt_parent);
		Free(rt_key(rt));  /* Also frees gateway.  See rt_setgate(). */
		Free(rt);
	}
}

#define	sa_equal(a1, a2) (bcmp((a1), (a2), (a1)->sa_len) == 0)

/*
 * Force a routing table entry to the specified
 * destination to go through the given gateway.
 * Normally called as a result of a routing redirect
 * message from the network layer.
 *
 * N.B.: must be called at splnet
 *
 */
void
rtredirect(
	struct sockaddr *dst,
	struct sockaddr *gateway,
	struct sockaddr *netmask,
	int flags,
	struct sockaddr *src,
	struct rtentry **rtp)
{
	struct rtentry *rt;
	struct rt_addrinfo info;
	struct ifaddr *ifa;
	short *stat = NULL;
	int error;

	/* verify the gateway is directly reachable */
	if ((ifa = ifa_ifwithnet(gateway)) == NULL) {
		error = ENETUNREACH;
		goto out;
	}

	/*
	 * If the redirect isn't from our current router for this dst,
	 * it's either old or wrong.  If it redirects us to ourselves,
	 * we have a routing loop, perhaps as a result of an interface
	 * going down recently.
	 */
	if (!(flags & RTF_DONE) &&
	    (rt = rtlookup(dst, 0, 0UL)) != NULL &&
	    (!sa_equal(src, rt->rt_gateway) || rt->rt_ifa != ifa)) {
		error = EINVAL;
		goto done;
	} else if (ifa_ifwithaddr(gateway)) {
		error = EHOSTUNREACH;
		goto done;
	}

	/*
	 * Create a new entry if we just got back a wildcard entry
	 * or the the lookup failed.  This is necessary for hosts
	 * which use routing redirects generated by smart gateways
	 * to dynamically build the routing tables.
	 */
	if (rt == NULL || (rt_mask(rt) != NULL && rt_mask(rt)->sa_len < 2))
		goto create;

	/*
	 * Don't listen to the redirect if it's for a route to an interface.
	 */
	if (rt->rt_flags & RTF_GATEWAY) {
		if ((!(rt->rt_flags & RTF_HOST)) && (flags & RTF_HOST)) {
			/*
			 * Changing from route to net => route to host.
			 * Create new route, rather than smashing route to net.
			 */
create:
			if (rt != NULL)
				rtfree(rt);
			flags |=  RTF_GATEWAY | RTF_DYNAMIC;
			bzero(&info, sizeof(info));
			info.rti_info[RTAX_DST] = dst;
			info.rti_info[RTAX_GATEWAY] = gateway;
			info.rti_info[RTAX_NETMASK] = netmask;
			info.rti_ifa = ifa;
			info.rti_flags = flags;
			rt = NULL;
			error = rtrequest1(RTM_ADD, &info, &rt);
			if (rt != NULL)
				flags = rt->rt_flags;
			stat = &rtstat.rts_dynamic;
		} else {
			/*
			 * Smash the current notion of the gateway to
			 * this destination.  Should check about netmask!!!
			 */
			rt->rt_flags |= RTF_MODIFIED;
			flags |= RTF_MODIFIED;
			stat = &rtstat.rts_newgateway;
			/* Add the key and gateway (in one malloc'ed chunk). */
			rt_setgate(rt, rt_key(rt), gateway);
			error = 0;
		}
	} else {
		error = EHOSTUNREACH;
	}

done:
	if (rt != NULL) {
		if (rtp != NULL && error == 0)
			*rtp = rt;
		else
			rtfree(rt);
	}

out:
	if (error != 0)
		rtstat.rts_badredirect++;
	else if (stat != NULL)
		(*stat)++;

	bzero(&info, sizeof(info));
	info.rti_info[RTAX_DST] = dst;
	info.rti_info[RTAX_GATEWAY] = gateway;
	info.rti_info[RTAX_NETMASK] = netmask;
	info.rti_info[RTAX_AUTHOR] = src;
	rt_missmsg(RTM_REDIRECT, &info, flags, error);
}

/*
* Routing table ioctl interface.
*/
int
rtioctl(u_long req, caddr_t data, struct thread *td)
{
#ifdef INET
	/* Multicast goop, grrr... */
	return mrt_ioctl ? mrt_ioctl(req, data) : EOPNOTSUPP;
#else
	return ENXIO;
#endif
}

struct ifaddr *
ifa_ifwithroute(int flags, struct sockaddr *dst, struct sockaddr *gateway)
{
	struct ifaddr *ifa;

	if (!(flags & RTF_GATEWAY)) {
		/*
		 * If we are adding a route to an interface,
		 * and the interface is a point-to-point link,
		 * we should search for the destination
		 * as our clue to the interface.  Otherwise
		 * we can use the local address.
		 */
		ifa = NULL;
		if (flags & RTF_HOST) {
			ifa = ifa_ifwithdstaddr(dst);
		}
		if (ifa == NULL)
			ifa = ifa_ifwithaddr(gateway);
	} else {
		/*
		 * If we are adding a route to a remote net
		 * or host, the gateway may still be on the
		 * other end of a pt to pt link.
		 */
		ifa = ifa_ifwithdstaddr(gateway);
	}
	if (ifa == NULL)
		ifa = ifa_ifwithnet(gateway);
	if (ifa == NULL) {
		struct rtentry *rt = rtlookup(gateway, 0, 0UL);

		if (rt == NULL)
			return (NULL);
		rt->rt_refcnt--;
		if ((ifa = rt->rt_ifa) == NULL)
			return (NULL);
	}
	if (ifa->ifa_addr->sa_family != dst->sa_family) {
		struct ifaddr *oifa = ifa;

		ifa = ifaof_ifpforaddr(dst, ifa->ifa_ifp);
		if (ifa == NULL)
			ifa = oifa;
	}
	return (ifa);
}

static int rt_fixdelete (struct radix_node *, void *);
static int rt_fixchange (struct radix_node *, void *);

struct rtfc_arg {
	struct rtentry *rt0;
	struct radix_node_head *rnh;
};

int
rt_getifa(struct rt_addrinfo *info)
{
	struct sockaddr *gateway = info->rti_info[RTAX_GATEWAY];
	struct sockaddr *dst = info->rti_info[RTAX_DST];
	struct sockaddr *ifaaddr = info->rti_info[RTAX_IFA];
	struct sockaddr *ifpaddr = info->rti_info[RTAX_IFP];
	int flags = info->rti_flags;
	struct ifaddr *ifa;
	int error = 0;

	/*
	 * ifp may be specified by sockaddr_dl
	 * when protocol address is ambiguous.
	 */
	if (info->rti_ifp == NULL && ifpaddr != NULL &&
	    ifpaddr->sa_family == AF_LINK &&
	    (ifa = ifa_ifwithnet(ifpaddr)) != NULL)
		info->rti_ifp = ifa->ifa_ifp;
	if (info->rti_ifa == NULL && ifaaddr != NULL)
		info->rti_ifa = ifa_ifwithaddr(ifaaddr);
	if (info->rti_ifa == NULL) {
		struct sockaddr *sa;

		sa = ifaaddr != NULL ? ifaaddr :
		    (gateway != NULL ? gateway : dst);
		if (sa != NULL && info->rti_ifp != NULL)
			info->rti_ifa = ifaof_ifpforaddr(sa, info->rti_ifp);
		else if (dst != NULL && gateway != NULL)
			info->rti_ifa = ifa_ifwithroute(flags, dst, gateway);
		else if (sa != NULL)
			info->rti_ifa = ifa_ifwithroute(flags, sa, sa);
	}
	if ((ifa = info->rti_ifa) != NULL) {
		if (info->rti_ifp == NULL)
			info->rti_ifp = ifa->ifa_ifp;
	} else
		error = ENETUNREACH;
	return (error);
}

/*
 * Do appropriate manipulations of a routing tree given
 * all the bits of info needed
 */
int
rtrequest(
	int req,
	struct sockaddr *dst,
	struct sockaddr *gateway,
	struct sockaddr *netmask,
	int flags,
	struct rtentry **ret_nrt)
{
	struct rt_addrinfo info;

	bzero(&info, sizeof info);
	info.rti_flags = flags;
	info.rti_info[RTAX_DST] = dst;
	info.rti_info[RTAX_GATEWAY] = gateway;
	info.rti_info[RTAX_NETMASK] = netmask;
	return rtrequest1(req, &info, ret_nrt);
}

int
rtrequest1(int req, struct rt_addrinfo *info, struct rtentry **ret_nrt)
{
	struct sockaddr *dst = info->rti_info[RTAX_DST];
	struct rtentry *rt;
	struct radix_node *rn;
	struct radix_node_head *rnh;
	struct ifaddr *ifa;
	struct sockaddr *ndst;
	int error = 0;
	int s;

#define gotoerr(x) { error = x ; goto bad; }

	s = splnet();
	/*
	 * Find the correct routing tree to use for this Address Family
	 */
	if ((rnh = rt_tables[dst->sa_family]) == NULL)
		gotoerr(EAFNOSUPPORT);

	/*
	 * If we are adding a host route then we don't want to put
	 * a netmask in the tree, nor do we want to clone it.
	 */
	if (info->rti_flags & RTF_HOST) {
		info->rti_info[RTAX_NETMASK] = NULL;
		info->rti_flags &= ~(RTF_CLONING | RTF_PRCLONING);
	}

	switch (req) {
	case RTM_DELETE:
		/* Remove the item from the tree. */
		rn = rnh->rnh_deladdr((char *)info->rti_info[RTAX_DST],
				      (char *)info->rti_info[RTAX_NETMASK],
				      rnh);
		if (rn == NULL)
			gotoerr(ESRCH);
		KASSERT(!(rn->rn_flags & (RNF_ACTIVE | RNF_ROOT)),
			("rnh_deladdr returned flags 0x%x", rn->rn_flags));
		rt = (struct rtentry *)rn;

		/* Free any routes cloned from this one. */
		if ((rt->rt_flags & (RTF_CLONING | RTF_PRCLONING)) &&
		    rt_mask(rt) != NULL) {
			rnh->rnh_walktree_from(rnh, (char *)rt_key(rt),
					       (char *)rt_mask(rt),
					       rt_fixdelete, rt);
		}

		if (rt->rt_gwroute != NULL) {
			RTFREE(rt->rt_gwroute);
			rt->rt_gwroute = NULL;
		}

		/*
		 * NB: RTF_UP must be set during the search above,
		 * because we might delete the last ref, causing
		 * rt to get freed prematurely.
		 */
		rt->rt_flags &= ~RTF_UP;

		/* Give the protocol a chance to keep things in sync. */
		if ((ifa = rt->rt_ifa) && ifa->ifa_rtrequest)
			ifa->ifa_rtrequest(RTM_DELETE, rt, info);

		/*
		 * If the caller wants it, then it can have it,
		 * but it's up to it to free the rtentry as we won't be
		 * doing it.
		 */
		if (ret_nrt != NULL) {
			*ret_nrt = rt;
		} else if (rt->rt_refcnt <= 0) {
			rt->rt_refcnt++;  /* refcnt > 0 required for rtfree() */
			rtfree(rt);
		}
		break;

	case RTM_RESOLVE:
		if (ret_nrt == NULL || (rt = *ret_nrt) == NULL)
			gotoerr(EINVAL);
		ifa = rt->rt_ifa;
		info->rti_flags = rt->rt_flags &
		    ~(RTF_CLONING | RTF_PRCLONING | RTF_STATIC);
		info->rti_flags |= RTF_WASCLONED;
		info->rti_info[RTAX_GATEWAY] = rt->rt_gateway;
		if ((info->rti_info[RTAX_NETMASK] = rt->rt_genmask) == NULL)
			info->rti_flags |= RTF_HOST;
		goto makeroute;

	case RTM_ADD:
		KASSERT(!(info->rti_flags & RTF_GATEWAY) ||
			info->rti_info[RTAX_GATEWAY] != NULL,
		    ("rtrequest: GATEWAY but no gateway"));

		if (info->rti_ifa == NULL && (error = rt_getifa(info)))
			gotoerr(error);
		ifa = info->rti_ifa;
makeroute:
		R_Malloc(rt, struct rtentry *, sizeof *rt);
		if (rt == NULL)
			gotoerr(ENOBUFS);
		bzero(rt, sizeof *rt);
		rt->rt_flags = RTF_UP | info->rti_flags;
		error = rt_setgate(rt, dst, info->rti_info[RTAX_GATEWAY]);
		if (error != 0) {
			Free(rt);
			gotoerr(error);
		}

		ndst = rt_key(rt);
		if (info->rti_info[RTAX_NETMASK] != NULL)
			rt_maskedcopy(dst, ndst, info->rti_info[RTAX_NETMASK]);
		else
			bcopy(dst, ndst, dst->sa_len);

		/*
		 * Note that we now have a reference to the ifa.
		 * This moved from below so that rnh->rnh_addaddr() can
		 * examine the ifa and  ifa->ifa_ifp if it so desires.
		 */
		IFAREF(ifa);
		rt->rt_ifa = ifa;
		rt->rt_ifp = ifa->ifa_ifp;
		/* XXX mtu manipulation will be done in rnh_addaddr -- itojun */

		rn = rnh->rnh_addaddr((char *)ndst,
				      (char *)info->rti_info[RTAX_NETMASK],
				      rnh, rt->rt_nodes);
		if (rn == NULL) {
			struct rtentry *oldrt;

			/*
			 * We already have one of these in the tree.
			 * We do a special hack: if the old route was
			 * cloned, then we blow it away and try
			 * re-inserting the new one.
			 */
			oldrt = rtlookup(ndst, 0, RTF_CLONING | RTF_PRCLONING);
			if (oldrt != NULL) {
				--oldrt->rt_refcnt;
				if (oldrt->rt_flags & RTF_WASCLONED) {
					rtrequest(RTM_DELETE, rt_key(oldrt),
						  oldrt->rt_gateway,
						  rt_mask(oldrt),
						  oldrt->rt_flags, NULL);
					rn = rnh->rnh_addaddr((char *)ndst,
						  (char *)
						  info->rti_info[RTAX_NETMASK],
						  rnh, rt->rt_nodes);
				}
			}
		}

		/*
		 * If it still failed to go into the tree,
		 * then un-make it (this should be a function).
		 */
		if (rn == NULL) {
			if (rt->rt_gwroute != NULL)
				rtfree(rt->rt_gwroute);
			IFAFREE(ifa);
			Free(rt_key(rt));
			Free(rt);
			gotoerr(EEXIST);
		}

		/*
		 * If we got here from RESOLVE, then we are cloning
		 * so clone the rest, and note that we
		 * are a clone (and increment the parent's references)
		 */
		if (req == RTM_RESOLVE) {
			rt->rt_rmx = (*ret_nrt)->rt_rmx;    /* copy metrics */
			rt->rt_rmx.rmx_pksent = 0;  /* reset packet counter */
			if ((*ret_nrt)->rt_flags &
				       (RTF_CLONING | RTF_PRCLONING)) {
				rt->rt_parent = *ret_nrt;
				(*ret_nrt)->rt_refcnt++;
			}
		}

		/*
		 * if this protocol has something to add to this then
		 * allow it to do that as well.
		 */
		if (ifa->ifa_rtrequest != NULL)
			ifa->ifa_rtrequest(req, rt, info);

		/*
		 * We repeat the same procedure from rt_setgate() here because
		 * it doesn't fire when we call it there because the node
		 * hasn't been added to the tree yet.
		 */
		if (req == RTM_ADD && !(rt->rt_flags & RTF_HOST) &&
		    rt_mask(rt) != NULL) {
			struct rtfc_arg arg = { rt, rnh };

			rnh->rnh_walktree_from(rnh, (char *)rt_key(rt),
					       (char *)rt_mask(rt),
					       rt_fixchange, &arg);
		}

		/*
		 * Return the resulting rtentry,
		 * increasing the number of references by one.
		 */
		if (ret_nrt != NULL) {
			rt->rt_refcnt++;
			*ret_nrt = rt;
		}
		break;
	default:
		error = EOPNOTSUPP;
	}
bad:
	splx(s);
	return (error);
}

/*
 * Called from rtrequest(RTM_DELETE, ...) to fix up the route's ``family''
 * (i.e., the routes related to it by the operation of cloning).  This
 * routine is iterated over all potential former-child-routes by way of
 * rnh->rnh_walktree_from() above, and those that actually are children of
 * the late parent (passed in as VP here) are themselves deleted.
 */
static int
rt_fixdelete(struct radix_node *rn, void *vp)
{
	struct rtentry *rt = (struct rtentry *)rn;
	struct rtentry *rt0 = vp;

	if (rt->rt_parent == rt0 &&
	    !(rt->rt_flags & (RTF_PINNED | RTF_CLONING | RTF_PRCLONING))) {
		return rtrequest(RTM_DELETE, rt_key(rt), NULL, rt_mask(rt),
				 rt->rt_flags, NULL);
	}
	return 0;
}

/*
 * This routine is called from rt_setgate() to do the analogous thing for
 * adds and changes.  There is the added complication in this case of a
 * middle insert; i.e., insertion of a new network route between an older
 * network route and (cloned) host routes.  For this reason, a simple check
 * of rt->rt_parent is insufficient; each candidate route must be tested
 * against the (mask, value) of the new route (passed as before in vp)
 * to see if the new route matches it.
 *
 * XXX - it may be possible to do fixdelete() for changes and reserve this
 * routine just for adds.  I'm not sure why I thought it was necessary to do
 * changes this way.
 */
#ifdef DEBUG
static int rtfcdebug = 0;
#endif

static int
rt_fixchange(struct radix_node *rn, void *vp)
{
	struct rtentry *rt = (struct rtentry *)rn;
	struct rtfc_arg *ap = vp;
	struct rtentry *rt0 = ap->rt0;
	struct radix_node_head *rnh = ap->rnh;
	u_char *xk1, *xm1, *xk2, *xmp;
	int i, len, mlen;

#ifdef DEBUG
	if (rtfcdebug)
		printf("rt_fixchange: rt %p, rt0 %p\n", rt, rt0);
#endif

	if (rt->rt_parent == NULL ||
	    (rt->rt_flags & (RTF_PINNED | RTF_CLONING | RTF_PRCLONING))) {
#ifdef DEBUG
		if (rtfcdebug) printf("no parent, pinned or cloning\n");
#endif
		return 0;
	}

	if (rt->rt_parent == rt0) {
#ifdef DEBUG
		if (rtfcdebug) printf("parent match\n");
#endif
		return rtrequest(RTM_DELETE, rt_key(rt), NULL, rt_mask(rt),
				 rt->rt_flags, NULL);
	}

	/*
	 * There probably is a function somewhere which does this...
	 * if not, there should be.
	 */
	len = imin(rt_key(rt0)->sa_len, rt_key(rt)->sa_len);

	xk1 = (u_char *)rt_key(rt0);
	xm1 = (u_char *)rt_mask(rt0);
	xk2 = (u_char *)rt_key(rt);

	/* avoid applying a less specific route */
	xmp = (u_char *)rt_mask(rt->rt_parent);
	mlen = rt_key(rt->rt_parent)->sa_len;
	if (mlen > rt_key(rt0)->sa_len) {
#ifdef DEBUG
		if (rtfcdebug)
			printf("rt_fixchange: inserting a less "
			       "specific route\n");
#endif
		return 0;
	}
	for (i = rnh->rnh_treetop->rn_offset; i < mlen; i++) {
		if ((xmp[i] & ~(xmp[i] ^ xm1[i])) != xmp[i]) {
#ifdef DEBUG
			if (rtfcdebug)
				printf("rt_fixchange: inserting a less "
				       "specific route\n");
#endif
			return 0;
		}
	}

	for (i = rnh->rnh_treetop->rn_offset; i < len; i++) {
		if ((xk2[i] & xm1[i]) != xk1[i]) {
#ifdef DEBUG
			if (rtfcdebug) printf("no match\n");
#endif
			return 0;
		}
	}

	/*
	 * OK, this node is a clone, and matches the node currently being
	 * changed/added under the node's mask.  So, get rid of it.
	 */
#ifdef DEBUG
	if (rtfcdebug) printf("deleting\n");
#endif
	return rtrequest(RTM_DELETE, rt_key(rt), NULL, rt_mask(rt),
			 rt->rt_flags, NULL);
}

#define ROUNDUP(a) (a>0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))

int
rt_setgate(struct rtentry *rt0, struct sockaddr *dst, struct sockaddr *gate)
{
	char *space, *oldspace;
	int dlen = ROUNDUP(dst->sa_len), glen = ROUNDUP(gate->sa_len);
	struct rtentry *rt = rt0;
	struct radix_node_head *rnh = rt_tables[dst->sa_family];

	/*
	 * A host route with the destination equal to the gateway
	 * will interfere with keeping LLINFO in the routing
	 * table, so disallow it.
	 */
	if (((rt0->rt_flags & (RTF_HOST | RTF_GATEWAY | RTF_LLINFO)) ==
			      (RTF_HOST | RTF_GATEWAY)) &&
	    dst->sa_len == gate->sa_len &&
	    sa_equal(dst, gate)) {
		/*
		 * The route might already exist if this is an RTM_CHANGE
		 * or a routing redirect, so try to delete it.
		 */
		if (rt_key(rt0) != NULL)
			rtrequest(RTM_DELETE, rt_key(rt0), rt0->rt_gateway,
				  rt_mask(rt0), rt0->rt_flags, NULL);
		return EADDRNOTAVAIL;
	}

	/*
	 * Both dst and gateway are stored in the same malloc'ed chunk
	 * (If I ever get my hands on....)
	 * if we need to malloc a new chunk, then keep the old one around
	 * till we don't need it any more.
	 */
	if (rt->rt_gateway == NULL || glen > ROUNDUP(rt->rt_gateway->sa_len)) {
		oldspace = (char *)rt_key(rt);
		R_Malloc(space, char *, dlen + glen);
		if (space == NULL)
			return ENOBUFS;
		rt->rt_nodes->rn_key = space;
	} else {
		space = (char *)rt_key(rt);	/* Just use the old space. */
		oldspace = NULL;
	}

	/* Set the gateway value. */
	rt->rt_gateway = (struct sockaddr *)(space + dlen);
	bcopy(gate, rt->rt_gateway, glen);

	if (oldspace != NULL) {
		/*
		 * If we allocated a new chunk, preserve the original dst.
		 * This way, rt_setgate() really just sets the gate
		 * and leaves the dst field alone.
		 */
		bcopy(dst, space, dlen);
		Free(oldspace);
	}

	/*
	 * If there is already a gwroute, it's now almost definitely wrong
	 * so drop it.
	 */
	if (rt->rt_gwroute != NULL) {
		RTFREE(rt->rt_gwroute);
		rt->rt_gwroute = NULL;
	}
	if (rt->rt_flags & RTF_GATEWAY) {
		/*
		 * Cloning loop avoidance: In the presence of
		 * protocol-cloning and bad configuration, it is
		 * possible to get stuck in bottomless mutual recursion
		 * (rtrequest rt_setgate rtlookup).  We avoid this
		 * by not allowing protocol-cloning to operate for
		 * gateways (which is probably the correct choice
		 * anyway), and avoid the resulting reference loops
		 * by disallowing any route to run through itself as
		 * a gateway.  This is obviously mandatory when we
		 * get rt->rt_output().
		 *
		 * This breaks TTCP!  XXX JH
		 */
		rt->rt_gwroute = rtlookup(gate, 1, RTF_PRCLONING);
		if (rt->rt_gwroute == rt) {
			rt->rt_gwroute = NULL;
			--rt->rt_refcnt;
			return EDQUOT; /* failure */
		}
	}

	/*
	 * This isn't going to do anything useful for host routes, so
	 * don't bother.  Also make sure we have a reasonable mask
	 * (we don't yet have one during adds).
	 */
	if (!(rt->rt_flags & RTF_HOST) && rt_mask(rt) != NULL) {
		struct rtfc_arg arg = { rt, rnh };

		rnh->rnh_walktree_from(rnh, (char *)rt_key(rt),
				       (char *)rt_mask(rt),
				       rt_fixchange, &arg);
	}

	return 0;
}

static void
rt_maskedcopy(
	struct sockaddr *src,
	struct sockaddr *dst,
	struct sockaddr *netmask)
{
	u_char *cp1 = (u_char *)src;
	u_char *cp2 = (u_char *)dst;
	u_char *cp3 = (u_char *)netmask;
	u_char *cplim = cp2 + *cp3;
	u_char *cplim2 = cp2 + *cp1;

	*cp2++ = *cp1++; *cp2++ = *cp1++; /* copies sa_len & sa_family */
	cp3 += 2;
	if (cplim > cplim2)
		cplim = cplim2;
	while (cp2 < cplim)
		*cp2++ = *cp1++ & *cp3++;
	if (cp2 < cplim2)
		bzero(cp2, cplim2 - cp2);
}

int
rt_llroute(struct sockaddr *dst, struct rtentry *rt0, struct rtentry **drt)
{
	struct rtentry *up_rt, *rt;

	if (!(rt0->rt_flags & RTF_UP)) {
		up_rt = rtlookup(dst, 1, 0UL);
		if (up_rt == NULL)
			return (EHOSTUNREACH);
		up_rt->rt_refcnt--;
	} else
		up_rt = rt0;
	if (up_rt->rt_flags & RTF_GATEWAY) {
		if (up_rt->rt_gwroute == NULL) {
			up_rt->rt_gwroute = rtlookup(up_rt->rt_gateway, 1, 0UL);
			if (up_rt->rt_gwroute == NULL)
				return (EHOSTUNREACH);
		} else if (!(up_rt->rt_gwroute->rt_flags & RTF_UP)) {
			rtfree(up_rt->rt_gwroute);
			up_rt->rt_gwroute = rtlookup(up_rt->rt_gateway, 1, 0UL);
			if (up_rt->rt_gwroute == NULL)
				return (EHOSTUNREACH);
		}
		rt = up_rt->rt_gwroute;
	} else
		rt = up_rt;
	if (rt->rt_flags & RTF_REJECT &&
	    (rt->rt_rmx.rmx_expire == 0 ||		/* rt doesn't expire */
	     time_second < rt->rt_rmx.rmx_expire))	/* rt not expired */
		return (rt->rt_flags & RTF_HOST ?  EHOSTDOWN : EHOSTUNREACH);
	*drt = rt;
	return 0;
}

/*
 * Set up a routing table entry, normally
 * for an interface.
 */
int
rtinit(struct ifaddr *ifa, int cmd, int flags)
{
	struct sockaddr *dst, *deldst, *netmask;
	struct rtentry *rt;
	struct rtentry *nrt = NULL;
	struct mbuf *m = NULL;
	struct radix_node_head *rnh;
	struct radix_node *rn;
	struct rt_addrinfo info;
	int error;

	if (flags & RTF_HOST) {
		dst = ifa->ifa_dstaddr;
		netmask = NULL;
	} else {
		dst = ifa->ifa_addr;
		netmask = ifa->ifa_netmask;
	}
	/*
	 * If it's a delete, check that if it exists, it's on the correct
	 * interface or we might scrub a route to another ifa which would
	 * be confusing at best and possibly worse.
	 */
	if (cmd == RTM_DELETE) {
		/*
		 * It's a delete, so it should already exist..
		 * If it's a net, mask off the host bits
		 * (Assuming we have a mask)
		 */
		if (netmask != NULL) {
			m = m_get(MB_DONTWAIT, MT_SONAME);
			if (m == NULL)
				return (ENOBUFS);
			deldst = mtod(m, struct sockaddr *);
			rt_maskedcopy(dst, deldst, netmask);
			dst = deldst;
		}
		/*
		 * Look up an rtentry that is in the routing tree and
		 * contains the correct info.
		 */
		if ((rnh = rt_tables[dst->sa_family]) == NULL ||
		    (rn = rnh->rnh_lookup((char *)dst,
					  (char *)netmask, rnh)) == NULL ||
		    ((struct rtentry *)rn)->rt_ifa != ifa ||
		    !sa_equal((struct sockaddr *)rn->rn_key, dst)) {
			if (m != NULL)
				m_free(m);
			return (flags & RTF_HOST ? EHOSTUNREACH : ENETUNREACH);
		}
		/* XXX */
#if 0
		else {
			/*
			 * One would think that as we are deleting, and we know
			 * it doesn't exist, we could just return at this point
			 * with an "ELSE" clause, but apparently not..
			 */
			return (flags & RTF_HOST ? EHOSTUNREACH : ENETUNREACH);
		}
#endif
	}
	/*
	 * Do the actual request
	 */
	bzero(&info, sizeof(info));
	info.rti_ifa = ifa;
	info.rti_flags = flags | ifa->ifa_flags;
	info.rti_info[RTAX_DST] = dst;
	info.rti_info[RTAX_GATEWAY] = ifa->ifa_addr;
	info.rti_info[RTAX_NETMASK] = netmask;
	error = rtrequest1(cmd, &info, &nrt);
	if (error == 0 && (rt = nrt) != NULL) {
		/*
		 * notify any listening routing agents of the change
		 */
		rt_newaddrmsg(cmd, ifa, error, rt);
		if (cmd == RTM_DELETE) {
			/*
			 * If we are deleting, and we found an entry, then
			 * it's been removed from the tree.. now throw it away.
			 */
			if (rt->rt_refcnt <= 0) {
				rt->rt_refcnt++; /* make a 1->0 transition */
				rtfree(rt);
			}
		} else if (cmd == RTM_ADD) {
			/*
			 * We just wanted to add it.. we don't actually
			 * need a reference.
			 */
			rt->rt_refcnt--;
		}
	}
	if (m != NULL)
		m_free(m);
	return (error);
}

/* This must be before ip6_init2(), which is now SI_ORDER_MIDDLE */
SYSINIT(route, SI_SUB_PROTO_DOMAIN, SI_ORDER_THIRD, route_init, 0);
