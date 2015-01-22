/*	$OpenBSD: pf_if.c,v 1.54 2008/06/14 16:55:28 mk Exp $ */

/*
 * Copyright 2005 Henning Brauer <henning@openbsd.org>
 * Copyright 2005 Ryan McBride <mcbride@openbsd.org>
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2003 Cedric Berger
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/eventhandler.h>
#include <sys/filio.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/kernel.h>
#include <sys/thread2.h>
#include <sys/time.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>

#include <net/pf/pfvar.h>

#ifdef INET6
#include <netinet/ip6.h>
#endif /* INET6 */

struct pfi_kif		 *pfi_all = NULL;
struct pfi_ifhead	  pfi_ifs;
long			  pfi_update = 1;
struct pfr_addr		 *pfi_buffer;
int			  pfi_buffer_cnt;
int			  pfi_buffer_max;

eventhandler_tag	 pfi_clone_cookie = NULL;
eventhandler_tag	 pfi_attach_cookie = NULL;
eventhandler_tag	 pfi_detach_cookie = NULL;

void		 pfi_kif_update(struct pfi_kif *);
void		 pfi_dynaddr_update(struct pfi_dynaddr *dyn);
void		 pfi_table_update(struct pfr_ktable *, struct pfi_kif *,
		    int, int);
void		 pfi_kifaddr_update(void *);
void		 pfi_instance_add(struct ifnet *, int, int);
void		 pfi_address_add(struct sockaddr *, int, int);
int		 pfi_if_compare(struct pfi_kif *, struct pfi_kif *);
int		 pfi_skip_if(const char *, struct pfi_kif *);
int		 pfi_unmask(void *);
void		 pfi_kifaddr_update_event(void *, struct ifnet *,
		     enum ifaddr_event, struct ifaddr *);
/*XXX jl void		 pfi_attach_clone_event(void *, struct if_clone *);*/
void		 pfi_attach_ifnet_event(void *, struct ifnet *);
void		 pfi_detach_ifnet_event(void *, struct ifnet *);

RB_PROTOTYPE(pfi_ifhead, pfi_kif, pfik_tree, pfi_if_compare);
RB_GENERATE(pfi_ifhead, pfi_kif, pfik_tree, pfi_if_compare);

#define PFI_BUFFER_MAX		0x10000
MALLOC_DEFINE(PFI_MTYPE, "pf_if", "pf interface table");
static MALLOC_DEFINE(M_PFIADDRPL, "pfiaddrpl", "pf interface address pool list");


void
pfi_initialize(void)
{
	struct ifnet	*ifp;

	if (pfi_all != NULL)	/* already initialized */
		return;

	pfi_buffer_max = 64;
	pfi_buffer = kmalloc(pfi_buffer_max * sizeof(*pfi_buffer),
	    PFI_MTYPE, M_WAITOK);

	if ((pfi_all = pfi_kif_get(IFG_ALL)) == NULL)
		panic("pfi_kif_get for pfi_all failed");

	/* XXX ALMOST MPSAFE */
	ifnet_lock();
	TAILQ_FOREACH(ifp, &ifnetlist, if_link) {
		if (ifp->if_dunit != IF_DUNIT_NONE)
			pfi_attach_ifnet(ifp);
	}
	ifnet_unlock();
	pfi_attach_cookie = EVENTHANDLER_REGISTER(ifnet_attach_event,
	    pfi_attach_ifnet_event, NULL, EVENTHANDLER_PRI_ANY);
	pfi_detach_cookie = EVENTHANDLER_REGISTER(ifnet_detach_event,
	    pfi_detach_ifnet_event, NULL, EVENTHANDLER_PRI_ANY);
/* XXX jl	pfi_clone_cookie = EVENTHANDLER_REGISTER(if_clone_event,
	    pfi_attach_clone_event, NULL, EVENTHANDLER_PRI_ANY); */

	if ((pfi_all = pfi_kif_get(IFG_ALL)) == NULL)
		panic("pfi_kif_get for pfi_all failed");
}

