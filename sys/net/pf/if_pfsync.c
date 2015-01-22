/*
 * Copyright (c) 2002 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $OpenBSD: if_pfsync.c,v 1.98 2008/06/29 08:42:15 mcbride Exp $
 */

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_carp.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/proc.h>
#include <sys/priv.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/msgport2.h>
#include <sys/sockio.h>
#include <sys/thread2.h>

#include <machine/inttypes.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/ifq_var.h>
#include <net/route.h>
#include <net/bpf.h>
#include <net/netisr2.h>
#include <net/netmsg2.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip_carp.h>
#include <netinet/tcp.h>
#include <netinet/tcp_seq.h>

#ifdef	INET
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#endif

#ifdef INET6
#include <netinet6/nd6.h>
#endif /* INET6 */

#include <net/pf/pfvar.h>
#include <net/pf/if_pfsync.h>

#define	PFSYNCNAME	"pfsync"

#define PFSYNC_MINMTU	\
    (sizeof(struct pfsync_header) + sizeof(struct pf_state))

#ifdef PFSYNCDEBUG
#define DPRINTF(x)    do { if (pfsyncdebug) kprintf x ; } while (0)
int pfsyncdebug;
#else
#define DPRINTF(x)
#endif

struct pfsync_softc	*pfsyncif = NULL;
struct pfsyncstats	 pfsyncstats;

void	pfsyncattach(int);
static int	pfsync_clone_destroy(struct ifnet *);
static int	pfsync_clone_create(struct if_clone *, int, caddr_t);
void	pfsync_setmtu(struct pfsync_softc *, int);
int	pfsync_alloc_scrub_memory(struct pfsync_state_peer *,
	    struct pf_state_peer *);
int	pfsyncoutput(struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *);
int	pfsyncioctl(struct ifnet *, u_long, caddr_t, struct ucred *);
void	pfsyncstart(struct ifnet *, struct ifaltq_subque *);

struct mbuf *pfsync_get_mbuf(struct pfsync_softc *, u_int8_t, void **);
int	pfsync_request_update(struct pfsync_state_upd *, struct in_addr *);
int	pfsync_sendout(struct pfsync_softc *);
int	pfsync_sendout_mbuf(struct pfsync_softc *, struct mbuf *);
void	pfsync_timeout(void *);
void	pfsync_send_bus(struct pfsync_softc *, u_int8_t);
void	pfsync_bulk_update(void *);
void	pfsync_bulkfail(void *);

static struct in_multi *pfsync_in_addmulti(struct ifnet *);
static void pfsync_in_delmulti(struct in_multi *);

static MALLOC_DEFINE(M_PFSYNC, PFSYNCNAME, "Packet Filter State Sync. Interface");
static LIST_HEAD(pfsync_list, pfsync_softc) pfsync_list;

int	pfsync_sync_ok;

struct if_clone	pfsync_cloner =
    IF_CLONE_INITIALIZER("pfsync", pfsync_clone_create, pfsync_clone_destroy, 1 ,1);

void
pfsyncattach(int npfsync)
{
	if_clone_attach(&pfsync_cloner);
}
static int
pfsync_clone_create(struct if_clone *ifc, int unit, caddr_t param __unused)
{
	struct pfsync_softc *sc;
	struct ifnet *ifp;

	lwkt_gettoken(&pf_token);

	sc = kmalloc(sizeof(*sc), M_PFSYNC, M_WAITOK | M_ZERO);
	pfsync_sync_ok = 1;
	sc->sc_mbuf = NULL;
	sc->sc_mbuf_net = NULL;
	sc->sc_mbuf_tdb = NULL;
	sc->sc_statep.s = NULL;
	sc->sc_statep_net.s = NULL;
	sc->sc_statep_tdb.t = NULL;
	sc->sc_maxupdates = 128;
	sc->sc_sync_peer.s_addr =htonl(INADDR_PFSYNC_GROUP);
	sc->sc_sendaddr.s_addr = htonl(INADDR_PFSYNC_GROUP);
	sc->sc_ureq_received = 0;
	sc->sc_ureq_sent = 0;
	sc->sc_bulk_send_next = NULL;
	sc->sc_bulk_terminator = NULL;
	sc->sc_bulk_send_cpu = 0;
	sc->sc_bulk_terminator_cpu = 0;
	sc->sc_imo.imo_max_memberships = IP_MAX_MEMBERSHIPS;
	lwkt_reltoken(&pf_token);
	ifp = &sc->sc_if;
	ksnprintf(ifp->if_xname, sizeof ifp->if_xname, "pfsync%d", unit);
	if_initname(ifp, ifc->ifc_name, unit);
	ifp->if_ioctl = pfsyncioctl;
	ifp->if_output = pfsyncoutput;
	ifp->if_start = pfsyncstart;
	ifp->if_type = IFT_PFSYNC;
	ifq_set_maxlen(&ifp->if_snd, ifqmaxlen);
	ifp->if_hdrlen = PFSYNC_HDRLEN;
	ifp->if_baudrate = IF_Mbps(100);
	ifp->if_softc = sc;
	pfsync_setmtu(sc, MCLBYTES);
	callout_init(&sc->sc_tmo);
	/* callout_init(&sc->sc_tdb_tmo); XXX we don't support tdb (yet) */
	callout_init(&sc->sc_bulk_tmo);
	callout_init(&sc->sc_bulkfail_tmo);
	if_attach(ifp, NULL);

	LIST_INSERT_HEAD(&pfsync_list, sc, sc_next);


#if NCARP > 0
	if_addgroup(ifp, "carp");
#endif

#if NBPFILTER > 0
	bpfattach(&sc->sc_if, DLT_PFSYNC, PFSYNC_HDRLEN);
#endif
	lwkt_gettoken(&pf_token);

	lwkt_reltoken(&pf_token);
	return (0);
}

static int
pfsync_clone_destroy(struct ifnet *ifp)
{
	lwkt_gettoken(&pf_token);
	lwkt_reltoken(&pf_token);

	struct pfsync_softc *sc = ifp->if_softc;
	callout_stop(&sc->sc_tmo);
	/* callout_stop(&sc->sc_tdb_tmo); XXX we don't support tdb (yet) */
	callout_stop(&sc->sc_bulk_tmo);
	callout_stop(&sc->sc_bulkfail_tmo);
#if NCARP > 0
	if (!pfsync_sync_ok)
		carp_group_demote_adj(&sc->sc_if, -1);
#endif
#if NBPFILTER > 0
	bpfdetach(ifp);
#endif
	if_detach(ifp);
	lwkt_gettoken(&pf_token);
	LIST_REMOVE(sc, sc_next);
	kfree(sc, M_PFSYNC);
	lwkt_reltoken(&pf_token);


	return 0;
}

/*
 * Start output on the pfsync interface.
 */
void
pfsyncstart(struct ifnet *ifp, struct ifaltq_subque *ifsq)
{
	ASSERT_ALTQ_SQ_DEFAULT(ifp, ifsq);
	ifsq_purge(ifsq);
}

int
pfsync_alloc_scrub_memory(struct pfsync_state_peer *s,
    struct pf_state_peer *d)
{
	if (s->scrub.scrub_flag && d->scrub == NULL) {
		d->scrub = kmalloc(sizeof(struct pf_state_scrub), M_PFSYNC, M_NOWAIT|M_ZERO);

		if (d->scrub == NULL)
			return (ENOMEM);
	}

	return (0);
}