void
pfi_cleanup(void)
{
	struct pfi_kif *p, key;
	struct ifnet *ifp;

	EVENTHANDLER_DEREGISTER(ifnet_attach_event, pfi_attach_cookie);
	EVENTHANDLER_DEREGISTER(ifnet_detach_event, pfi_detach_cookie);
	EVENTHANDLER_DEREGISTER(if_clone_event, pfi_clone_cookie);

	/* release PFI_IFLAG_INSTANCE */
	/* XXX ALMOST MPSAFE */
	ifnet_lock();
	TAILQ_FOREACH(ifp, &ifnetlist, if_link) {
		strlcpy(key.pfik_name, ifp->if_xname, sizeof(key.pfik_name));
		p = RB_FIND(pfi_ifhead, &pfi_ifs, &key);
		if (p != NULL)
			pfi_detach_ifnet(ifp);
	}
	ifnet_unlock();

	/* XXX clear all other interface group */
	while ((p = RB_MIN(pfi_ifhead, &pfi_ifs))) {
		RB_REMOVE(pfi_ifhead, &pfi_ifs, p);

		kfree(p, PFI_MTYPE);
	}
	kfree(pfi_buffer, PFI_MTYPE);
	pfi_buffer = NULL;
	pfi_all = NULL;
}

/*
 * Wrapper functions for FreeBSD eventhandler
 */
void
pfi_kifaddr_update_event(void *arg, struct ifnet *ifp,
    enum ifaddr_event event __unused, struct ifaddr *ifa __unused)
{
	struct pfi_kif *p = arg;

	/* 
	 * Check to see if it is 'our' interface as we do not have per
	 * interface hooks and thus get an update for every interface.
	 */
	if (p && p->pfik_ifp == ifp)
		pfi_kifaddr_update(p);
}

/* XXX jl
void
pfi_attach_clone_event(void *arg __unused, struct if_clone *ifc)
{
	pfi_attach_clone(ifc);
}
*/

void
pfi_attach_ifnet_event(void *arg __unused, struct ifnet *ifp)
{
	if (ifp->if_dunit != IF_DUNIT_NONE)
		pfi_attach_ifnet(ifp);
}

void
pfi_detach_ifnet_event(void *arg __unused, struct ifnet *ifp)
{
	pfi_detach_ifnet(ifp);
}

struct pfi_kif *
pfi_kif_get(const char *kif_name)
{
	struct pfi_kif		*kif;
	struct pfi_kif_cmp	 s;

	bzero(&s, sizeof(s));
	strlcpy(s.pfik_ifname, kif_name, sizeof(s.pfik_ifname));
	if ((kif = RB_FIND(pfi_ifhead, &pfi_ifs, (struct pfi_kif *)&s)) != NULL)
		return (kif);

	/* create new one */
	if ((kif = kmalloc(sizeof(*kif), PFI_MTYPE, M_WAITOK | M_ZERO)) == NULL)
		return (NULL);

	strlcpy(kif->pfik_name, kif_name, sizeof(kif->pfik_name));
	kif->pfik_tzero = time_second;
	TAILQ_INIT(&kif->pfik_dynaddrs);

	RB_INSERT(pfi_ifhead, &pfi_ifs, kif);
	return (kif);
}

void
pfi_kif_ref(struct pfi_kif *kif, enum pfi_kif_refs what)
{
	switch (what) {
	case PFI_KIF_REF_RULE:
		kif->pfik_rules++;
		break;
	case PFI_KIF_REF_STATE:
		kif->pfik_states++;
		break;
	default:
		panic("pfi_kif_ref with unknown type");
	}
}

void
pfi_kif_unref(struct pfi_kif *kif, enum pfi_kif_refs what)
{
	if (kif == NULL)
		return;

	switch (what) {
	case PFI_KIF_REF_NONE:
		break;
	case PFI_KIF_REF_RULE:
		if (kif->pfik_rules <= 0) {
			kprintf("pfi_kif_unref: rules refcount <= 0\n");
			return;
		}
		kif->pfik_rules--;
		break;
	case PFI_KIF_REF_STATE:
		if (kif->pfik_states <= 0) {
			kprintf("pfi_kif_unref: state refcount <= 0\n");
			return;
		}
		kif->pfik_states--;
		break;
	default:
		panic("pfi_kif_unref with unknown type");
	}

	if (kif->pfik_ifp != NULL || kif->pfik_group != NULL || kif == pfi_all)
		return;

	if (kif->pfik_rules || kif->pfik_states)
		return;

	RB_REMOVE(pfi_ifhead, &pfi_ifs, kif);
	kfree(kif, PFI_MTYPE);
}

int
pfi_kif_match(struct pfi_kif *rule_kif, struct pfi_kif *packet_kif)
{
	struct ifg_list	*p;

	if (rule_kif == NULL || rule_kif == packet_kif)
		return (1);

	if (rule_kif->pfik_group != NULL)
		TAILQ_FOREACH(p, &packet_kif->pfik_ifp->if_groups, ifgl_next)
			if (p->ifgl_group == rule_kif->pfik_group)
				return (1);

	return (0);
}

void
pfi_attach_ifnet(struct ifnet *ifp)
{
	struct pfi_kif		*kif;

	pfi_initialize();
	crit_enter();
	pfi_update++;
	if ((kif = pfi_kif_get(ifp->if_xname)) == NULL)
		panic("pfi_kif_get failed");

	kif->pfik_ifp = ifp;
	ifp->if_pf_kif = (caddr_t)kif;

	pfi_kif_update(kif);

	crit_exit();
}

void
pfi_detach_ifnet(struct ifnet *ifp)
{
	struct pfi_kif		*kif;

	if ((kif = (struct pfi_kif *)ifp->if_pf_kif) == NULL)
		return;

	crit_enter();
	pfi_update++;
	pfi_kif_update(kif);

	kif->pfik_ifp = NULL;
	ifp->if_pf_kif = NULL;
	pfi_kif_unref(kif, PFI_KIF_REF_NONE);
	crit_exit();
}

void
pfi_attach_ifgroup(struct ifg_group *ifg)
{
	struct pfi_kif	*kif;

	pfi_initialize();
	crit_enter();
	pfi_update++;
	if ((kif = pfi_kif_get(ifg->ifg_group)) == NULL)
		panic("pfi_kif_get failed");

	kif->pfik_group = ifg;
	ifg->ifg_pf_kif = (caddr_t)kif;

	crit_exit();
}

void
pfi_detach_ifgroup(struct ifg_group *ifg)
{
	struct pfi_kif	*kif;

	if ((kif = (struct pfi_kif *)ifg->ifg_pf_kif) == NULL)
		return;

	crit_enter();
	pfi_update++;

	kif->pfik_group = NULL;
	ifg->ifg_pf_kif = NULL;
	pfi_kif_unref(kif, PFI_KIF_REF_NONE);
	crit_exit();
}

void
pfi_group_change(const char *group)
{
	struct pfi_kif		*kif;

	crit_enter();
	pfi_update++;
	if ((kif = pfi_kif_get(group)) == NULL)
		panic("pfi_kif_get failed");

	pfi_kif_update(kif);

	crit_exit();
}

int
pfi_match_addr(struct pfi_dynaddr *dyn, struct pf_addr *a, sa_family_t af)
{
	switch (af) {
#ifdef INET
	case AF_INET:
		switch (dyn->pfid_acnt4) {
		case 0:
			return (0);
		case 1:
			return (PF_MATCHA(0, &dyn->pfid_addr4,
			    &dyn->pfid_mask4, a, AF_INET));
		default:
			return (pfr_match_addr(dyn->pfid_kt, a, AF_INET));
		}
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		switch (dyn->pfid_acnt6) {
		case 0:
			return (0);
		case 1:
			return (PF_MATCHA(0, &dyn->pfid_addr6,
			    &dyn->pfid_mask6, a, AF_INET6));
		default:
			return (pfr_match_addr(dyn->pfid_kt, a, AF_INET6));
		}
		break;
#endif /* INET6 */
	default:
		return (0);
	}
}