void
pfsync_state_export(struct pfsync_state *sp, struct pf_state *st)
{
	bzero(sp, sizeof(struct pfsync_state));

	/* copy from state key */
	sp->key[PF_SK_WIRE].addr[0] = st->key[PF_SK_WIRE]->addr[0];
	sp->key[PF_SK_WIRE].addr[1] = st->key[PF_SK_WIRE]->addr[1];
	sp->key[PF_SK_WIRE].port[0] = st->key[PF_SK_WIRE]->port[0];
	sp->key[PF_SK_WIRE].port[1] = st->key[PF_SK_WIRE]->port[1];
	sp->key[PF_SK_STACK].addr[0] = st->key[PF_SK_STACK]->addr[0];
	sp->key[PF_SK_STACK].addr[1] = st->key[PF_SK_STACK]->addr[1];
	sp->key[PF_SK_STACK].port[0] = st->key[PF_SK_STACK]->port[0];
	sp->key[PF_SK_STACK].port[1] = st->key[PF_SK_STACK]->port[1];
	sp->proto = st->key[PF_SK_WIRE]->proto;
	sp->af = st->key[PF_SK_WIRE]->af;

	/* copy from state */
	strlcpy(sp->ifname, st->kif->pfik_name, sizeof(sp->ifname));
	bcopy(&st->rt_addr, &sp->rt_addr, sizeof(sp->rt_addr));
	sp->creation = htonl(time_second - st->creation);
	sp->expire = pf_state_expires(st);
	if (sp->expire <= time_second)
		sp->expire = htonl(0);
	else
		sp->expire = htonl(sp->expire - time_second);

	sp->direction = st->direction;
	sp->log = st->log;
	sp->cpuid = st->cpuid;
	sp->pickup_mode = st->pickup_mode;
	sp->timeout = st->timeout;
	sp->state_flags = st->state_flags;
	if (st->src_node)
		sp->sync_flags |= PFSYNC_FLAG_SRCNODE;
	if (st->nat_src_node)
		sp->sync_flags |= PFSYNC_FLAG_NATSRCNODE;

	bcopy(&st->id, &sp->id, sizeof(sp->id));
	sp->creatorid = st->creatorid;
	pf_state_peer_hton(&st->src, &sp->src);
	pf_state_peer_hton(&st->dst, &sp->dst);

	if (st->rule.ptr == NULL)
		sp->rule = htonl(-1);
	else
		sp->rule = htonl(st->rule.ptr->nr);
	if (st->anchor.ptr == NULL)
		sp->anchor = htonl(-1);
	else
		sp->anchor = htonl(st->anchor.ptr->nr);
	if (st->nat_rule.ptr == NULL)
		sp->nat_rule = htonl(-1);
	else
		sp->nat_rule = htonl(st->nat_rule.ptr->nr);

	pf_state_counter_hton(st->packets[0], sp->packets[0]);
	pf_state_counter_hton(st->packets[1], sp->packets[1]);
	pf_state_counter_hton(st->bytes[0], sp->bytes[0]);
	pf_state_counter_hton(st->bytes[1], sp->bytes[1]);

}

int
pfsync_state_import(struct pfsync_state *sp, u_int8_t flags)
{
	struct pf_state	*st = NULL;
	struct pf_state_key *skw = NULL, *sks = NULL;
	struct pf_rule *r = NULL;
	struct pfi_kif	*kif;
	int pool_flags;
	int error;

	if (sp->creatorid == 0 && pf_status.debug >= PF_DEBUG_MISC) {
		kprintf("pfsync_insert_net_state: invalid creator id:"
		    " %08x\n", ntohl(sp->creatorid));
		return (EINVAL);
	}

	if ((kif = pfi_kif_get(sp->ifname)) == NULL) {
		if (pf_status.debug >= PF_DEBUG_MISC)
			kprintf("pfsync_insert_net_state: "
			    "unknown interface: %s\n", sp->ifname);
		if (flags & PFSYNC_SI_IOCTL)
			return (EINVAL);
		return (0);	/* skip this state */
	}

	/*
	 * If the ruleset checksums match or the state is coming from the ioctl,
	 * it's safe to associate the state with the rule of that number.
	 */
	if (sp->rule != htonl(-1) && sp->anchor == htonl(-1) &&
	    (flags & (PFSYNC_SI_IOCTL | PFSYNC_SI_CKSUM)) && ntohl(sp->rule) <
	    pf_main_ruleset.rules[PF_RULESET_FILTER].active.rcount)
		r = pf_main_ruleset.rules[
		    PF_RULESET_FILTER].active.ptr_array[ntohl(sp->rule)];
	else
		r = &pf_default_rule;

	if ((r->max_states && r->states_cur >= r->max_states))
		goto cleanup;

	if (flags & PFSYNC_SI_IOCTL)
		pool_flags = M_WAITOK | M_NULLOK | M_ZERO;
	else
		pool_flags = M_WAITOK | M_ZERO;

	if ((st = kmalloc(sizeof(struct pf_state), M_PFSYNC, pool_flags)) == NULL)
		goto cleanup;
	lockinit(&st->lk, "pfstlk", 0, 0);

	if ((skw = pf_alloc_state_key(pool_flags)) == NULL)
		goto cleanup;

	if (PF_ANEQ(&sp->key[PF_SK_WIRE].addr[0],
	    &sp->key[PF_SK_STACK].addr[0], sp->af) ||
	    PF_ANEQ(&sp->key[PF_SK_WIRE].addr[1],
	    &sp->key[PF_SK_STACK].addr[1], sp->af) ||
	    sp->key[PF_SK_WIRE].port[0] != sp->key[PF_SK_STACK].port[0] ||
	    sp->key[PF_SK_WIRE].port[1] != sp->key[PF_SK_STACK].port[1]) {
		if ((sks = pf_alloc_state_key(pool_flags)) == NULL)
			goto cleanup;
	} else
		sks = skw;

	/* allocate memory for scrub info */
	if (pfsync_alloc_scrub_memory(&sp->src, &st->src) ||
	    pfsync_alloc_scrub_memory(&sp->dst, &st->dst))
		goto cleanup;

	/* copy to state key(s) */
	skw->addr[0] = sp->key[PF_SK_WIRE].addr[0];
	skw->addr[1] = sp->key[PF_SK_WIRE].addr[1];
	skw->port[0] = sp->key[PF_SK_WIRE].port[0];
	skw->port[1] = sp->key[PF_SK_WIRE].port[1];
	skw->proto = sp->proto;
	skw->af = sp->af;
	if (sks != skw) {
		sks->addr[0] = sp->key[PF_SK_STACK].addr[0];
		sks->addr[1] = sp->key[PF_SK_STACK].addr[1];
		sks->port[0] = sp->key[PF_SK_STACK].port[0];
		sks->port[1] = sp->key[PF_SK_STACK].port[1];
		sks->proto = sp->proto;
		sks->af = sp->af;
	}

	/* copy to state */
	bcopy(&sp->rt_addr, &st->rt_addr, sizeof(st->rt_addr));
	st->creation = time_second - ntohl(sp->creation);
	st->expire = time_second;
	if (sp->expire) {
		/* XXX No adaptive scaling. */
		st->expire -= r->timeout[sp->timeout] - ntohl(sp->expire);
	}

	st->expire = ntohl(sp->expire) + time_second;
	st->direction = sp->direction;
	st->log = sp->log;
	st->timeout = sp->timeout;
	st->state_flags = sp->state_flags;
	if (!(flags & PFSYNC_SI_IOCTL))
		st->sync_flags = PFSTATE_FROMSYNC;

	bcopy(sp->id, &st->id, sizeof(st->id));
	st->creatorid = sp->creatorid;
	pf_state_peer_ntoh(&sp->src, &st->src);
	pf_state_peer_ntoh(&sp->dst, &st->dst);

	st->rule.ptr = r;
	st->nat_rule.ptr = NULL;
	st->anchor.ptr = NULL;
	st->rt_kif = NULL;

	st->pfsync_time = 0;


	/* XXX when we have nat_rule/anchors, use STATE_INC_COUNTERS */
	r->states_cur++;
	r->states_tot++;

	if ((error = pf_state_insert(kif, skw, sks, st)) != 0) {
		/* XXX when we have nat_rule/anchors, use STATE_DEC_COUNTERS */
		r->states_cur--;
		goto cleanup_state;
	}

	return (0);

 cleanup:
	error = ENOMEM;
	if (skw == sks)
		sks = NULL;
	if (skw != NULL)
		kfree(skw, M_PFSYNC);
	if (sks != NULL)
		kfree(sks, M_PFSYNC);

 cleanup_state:	/* pf_state_insert frees the state keys */
	if (st) {
		if (st->dst.scrub)
			kfree(st->dst.scrub, M_PFSYNC);
		if (st->src.scrub)
			kfree(st->src.scrub, M_PFSYNC);
		kfree(st, M_PFSYNC);
	}
	return (error);
}