int
pfi_dynaddr_setup(struct pf_addr_wrap *aw, sa_family_t af)
{
	struct pfi_dynaddr	*dyn;
	char			 tblname[PF_TABLE_NAME_SIZE];
	struct pf_ruleset	*ruleset = NULL;
	int			 rv = 0;

	if (aw->type != PF_ADDR_DYNIFTL)
		return (0);
	if ((dyn = kmalloc(sizeof(struct pfi_dynaddr), M_PFIADDRPL, M_WAITOK|M_NULLOK|M_ZERO))
	    == NULL)
		return (1);

	crit_enter();
	if (!strcmp(aw->v.ifname, "self"))
		dyn->pfid_kif = pfi_kif_get(IFG_ALL);
	else
		dyn->pfid_kif = pfi_kif_get(aw->v.ifname);
	if (dyn->pfid_kif == NULL) {
		rv = 1;
		goto _bad;
	}
	pfi_kif_ref(dyn->pfid_kif, PFI_KIF_REF_RULE);

	dyn->pfid_net = pfi_unmask(&aw->v.a.mask);
	if (af == AF_INET && dyn->pfid_net == 32)
		dyn->pfid_net = 128;
	strlcpy(tblname, aw->v.ifname, sizeof(tblname));
	if (aw->iflags & PFI_AFLAG_NETWORK)
		strlcat(tblname, ":network", sizeof(tblname));
	if (aw->iflags & PFI_AFLAG_BROADCAST)
		strlcat(tblname, ":broadcast", sizeof(tblname));
	if (aw->iflags & PFI_AFLAG_PEER)
		strlcat(tblname, ":peer", sizeof(tblname));
	if (aw->iflags & PFI_AFLAG_NOALIAS)
		strlcat(tblname, ":0", sizeof(tblname));
	if (dyn->pfid_net != 128)
		ksnprintf(tblname + strlen(tblname),
		    sizeof(tblname) - strlen(tblname), "/%d", dyn->pfid_net);
	if ((ruleset = pf_find_or_create_ruleset(PF_RESERVED_ANCHOR)) == NULL) {
		rv = 1;
		goto _bad;
	}

	if ((dyn->pfid_kt = pfr_attach_table(ruleset, tblname)) == NULL) {
		rv = 1;
		goto _bad;
	}

	dyn->pfid_kt->pfrkt_flags |= PFR_TFLAG_ACTIVE;
	dyn->pfid_iflags = aw->iflags;
	dyn->pfid_af = af;

	TAILQ_INSERT_TAIL(&dyn->pfid_kif->pfik_dynaddrs, dyn, entry);
	aw->p.dyn = dyn;
	pfi_kif_update(dyn->pfid_kif);
	crit_exit();
	return (0);

_bad:
	if (dyn->pfid_kt != NULL)
		pfr_detach_table(dyn->pfid_kt);
	if (ruleset != NULL)
		pf_remove_if_empty_ruleset(ruleset);
	if (dyn->pfid_kif != NULL)
		pfi_kif_unref(dyn->pfid_kif, PFI_KIF_REF_RULE);
	kfree(dyn, M_PFIADDRPL);
	crit_exit();
	return (rv);
}

void
pfi_kif_update(struct pfi_kif *kif)
{
	struct ifg_list		*ifgl;
	struct pfi_dynaddr	*p;

	/* update all dynaddr */
	TAILQ_FOREACH(p, &kif->pfik_dynaddrs, entry)
		pfi_dynaddr_update(p);

	/* again for all groups kif is member of */
	if (kif->pfik_ifp != NULL)
		TAILQ_FOREACH(ifgl, &kif->pfik_ifp->if_groups, ifgl_next)
			pfi_kif_update((struct pfi_kif *)
			    ifgl->ifgl_group->ifg_pf_kif);
}

void
pfi_dynaddr_update(struct pfi_dynaddr *dyn)
{
	struct pfi_kif		*kif;
	struct pfr_ktable	*kt;

	if (dyn == NULL || dyn->pfid_kif == NULL || dyn->pfid_kt == NULL)
		panic("pfi_dynaddr_update");

	kif = dyn->pfid_kif;
	kt = dyn->pfid_kt;

	if (kt->pfrkt_larg != pfi_update) {
		/* this table needs to be brought up-to-date */
		pfi_table_update(kt, kif, dyn->pfid_net, dyn->pfid_iflags);
		kt->pfrkt_larg = pfi_update;
	}
	pfr_dynaddr_update(kt, dyn);
}

void
pfi_table_update(struct pfr_ktable *kt, struct pfi_kif *kif, int net, int flags)
{
	int			 e, size2 = 0;
	struct ifg_member	*ifgm;

	pfi_buffer_cnt = 0;

	if (kif->pfik_ifp != NULL)
		pfi_instance_add(kif->pfik_ifp, net, flags);
	else if (kif->pfik_group != NULL)
		TAILQ_FOREACH(ifgm, &kif->pfik_group->ifg_members, ifgm_next)
			pfi_instance_add(ifgm->ifgm_ifp, net, flags);

	if ((e = pfr_set_addrs(&kt->pfrkt_t, pfi_buffer, pfi_buffer_cnt, &size2,
	    NULL, NULL, NULL, 0, PFR_TFLAG_ALLMASK)))
		kprintf("pfi_table_update: cannot set %d new addresses "
		    "into table %s: %d\n", pfi_buffer_cnt, kt->pfrkt_name, e);
}

void
pfi_instance_add(struct ifnet *ifp, int net, int flags)
{
	struct ifaddr_container *ifac;
	int		 got4 = 0, got6 = 0;
	int		 net2, af;

	if (ifp == NULL)
		return;
	TAILQ_FOREACH(ifac, &ifp->if_addrheads[mycpuid], ifa_link) {
		struct ifaddr *ia = ifac->ifa;

		if (ia->ifa_addr == NULL)
			continue;
		af = ia->ifa_addr->sa_family;
		if (af != AF_INET && af != AF_INET6)
			continue;
		/*
		 * XXX: For point-to-point interfaces, (ifname:0) and IPv4,
		 *	jump over address without a proper route to work
		 *	around a problem with ppp not fully removing the
		 *	address used during IPCP.
		 */
		if ((ifp->if_flags & IFF_POINTOPOINT) &&
		    !(ia->ifa_flags & IFA_ROUTE) &&
		    (flags & PFI_AFLAG_NOALIAS) && (af == AF_INET))
			continue;
		if ((flags & PFI_AFLAG_BROADCAST) && af == AF_INET6)
			continue;
		if ((flags & PFI_AFLAG_BROADCAST) &&
		    !(ifp->if_flags & IFF_BROADCAST))
			continue;
		if ((flags & PFI_AFLAG_PEER) &&
		    !(ifp->if_flags & IFF_POINTOPOINT))
			continue;
		if ((flags & PFI_AFLAG_NETWORK) && af == AF_INET6 &&
		    IN6_IS_ADDR_LINKLOCAL(
		    &((struct sockaddr_in6 *)ia->ifa_addr)->sin6_addr))
			continue;
		if (flags & PFI_AFLAG_NOALIAS) {
			if (af == AF_INET && got4)
				continue;
			if (af == AF_INET6 && got6)
				continue;
		}
		if (af == AF_INET)
			got4 = 1;
		else if (af == AF_INET6)
			got6 = 1;
		net2 = net;
		if (net2 == 128 && (flags & PFI_AFLAG_NETWORK)) {
			if (af == AF_INET)
				net2 = pfi_unmask(&((struct sockaddr_in *)
				    ia->ifa_netmask)->sin_addr);
			else if (af == AF_INET6)
				net2 = pfi_unmask(&((struct sockaddr_in6 *)
				    ia->ifa_netmask)->sin6_addr);
		}
		if (af == AF_INET && net2 > 32)
			net2 = 32;
		if (flags & PFI_AFLAG_BROADCAST)
			pfi_address_add(ia->ifa_broadaddr, af, net2);
		else if (flags & PFI_AFLAG_PEER)
			pfi_address_add(ia->ifa_dstaddr, af, net2);
		else
			pfi_address_add(ia->ifa_addr, af, net2);
	}
}