void
pfsync_input(struct mbuf *m, ...)
{
	struct ip *ip = mtod(m, struct ip *);
	struct pfsync_header *ph;
	struct pfsync_softc *sc = pfsyncif;
	struct pf_state *st;
	struct pf_state_key *sk;
	struct pf_state_item *si;
	struct pf_state_cmp id_key;
	struct pfsync_state *sp;
	struct pfsync_state_upd *up;
	struct pfsync_state_del *dp;
	struct pfsync_state_clr *cp;
	struct pfsync_state_upd_req *rup;
	struct pfsync_state_bus *bus;
#ifdef IPSEC
	struct pfsync_tdb *pt;
#endif
	struct in_addr src;
	struct mbuf *mp;
	int iplen, action, error, i, count, offp, sfail, stale = 0;
	u_int8_t flags = 0;

	/* This function is not yet called from anywhere */
	/* Still we assume for safety that pf_token must be held */
	ASSERT_LWKT_TOKEN_HELD(&pf_token);

	pfsyncstats.pfsyncs_ipackets++;

	/* verify that we have a sync interface configured */
	if (!sc || !sc->sc_sync_ifp || !pf_status.running)
		goto done;

	/* verify that the packet came in on the right interface */
	if (sc->sc_sync_ifp != m->m_pkthdr.rcvif) {
		pfsyncstats.pfsyncs_badif++;
		goto done;
	}

	/* verify that the IP TTL is 255.  */
	if (ip->ip_ttl != PFSYNC_DFLTTL) {
		pfsyncstats.pfsyncs_badttl++;
		goto done;
	}

	iplen = ip->ip_hl << 2;

	if (m->m_pkthdr.len < iplen + sizeof(*ph)) {
		pfsyncstats.pfsyncs_hdrops++;
		goto done;
	}

	if (iplen + sizeof(*ph) > m->m_len) {
		if ((m = m_pullup(m, iplen + sizeof(*ph))) == NULL) {
			pfsyncstats.pfsyncs_hdrops++;
			goto done;
		}
		ip = mtod(m, struct ip *);
	}
	ph = (struct pfsync_header *)((char *)ip + iplen);

	/* verify the version */
	if (ph->version != PFSYNC_VERSION) {
		pfsyncstats.pfsyncs_badver++;
		goto done;
	}

	action = ph->action;
	count = ph->count;

	/* make sure it's a valid action code */
	if (action >= PFSYNC_ACT_MAX) {
		pfsyncstats.pfsyncs_badact++;
		goto done;
	}

	/* Cheaper to grab this now than having to mess with mbufs later */
	src = ip->ip_src;

	if (!bcmp(&ph->pf_chksum, &pf_status.pf_chksum, PF_MD5_DIGEST_LENGTH))
		flags |= PFSYNC_SI_CKSUM;

	switch (action) {
	case PFSYNC_ACT_CLR: {
		struct pf_state *nexts;
		struct pf_state_key *nextsk;
		struct pfi_kif *kif;
		globaldata_t save_gd = mycpu;
		int nn;

		u_int32_t creatorid;
		if ((mp = m_pulldown(m, iplen + sizeof(*ph),
		    sizeof(*cp), &offp)) == NULL) {
			pfsyncstats.pfsyncs_badlen++;
			return;
		}
		cp = (struct pfsync_state_clr *)(mp->m_data + offp);
		creatorid = cp->creatorid;

		crit_enter();
		if (cp->ifname[0] == '\0') {
			lwkt_gettoken(&pf_token);
			for (nn = 0; nn < ncpus; ++nn) {
				lwkt_setcpu_self(globaldata_find(nn));
				for (st = RB_MIN(pf_state_tree_id,
						 &tree_id[nn]);
				     st; st = nexts) {
					nexts = RB_NEXT(pf_state_tree_id,
							&tree_id[n], st);
					if (st->creatorid == creatorid) {
						st->sync_flags |=
							PFSTATE_FROMSYNC;
						pf_unlink_state(st);
					}
				}
			}
			lwkt_setcpu_self(save_gd);
			lwkt_reltoken(&pf_token);
		} else {
			if ((kif = pfi_kif_get(cp->ifname)) == NULL) {
				crit_exit();
				return;
			}
			/* XXX correct? */
			lwkt_gettoken(&pf_token);
			for (nn = 0; nn < ncpus; ++nn) {
				lwkt_setcpu_self(globaldata_find(nn));
				for (sk = RB_MIN(pf_state_tree,
						 &pf_statetbl[nn]);
				     sk;
				     sk = nextsk) {
					nextsk = RB_NEXT(pf_state_tree,
					    &pf_statetbl[n], sk);
					TAILQ_FOREACH(si, &sk->states, entry) {
						if (si->s->creatorid ==
						    creatorid) {
							si->s->sync_flags |=
							    PFSTATE_FROMSYNC;
							pf_unlink_state(si->s);
						}
					}
				}
			}
			lwkt_setcpu_self(save_gd);
			lwkt_reltoken(&pf_token);
		}
		crit_exit();

		break;
	}
	case PFSYNC_ACT_INS:
		if ((mp = m_pulldown(m, iplen + sizeof(*ph),
		    count * sizeof(*sp), &offp)) == NULL) {
			pfsyncstats.pfsyncs_badlen++;
			return;
		}

		crit_enter();
		for (i = 0, sp = (struct pfsync_state *)(mp->m_data + offp);
		    i < count; i++, sp++) {
			/* check for invalid values */
			if (sp->timeout >= PFTM_MAX ||
			    sp->src.state > PF_TCPS_PROXY_DST ||
			    sp->dst.state > PF_TCPS_PROXY_DST ||
			    sp->direction > PF_OUT ||
			    (sp->af != AF_INET && sp->af != AF_INET6)) {
				if (pf_status.debug >= PF_DEBUG_MISC)
					kprintf("pfsync_insert: PFSYNC_ACT_INS: "
					    "invalid value\n");
				pfsyncstats.pfsyncs_badval++;
				continue;
			}

			if ((error = pfsync_state_import(sp, flags))) {
				if (error == ENOMEM) {
					crit_exit();
					goto done;
				}
			}
		}
		crit_exit();
		break;
	case PFSYNC_ACT_UPD:
		if ((mp = m_pulldown(m, iplen + sizeof(*ph),
		    count * sizeof(*sp), &offp)) == NULL) {
			pfsyncstats.pfsyncs_badlen++;
			return;
		}

		crit_enter();
		for (i = 0, sp = (struct pfsync_state *)(mp->m_data + offp);
		    i < count; i++, sp++) {
			int flags = PFSYNC_FLAG_STALE;

			/* check for invalid values */
			if (sp->timeout >= PFTM_MAX ||
			    sp->src.state > PF_TCPS_PROXY_DST ||
			    sp->dst.state > PF_TCPS_PROXY_DST) {
				if (pf_status.debug >= PF_DEBUG_MISC)
					kprintf("pfsync_insert: PFSYNC_ACT_UPD: "
					    "invalid value\n");
				pfsyncstats.pfsyncs_badval++;
				continue;
			}

			bcopy(sp->id, &id_key.id, sizeof(id_key.id));
			id_key.creatorid = sp->creatorid;

			st = pf_find_state_byid(&id_key);
			if (st == NULL) {
				/* insert the update */
				if (pfsync_state_import(sp, flags))
					pfsyncstats.pfsyncs_badstate++;
				continue;
			}
			sk = st->key[PF_SK_WIRE];	/* XXX right one? */
			sfail = 0;
			if (sk->proto == IPPROTO_TCP) {
				/*
				 * The state should never go backwards except
				 * for syn-proxy states.  Neither should the
				 * sequence window slide backwards.
				 */
				if (st->src.state > sp->src.state &&
				    (st->src.state < PF_TCPS_PROXY_SRC ||
				    sp->src.state >= PF_TCPS_PROXY_SRC))
					sfail = 1;
				else if (SEQ_GT(st->src.seqlo,
				    ntohl(sp->src.seqlo)))
					sfail = 3;
				else if (st->dst.state > sp->dst.state) {
					/* There might still be useful
					 * information about the src state here,
					 * so import that part of the update,
					 * then "fail" so we send the updated
					 * state back to the peer who is missing
					 * our what we know. */
					pf_state_peer_ntoh(&sp->src, &st->src);
					/* XXX do anything with timeouts? */
					sfail = 7;
					flags = 0;
				} else if (st->dst.state >= TCPS_SYN_SENT &&
				    SEQ_GT(st->dst.seqlo, ntohl(sp->dst.seqlo)))
					sfail = 4;
			} else {
				/*
				 * Non-TCP protocol state machine always go
				 * forwards
				 */
				if (st->src.state > sp->src.state)
					sfail = 5;
				else if (st->dst.state > sp->dst.state)
					sfail = 6;
			}
			if (sfail) {
				if (pf_status.debug >= PF_DEBUG_MISC)
					kprintf("pfsync: %s stale update "
					    "(%d) id: %016jx "
					    "creatorid: %08x\n",
					    (sfail < 7 ?  "ignoring"
					     : "partial"), sfail,
					    (uintmax_t)be64toh(st->id),
					    ntohl(st->creatorid));
				pfsyncstats.pfsyncs_stale++;

				if (!(sp->sync_flags & PFSTATE_STALE)) {
					/* we have a better state, send it */
					if (sc->sc_mbuf != NULL && !stale)
						pfsync_sendout(sc);
					stale++;
					if (!st->sync_flags)
						pfsync_pack_state(
						    PFSYNC_ACT_UPD, st, flags);
				}
				continue;
			}
			pfsync_alloc_scrub_memory(&sp->dst, &st->dst);
			pf_state_peer_ntoh(&sp->src, &st->src);
			pf_state_peer_ntoh(&sp->dst, &st->dst);
			st->expire = ntohl(sp->expire) + time_second;
			st->timeout = sp->timeout;
		}
		if (stale && sc->sc_mbuf != NULL)
			pfsync_sendout(sc);
		crit_exit();
		break;
	/*
	 * It's not strictly necessary for us to support the "uncompressed"
	 * delete action, but it's relatively simple and maintains consistency.
	 */
	case PFSYNC_ACT_DEL:
		if ((mp = m_pulldown(m, iplen + sizeof(*ph),
		    count * sizeof(*sp), &offp)) == NULL) {
			pfsyncstats.pfsyncs_badlen++;
			return;
		}

		crit_enter();
		for (i = 0, sp = (struct pfsync_state *)(mp->m_data + offp);
		    i < count; i++, sp++) {
			bcopy(sp->id, &id_key.id, sizeof(id_key.id));
			id_key.creatorid = sp->creatorid;

			st = pf_find_state_byid(&id_key);
			if (st == NULL) {
				pfsyncstats.pfsyncs_badstate++;
				continue;
			}
			st->sync_flags |= PFSTATE_FROMSYNC;
			pf_unlink_state(st);
		}
		crit_exit();
		break;
	case PFSYNC_ACT_UPD_C: {
		int update_requested = 0;

		if ((mp = m_pulldown(m, iplen + sizeof(*ph),
		    count * sizeof(*up), &offp)) == NULL) {
			pfsyncstats.pfsyncs_badlen++;
			return;
		}

		crit_enter();
		for (i = 0, up = (struct pfsync_state_upd *)(mp->m_data + offp);
		    i < count; i++, up++) {
			/* check for invalid values */
			if (up->timeout >= PFTM_MAX ||
			    up->src.state > PF_TCPS_PROXY_DST ||
			    up->dst.state > PF_TCPS_PROXY_DST) {
				if (pf_status.debug >= PF_DEBUG_MISC)
					kprintf("pfsync_insert: "
					    "PFSYNC_ACT_UPD_C: "
					    "invalid value\n");
				pfsyncstats.pfsyncs_badval++;
				continue;
			}

			bcopy(up->id, &id_key.id, sizeof(id_key.id));
			id_key.creatorid = up->creatorid;

			st = pf_find_state_byid(&id_key);
			if (st == NULL) {
				/* We don't have this state. Ask for it. */
				error = pfsync_request_update(up, &src);
				if (error == ENOMEM) {
					crit_exit();
					goto done;
				}
				update_requested = 1;
				pfsyncstats.pfsyncs_badstate++;
				continue;
			}
			sk = st->key[PF_SK_WIRE]; /* XXX right one? */
			sfail = 0;
			if (sk->proto == IPPROTO_TCP) {
				/*
				 * The state should never go backwards except
				 * for syn-proxy states.  Neither should the
				 * sequence window slide backwards.
				 */
				if (st->src.state > up->src.state &&
				    (st->src.state < PF_TCPS_PROXY_SRC ||
				    up->src.state >= PF_TCPS_PROXY_SRC))
					sfail = 1;
				else if (st->dst.state > up->dst.state)
					sfail = 2;
				else if (SEQ_GT(st->src.seqlo,
				    ntohl(up->src.seqlo)))
					sfail = 3;
				else if (st->dst.state >= TCPS_SYN_SENT &&
				    SEQ_GT(st->dst.seqlo, ntohl(up->dst.seqlo)))
					sfail = 4;
			} else {
				/*
				 * Non-TCP protocol state machine always go
				 * forwards
				 */
				if (st->src.state > up->src.state)
					sfail = 5;
				else if (st->dst.state > up->dst.state)
					sfail = 6;
			}
			if (sfail) {
				if (pf_status.debug >= PF_DEBUG_MISC)
					kprintf("pfsync: ignoring stale update "
					    "(%d) id: %016" PRIx64 " "
					    "creatorid: %08x\n", sfail,
					    be64toh(st->id),
					    ntohl(st->creatorid));
				pfsyncstats.pfsyncs_stale++;

				/* we have a better state, send it out */
				if ((!stale || update_requested) &&
				    sc->sc_mbuf != NULL) {
					pfsync_sendout(sc);
					update_requested = 0;
				}
				stale++;
				if (!st->sync_flags)
					pfsync_pack_state(PFSYNC_ACT_UPD, st,
					    PFSYNC_FLAG_STALE);
				continue;
			}
			pfsync_alloc_scrub_memory(&up->dst, &st->dst);
			pf_state_peer_ntoh(&up->src, &st->src);
			pf_state_peer_ntoh(&up->dst, &st->dst);
			st->expire = ntohl(up->expire) + time_second;
			st->timeout = up->timeout;
		}
		if ((update_requested || stale) && sc->sc_mbuf)
			pfsync_sendout(sc);
		crit_exit();
		break;
	}
	case PFSYNC_ACT_DEL_C:
		if ((mp = m_pulldown(m, iplen + sizeof(*ph),
		    count * sizeof(*dp), &offp)) == NULL) {
			pfsyncstats.pfsyncs_badlen++;
			return;
		}

		crit_enter();
		for (i = 0, dp = (struct pfsync_state_del *)(mp->m_data + offp);
		    i < count; i++, dp++) {
			bcopy(dp->id, &id_key.id, sizeof(id_key.id));
			id_key.creatorid = dp->creatorid;

			st = pf_find_state_byid(&id_key);
			if (st == NULL) {
				pfsyncstats.pfsyncs_badstate++;
				continue;
			}
			st->sync_flags |= PFSTATE_FROMSYNC;
			pf_unlink_state(st);
		}
		crit_exit();
		break;
	case PFSYNC_ACT_INS_F:
	case PFSYNC_ACT_DEL_F:
		/* not implemented */
		break;
	case PFSYNC_ACT_UREQ:
		if ((mp = m_pulldown(m, iplen + sizeof(*ph),
		    count * sizeof(*rup), &offp)) == NULL) {
			pfsyncstats.pfsyncs_badlen++;
			return;
		}

		crit_enter();
		if (sc->sc_mbuf != NULL)
			pfsync_sendout(sc);
		for (i = 0,
		    rup = (struct pfsync_state_upd_req *)(mp->m_data + offp);
		    i < count; i++, rup++) {
			bcopy(rup->id, &id_key.id, sizeof(id_key.id));
			id_key.creatorid = rup->creatorid;

			if (id_key.id == 0 && id_key.creatorid == 0) {
				sc->sc_ureq_received = mycpu->gd_time_seconds;
				if (sc->sc_bulk_send_next == NULL) {
					if (++sc->sc_bulk_send_cpu >= ncpus)
						sc->sc_bulk_send_cpu = 0;
					sc->sc_bulk_send_next =
					    TAILQ_FIRST(&state_list[sc->sc_bulk_send_cpu]);
				}
				sc->sc_bulk_terminator =
					sc->sc_bulk_send_next;
				sc->sc_bulk_terminator_cpu =
					sc->sc_bulk_send_cpu;
				if (pf_status.debug >= PF_DEBUG_MISC)
					kprintf("pfsync: received "
					    "bulk update request\n");
				pfsync_send_bus(sc, PFSYNC_BUS_START);
				lwkt_reltoken(&pf_token);
				callout_init(&sc->sc_bulk_tmo);
				lwkt_gettoken(&pf_token);
			} else {
				st = pf_find_state_byid(&id_key);
				if (st == NULL) {
					pfsyncstats.pfsyncs_badstate++;
					continue;
				}
				if (!st->sync_flags)
					pfsync_pack_state(PFSYNC_ACT_UPD,
					    st, 0);
			}
		}
		if (sc->sc_mbuf != NULL)
			pfsync_sendout(sc);
		crit_exit();
		break;
	case PFSYNC_ACT_BUS:
		/* If we're not waiting for a bulk update, who cares. */
		if (sc->sc_ureq_sent == 0)
			break;

		if ((mp = m_pulldown(m, iplen + sizeof(*ph),
		    sizeof(*bus), &offp)) == NULL) {
			pfsyncstats.pfsyncs_badlen++;
			return;
		}
		bus = (struct pfsync_state_bus *)(mp->m_data + offp);
		switch (bus->status) {
		case PFSYNC_BUS_START:
			lwkt_reltoken(&pf_token);
			callout_reset(&sc->sc_bulkfail_tmo,
			    pf_pool_limits[PF_LIMIT_STATES].limit /
			    (PFSYNC_BULKPACKETS * sc->sc_maxcount), 
			    pfsync_bulkfail, LIST_FIRST(&pfsync_list));
			lwkt_gettoken(&pf_token);
			if (pf_status.debug >= PF_DEBUG_MISC)
				kprintf("pfsync: received bulk "
				    "update start\n");
			break;
		case PFSYNC_BUS_END:
			if (mycpu->gd_time_seconds - ntohl(bus->endtime) >=
			    sc->sc_ureq_sent) {
				/* that's it, we're happy */
				sc->sc_ureq_sent = 0;
				sc->sc_bulk_tries = 0;
				lwkt_reltoken(&pf_token);
				callout_stop(&sc->sc_bulkfail_tmo);
				lwkt_gettoken(&pf_token);
#if NCARP > 0
				if (!pfsync_sync_ok) {
					lwkt_reltoken(&pf_token);
					carp_group_demote_adj(&sc->sc_if, -1);
					lwkt_gettoken(&pf_token);
				}
#endif
				pfsync_sync_ok = 1;
				if (pf_status.debug >= PF_DEBUG_MISC)
					kprintf("pfsync: received valid "
					    "bulk update end\n");
			} else {
				if (pf_status.debug >= PF_DEBUG_MISC)
					kprintf("pfsync: received invalid "
					    "bulk update end: bad timestamp\n");
			}
			break;
		}
		break;
#ifdef IPSEC
	case PFSYNC_ACT_TDB_UPD:
		if ((mp = m_pulldown(m, iplen + sizeof(*ph),
		    count * sizeof(*pt), &offp)) == NULL) {
			pfsyncstats.pfsyncs_badlen++;
			return;
		}
		crit_enter();
		for (i = 0, pt = (struct pfsync_tdb *)(mp->m_data + offp);
		    i < count; i++, pt++)
			pfsync_update_net_tdb(pt);
		crit_exit();
		break;
#endif
	}

done:
	if (m)
		m_freem(m);
}