void
pfi_address_add(struct sockaddr *sa, int af, int net)
{
	struct pfr_addr	*p;
	int		 i;

	if (pfi_buffer_cnt >= pfi_buffer_max) {
		int		 new_max = pfi_buffer_max * 2;

		if (new_max > PFI_BUFFER_MAX) {
			kprintf("pfi_address_add: address buffer full (%d/%d)\n",
			    pfi_buffer_cnt, PFI_BUFFER_MAX);
			return;
		}
		p = kmalloc(new_max * sizeof(*pfi_buffer), PFI_MTYPE, M_WAITOK);
		memcpy(pfi_buffer, p, pfi_buffer_cnt * sizeof(*pfi_buffer));
		/* no need to zero buffer */
		kfree(pfi_buffer, PFI_MTYPE);
		pfi_buffer = p;
		pfi_buffer_max = new_max;
	}
	if (af == AF_INET && net > 32)
		net = 128;
	p = pfi_buffer + pfi_buffer_cnt++;
	bzero(p, sizeof(*p));
	p->pfra_af = af;
	p->pfra_net = net;
	if (af == AF_INET)
		p->pfra_ip4addr = ((struct sockaddr_in *)sa)->sin_addr;
	else if (af == AF_INET6) {
		p->pfra_ip6addr = ((struct sockaddr_in6 *)sa)->sin6_addr;
		if (IN6_IS_SCOPE_EMBED(&p->pfra_ip6addr))
			p->pfra_ip6addr.s6_addr16[1] = 0;
	}
	/* mask network address bits */
	if (net < 128)
		((caddr_t)p)[p->pfra_net/8] &= ~(0xFF >> (p->pfra_net%8));
	for (i = (p->pfra_net+7)/8; i < sizeof(p->pfra_u); i++)
		((caddr_t)p)[i] = 0;
}

void
pfi_dynaddr_remove(struct pf_addr_wrap *aw)
{
	if (aw->type != PF_ADDR_DYNIFTL || aw->p.dyn == NULL ||
	    aw->p.dyn->pfid_kif == NULL || aw->p.dyn->pfid_kt == NULL)
		return;

	crit_enter();
	TAILQ_REMOVE(&aw->p.dyn->pfid_kif->pfik_dynaddrs, aw->p.dyn, entry);
	pfi_kif_unref(aw->p.dyn->pfid_kif, PFI_KIF_REF_RULE);
	aw->p.dyn->pfid_kif = NULL;
	pfr_detach_table(aw->p.dyn->pfid_kt);
	aw->p.dyn->pfid_kt = NULL;
	kfree(aw->p.dyn, M_PFIADDRPL);
	aw->p.dyn = NULL;
	crit_exit();
}

void
pfi_dynaddr_copyout(struct pf_addr_wrap *aw)
{
	if (aw->type != PF_ADDR_DYNIFTL || aw->p.dyn == NULL ||
	    aw->p.dyn->pfid_kif == NULL)
		return;
	aw->p.dyncnt = aw->p.dyn->pfid_acnt4 + aw->p.dyn->pfid_acnt6;
}

void
pfi_kifaddr_update(void *v)
{
	struct pfi_kif		*kif = (struct pfi_kif *)v;

	crit_enter();
	pfi_update++;
	pfi_kif_update(kif);
	crit_exit();
}

int
pfi_if_compare(struct pfi_kif *p, struct pfi_kif *q)
{
	return (strncmp(p->pfik_name, q->pfik_name, IFNAMSIZ));
}

void
pfi_update_status(const char *name, struct pf_status *pfs)
{
	struct pfi_kif		*p;
	struct pfi_kif_cmp 	 key;
	struct ifg_member	 p_member, *ifgm;
	TAILQ_HEAD(, ifg_member) ifg_members;
	int			 i, j, k;

	strlcpy(key.pfik_ifname, name, sizeof(key.pfik_ifname));
	crit_enter();
	p = RB_FIND(pfi_ifhead, &pfi_ifs, (struct pfi_kif *)&key);
	if (p == NULL) {
		crit_exit();
		return;
	}
	if (p->pfik_group != NULL) {
		bcopy(&p->pfik_group->ifg_members, &ifg_members,
		    sizeof(ifg_members));
	} else {
		/* build a temporary list for p only */
		bzero(&p_member, sizeof(p_member));
		p_member.ifgm_ifp = p->pfik_ifp;
		TAILQ_INIT(&ifg_members);
		TAILQ_INSERT_TAIL(&ifg_members, &p_member, ifgm_next);
	}
	if (pfs) {
		bzero(pfs->pcounters, sizeof(pfs->pcounters));
		bzero(pfs->bcounters, sizeof(pfs->bcounters));
	}
	TAILQ_FOREACH(ifgm, &ifg_members, ifgm_next) {
		if (ifgm->ifgm_ifp == NULL)
			continue;
		p = (struct pfi_kif *)ifgm->ifgm_ifp->if_pf_kif;

		/* just clear statistics */
		if (pfs == NULL) {
			bzero(p->pfik_packets, sizeof(p->pfik_packets));
			bzero(p->pfik_bytes, sizeof(p->pfik_bytes));
			p->pfik_tzero = time_second;
			continue;
		}
		for (i = 0; i < 2; i++)
			for (j = 0; j < 2; j++)
				for (k = 0; k < 2; k++) {
					pfs->pcounters[i][j][k] +=
						p->pfik_packets[i][j][k];
					pfs->bcounters[i][j] +=
						p->pfik_bytes[i][j][k];
				}
	}
	crit_exit();
}

int
pfi_get_ifaces(const char *name, struct pfi_kif *buf, int *size)
{
	struct pfi_kif	*p, *nextp;
	int		 n = 0;

	crit_enter();
	for (p = RB_MIN(pfi_ifhead, &pfi_ifs); p; p = nextp) {
		nextp = RB_NEXT(pfi_ifhead, &pfi_ifs, p);
		if (pfi_skip_if(name, p))
			continue;
		if (*size > n++) {
			if (!p->pfik_tzero)
				p->pfik_tzero = time_second;
			pfi_kif_ref(p, PFI_KIF_REF_RULE);
			if (copyout(p, buf++, sizeof(*buf))) {
				pfi_kif_unref(p, PFI_KIF_REF_RULE);
				crit_exit();
				return (EFAULT);
			}
			nextp = RB_NEXT(pfi_ifhead, &pfi_ifs, p);
			pfi_kif_unref(p, PFI_KIF_REF_RULE);
		}
	}
	crit_exit();
	*size = n;
	return (0);
}

int
pfi_skip_if(const char *filter, struct pfi_kif *p)
{
	int	n;

	if (filter == NULL || !*filter)
		return (0);
	if (!strcmp(p->pfik_name, filter))
		return (0);	/* exact match */
	n = strlen(filter);
	if (n < 1 || n >= IFNAMSIZ)
		return (1);	/* sanity check */
	if (filter[n-1] >= '0' && filter[n-1] <= '9')
		return (1);	/* only do exact match in that case */
	if (strncmp(p->pfik_name, filter, n))
		return (1);	/* prefix doesn't match */
	return (p->pfik_name[n] < '0' || p->pfik_name[n] > '9');
}

int
pfi_set_flags(const char *name, int flags)
{
	struct pfi_kif	*p;

	crit_enter();
	RB_FOREACH(p, pfi_ifhead, &pfi_ifs) {
		if (pfi_skip_if(name, p))
			continue;
		p->pfik_flags |= flags;
	}
	crit_exit();
	return (0);
}

int
pfi_clear_flags(const char *name, int flags)
{
	struct pfi_kif	*p;

	crit_enter();
	RB_FOREACH(p, pfi_ifhead, &pfi_ifs) {
		if (pfi_skip_if(name, p))
			continue;
		p->pfik_flags &= ~flags;
	}
	crit_exit();
	return (0);
}

/* from pf_print_state.c */
int
pfi_unmask(void *addr)
{
	struct pf_addr *m = addr;
	int i = 31, j = 0, b = 0;
	u_int32_t tmp;

	while (j < 4 && m->addr32[j] == 0xffffffff) {
		b += 32;
		j++;
	}
	if (j < 4) {
		tmp = ntohl(m->addr32[j]);
		for (i = 31; tmp & (1 << i); --i)
			b++;
	}
	return (b);
}