int
pfsyncoutput(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
	struct rtentry *rt)
{
	m_freem(m);
	return (0);
}

/* ARGSUSED */
int
pfsyncioctl(struct ifnet *ifp, u_long cmd, caddr_t data, struct ucred *cr)
{
	struct pfsync_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct ip_moptions *imo = &sc->sc_imo;
	struct pfsyncreq pfsyncr;
	struct ifnet    *sifp;
	int error;

	lwkt_gettoken(&pf_token);

	switch (cmd) {
	case SIOCSIFADDR:
	case SIOCAIFADDR:
	case SIOCSIFDSTADDR:
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP)
			ifp->if_flags |= IFF_RUNNING;
		else
			ifp->if_flags &= ~IFF_RUNNING;
		break;
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < PFSYNC_MINMTU) {
			lwkt_reltoken(&pf_token);
			return (EINVAL);
		}	
		if (ifr->ifr_mtu > MCLBYTES)
			ifr->ifr_mtu = MCLBYTES;
		crit_enter();
		if (ifr->ifr_mtu < ifp->if_mtu)
			pfsync_sendout(sc);
		pfsync_setmtu(sc, ifr->ifr_mtu);
		crit_exit();
		break;
	case SIOCGETPFSYNC:
		bzero(&pfsyncr, sizeof(pfsyncr));
		if (sc->sc_sync_ifp)
			strlcpy(pfsyncr.pfsyncr_syncdev,
			    sc->sc_sync_ifp->if_xname, IFNAMSIZ);
		pfsyncr.pfsyncr_syncpeer = sc->sc_sync_peer;
		pfsyncr.pfsyncr_maxupdates = sc->sc_maxupdates;
		lwkt_reltoken(&pf_token);
		if ((error = copyout(&pfsyncr, ifr->ifr_data, sizeof(pfsyncr))))
			return (error);
		lwkt_gettoken(&pf_token);
		break;
	case SIOCSETPFSYNC:
		if ((error = priv_check_cred(cr, PRIV_ROOT, NULL_CRED_OKAY)) != 0) {
			lwkt_reltoken(&pf_token);
			return (error);
		}
		if ((error = copyin(ifr->ifr_data, &pfsyncr, sizeof(pfsyncr)))) {
			lwkt_reltoken(&pf_token);
			return (error);
		}

		if (pfsyncr.pfsyncr_syncpeer.s_addr == 0)
			sc->sc_sync_peer.s_addr = INADDR_PFSYNC_GROUP;
		else
			sc->sc_sync_peer.s_addr =
			    pfsyncr.pfsyncr_syncpeer.s_addr;

		if (pfsyncr.pfsyncr_maxupdates > 255) {
			lwkt_reltoken(&pf_token);
			return (EINVAL);
		}
		sc->sc_maxupdates = pfsyncr.pfsyncr_maxupdates;

		if (pfsyncr.pfsyncr_syncdev[0] == 0) {
			sc->sc_sync_ifp = NULL;
			if (sc->sc_mbuf_net != NULL) {
				/* Don't keep stale pfsync packets around. */
				crit_enter();
				m_freem(sc->sc_mbuf_net);
				sc->sc_mbuf_net = NULL;
				sc->sc_statep_net.s = NULL;
				crit_exit();
			}
			if (imo->imo_num_memberships > 0) {
				pfsync_in_delmulti(imo->imo_membership[--imo->imo_num_memberships]);
				imo->imo_multicast_ifp = NULL;
			}
			break;
		}

		/*
		 * XXX not that MPSAFE; pfsync needs serious rework
		 */
		ifnet_deserialize_all(ifp);
		ifnet_lock();
		sifp = ifunit(pfsyncr.pfsyncr_syncdev);
		ifnet_unlock();
		ifnet_serialize_all(ifp);

		if (sifp == NULL) {
			lwkt_reltoken(&pf_token);
			return (EINVAL);
		}

		crit_enter();
		if (sifp->if_mtu < sc->sc_if.if_mtu ||
		    (sc->sc_sync_ifp != NULL &&
		    sifp->if_mtu < sc->sc_sync_ifp->if_mtu) ||
		    sifp->if_mtu < MCLBYTES - sizeof(struct ip))
			pfsync_sendout(sc);
		sc->sc_sync_ifp = sifp;

		pfsync_setmtu(sc, sc->sc_if.if_mtu);

		if (imo->imo_num_memberships > 0) {
			pfsync_in_delmulti(imo->imo_membership[--imo->imo_num_memberships]);
			imo->imo_multicast_ifp = NULL;
		}

		if (sc->sc_sync_ifp &&
		    sc->sc_sync_peer.s_addr == INADDR_PFSYNC_GROUP) {
			if (!(sc->sc_sync_ifp->if_flags & IFF_MULTICAST)) {
				sc->sc_sync_ifp = NULL;
				lwkt_reltoken(&pf_token);
				crit_exit();
				return (EADDRNOTAVAIL);
			}

			if ((imo->imo_membership[0] =
			    pfsync_in_addmulti(sc->sc_sync_ifp)) == NULL) {
				sc->sc_sync_ifp = NULL;
				lwkt_reltoken(&pf_token);
				crit_exit();
				return (ENOBUFS);
			}
			imo->imo_num_memberships++;
			imo->imo_multicast_ifp = sc->sc_sync_ifp;
			imo->imo_multicast_ttl = PFSYNC_DFLTTL;
			imo->imo_multicast_loop = 0;
		}

		if (sc->sc_sync_ifp ||
		    sc->sc_sendaddr.s_addr != INADDR_PFSYNC_GROUP) {
			/* Request a full state table update. */
			sc->sc_ureq_sent = mycpu->gd_time_seconds;
#if NCARP > 0
			if (pfsync_sync_ok)
				carp_group_demote_adj(&sc->sc_if, 1);
#endif
			pfsync_sync_ok = 0;
			if (pf_status.debug >= PF_DEBUG_MISC)
				kprintf("pfsync: requesting bulk update\n");
			lwkt_reltoken(&pf_token);
			callout_reset(&sc->sc_bulkfail_tmo, 5 * hz,
			    pfsync_bulkfail, LIST_FIRST(&pfsync_list));
			lwkt_gettoken(&pf_token);
			error = pfsync_request_update(NULL, NULL);
			if (error == ENOMEM) {
				lwkt_reltoken(&pf_token);
				crit_exit();
				return (ENOMEM);
			}
			pfsync_sendout(sc);
		}
		crit_exit();

		break;

	default:
		lwkt_reltoken(&pf_token);
		return (ENOTTY);
	}

	lwkt_reltoken(&pf_token);
	return (0);
}

void
pfsync_setmtu(struct pfsync_softc *sc, int mtu_req)
{
	int mtu;

	if (sc->sc_sync_ifp && sc->sc_sync_ifp->if_mtu < mtu_req)
		mtu = sc->sc_sync_ifp->if_mtu;
	else
		mtu = mtu_req;

	sc->sc_maxcount = (mtu - sizeof(struct pfsync_header)) /
	    sizeof(struct pfsync_state);
	if (sc->sc_maxcount > 254)
	    sc->sc_maxcount = 254;
	sc->sc_if.if_mtu = sizeof(struct pfsync_header) +
	    sc->sc_maxcount * sizeof(struct pfsync_state);
}

struct mbuf *
pfsync_get_mbuf(struct pfsync_softc *sc, u_int8_t action, void **sp)
{
	struct pfsync_header *h;
	struct mbuf *m;
	int len;

	ASSERT_LWKT_TOKEN_HELD(&pf_token);

	MGETHDR(m, M_WAITOK, MT_DATA);
	if (m == NULL) {
		IFNET_STAT_INC(&sc->sc_if, oerrors, 1);
		return (NULL);
	}

	switch (action) {
	case PFSYNC_ACT_CLR:
		len = sizeof(struct pfsync_header) +
		    sizeof(struct pfsync_state_clr);
		break;
	case PFSYNC_ACT_UPD_C:
		len = (sc->sc_maxcount * sizeof(struct pfsync_state_upd)) +
		    sizeof(struct pfsync_header);
		break;
	case PFSYNC_ACT_DEL_C:
		len = (sc->sc_maxcount * sizeof(struct pfsync_state_del)) +
		    sizeof(struct pfsync_header);
		break;
	case PFSYNC_ACT_UREQ:
		len = (sc->sc_maxcount * sizeof(struct pfsync_state_upd_req)) +
		    sizeof(struct pfsync_header);
		break;
	case PFSYNC_ACT_BUS:
		len = sizeof(struct pfsync_header) +
		    sizeof(struct pfsync_state_bus);
		break;
	case PFSYNC_ACT_TDB_UPD:
		len = (sc->sc_maxcount * sizeof(struct pfsync_tdb)) +
		    sizeof(struct pfsync_header);
		break;
	default:
		len = (sc->sc_maxcount * sizeof(struct pfsync_state)) +
		    sizeof(struct pfsync_header);
		break;
	}

	if (len > MHLEN) {
		MCLGET(m, M_WAITOK);
		if ((m->m_flags & M_EXT) == 0) {
			m_free(m);
			IFNET_STAT_INC(&sc->sc_if, oerrors, 1);
			return (NULL);
		}
		m->m_data += (MCLBYTES - len) &~ (sizeof(long) - 1);
	} else
		MH_ALIGN(m, len);

	m->m_pkthdr.rcvif = NULL;
	m->m_pkthdr.len = m->m_len = sizeof(struct pfsync_header);
	h = mtod(m, struct pfsync_header *);
	h->version = PFSYNC_VERSION;
	h->af = 0;
	h->count = 0;
	h->action = action;

	*sp = (void *)((char *)h + PFSYNC_HDRLEN);
	lwkt_reltoken(&pf_token);
	callout_reset(&sc->sc_tmo, hz, pfsync_timeout,
	    LIST_FIRST(&pfsync_list));
	lwkt_gettoken(&pf_token);
	return (m);
}

int
pfsync_pack_state(u_int8_t action, struct pf_state *st, int flags)
{
	struct ifnet *ifp = NULL;
	struct pfsync_softc *sc = pfsyncif;
	struct pfsync_header *h, *h_net;
	struct pfsync_state *sp = NULL;
	struct pfsync_state_upd *up = NULL;
	struct pfsync_state_del *dp = NULL;
	int ret = 0;
	u_int8_t i = 255, newaction = 0;

	if (sc == NULL)
		return (0);
	ifp = &sc->sc_if;

	/*
	 * If a packet falls in the forest and there's nobody around to
	 * hear, does it make a sound?
	 */
	if (ifp->if_bpf == NULL && sc->sc_sync_ifp == NULL &&
	    sc->sc_sync_peer.s_addr == INADDR_PFSYNC_GROUP) {
		/* Don't leave any stale pfsync packets hanging around. */
		if (sc->sc_mbuf != NULL) {
			m_freem(sc->sc_mbuf);
			sc->sc_mbuf = NULL;
			sc->sc_statep.s = NULL;
		}
		return (0);
	}

	if (action >= PFSYNC_ACT_MAX)
		return (EINVAL);

	crit_enter();
	if (sc->sc_mbuf == NULL) {
		if ((sc->sc_mbuf = pfsync_get_mbuf(sc, action,
		    (void *)&sc->sc_statep.s)) == NULL) {
			crit_exit();
			return (ENOMEM);
		}
		h = mtod(sc->sc_mbuf, struct pfsync_header *);
	} else {
		h = mtod(sc->sc_mbuf, struct pfsync_header *);
		if (h->action != action) {
			pfsync_sendout(sc);
			if ((sc->sc_mbuf = pfsync_get_mbuf(sc, action,
			    (void *)&sc->sc_statep.s)) == NULL) {
				crit_exit();
				return (ENOMEM);
			}
			h = mtod(sc->sc_mbuf, struct pfsync_header *);
		} else {
			/*
			 * If it's an update, look in the packet to see if
			 * we already have an update for the state.
			 */
			if (action == PFSYNC_ACT_UPD && sc->sc_maxupdates) {
				struct pfsync_state *usp =
				    (void *)((char *)h + PFSYNC_HDRLEN);

				for (i = 0; i < h->count; i++) {
					if (!memcmp(usp->id, &st->id,
					    PFSYNC_ID_LEN) &&
					    usp->creatorid == st->creatorid) {
						sp = usp;
						sp->updates++;
						break;
					}
					usp++;
				}
			}
		}
	}

	st->pfsync_time = mycpu->gd_time_seconds;

	if (sp == NULL) {
		/* not a "duplicate" update */
		i = 255;
		sp = sc->sc_statep.s++;
		sc->sc_mbuf->m_pkthdr.len =
		    sc->sc_mbuf->m_len += sizeof(struct pfsync_state);
		h->count++;
		bzero(sp, sizeof(*sp));

		pfsync_state_export(sp, st);

		if (flags & PFSYNC_FLAG_STALE)
			sp->sync_flags |= PFSTATE_STALE;
	} else {
		pf_state_peer_hton(&st->src, &sp->src);
		pf_state_peer_hton(&st->dst, &sp->dst);

		if (st->expire <= time_second)
			sp->expire = htonl(0);
		else
			sp->expire = htonl(st->expire - time_second);
	}

	/* do we need to build "compressed" actions for network transfer? */
	if (sc->sc_sync_ifp && flags & PFSYNC_FLAG_COMPRESS) {
		switch (action) {
		case PFSYNC_ACT_UPD:
			newaction = PFSYNC_ACT_UPD_C;
			break;
		case PFSYNC_ACT_DEL:
			newaction = PFSYNC_ACT_DEL_C;
			break;
		default:
			/* by default we just send the uncompressed states */
			break;
		}
	}

	if (newaction) {
		if (sc->sc_mbuf_net == NULL) {
			if ((sc->sc_mbuf_net = pfsync_get_mbuf(sc, newaction,
			    (void *)&sc->sc_statep_net.s)) == NULL) {
				crit_exit();
				return (ENOMEM);
			}
		}
		h_net = mtod(sc->sc_mbuf_net, struct pfsync_header *);

		switch (newaction) {
		case PFSYNC_ACT_UPD_C:
			if (i != 255) {
				up = (void *)((char *)h_net +
				    PFSYNC_HDRLEN + (i * sizeof(*up)));
				up->updates++;
			} else {
				h_net->count++;
				sc->sc_mbuf_net->m_pkthdr.len =
				    sc->sc_mbuf_net->m_len += sizeof(*up);
				up = sc->sc_statep_net.u++;

				bzero(up, sizeof(*up));
				bcopy(&st->id, up->id, sizeof(up->id));
				up->creatorid = st->creatorid;
			}
			up->timeout = st->timeout;
			up->expire = sp->expire;
			up->src = sp->src;
			up->dst = sp->dst;
			break;
		case PFSYNC_ACT_DEL_C:
			sc->sc_mbuf_net->m_pkthdr.len =
			    sc->sc_mbuf_net->m_len += sizeof(*dp);
			dp = sc->sc_statep_net.d++;
			h_net->count++;

			bzero(dp, sizeof(*dp));
			bcopy(&st->id, dp->id, sizeof(dp->id));
			dp->creatorid = st->creatorid;
			break;
		}
	}

	if (h->count == sc->sc_maxcount ||
	    (sc->sc_maxupdates && (sp->updates >= sc->sc_maxupdates)))
		ret = pfsync_sendout(sc);

	crit_exit();
	return (ret);
}

int
pfsync_request_update(struct pfsync_state_upd *up, struct in_addr *src)
{
	struct pfsync_header *h;
	struct pfsync_softc *sc = pfsyncif;
	struct pfsync_state_upd_req *rup;
	int ret = 0;

	if (sc == NULL)
		return (0);

	if (sc->sc_mbuf == NULL) {
		if ((sc->sc_mbuf = pfsync_get_mbuf(sc, PFSYNC_ACT_UREQ,
		    (void *)&sc->sc_statep.s)) == NULL)
			return (ENOMEM);
		h = mtod(sc->sc_mbuf, struct pfsync_header *);
	} else {
		h = mtod(sc->sc_mbuf, struct pfsync_header *);
		if (h->action != PFSYNC_ACT_UREQ) {
			pfsync_sendout(sc);
			if ((sc->sc_mbuf = pfsync_get_mbuf(sc, PFSYNC_ACT_UREQ,
			    (void *)&sc->sc_statep.s)) == NULL)
				return (ENOMEM);
			h = mtod(sc->sc_mbuf, struct pfsync_header *);
		}
	}

	if (src != NULL)
		sc->sc_sendaddr = *src;
	sc->sc_mbuf->m_pkthdr.len = sc->sc_mbuf->m_len += sizeof(*rup);
	h->count++;
	rup = sc->sc_statep.r++;
	bzero(rup, sizeof(*rup));
	if (up != NULL) {
		bcopy(up->id, rup->id, sizeof(rup->id));
		rup->creatorid = up->creatorid;
	}

	if (h->count == sc->sc_maxcount)
		ret = pfsync_sendout(sc);

	return (ret);
}

int
pfsync_clear_states(u_int32_t creatorid, char *ifname)
{
	struct pfsync_softc *sc = pfsyncif;
	struct pfsync_state_clr *cp;
	int ret;

	if (sc == NULL)
		return (0);

	crit_enter();
	if (sc->sc_mbuf != NULL)
		pfsync_sendout(sc);
	if ((sc->sc_mbuf = pfsync_get_mbuf(sc, PFSYNC_ACT_CLR,
	    (void *)&sc->sc_statep.c)) == NULL) {
		crit_exit();
		return (ENOMEM);
	}
	sc->sc_mbuf->m_pkthdr.len = sc->sc_mbuf->m_len += sizeof(*cp);
	cp = sc->sc_statep.c;
	cp->creatorid = creatorid;
	if (ifname != NULL)
		strlcpy(cp->ifname, ifname, IFNAMSIZ);

	ret = (pfsync_sendout(sc));
	crit_exit();
	return (ret);
}

void
pfsync_timeout(void *v)
{
	struct pfsync_softc *sc = v;

	crit_enter();
	pfsync_sendout(sc);
	crit_exit();
}

void
pfsync_send_bus(struct pfsync_softc *sc, u_int8_t status)
{
	struct pfsync_state_bus *bus;

	if (sc->sc_mbuf != NULL)
		pfsync_sendout(sc);

	if (pfsync_sync_ok &&
	    (sc->sc_mbuf = pfsync_get_mbuf(sc, PFSYNC_ACT_BUS,
	    (void *)&sc->sc_statep.b)) != NULL) {
		sc->sc_mbuf->m_pkthdr.len = sc->sc_mbuf->m_len += sizeof(*bus);
		bus = sc->sc_statep.b;
		bus->creatorid = pf_status.hostid;
		bus->status = status;
		bus->endtime = htonl(mycpu->gd_time_seconds - sc->sc_ureq_received);
		pfsync_sendout(sc);
	}
}

void
pfsync_bulk_update(void *v)
{
	struct pfsync_softc *sc = v;
	int i = 0;
	int cpu;
	struct pf_state *state;

	ASSERT_LWKT_TOKEN_HELD(&pf_token);

	crit_enter();
	if (sc->sc_mbuf != NULL)
		pfsync_sendout(sc);

	/*
	 * Grab at most PFSYNC_BULKPACKETS worth of states which have not
	 * been sent since the latest request was made.
	 */
	state = sc->sc_bulk_send_next;
	cpu = sc->sc_bulk_send_cpu;
	if (state)
		do {
			/* send state update if syncable and not already sent */
			if (!state->sync_flags
			    && state->timeout < PFTM_MAX
			    && state->pfsync_time <= sc->sc_ureq_received) {
				pfsync_pack_state(PFSYNC_ACT_UPD, state, 0);
				i++;
			}

			/* figure next state to send */
			state = TAILQ_NEXT(state, entry_list);

			/* wrap to start of list if we hit the end */
			if (state == NULL) {
				if (++cpu >= ncpus)
					cpu = 0;
				state = TAILQ_FIRST(&state_list[cpu]);
			}
		} while (i < sc->sc_maxcount * PFSYNC_BULKPACKETS &&
		    cpu != sc->sc_bulk_terminator_cpu &&
		    state != sc->sc_bulk_terminator);

	if (state == NULL || (cpu == sc->sc_bulk_terminator_cpu &&
			      state == sc->sc_bulk_terminator)) {
		/* we're done */
		pfsync_send_bus(sc, PFSYNC_BUS_END);
		sc->sc_ureq_received = 0;
		sc->sc_bulk_send_next = NULL;
		sc->sc_bulk_terminator = NULL;
		sc->sc_bulk_send_cpu = 0;
		sc->sc_bulk_terminator_cpu = 0;
		lwkt_reltoken(&pf_token);
		callout_stop(&sc->sc_bulk_tmo);
		lwkt_gettoken(&pf_token);
		if (pf_status.debug >= PF_DEBUG_MISC)
			kprintf("pfsync: bulk update complete\n");
	} else {
		/* look again for more in a bit */
		lwkt_reltoken(&pf_token);
		callout_reset(&sc->sc_bulk_tmo, 1, pfsync_timeout,
			    LIST_FIRST(&pfsync_list));
		lwkt_gettoken(&pf_token);
		sc->sc_bulk_send_next = state;
		sc->sc_bulk_send_cpu = cpu;
	}
	if (sc->sc_mbuf != NULL)
		pfsync_sendout(sc);
	crit_exit();
}

void
pfsync_bulkfail(void *v)
{
	struct pfsync_softc *sc = v;
	int error;

	ASSERT_LWKT_TOKEN_HELD(&pf_token);

	if (sc->sc_bulk_tries++ < PFSYNC_MAX_BULKTRIES) {
		/* Try again in a bit */
		lwkt_reltoken(&pf_token);
		callout_reset(&sc->sc_bulkfail_tmo, 5 * hz, pfsync_bulkfail,
		    LIST_FIRST(&pfsync_list));
		lwkt_gettoken(&pf_token);
		crit_enter();
		error = pfsync_request_update(NULL, NULL);
		if (error == ENOMEM) {
			if (pf_status.debug >= PF_DEBUG_MISC)
				kprintf("pfsync: cannot allocate mbufs for "
				    "bulk update\n");
		} else
			pfsync_sendout(sc);
		crit_exit();
	} else {
		/* Pretend like the transfer was ok */
		sc->sc_ureq_sent = 0;
		sc->sc_bulk_tries = 0;
#if NCARP > 0
		if (!pfsync_sync_ok)
			carp_group_demote_adj(&sc->sc_if, -1);
#endif
		pfsync_sync_ok = 1;
		if (pf_status.debug >= PF_DEBUG_MISC)
			kprintf("pfsync: failed to receive "
			    "bulk update status\n");
		lwkt_reltoken(&pf_token);
		callout_stop(&sc->sc_bulkfail_tmo);
		lwkt_gettoken(&pf_token);
	}
}

/* This must be called in splnet() */
int
pfsync_sendout(struct pfsync_softc *sc)
{
#if NBPFILTER > 0
	struct ifnet *ifp = &sc->sc_if;
#endif
	struct mbuf *m;

	ASSERT_LWKT_TOKEN_HELD(&pf_token);

	lwkt_reltoken(&pf_token);
	callout_stop(&sc->sc_tmo);
	lwkt_gettoken(&pf_token);

	if (sc->sc_mbuf == NULL)
		return (0);
	m = sc->sc_mbuf;
	sc->sc_mbuf = NULL;
	sc->sc_statep.s = NULL;

#if NBPFILTER > 0
	if (ifp->if_bpf) {
		bpf_gettoken();
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
		bpf_reltoken();
	}
#endif

	if (sc->sc_mbuf_net) {
		m_freem(m);
		m = sc->sc_mbuf_net;
		sc->sc_mbuf_net = NULL;
		sc->sc_statep_net.s = NULL;
	}

	return pfsync_sendout_mbuf(sc, m);
}

int
pfsync_sendout_mbuf(struct pfsync_softc *sc, struct mbuf *m)
{
	struct sockaddr sa;
	struct ip *ip;

	if (sc->sc_sync_ifp ||
	    sc->sc_sync_peer.s_addr != INADDR_PFSYNC_GROUP) {
		M_PREPEND(m, sizeof(struct ip), M_WAITOK);
		if (m == NULL) {
			pfsyncstats.pfsyncs_onomem++;
			return (0);
		}
		ip = mtod(m, struct ip *);
		ip->ip_v = IPVERSION;
		ip->ip_hl = sizeof(*ip) >> 2;
		ip->ip_tos = IPTOS_LOWDELAY;
		ip->ip_len = htons(m->m_pkthdr.len);
		ip->ip_id = htons(ip_randomid());
		ip->ip_off = htons(IP_DF);
		ip->ip_ttl = PFSYNC_DFLTTL;
		ip->ip_p = IPPROTO_PFSYNC;
		ip->ip_sum = 0;

		bzero(&sa, sizeof(sa));
		ip->ip_src.s_addr = INADDR_ANY;

		if (sc->sc_sendaddr.s_addr == INADDR_PFSYNC_GROUP)
			m->m_flags |= M_MCAST;
		ip->ip_dst = sc->sc_sendaddr;
		sc->sc_sendaddr.s_addr = sc->sc_sync_peer.s_addr;

		pfsyncstats.pfsyncs_opackets++;

		if (ip_output(m, NULL, NULL, IP_RAWOUTPUT, &sc->sc_imo, NULL))
			pfsyncstats.pfsyncs_oerrors++;
	} else
		m_freem(m);

	return (0);
}

static int
pfsync_modevent(module_t mod, int type, void *data)
{
	int error = 0;

	struct pfsync_softc	*pfs_if, *tmp;

	lwkt_gettoken(&pf_token);

	switch (type) {
	case MOD_LOAD:
		LIST_INIT(&pfsync_list);
		lwkt_reltoken(&pf_token);
		if_clone_attach(&pfsync_cloner);
		lwkt_gettoken(&pf_token);
		/* Override the function pointer for pf_ioctl.c */
		break;

	case MOD_UNLOAD:
		lwkt_reltoken(&pf_token);
		if_clone_detach(&pfsync_cloner);
		lwkt_gettoken(&pf_token);
		LIST_FOREACH_MUTABLE(pfs_if, &pfsync_list, sc_next, tmp) {
			pfsync_clone_destroy(&pfs_if->sc_if);
		}
		break;

	default:
		error = EINVAL;
		break;
	}

	lwkt_reltoken(&pf_token);
	return error;
}

static moduledata_t pfsync_mod = {
	"pfsync",
	pfsync_modevent,
	0
};

#define PFSYNC_MODVER 44

DECLARE_MODULE(pfsync, pfsync_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(pfsync, PFSYNC_MODVER);

static void
pfsync_in_addmulti_dispatch(netmsg_t nmsg)
{
	struct lwkt_msg *lmsg = &nmsg->lmsg;
	struct ifnet *ifp = lmsg->u.ms_resultp;
	struct in_addr addr;

	addr.s_addr = INADDR_PFSYNC_GROUP;
	lmsg->u.ms_resultp = in_addmulti(&addr, ifp);

	lwkt_replymsg(lmsg, 0);
}

static struct in_multi *
pfsync_in_addmulti(struct ifnet *ifp)
{
	struct netmsg_base nmsg;
	struct lwkt_msg *lmsg = &nmsg.lmsg;

	netmsg_init(&nmsg, NULL, &curthread->td_msgport, 0,
	    pfsync_in_addmulti_dispatch);
	lmsg->u.ms_resultp = ifp;

	lwkt_domsg(netisr_cpuport(0), lmsg, 0);
	return lmsg->u.ms_resultp;
}

static void
pfsync_in_delmulti_dispatch(netmsg_t nmsg)
{
	struct lwkt_msg *lmsg = &nmsg->lmsg;

	in_delmulti(lmsg->u.ms_resultp);
	lwkt_replymsg(lmsg, 0);
}

static void
pfsync_in_delmulti(struct in_multi *inm)
{
	struct netmsg_base nmsg;
	struct lwkt_msg *lmsg = &nmsg.lmsg;

	netmsg_init(&nmsg, NULL, &curthread->td_msgport, 0,
	    pfsync_in_delmulti_dispatch);
	lmsg->u.ms_resultp = inm;

	lwkt_domsg(netisr_cpuport(0), lmsg, 0);
}
