/*
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
 *
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2002 - 2008 Henning Brauer
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
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/filio.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/kernel.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/endian.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/spinlock.h>

#include <sys/md5.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/bpf.h>
#include <net/netisr2.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_seq.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <netinet/in_pcb.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/udp_var.h>
#include <netinet/icmp_var.h>
#include <netinet/if_ether.h>

#include <net/pf/pfvar.h>
#include <net/pf/if_pflog.h>

#include <net/pf/if_pfsync.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/nd6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_pcb.h>
#endif /* INET6 */

#include <sys/in_cksum.h>
#include <sys/ucred.h>
#include <machine/limits.h>
#include <sys/msgport2.h>
#include <sys/spinlock2.h>
#include <net/netmsg2.h>
#include <net/toeplitz2.h>

extern int ip_optcopy(struct ip *, struct ip *);
extern int debug_pfugidhack;

/*
 * pf_token - shared lock for cpu-localized operations,
 *	      exclusive lock otherwise.
 *
 * pf_gtoken- exclusive lock used for initialization.
 */
struct lwkt_token pf_token = LWKT_TOKEN_INITIALIZER(pf_token);
struct lwkt_token pf_gtoken = LWKT_TOKEN_INITIALIZER(pf_gtoken);

#define DPFPRINTF(n, x)	if (pf_status.debug >= (n)) kprintf x

#define FAIL(code)	{ error = (code); goto done; }

/*
 * Global variables
 */

/* mask radix tree */
struct radix_node_head	*pf_maskhead;

/* state tables */
struct pf_state_tree	 *pf_statetbl;		/* incls one global table */
struct pf_state		**purge_cur;
struct pf_altqqueue	 pf_altqs[2];
struct pf_palist	 pf_pabuf;
struct pf_altqqueue	*pf_altqs_active;
struct pf_altqqueue	*pf_altqs_inactive;
struct pf_status	 pf_status;

u_int32_t		 ticket_altqs_active;
u_int32_t		 ticket_altqs_inactive;
int			 altqs_inactive_open;
u_int32_t		 ticket_pabuf;

MD5_CTX			 pf_tcp_secret_ctx;
u_char			 pf_tcp_secret[16];
int			 pf_tcp_secret_init;
int			 pf_tcp_iss_off;

struct pf_anchor_stackframe {
	struct pf_ruleset			*rs;
	struct pf_rule				*r;
	struct pf_anchor_node			*parent;
	struct pf_anchor			*child;
} pf_anchor_stack[64];

struct malloc_type	 *pf_src_tree_pl, *pf_rule_pl, *pf_pooladdr_pl;
struct malloc_type	 *pf_state_pl, *pf_state_key_pl, *pf_state_item_pl;
struct malloc_type	 *pf_altq_pl;

void			 pf_print_host(struct pf_addr *, u_int16_t, u_int8_t);

void			 pf_init_threshold(struct pf_threshold *, u_int32_t,
			    u_int32_t);
void			 pf_add_threshold(struct pf_threshold *);
int			 pf_check_threshold(struct pf_threshold *);

void			 pf_change_ap(struct pf_addr *, u_int16_t *,
			    u_int16_t *, u_int16_t *, struct pf_addr *,
			    u_int16_t, u_int8_t, sa_family_t);
int			 pf_modulate_sack(struct mbuf *, int, struct pf_pdesc *,
			    struct tcphdr *, struct pf_state_peer *);
#ifdef INET6
void			 pf_change_a6(struct pf_addr *, u_int16_t *,
			    struct pf_addr *, u_int8_t);
#endif /* INET6 */
void			 pf_change_icmp(struct pf_addr *, u_int16_t *,
			    struct pf_addr *, struct pf_addr *, u_int16_t,
			    u_int16_t *, u_int16_t *, u_int16_t *,
			    u_int16_t *, u_int8_t, sa_family_t);
void			 pf_send_tcp(const struct pf_rule *, sa_family_t,
			    const struct pf_addr *, const struct pf_addr *,
			    u_int16_t, u_int16_t, u_int32_t, u_int32_t,
			    u_int8_t, u_int16_t, u_int16_t, u_int8_t, int,
			    u_int16_t, struct ether_header *, struct ifnet *);
void			 pf_send_icmp(struct mbuf *, u_int8_t, u_int8_t,
			    sa_family_t, struct pf_rule *);
struct pf_rule		*pf_match_translation(struct pf_pdesc *, struct mbuf *,
			    int, int, struct pfi_kif *,
			    struct pf_addr *, u_int16_t, struct pf_addr *,
			    u_int16_t, int);
struct pf_rule		*pf_get_translation(struct pf_pdesc *, struct mbuf *,
			    int, int, struct pfi_kif *, struct pf_src_node **,
			    struct pf_state_key **, struct pf_state_key **,
			    struct pf_state_key **, struct pf_state_key **,
			    struct pf_addr *, struct pf_addr *,
			    u_int16_t, u_int16_t);
void			 pf_detach_state(struct pf_state *);
int			 pf_state_key_setup(struct pf_pdesc *, struct pf_rule *,
			    struct pf_state_key **, struct pf_state_key **,
			    struct pf_state_key **, struct pf_state_key **,
			    struct pf_addr *, struct pf_addr *,
			    u_int16_t, u_int16_t);
void			 pf_state_key_detach(struct pf_state *, int);
u_int32_t		 pf_tcp_iss(struct pf_pdesc *);
int			 pf_test_rule(struct pf_rule **, struct pf_state **,
			    int, struct pfi_kif *, struct mbuf *, int,
			    void *, struct pf_pdesc *, struct pf_rule **,
			    struct pf_ruleset **, struct ifqueue *, struct inpcb *);
static __inline int	 pf_create_state(struct pf_rule *, struct pf_rule *,
			    struct pf_rule *, struct pf_pdesc *,
			    struct pf_src_node *, struct pf_state_key *,
			    struct pf_state_key *, struct pf_state_key *,
			    struct pf_state_key *, struct mbuf *, int,
			    u_int16_t, u_int16_t, int *, struct pfi_kif *,
			    struct pf_state **, int, u_int16_t, u_int16_t,
			    int);
int			 pf_test_fragment(struct pf_rule **, int,
			    struct pfi_kif *, struct mbuf *, void *,
			    struct pf_pdesc *, struct pf_rule **,
			    struct pf_ruleset **);
int			 pf_tcp_track_full(struct pf_state_peer *,
			    struct pf_state_peer *, struct pf_state **,
			    struct pfi_kif *, struct mbuf *, int,
			    struct pf_pdesc *, u_short *, int *);
int			pf_tcp_track_sloppy(struct pf_state_peer *,
			    struct pf_state_peer *, struct pf_state **,
			    struct pf_pdesc *, u_short *);
int			 pf_test_state_tcp(struct pf_state **, int,
			    struct pfi_kif *, struct mbuf *, int,
			    void *, struct pf_pdesc *, u_short *);
int			 pf_test_state_udp(struct pf_state **, int,
			    struct pfi_kif *, struct mbuf *, int,
			    void *, struct pf_pdesc *);
int			 pf_test_state_icmp(struct pf_state **, int,
			    struct pfi_kif *, struct mbuf *, int,
			    void *, struct pf_pdesc *, u_short *);
int			 pf_test_state_other(struct pf_state **, int,
			    struct pfi_kif *, struct mbuf *, struct pf_pdesc *);
void			 pf_step_into_anchor(int *, struct pf_ruleset **, int,
			    struct pf_rule **, struct pf_rule **, int *);
int			 pf_step_out_of_anchor(int *, struct pf_ruleset **,
			     int, struct pf_rule **, struct pf_rule **,
			     int *);
void			 pf_hash(struct pf_addr *, struct pf_addr *,
			    struct pf_poolhashkey *, sa_family_t);
int			 pf_map_addr(u_int8_t, struct pf_rule *,
			    struct pf_addr *, struct pf_addr *,
			    struct pf_addr *, struct pf_src_node **);
int			 pf_get_sport(struct pf_pdesc *,
			    sa_family_t, u_int8_t, struct pf_rule *,
			    struct pf_addr *, struct pf_addr *,
			    u_int16_t, u_int16_t,
			    struct pf_addr *, u_int16_t *,
			    u_int16_t, u_int16_t,
			    struct pf_src_node **);
void			 pf_route(struct mbuf **, struct pf_rule *, int,
			    struct ifnet *, struct pf_state *,
			    struct pf_pdesc *);
void			 pf_route6(struct mbuf **, struct pf_rule *, int,
			    struct ifnet *, struct pf_state *,
			    struct pf_pdesc *);
u_int8_t		 pf_get_wscale(struct mbuf *, int, u_int16_t,
			    sa_family_t);
u_int16_t		 pf_get_mss(struct mbuf *, int, u_int16_t,
			    sa_family_t);
u_int16_t		 pf_calc_mss(struct pf_addr *, sa_family_t,
				u_int16_t);
void			 pf_set_rt_ifp(struct pf_state *,
			    struct pf_addr *);
int			 pf_check_proto_cksum(struct mbuf *, int, int,
			    u_int8_t, sa_family_t);
struct pf_divert	*pf_get_divert(struct mbuf *);
void			 pf_print_state_parts(struct pf_state *,
			    struct pf_state_key *, struct pf_state_key *);
int			 pf_addr_wrap_neq(struct pf_addr_wrap *,
			    struct pf_addr_wrap *);
struct pf_state		*pf_find_state(struct pfi_kif *,
			    struct pf_state_key_cmp *, u_int, struct mbuf *);
int			 pf_src_connlimit(struct pf_state *);
int			 pf_check_congestion(struct ifqueue *);

extern int pf_end_threads;

struct pf_pool_limit pf_pool_limits[PF_LIMIT_MAX] = {
	{ &pf_state_pl, PFSTATE_HIWAT },
	{ &pf_src_tree_pl, PFSNODE_HIWAT },
	{ &pf_frent_pl, PFFRAG_FRENT_HIWAT },
	{ &pfr_ktable_pl, PFR_KTABLE_HIWAT },
	{ &pfr_kentry_pl, PFR_KENTRY_HIWAT }
};

/*
 * If route-to and direction is out we match with no further processing
 *	(rt_kif must be assigned and not equal to the out interface)
 * If reply-to and direction is in we match with no further processing
 *	(rt_kif must be assigned and not equal to the in interface)
 */
#define STATE_LOOKUP(i, k, d, s, m)					\
	do {								\
		s = pf_find_state(i, k, d, m);				\
		if (s == NULL || (s)->timeout == PFTM_PURGE)		\
			return (PF_DROP);				\
		if (d == PF_OUT &&					\
		    (((s)->rule.ptr->rt == PF_ROUTETO &&		\
		    (s)->rule.ptr->direction == PF_OUT) ||		\
		    ((s)->rule.ptr->rt == PF_REPLYTO &&			\
		    (s)->rule.ptr->direction == PF_IN)) &&		\
		    (s)->rt_kif != NULL &&				\
		    (s)->rt_kif != i)					\
			return (PF_PASS);				\
	} while (0)

#define BOUND_IFACE(r, k) \
	((r)->rule_flag & PFRULE_IFBOUND) ? (k) : pfi_all

#define STATE_INC_COUNTERS(s)				\
	do {						\
		atomic_add_int(&s->rule.ptr->states_cur, 1);	\
		s->rule.ptr->states_tot++;		\
		if (s->anchor.ptr != NULL) {		\
			atomic_add_int(&s->anchor.ptr->states_cur, 1);	\
			s->anchor.ptr->states_tot++;	\
		}					\
		if (s->nat_rule.ptr != NULL) {		\
			atomic_add_int(&s->nat_rule.ptr->states_cur, 1); \
			s->nat_rule.ptr->states_tot++;	\
		}					\
	} while (0)

#define STATE_DEC_COUNTERS(s)				\
	do {						\
		if (s->nat_rule.ptr != NULL)		\
			atomic_add_int(&s->nat_rule.ptr->states_cur, -1); \
		if (s->anchor.ptr != NULL)		\
			atomic_add_int(&s->anchor.ptr->states_cur, -1);	\
		atomic_add_int(&s->rule.ptr->states_cur, -1);		\
	} while (0)

static MALLOC_DEFINE(M_PFSTATEPL, "pfstatepl", "pf state pool list");
static MALLOC_DEFINE(M_PFSRCTREEPL, "pfsrctpl", "pf source tree pool list");
static MALLOC_DEFINE(M_PFSTATEKEYPL, "pfstatekeypl", "pf state key pool list");
static MALLOC_DEFINE(M_PFSTATEITEMPL, "pfstateitempl", "pf state item pool list");

static __inline int pf_src_compare(struct pf_src_node *, struct pf_src_node *);
static __inline int pf_state_compare_key(struct pf_state_key *,
				struct pf_state_key *);
static __inline int pf_state_compare_rkey(struct pf_state_key *,
				struct pf_state_key *);
static __inline int pf_state_compare_id(struct pf_state *,
				struct pf_state *);

struct pf_src_tree *tree_src_tracking;
struct pf_state_tree_id *tree_id;
struct pf_state_queue *state_list;
struct pf_counters *pf_counters;

RB_GENERATE(pf_src_tree, pf_src_node, entry, pf_src_compare);
RB_GENERATE(pf_state_tree, pf_state_key, entry, pf_state_compare_key);
RB_GENERATE(pf_state_rtree, pf_state_key, entry, pf_state_compare_rkey);
RB_GENERATE(pf_state_tree_id, pf_state, entry_id, pf_state_compare_id);

static __inline int
pf_src_compare(struct pf_src_node *a, struct pf_src_node *b)
{
	int	diff;

	if (a->rule.ptr > b->rule.ptr)
		return (1);
	if (a->rule.ptr < b->rule.ptr)
		return (-1);
	if ((diff = a->af - b->af) != 0)
		return (diff);
	switch (a->af) {
#ifdef INET
	case AF_INET:
		if (a->addr.addr32[0] > b->addr.addr32[0])
			return (1);
		if (a->addr.addr32[0] < b->addr.addr32[0])
			return (-1);
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		if (a->addr.addr32[3] > b->addr.addr32[3])
			return (1);
		if (a->addr.addr32[3] < b->addr.addr32[3])
			return (-1);
		if (a->addr.addr32[2] > b->addr.addr32[2])
			return (1);
		if (a->addr.addr32[2] < b->addr.addr32[2])
			return (-1);
		if (a->addr.addr32[1] > b->addr.addr32[1])
			return (1);
		if (a->addr.addr32[1] < b->addr.addr32[1])
			return (-1);
		if (a->addr.addr32[0] > b->addr.addr32[0])
			return (1);
		if (a->addr.addr32[0] < b->addr.addr32[0])
			return (-1);
		break;
#endif /* INET6 */
	}
	return (0);
}

u_int32_t
pf_state_hash(struct pf_state_key *sk)
{
	u_int32_t hv = (u_int32_t)(((intptr_t)sk >> 6) ^ ((intptr_t)sk >> 15));
	if (hv == 0)	/* disallow 0 */
		hv = 1;
	return(hv);
}

#ifdef INET6
void
pf_addrcpy(struct pf_addr *dst, struct pf_addr *src, sa_family_t af)
{
	switch (af) {
#ifdef INET
	case AF_INET:
		dst->addr32[0] = src->addr32[0];
		break;
#endif /* INET */
	case AF_INET6:
		dst->addr32[0] = src->addr32[0];
		dst->addr32[1] = src->addr32[1];
		dst->addr32[2] = src->addr32[2];
		dst->addr32[3] = src->addr32[3];
		break;
	}
}
#endif /* INET6 */

void
pf_init_threshold(struct pf_threshold *threshold,
    u_int32_t limit, u_int32_t seconds)
{
	threshold->limit = limit * PF_THRESHOLD_MULT;
	threshold->seconds = seconds;
	threshold->count = 0;
	threshold->last = time_second;
}

void
pf_add_threshold(struct pf_threshold *threshold)
{
	u_int32_t t = time_second, diff = t - threshold->last;

	if (diff >= threshold->seconds)
		threshold->count = 0;
	else
		threshold->count -= threshold->count * diff /
		    threshold->seconds;
	threshold->count += PF_THRESHOLD_MULT;
	threshold->last = t;
}

int
pf_check_threshold(struct pf_threshold *threshold)
{
	return (threshold->count > threshold->limit);
}

int
pf_src_connlimit(struct pf_state *state)
{
	int bad = 0;
	int cpu = mycpu->gd_cpuid;

	atomic_add_int(&state->src_node->conn, 1);
	state->src.tcp_est = 1;
	pf_add_threshold(&state->src_node->conn_rate);

	if (state->rule.ptr->max_src_conn &&
	    state->rule.ptr->max_src_conn <
	    state->src_node->conn) {
		PF_INC_LCOUNTER(LCNT_SRCCONN);
		bad++;
	}

	if (state->rule.ptr->max_src_conn_rate.limit &&
	    pf_check_threshold(&state->src_node->conn_rate)) {
		PF_INC_LCOUNTER(LCNT_SRCCONNRATE);
		bad++;
	}

	if (!bad)
		return 0;

	if (state->rule.ptr->overload_tbl) {
		struct pfr_addr p;
		u_int32_t	killed = 0;

		PF_INC_LCOUNTER(LCNT_OVERLOAD_TABLE);
		if (pf_status.debug >= PF_DEBUG_MISC) {
			kprintf("pf_src_connlimit: blocking address ");
			pf_print_host(&state->src_node->addr, 0,
			    state->key[PF_SK_WIRE]->af);
		}

		bzero(&p, sizeof(p));
		p.pfra_af = state->key[PF_SK_WIRE]->af;
		switch (state->key[PF_SK_WIRE]->af) {
#ifdef INET
		case AF_INET:
			p.pfra_net = 32;
			p.pfra_ip4addr = state->src_node->addr.v4;
			break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			p.pfra_net = 128;
			p.pfra_ip6addr = state->src_node->addr.v6;
			break;
#endif /* INET6 */
		}

		pfr_insert_kentry(state->rule.ptr->overload_tbl,
		    &p, time_second);

		/* kill existing states if that's required. */
		if (state->rule.ptr->flush) {
			struct pf_state_key *sk;
			struct pf_state *st;

			PF_INC_LCOUNTER(LCNT_OVERLOAD_FLUSH);
			RB_FOREACH(st, pf_state_tree_id, &tree_id[cpu]) {
				sk = st->key[PF_SK_WIRE];
				/*
				 * Kill states from this source.  (Only those
				 * from the same rule if PF_FLUSH_GLOBAL is not
				 * set).  (Only on current cpu).
				 */
				if (sk->af ==
				    state->key[PF_SK_WIRE]->af &&
				    ((state->direction == PF_OUT &&
				    PF_AEQ(&state->src_node->addr,
					&sk->addr[0], sk->af)) ||
				    (state->direction == PF_IN &&
				    PF_AEQ(&state->src_node->addr,
					&sk->addr[1], sk->af))) &&
				    (state->rule.ptr->flush &
				    PF_FLUSH_GLOBAL ||
				    state->rule.ptr == st->rule.ptr)) {
					st->timeout = PFTM_PURGE;
					st->src.state = st->dst.state =
					    TCPS_CLOSED;
					killed++;
				}
			}
			if (pf_status.debug >= PF_DEBUG_MISC)
				kprintf(", %u states killed", killed);
		}
		if (pf_status.debug >= PF_DEBUG_MISC)
			kprintf("\n");
	}

	/* kill this state */
	state->timeout = PFTM_PURGE;
	state->src.state = state->dst.state = TCPS_CLOSED;

	return 1;
}

int
pf_insert_src_node(struct pf_src_node **sn, struct pf_rule *rule,
    struct pf_addr *src, sa_family_t af)
{
	struct pf_src_node	k;
	int cpu = mycpu->gd_cpuid;

	bzero(&k, sizeof(k));	/* avoid gcc warnings */
	if (*sn == NULL) {
		k.af = af;
		PF_ACPY(&k.addr, src, af);
		if (rule->rule_flag & PFRULE_RULESRCTRACK ||
		    rule->rpool.opts & PF_POOL_STICKYADDR)
			k.rule.ptr = rule;
		else
			k.rule.ptr = NULL;
		PF_INC_SCOUNTER(SCNT_SRC_NODE_SEARCH);
		*sn = RB_FIND(pf_src_tree, &tree_src_tracking[cpu], &k);
	}
	if (*sn == NULL) {
		if (!rule->max_src_nodes ||
		    rule->src_nodes < rule->max_src_nodes)
			(*sn) = kmalloc(sizeof(struct pf_src_node),
					M_PFSRCTREEPL, M_NOWAIT|M_ZERO);
		else
			PF_INC_LCOUNTER(LCNT_SRCNODES);
		if ((*sn) == NULL)
			return (-1);

		pf_init_threshold(&(*sn)->conn_rate,
		    rule->max_src_conn_rate.limit,
		    rule->max_src_conn_rate.seconds);

		(*sn)->af = af;
		if (rule->rule_flag & PFRULE_RULESRCTRACK ||
		    rule->rpool.opts & PF_POOL_STICKYADDR)
			(*sn)->rule.ptr = rule;
		else
			(*sn)->rule.ptr = NULL;
		PF_ACPY(&(*sn)->addr, src, af);
		if (RB_INSERT(pf_src_tree,
		    &tree_src_tracking[cpu], *sn) != NULL) {
			if (pf_status.debug >= PF_DEBUG_MISC) {
				kprintf("pf: src_tree insert failed: ");
				pf_print_host(&(*sn)->addr, 0, af);
				kprintf("\n");
			}
			kfree(*sn, M_PFSRCTREEPL);
			return (-1);
		}

		/*
		 * Atomic op required to increment src_nodes in the rule
		 * because we hold a shared token here (decrements will use
		 * an exclusive token).
		 */
		(*sn)->creation = time_second;
		(*sn)->ruletype = rule->action;
		if ((*sn)->rule.ptr != NULL)
			atomic_add_int(&(*sn)->rule.ptr->src_nodes, 1);
		PF_INC_SCOUNTER(SCNT_SRC_NODE_INSERT);
		atomic_add_int(&pf_status.src_nodes, 1);
	} else {
		if (rule->max_src_states &&
		    (*sn)->states >= rule->max_src_states) {
			PF_INC_LCOUNTER(LCNT_SRCSTATES);
			return (-1);
		}
	}
	return (0);
}

/*
 * state table (indexed by the pf_state_key structure), normal RBTREE
 * comparison.
 */
static __inline int
pf_state_compare_key(struct pf_state_key *a, struct pf_state_key *b)
{
	int	diff;

	if ((diff = a->proto - b->proto) != 0)
		return (diff);
	if ((diff = a->af - b->af) != 0)
		return (diff);
	switch (a->af) {
#ifdef INET
	case AF_INET:
		if (a->addr[0].addr32[0] > b->addr[0].addr32[0])
			return (1);
		if (a->addr[0].addr32[0] < b->addr[0].addr32[0])
			return (-1);
		if (a->addr[1].addr32[0] > b->addr[1].addr32[0])
			return (1);
		if (a->addr[1].addr32[0] < b->addr[1].addr32[0])
			return (-1);
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		if (a->addr[0].addr32[3] > b->addr[0].addr32[3])
			return (1);
		if (a->addr[0].addr32[3] < b->addr[0].addr32[3])
			return (-1);
		if (a->addr[1].addr32[3] > b->addr[1].addr32[3])
			return (1);
		if (a->addr[1].addr32[3] < b->addr[1].addr32[3])
			return (-1);
		if (a->addr[0].addr32[2] > b->addr[0].addr32[2])
			return (1);
		if (a->addr[0].addr32[2] < b->addr[0].addr32[2])
			return (-1);
		if (a->addr[1].addr32[2] > b->addr[1].addr32[2])
			return (1);
		if (a->addr[1].addr32[2] < b->addr[1].addr32[2])
			return (-1);
		if (a->addr[0].addr32[1] > b->addr[0].addr32[1])
			return (1);
		if (a->addr[0].addr32[1] < b->addr[0].addr32[1])
			return (-1);
		if (a->addr[1].addr32[1] > b->addr[1].addr32[1])
			return (1);
		if (a->addr[1].addr32[1] < b->addr[1].addr32[1])
			return (-1);
		if (a->addr[0].addr32[0] > b->addr[0].addr32[0])
			return (1);
		if (a->addr[0].addr32[0] < b->addr[0].addr32[0])
			return (-1);
		if (a->addr[1].addr32[0] > b->addr[1].addr32[0])
			return (1);
		if (a->addr[1].addr32[0] < b->addr[1].addr32[0])
			return (-1);
		break;
#endif /* INET6 */
	}

	if ((diff = a->port[0] - b->port[0]) != 0)
		return (diff);
	if ((diff = a->port[1] - b->port[1]) != 0)
		return (diff);

	return (0);
}

/*
 * Used for RB_FIND only, compare in the reverse direction.  The
 * element to be reversed is always (a), since we obviously can't
 * reverse the state tree depicted by (b).
 */
static __inline int
pf_state_compare_rkey(struct pf_state_key *a, struct pf_state_key *b)
{
	int	diff;

	if ((diff = a->proto - b->proto) != 0)
		return (diff);
	if ((diff = a->af - b->af) != 0)
		return (diff);
	switch (a->af) {
#ifdef INET
	case AF_INET:
		if (a->addr[1].addr32[0] > b->addr[0].addr32[0])
			return (1);
		if (a->addr[1].addr32[0] < b->addr[0].addr32[0])
			return (-1);
		if (a->addr[0].addr32[0] > b->addr[1].addr32[0])
			return (1);
		if (a->addr[0].addr32[0] < b->addr[1].addr32[0])
			return (-1);
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		if (a->addr[1].addr32[3] > b->addr[0].addr32[3])
			return (1);
		if (a->addr[1].addr32[3] < b->addr[0].addr32[3])
			return (-1);
		if (a->addr[0].addr32[3] > b->addr[1].addr32[3])
			return (1);
		if (a->addr[0].addr32[3] < b->addr[1].addr32[3])
			return (-1);
		if (a->addr[1].addr32[2] > b->addr[0].addr32[2])
			return (1);
		if (a->addr[1].addr32[2] < b->addr[0].addr32[2])
			return (-1);
		if (a->addr[0].addr32[2] > b->addr[1].addr32[2])
			return (1);
		if (a->addr[0].addr32[2] < b->addr[1].addr32[2])
			return (-1);
		if (a->addr[1].addr32[1] > b->addr[0].addr32[1])
			return (1);
		if (a->addr[1].addr32[1] < b->addr[0].addr32[1])
			return (-1);
		if (a->addr[0].addr32[1] > b->addr[1].addr32[1])
			return (1);
		if (a->addr[0].addr32[1] < b->addr[1].addr32[1])
			return (-1);
		if (a->addr[1].addr32[0] > b->addr[0].addr32[0])
			return (1);
		if (a->addr[1].addr32[0] < b->addr[0].addr32[0])
			return (-1);
		if (a->addr[0].addr32[0] > b->addr[1].addr32[0])
			return (1);
		if (a->addr[0].addr32[0] < b->addr[1].addr32[0])
			return (-1);
		break;
#endif /* INET6 */
	}

	if ((diff = a->port[1] - b->port[0]) != 0)
		return (diff);
	if ((diff = a->port[0] - b->port[1]) != 0)
		return (diff);

	return (0);
}

static __inline int
pf_state_compare_id(struct pf_state *a, struct pf_state *b)
{
	if (a->id > b->id)
		return (1);
	if (a->id < b->id)
		return (-1);
	if (a->creatorid > b->creatorid)
		return (1);
	if (a->creatorid < b->creatorid)
		return (-1);

	return (0);
}

int
pf_state_key_attach(struct pf_state_key *sk, struct pf_state *s, int idx)
{
	struct pf_state_item	*si;
	struct pf_state_key     *cur;
	int cpu;
	int error;

	/*
	 * PFSTATE_STACK_GLOBAL is set when the state might not hash to the
	 * current cpu.  The keys are managed on the global statetbl tree
	 * for this case.  Only translations (RDR, NAT) can cause this.
	 *
	 * When this flag is not set we must still check the global statetbl
	 * for a collision, and if we find one we set the HALF_DUPLEX flag
	 * in the state.
	 */
	if (s->state_flags & PFSTATE_STACK_GLOBAL) {
		cpu = ncpus;
		lockmgr(&pf_global_statetbl_lock, LK_EXCLUSIVE);
	} else {
		cpu = mycpu->gd_cpuid;
		lockmgr(&pf_global_statetbl_lock, LK_SHARED);
	}
	KKASSERT(s->key[idx] == NULL);	/* XXX handle this? */

	if (pf_status.debug >= PF_DEBUG_MISC) {
		kprintf("state_key attach cpu %d (%08x:%d) %s (%08x:%d)\n",
			cpu,
			ntohl(sk->addr[0].addr32[0]), ntohs(sk->port[0]),
			(idx == PF_SK_WIRE ? "->" : "<-"),
			ntohl(sk->addr[1].addr32[0]), ntohs(sk->port[1]));
	}

	/*
	 * Check whether (e.g.) a PASS rule being put on a per-cpu tree
	 * collides with a translation rule on the global tree.  This is
	 * NOT an error.  We *WANT* to establish state for this case so the
	 * packet path is short-cutted and doesn't need to scan the ruleset
	 * on every packet.  But the established state will only see one
	 * side of a two-way packet conversation.  To prevent this from
	 * causing problems (e.g. generating a RST), we force PFSTATE_SLOPPY
	 * to be set on the established state.
	 *
	 * A collision against RDR state can only occur with a PASS IN in the
	 * opposite direction or a PASS OUT in the forwards direction.  This
	 * is because RDRs are processed on the input side.
	 *
	 * A collision against NAT state can only occur with a PASS IN in the
	 * forwards direction or a PASS OUT in the opposite direction.  This
	 * is because NATs are processed on the output side.
	 *
	 * In both situations we need to do a reverse addr/port test because
	 * the PASS IN or PASS OUT only establishes if it doesn't match the
	 * established RDR state in the forwards direction.  The direction
	 * flag has to be ignored (it will be one way for a PASS IN and the
	 * other way for a PASS OUT).
	 *
	 * pf_global_statetbl_lock will be locked shared when testing and
	 * not entering into the global state table.
	 */
	if (cpu != ncpus &&
	    (cur = RB_FIND(pf_state_rtree,
			   (struct pf_state_rtree *)&pf_statetbl[ncpus],
			   sk)) != NULL) {
		TAILQ_FOREACH(si, &cur->states, entry) {
			/*
			 * NOTE: We must ignore direction mismatches.
			 */
			if (si->s->kif == s->kif) {
				s->state_flags |= PFSTATE_HALF_DUPLEX |
						  PFSTATE_SLOPPY;
				if (pf_status.debug >= PF_DEBUG_MISC) {
					kprintf(
					    "pf: %s key attach collision "
					    "on %s: ",
					    (idx == PF_SK_WIRE) ?
					    "wire" : "stack",
					    s->kif->pfik_name);
					pf_print_state_parts(s,
					    (idx == PF_SK_WIRE) ? sk : NULL,
					    (idx == PF_SK_STACK) ? sk : NULL);
					kprintf("\n");
				}
				break;
			}
		}
	}

	/*
	 * Enter into either the per-cpu or the global state table.
	 *
	 * pf_global_statetbl_lock will be locked exclusively when entering
	 * into the global state table.
	 */
	if ((cur = RB_INSERT(pf_state_tree, &pf_statetbl[cpu], sk)) != NULL) {
		/* key exists. check for same kif, if none, add to key */
		TAILQ_FOREACH(si, &cur->states, entry) {
			if (si->s->kif == s->kif &&
			    si->s->direction == s->direction) {
				if (pf_status.debug >= PF_DEBUG_MISC) {
					kprintf(
					    "pf: %s key attach failed on %s: ",
					    (idx == PF_SK_WIRE) ?
					    "wire" : "stack",
					    s->kif->pfik_name);
					pf_print_state_parts(s,
					    (idx == PF_SK_WIRE) ? sk : NULL,
					    (idx == PF_SK_STACK) ? sk : NULL);
					kprintf("\n");
				}
				kfree(sk, M_PFSTATEKEYPL);
				error = -1;
				goto failed;	/* collision! */
			}
		}
		kfree(sk, M_PFSTATEKEYPL);

		s->key[idx] = cur;
	} else {
		s->key[idx] = sk;
	}

	if ((si = kmalloc(sizeof(struct pf_state_item),
			  M_PFSTATEITEMPL, M_NOWAIT)) == NULL) {
		pf_state_key_detach(s, idx);
		error = -1;
		goto failed;	/* collision! */
	}
	si->s = s;

	/* list is sorted, if-bound states before floating */
	if (s->kif == pfi_all)
		TAILQ_INSERT_TAIL(&s->key[idx]->states, si, entry);
	else
		TAILQ_INSERT_HEAD(&s->key[idx]->states, si, entry);

	error = 0;
failed:
	lockmgr(&pf_global_statetbl_lock, LK_RELEASE);
	return error;
}

/*
 * NOTE: Can only be called indirectly via the purge thread with pf_token
 *	 exclusively locked.
 */
void
pf_detach_state(struct pf_state *s)
{
	if (s->key[PF_SK_WIRE] == s->key[PF_SK_STACK])
		s->key[PF_SK_WIRE] = NULL;

	if (s->key[PF_SK_STACK] != NULL)
		pf_state_key_detach(s, PF_SK_STACK);

	if (s->key[PF_SK_WIRE] != NULL)
		pf_state_key_detach(s, PF_SK_WIRE);
}

/*
 * NOTE: Can only be called indirectly via the purge thread with pf_token
 *	 exclusively locked.
 */
void
pf_state_key_detach(struct pf_state *s, int idx)
{
	struct pf_state_item	*si;
	int cpu;

	/*
	 * PFSTATE_STACK_GLOBAL is set for translations when the translated
	 * address/port is not localized to the same cpu that the untranslated
	 * address/port is on.  The wire pf_state_key is managed on the global
	 * statetbl tree for this case.
	 */
	if (s->state_flags & PFSTATE_STACK_GLOBAL) {
		cpu = ncpus;
		lockmgr(&pf_global_statetbl_lock, LK_EXCLUSIVE);
	} else {
		cpu = mycpu->gd_cpuid;
	}

	si = TAILQ_FIRST(&s->key[idx]->states);
	while (si && si->s != s)
		si = TAILQ_NEXT(si, entry);

	if (si) {
		TAILQ_REMOVE(&s->key[idx]->states, si, entry);
		kfree(si, M_PFSTATEITEMPL);
	}

	if (TAILQ_EMPTY(&s->key[idx]->states)) {
		RB_REMOVE(pf_state_tree, &pf_statetbl[cpu], s->key[idx]);
		if (s->key[idx]->reverse)
			s->key[idx]->reverse->reverse = NULL;
		if (s->key[idx]->inp)
			s->key[idx]->inp->inp_pf_sk = NULL;
		kfree(s->key[idx], M_PFSTATEKEYPL);
	}
	s->key[idx] = NULL;

	if (s->state_flags & PFSTATE_STACK_GLOBAL)
		lockmgr(&pf_global_statetbl_lock, LK_RELEASE);
}

struct pf_state_key *
pf_alloc_state_key(int pool_flags)
{
	struct pf_state_key	*sk;

	sk = kmalloc(sizeof(struct pf_state_key), M_PFSTATEKEYPL, pool_flags);
	if (sk) {
		TAILQ_INIT(&sk->states);
	}
	return (sk);
}

int
pf_state_key_setup(struct pf_pdesc *pd, struct pf_rule *nr,
	struct pf_state_key **skw, struct pf_state_key **sks,
	struct pf_state_key **skp, struct pf_state_key **nkp,
	struct pf_addr *saddr, struct pf_addr *daddr,
	u_int16_t sport, u_int16_t dport)
{
	KKASSERT((*skp == NULL && *nkp == NULL));

	if ((*skp = pf_alloc_state_key(M_NOWAIT | M_ZERO)) == NULL)
		return (ENOMEM);

	PF_ACPY(&(*skp)->addr[pd->sidx], saddr, pd->af);
	PF_ACPY(&(*skp)->addr[pd->didx], daddr, pd->af);
	(*skp)->port[pd->sidx] = sport;
	(*skp)->port[pd->didx] = dport;
	(*skp)->proto = pd->proto;
	(*skp)->af = pd->af;

	if (nr != NULL) {
		if ((*nkp = pf_alloc_state_key(M_NOWAIT | M_ZERO)) == NULL)
			return (ENOMEM); /* caller must handle cleanup */

		/* XXX maybe just bcopy and TAILQ_INIT(&(*nkp)->states) */
		PF_ACPY(&(*nkp)->addr[0], &(*skp)->addr[0], pd->af);
		PF_ACPY(&(*nkp)->addr[1], &(*skp)->addr[1], pd->af);
		(*nkp)->port[0] = (*skp)->port[0];
		(*nkp)->port[1] = (*skp)->port[1];
		(*nkp)->proto = pd->proto;
		(*nkp)->af = pd->af;
	} else {
		*nkp = *skp;
	}

	if (pd->dir == PF_IN) {
		*skw = *skp;
		*sks = *nkp;
	} else {
		*sks = *skp;
		*skw = *nkp;
	}
	return (0);
}

/*
 * Insert pf_state with one or two state keys (allowing a reverse path lookup
 * which is used by NAT).  In the NAT case skw is the initiator (?) and
 * sks is the target.
 */
int
pf_state_insert(struct pfi_kif *kif, struct pf_state_key *skw,
		struct pf_state_key *sks, struct pf_state *s)
{
	int cpu = mycpu->gd_cpuid;

	s->kif = kif;
	s->cpuid = cpu;

	if (skw == sks) {
		if (pf_state_key_attach(skw, s, PF_SK_WIRE))
			return (-1);
		s->key[PF_SK_STACK] = s->key[PF_SK_WIRE];
	} else {
		/*
		skw->reverse = sks;
		sks->reverse = skw;
		*/
		if (pf_state_key_attach(skw, s, PF_SK_WIRE)) {
			kfree(sks, M_PFSTATEKEYPL);
			return (-1);
		}
		if (pf_state_key_attach(sks, s, PF_SK_STACK)) {
			pf_state_key_detach(s, PF_SK_WIRE);
			return (-1);
		}
	}

	if (s->id == 0 && s->creatorid == 0) {
		u_int64_t sid;

		sid = atomic_fetchadd_long(&pf_status.stateid, 1);
		s->id = htobe64(sid);
		s->creatorid = pf_status.hostid;
	}

	/*
	 * Calculate hash code for altq
	 */
	s->hash = crc32(s->key[PF_SK_WIRE], PF_STATE_KEY_HASH_LENGTH);

	if (RB_INSERT(pf_state_tree_id, &tree_id[cpu], s) != NULL) {
		if (pf_status.debug >= PF_DEBUG_MISC) {
			kprintf("pf: state insert failed: "
			    "id: %016jx creatorid: %08x",
			      (uintmax_t)be64toh(s->id), ntohl(s->creatorid));
			if (s->sync_flags & PFSTATE_FROMSYNC)
				kprintf(" (from sync)");
			kprintf("\n");
		}
		pf_detach_state(s);
		return (-1);
	}
	TAILQ_INSERT_TAIL(&state_list[cpu], s, entry_list);
	PF_INC_FCOUNTER(FCNT_STATE_INSERT);
	atomic_add_int(&pf_status.states, 1);
	pfi_kif_ref(kif, PFI_KIF_REF_STATE);
	pfsync_insert_state(s);
	return (0);
}

struct pf_state *
pf_find_state_byid(struct pf_state_cmp *key)
{
	int cpu = mycpu->gd_cpuid;

	PF_INC_FCOUNTER(FCNT_STATE_SEARCH);

	return (RB_FIND(pf_state_tree_id, &tree_id[cpu],
			(struct pf_state *)key));
}

/*
 * WARNING! May return a state structure that was localized to another cpu,
 *	    destruction is typically protected by the callers pf_token.
 *	    The element can only be destroyed
 */
struct pf_state *
pf_find_state(struct pfi_kif *kif, struct pf_state_key_cmp *key, u_int dir,
	      struct mbuf *m)
{
	struct pf_state_key	*skey = (void *)key;
	struct pf_state_key	*sk;
	struct pf_state_item	*si;
	struct pf_state *s;
	int cpu = mycpu->gd_cpuid;
	int globalstl = 0;

	PF_INC_FCOUNTER(FCNT_STATE_SEARCH);

	if (dir == PF_OUT && m->m_pkthdr.pf.statekey &&
	    ((struct pf_state_key *)m->m_pkthdr.pf.statekey)->reverse) {
		sk = ((struct pf_state_key *)m->m_pkthdr.pf.statekey)->reverse;
	} else {
		sk = RB_FIND(pf_state_tree, &pf_statetbl[cpu], skey);
		if (sk == NULL) {
			lockmgr(&pf_global_statetbl_lock, LK_SHARED);
			sk = RB_FIND(pf_state_tree, &pf_statetbl[ncpus], skey);
			if (sk == NULL) {
				lockmgr(&pf_global_statetbl_lock, LK_RELEASE);
				return (NULL);
			}
			globalstl = 1;
		}
		if (dir == PF_OUT && m->m_pkthdr.pf.statekey) {
			((struct pf_state_key *)
			    m->m_pkthdr.pf.statekey)->reverse = sk;
			sk->reverse = m->m_pkthdr.pf.statekey;
		}
	}
	if (dir == PF_OUT)
		m->m_pkthdr.pf.statekey = NULL;

	/* list is sorted, if-bound states before floating ones */
	TAILQ_FOREACH(si, &sk->states, entry) {
		if ((si->s->kif == pfi_all || si->s->kif == kif) &&
		    sk == (dir == PF_IN ? si->s->key[PF_SK_WIRE] :
					  si->s->key[PF_SK_STACK])) {
			break;
		}
	}

	/*
	 * Extract state before potentially releasing the global statetbl
	 * lock.  Ignore the state if the create is still in-progress as
	 * it can be deleted out from under us by the owning localized cpu.
	 * However, if CREATEINPROG is not set, state can only be deleted
	 * by the purge thread which we are protected from via our shared
	 * pf_token.
	 */
	if (si) {
		s = si->s;
		if (s && (s->state_flags & PFSTATE_CREATEINPROG))
			s = NULL;
	} else {
		s = NULL;
	}
	if (globalstl)
		lockmgr(&pf_global_statetbl_lock, LK_RELEASE);
	return s;
}

/*
 * WARNING! May return a state structure that was localized to another cpu,
 *	    destruction is typically protected by the callers pf_token.
 */
struct pf_state *
pf_find_state_all(struct pf_state_key_cmp *key, u_int dir, int *more)
{
	struct pf_state_key	*skey = (void *)key;
	struct pf_state_key	*sk;
	struct pf_state_item	*si, *ret = NULL;
	struct pf_state		*s;
	int cpu = mycpu->gd_cpuid;
	int globalstl = 0;

	PF_INC_FCOUNTER(FCNT_STATE_SEARCH);

	sk = RB_FIND(pf_state_tree, &pf_statetbl[cpu], skey);
	if (sk == NULL) {
		lockmgr(&pf_global_statetbl_lock, LK_SHARED);
		sk = RB_FIND(pf_state_tree, &pf_statetbl[ncpus], skey);
		globalstl = 1;
	}
	if (sk != NULL) {
		TAILQ_FOREACH(si, &sk->states, entry)
			if (dir == PF_INOUT ||
			    (sk == (dir == PF_IN ? si->s->key[PF_SK_WIRE] :
			    si->s->key[PF_SK_STACK]))) {
				if (more == NULL) {
					ret = si;
					break;
				}
				if (ret)
					(*more)++;
				else
					ret = si;
			}
	}

	/*
	 * Extract state before potentially releasing the global statetbl
	 * lock.  Ignore the state if the create is still in-progress as
	 * it can be deleted out from under us by the owning localized cpu.
	 * However, if CREATEINPROG is not set, state can only be deleted
	 * by the purge thread which we are protected from via our shared
	 * pf_token.
	 */
	if (ret) {
		s = ret->s;
		if (s && (s->state_flags & PFSTATE_CREATEINPROG))
			s = NULL;
	} else {
		s = NULL;
	}
	if (globalstl)
		lockmgr(&pf_global_statetbl_lock, LK_RELEASE);
	return s;
}

/* END state table stuff */

void
pf_purge_thread(void *v)
{
	globaldata_t save_gd = mycpu;
	int nloops = 0;
	int locked = 0;
	int nn;
	int endingit;

	for (;;) {
		tsleep(pf_purge_thread, PWAIT, "pftm", 1 * hz);

		endingit = pf_end_threads;

		for (nn = 0; nn < ncpus; ++nn) {
			lwkt_setcpu_self(globaldata_find(nn));

			lwkt_gettoken(&pf_token);
			lockmgr(&pf_consistency_lock, LK_EXCLUSIVE);
			crit_enter();

			/*
			 * process a fraction of the state table every second
			 */
			if(!pf_purge_expired_states(
				1 + (pf_status.states /
				     pf_default_rule.timeout[
					PFTM_INTERVAL]), 0)) {
				pf_purge_expired_states(
					1 + (pf_status.states /
					     pf_default_rule.timeout[
						PFTM_INTERVAL]), 1);
			}

			/*
			 * purge other expired types every PFTM_INTERVAL
			 * seconds
			 */
			if (++nloops >=
			    pf_default_rule.timeout[PFTM_INTERVAL]) {
				pf_purge_expired_fragments();
				if (!pf_purge_expired_src_nodes(locked)) {
					pf_purge_expired_src_nodes(1);
				}
				nloops = 0;
			}

			/*
			 * If terminating the thread, clean everything out
			 * (on all cpus).
			 */
			if (endingit) {
				pf_purge_expired_states(pf_status.states, 0);
				pf_purge_expired_fragments();
				pf_purge_expired_src_nodes(1);
			}

			crit_exit();
			lockmgr(&pf_consistency_lock, LK_RELEASE);
			lwkt_reltoken(&pf_token);
		}
		lwkt_setcpu_self(save_gd);
		if (endingit)
			break;
	}

	/*
	 * Thread termination
	 */
	pf_end_threads++;
	wakeup(pf_purge_thread);
	kthread_exit();
}

u_int32_t
pf_state_expires(const struct pf_state *state)
{
	u_int32_t	timeout;
	u_int32_t	start;
	u_int32_t	end;
	u_int32_t	states;

	/* handle all PFTM_* > PFTM_MAX here */
	if (state->timeout == PFTM_PURGE)
		return (time_second);
	if (state->timeout == PFTM_UNTIL_PACKET)
		return (0);
	KKASSERT(state->timeout != PFTM_UNLINKED);
	KKASSERT(state->timeout < PFTM_MAX);
	timeout = state->rule.ptr->timeout[state->timeout];
	if (!timeout)
		timeout = pf_default_rule.timeout[state->timeout];
	start = state->rule.ptr->timeout[PFTM_ADAPTIVE_START];
	if (start) {
		end = state->rule.ptr->timeout[PFTM_ADAPTIVE_END];
		states = state->rule.ptr->states_cur;
	} else {
		start = pf_default_rule.timeout[PFTM_ADAPTIVE_START];
		end = pf_default_rule.timeout[PFTM_ADAPTIVE_END];
		states = pf_status.states;
	}

	/*
	 * If the number of states exceeds allowed values, adaptively
	 * timeout the state more quickly.  This can be very dangerous
	 * to legitimate connections, however, so defray the timeout
	 * based on the packet count.
	 *
	 * Retain from 0-100% based on number of states.
	 *
	 * Recover up to 50% of the lost portion if there was
	 * packet traffic (100 pkts = 50%).
	 */
	if (end && states > start && start < end) {
		u_int32_t n;			/* timeout retention 0-100% */
		u_int64_t pkts;
#if 0
		static struct krate boorate = { .freq = 1 };
#endif

		/*
		 * Reduce timeout by n% (0-100)
		 */
		n = (states - start) * 100 / (end - start);
		if (n > 100)
			n = 0;
		else
			n = 100 - n;

		/*
		 * But claw back some of the reduction based on packet
		 * count associated with the state.
		 */
		pkts = state->packets[0] + state->packets[1];
		if (pkts > 100)
			pkts = 100;
#if 0
		krateprintf(&boorate, "timeout %-4u n=%u pkts=%-3lu -> %lu\n",
			timeout, n, pkts, n + (100 - n) * pkts / 200);
#endif

		n += (100 - n) * pkts / 200;	/* recover by up-to 50% */
		timeout = timeout * n / 100;

	}
	return (state->expire + timeout);
}

/*
 * (called with exclusive pf_token)
 */
int
pf_purge_expired_src_nodes(int waslocked)
{
	struct pf_src_node *cur, *next;
	int locked = waslocked;
	int cpu = mycpu->gd_cpuid;

	for (cur = RB_MIN(pf_src_tree, &tree_src_tracking[cpu]);
	     cur;
	     cur = next) {
		next = RB_NEXT(pf_src_tree, &tree_src_tracking[cpu], cur);

		if (cur->states <= 0 && cur->expire <= time_second) {
			 if (!locked) {
				 lockmgr(&pf_consistency_lock, LK_EXCLUSIVE);
			 	 next = RB_NEXT(pf_src_tree,
				     &tree_src_tracking[cpu], cur);
				 locked = 1;
			 }
			 if (cur->rule.ptr != NULL) {
				/*
				 * decrements in rule should be ok, token is
				 * held exclusively in this code path.
				 */
				 atomic_add_int(&cur->rule.ptr->src_nodes, -1);
				 if (cur->rule.ptr->states_cur <= 0 &&
				     cur->rule.ptr->max_src_nodes <= 0)
					 pf_rm_rule(NULL, cur->rule.ptr);
			 }
			 RB_REMOVE(pf_src_tree, &tree_src_tracking[cpu], cur);
			 PF_INC_SCOUNTER(SCNT_SRC_NODE_REMOVALS);
			 atomic_add_int(&pf_status.src_nodes, -1);
			 kfree(cur, M_PFSRCTREEPL);
		}
	}
	if (locked && !waslocked)
		lockmgr(&pf_consistency_lock, LK_RELEASE);
	return(1);
}

void
pf_src_tree_remove_state(struct pf_state *s)
{
	u_int32_t timeout;

	if (s->src_node != NULL) {
		if (s->src.tcp_est)
			atomic_add_int(&s->src_node->conn, -1);
		if (--s->src_node->states <= 0) {
			timeout = s->rule.ptr->timeout[PFTM_SRC_NODE];
			if (!timeout) {
				timeout =
				    pf_default_rule.timeout[PFTM_SRC_NODE];
			}
			s->src_node->expire = time_second + timeout;
		}
	}
	if (s->nat_src_node != s->src_node && s->nat_src_node != NULL) {
		if (--s->nat_src_node->states <= 0) {
			timeout = s->rule.ptr->timeout[PFTM_SRC_NODE];
			if (!timeout)
				timeout =
				    pf_default_rule.timeout[PFTM_SRC_NODE];
			s->nat_src_node->expire = time_second + timeout;
		}
	}
	s->src_node = s->nat_src_node = NULL;
}

/* callers should be at crit_enter() */
void
pf_unlink_state(struct pf_state *cur)
{
	int cpu = mycpu->gd_cpuid;

	if (cur->src.state == PF_TCPS_PROXY_DST) {
		/* XXX wire key the right one? */
		pf_send_tcp(cur->rule.ptr, cur->key[PF_SK_WIRE]->af,
		    &cur->key[PF_SK_WIRE]->addr[1],
		    &cur->key[PF_SK_WIRE]->addr[0],
		    cur->key[PF_SK_WIRE]->port[1],
		    cur->key[PF_SK_WIRE]->port[0],
		    cur->src.seqhi, cur->src.seqlo + 1,
		    TH_RST|TH_ACK, 0, 0, 0, 1, cur->tag, NULL, NULL);
	}
	RB_REMOVE(pf_state_tree_id, &tree_id[cpu], cur);
	if (cur->creatorid == pf_status.hostid)
		pfsync_delete_state(cur);
	cur->timeout = PFTM_UNLINKED;
	pf_src_tree_remove_state(cur);
	pf_detach_state(cur);
}

/*
 * callers should be at crit_enter() and hold pf_consistency_lock exclusively.
 * pf_token must also be held exclusively.
 */
void
pf_free_state(struct pf_state *cur)
{
	int cpu = mycpu->gd_cpuid;

	KKASSERT(cur->cpuid == cpu);

	if (pfsyncif != NULL &&
	    (pfsyncif->sc_bulk_send_next == cur ||
	    pfsyncif->sc_bulk_terminator == cur))
		return;
	KKASSERT(cur->timeout == PFTM_UNLINKED);
	/*
	 * decrements in rule should be ok, token is
	 * held exclusively in this code path.
	 */
	if (--cur->rule.ptr->states_cur <= 0 &&
	    cur->rule.ptr->src_nodes <= 0)
		pf_rm_rule(NULL, cur->rule.ptr);
	if (cur->nat_rule.ptr != NULL) {
		if (--cur->nat_rule.ptr->states_cur <= 0 &&
			cur->nat_rule.ptr->src_nodes <= 0) {
			pf_rm_rule(NULL, cur->nat_rule.ptr);
		}
	}
	if (cur->anchor.ptr != NULL) {
		if (--cur->anchor.ptr->states_cur <= 0)
			pf_rm_rule(NULL, cur->anchor.ptr);
	}
	pf_normalize_tcp_cleanup(cur);
	pfi_kif_unref(cur->kif, PFI_KIF_REF_STATE);

	/*
	 * We may be freeing pf_purge_expired_states()'s saved scan entry,
	 * adjust it if necessary.
	 */
	if (purge_cur[cpu] == cur) {
		kprintf("PURGE CONFLICT\n");
		purge_cur[cpu] = TAILQ_NEXT(purge_cur[cpu], entry_list);
	}
	TAILQ_REMOVE(&state_list[cpu], cur, entry_list);
	if (cur->tag)
		pf_tag_unref(cur->tag);
	kfree(cur, M_PFSTATEPL);
	PF_INC_FCOUNTER(FCNT_STATE_REMOVALS);
	atomic_add_int(&pf_status.states, -1);
}

int
pf_purge_expired_states(u_int32_t maxcheck, int waslocked)
{
	struct pf_state		*cur;
	int locked = waslocked;
	int cpu = mycpu->gd_cpuid;

	while (maxcheck--) {
		/*
		 * Wrap to start of list when we hit the end
		 */
		cur = purge_cur[cpu];
		if (cur == NULL) {
			cur = TAILQ_FIRST(&state_list[cpu]);
			if (cur == NULL)
				break;	/* list empty */
		}

		/*
		 * Setup next (purge_cur) while we process this one.  If
		 * we block and something else deletes purge_cur,
		 * pf_free_state() will adjust it further ahead.
		 */
		purge_cur[cpu] = TAILQ_NEXT(cur, entry_list);

		if (cur->timeout == PFTM_UNLINKED) {
			/* free unlinked state */
			if (! locked) {
				lockmgr(&pf_consistency_lock, LK_EXCLUSIVE);
				locked = 1;
			}
			pf_free_state(cur);
		} else if (pf_state_expires(cur) <= time_second) {
			/* unlink and free expired state */
			pf_unlink_state(cur);
			if (! locked) {
				if (!lockmgr(&pf_consistency_lock, LK_EXCLUSIVE))
					return (0);
				locked = 1;
			}
			pf_free_state(cur);
		}
	}

	if (locked)
		lockmgr(&pf_consistency_lock, LK_RELEASE);
	return (1);
}

int
pf_tbladdr_setup(struct pf_ruleset *rs, struct pf_addr_wrap *aw)
{
	if (aw->type != PF_ADDR_TABLE)
		return (0);
	if ((aw->p.tbl = pfr_attach_table(rs, aw->v.tblname)) == NULL)
		return (1);
	return (0);
}

void
pf_tbladdr_remove(struct pf_addr_wrap *aw)
{
	if (aw->type != PF_ADDR_TABLE || aw->p.tbl == NULL)
		return;
	pfr_detach_table(aw->p.tbl);
	aw->p.tbl = NULL;
}

void
pf_tbladdr_copyout(struct pf_addr_wrap *aw)
{
	struct pfr_ktable *kt = aw->p.tbl;

	if (aw->type != PF_ADDR_TABLE || kt == NULL)
		return;
	if (!(kt->pfrkt_flags & PFR_TFLAG_ACTIVE) && kt->pfrkt_root != NULL)
		kt = kt->pfrkt_root;
	aw->p.tbl = NULL;
	aw->p.tblcnt = (kt->pfrkt_flags & PFR_TFLAG_ACTIVE) ?
		kt->pfrkt_cnt : -1;
}

void
pf_print_host(struct pf_addr *addr, u_int16_t p, sa_family_t af)
{
	switch (af) {
#ifdef INET
	case AF_INET: {
		u_int32_t a = ntohl(addr->addr32[0]);
		kprintf("%u.%u.%u.%u", (a>>24)&255, (a>>16)&255,
		    (a>>8)&255, a&255);
		if (p) {
			p = ntohs(p);
			kprintf(":%u", p);
		}
		break;
	}
#endif /* INET */
#ifdef INET6
	case AF_INET6: {
		u_int16_t b;
		u_int8_t i, curstart, curend, maxstart, maxend;
		curstart = curend = maxstart = maxend = 255;
		for (i = 0; i < 8; i++) {
			if (!addr->addr16[i]) {
				if (curstart == 255)
					curstart = i;
				curend = i;
			} else {
				if ((curend - curstart) >
				    (maxend - maxstart)) {
					maxstart = curstart;
					maxend = curend;
				}
				curstart = curend = 255;
			}
		}
		if ((curend - curstart) >
		    (maxend - maxstart)) {
			maxstart = curstart;
			maxend = curend;
		}
		for (i = 0; i < 8; i++) {
			if (i >= maxstart && i <= maxend) {
				if (i == 0)
					kprintf(":");
				if (i == maxend)
					kprintf(":");
			} else {
				b = ntohs(addr->addr16[i]);
				kprintf("%x", b);
				if (i < 7)
					kprintf(":");
			}
		}
		if (p) {
			p = ntohs(p);
			kprintf("[%u]", p);
		}
		break;
	}
#endif /* INET6 */
	}
}

void
pf_print_state(struct pf_state *s)
{
	pf_print_state_parts(s, NULL, NULL);
}

void
pf_print_state_parts(struct pf_state *s,
    struct pf_state_key *skwp, struct pf_state_key *sksp)
{
	struct pf_state_key *skw, *sks;
	u_int8_t proto, dir;

	/* Do our best to fill these, but they're skipped if NULL */
	skw = skwp ? skwp : (s ? s->key[PF_SK_WIRE] : NULL);
	sks = sksp ? sksp : (s ? s->key[PF_SK_STACK] : NULL);
	proto = skw ? skw->proto : (sks ? sks->proto : 0);
	dir = s ? s->direction : 0;

	switch (proto) {
	case IPPROTO_TCP:
		kprintf("TCP ");
		break;
	case IPPROTO_UDP:
		kprintf("UDP ");
		break;
	case IPPROTO_ICMP:
		kprintf("ICMP ");
		break;
	case IPPROTO_ICMPV6:
		kprintf("ICMPV6 ");
		break;
	default:
		kprintf("%u ", skw->proto);
		break;
	}
	switch (dir) {
	case PF_IN:
		kprintf(" in");
		break;
	case PF_OUT:
		kprintf(" out");
		break;
	}
	if (skw) {
		kprintf(" wire: ");
		pf_print_host(&skw->addr[0], skw->port[0], skw->af);
		kprintf(" ");
		pf_print_host(&skw->addr[1], skw->port[1], skw->af);
	}
	if (sks) {
		kprintf(" stack: ");
		if (sks != skw) {
			pf_print_host(&sks->addr[0], sks->port[0], sks->af);
			kprintf(" ");
			pf_print_host(&sks->addr[1], sks->port[1], sks->af);
		} else
			kprintf("-");
	}
	if (s) {
		if (proto == IPPROTO_TCP) {
			kprintf(" [lo=%u high=%u win=%u modulator=%u",
			    s->src.seqlo, s->src.seqhi,
			    s->src.max_win, s->src.seqdiff);
			if (s->src.wscale && s->dst.wscale)
				kprintf(" wscale=%u",
				    s->src.wscale & PF_WSCALE_MASK);
			kprintf("]");
			kprintf(" [lo=%u high=%u win=%u modulator=%u",
			    s->dst.seqlo, s->dst.seqhi,
			    s->dst.max_win, s->dst.seqdiff);
			if (s->src.wscale && s->dst.wscale)
				kprintf(" wscale=%u",
				s->dst.wscale & PF_WSCALE_MASK);
			kprintf("]");
		}
		kprintf(" %u:%u", s->src.state, s->dst.state);
	}
}

void
pf_print_flags(u_int8_t f)
{
	if (f)
		kprintf(" ");
	if (f & TH_FIN)
		kprintf("F");
	if (f & TH_SYN)
		kprintf("S");
	if (f & TH_RST)
		kprintf("R");
	if (f & TH_PUSH)
		kprintf("P");
	if (f & TH_ACK)
		kprintf("A");
	if (f & TH_URG)
		kprintf("U");
	if (f & TH_ECE)
		kprintf("E");
	if (f & TH_CWR)
		kprintf("W");
}

#define	PF_SET_SKIP_STEPS(i)					\
	do {							\
		while (head[i] != cur) {			\
			head[i]->skip[i].ptr = cur;		\
			head[i] = TAILQ_NEXT(head[i], entries);	\
		}						\
	} while (0)

void
pf_calc_skip_steps(struct pf_rulequeue *rules)
{
	struct pf_rule *cur, *prev, *head[PF_SKIP_COUNT];
	int i;

	cur = TAILQ_FIRST(rules);
	prev = cur;
	for (i = 0; i < PF_SKIP_COUNT; ++i)
		head[i] = cur;
	while (cur != NULL) {

		if (cur->kif != prev->kif || cur->ifnot != prev->ifnot)
			PF_SET_SKIP_STEPS(PF_SKIP_IFP);
		if (cur->direction != prev->direction)
			PF_SET_SKIP_STEPS(PF_SKIP_DIR);
		if (cur->af != prev->af)
			PF_SET_SKIP_STEPS(PF_SKIP_AF);
		if (cur->proto != prev->proto)
			PF_SET_SKIP_STEPS(PF_SKIP_PROTO);
		if (cur->src.neg != prev->src.neg ||
		    pf_addr_wrap_neq(&cur->src.addr, &prev->src.addr))
			PF_SET_SKIP_STEPS(PF_SKIP_SRC_ADDR);
		if (cur->src.port[0] != prev->src.port[0] ||
		    cur->src.port[1] != prev->src.port[1] ||
		    cur->src.port_op != prev->src.port_op)
			PF_SET_SKIP_STEPS(PF_SKIP_SRC_PORT);
		if (cur->dst.neg != prev->dst.neg ||
		    pf_addr_wrap_neq(&cur->dst.addr, &prev->dst.addr))
			PF_SET_SKIP_STEPS(PF_SKIP_DST_ADDR);
		if (cur->dst.port[0] != prev->dst.port[0] ||
		    cur->dst.port[1] != prev->dst.port[1] ||
		    cur->dst.port_op != prev->dst.port_op)
			PF_SET_SKIP_STEPS(PF_SKIP_DST_PORT);

		prev = cur;
		cur = TAILQ_NEXT(cur, entries);
	}
	for (i = 0; i < PF_SKIP_COUNT; ++i)
		PF_SET_SKIP_STEPS(i);
}

int
pf_addr_wrap_neq(struct pf_addr_wrap *aw1, struct pf_addr_wrap *aw2)
{
	if (aw1->type != aw2->type)
		return (1);
	switch (aw1->type) {
	case PF_ADDR_ADDRMASK:
	case PF_ADDR_RANGE:
		if (PF_ANEQ(&aw1->v.a.addr, &aw2->v.a.addr, AF_INET6))
			return (1);
		if (PF_ANEQ(&aw1->v.a.mask, &aw2->v.a.mask, AF_INET6))
			return (1);
		return (0);
	case PF_ADDR_DYNIFTL:
		return (aw1->p.dyn->pfid_kt != aw2->p.dyn->pfid_kt);
	case PF_ADDR_NOROUTE:
	case PF_ADDR_URPFFAILED:
		return (0);
	case PF_ADDR_TABLE:
		return (aw1->p.tbl != aw2->p.tbl);
	case PF_ADDR_RTLABEL:
		return (aw1->v.rtlabel != aw2->v.rtlabel);
	default:
		kprintf("invalid address type: %d\n", aw1->type);
		return (1);
	}
}

u_int16_t
pf_cksum_fixup(u_int16_t cksum, u_int16_t old, u_int16_t new, u_int8_t udp)
{
	u_int32_t	l;

	if (udp && !cksum)
		return (0x0000);
	l = cksum + old - new;
	l = (l >> 16) + (l & 65535);
	l = l & 65535;
	if (udp && !l)
		return (0xFFFF);
	return (l);
}

void
pf_change_ap(struct pf_addr *a, u_int16_t *p, u_int16_t *ic, u_int16_t *pc,
    struct pf_addr *an, u_int16_t pn, u_int8_t u, sa_family_t af)
{
	struct pf_addr	ao;
	u_int16_t	po = *p;

	PF_ACPY(&ao, a, af);
	PF_ACPY(a, an, af);

	*p = pn;

	switch (af) {
#ifdef INET
	case AF_INET:
		*ic = pf_cksum_fixup(pf_cksum_fixup(*ic,
		    ao.addr16[0], an->addr16[0], 0),
		    ao.addr16[1], an->addr16[1], 0);
		*p = pn;
		*pc = pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(*pc,
		    ao.addr16[0], an->addr16[0], u),
		    ao.addr16[1], an->addr16[1], u),
		    po, pn, u);
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		*pc = pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
		    pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
		    pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(*pc,
		    ao.addr16[0], an->addr16[0], u),
		    ao.addr16[1], an->addr16[1], u),
		    ao.addr16[2], an->addr16[2], u),
		    ao.addr16[3], an->addr16[3], u),
		    ao.addr16[4], an->addr16[4], u),
		    ao.addr16[5], an->addr16[5], u),
		    ao.addr16[6], an->addr16[6], u),
		    ao.addr16[7], an->addr16[7], u),
		    po, pn, u);
		break;
#endif /* INET6 */
	}
}


/* Changes a u_int32_t.  Uses a void * so there are no align restrictions */
void
pf_change_a(void *a, u_int16_t *c, u_int32_t an, u_int8_t u)
{
	u_int32_t	ao;

	memcpy(&ao, a, sizeof(ao));
	memcpy(a, &an, sizeof(u_int32_t));
	*c = pf_cksum_fixup(pf_cksum_fixup(*c, ao / 65536, an / 65536, u),
	    ao % 65536, an % 65536, u);
}

#ifdef INET6
void
pf_change_a6(struct pf_addr *a, u_int16_t *c, struct pf_addr *an, u_int8_t u)
{
	struct pf_addr	ao;

	PF_ACPY(&ao, a, AF_INET6);
	PF_ACPY(a, an, AF_INET6);

	*c = pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
	    pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
	    pf_cksum_fixup(pf_cksum_fixup(*c,
	    ao.addr16[0], an->addr16[0], u),
	    ao.addr16[1], an->addr16[1], u),
	    ao.addr16[2], an->addr16[2], u),
	    ao.addr16[3], an->addr16[3], u),
	    ao.addr16[4], an->addr16[4], u),
	    ao.addr16[5], an->addr16[5], u),
	    ao.addr16[6], an->addr16[6], u),
	    ao.addr16[7], an->addr16[7], u);
}
#endif /* INET6 */

void
pf_change_icmp(struct pf_addr *ia, u_int16_t *ip, struct pf_addr *oa,
    struct pf_addr *na, u_int16_t np, u_int16_t *pc, u_int16_t *h2c,
    u_int16_t *ic, u_int16_t *hc, u_int8_t u, sa_family_t af)
{
	struct pf_addr	oia, ooa;

	PF_ACPY(&oia, ia, af);
	if (oa)
		PF_ACPY(&ooa, oa, af);

	/* Change inner protocol port, fix inner protocol checksum. */
	if (ip != NULL) {
		u_int16_t	oip = *ip;
		u_int32_t	opc = 0;

		if (pc != NULL)
			opc = *pc;
		*ip = np;
		if (pc != NULL)
			*pc = pf_cksum_fixup(*pc, oip, *ip, u);
		*ic = pf_cksum_fixup(*ic, oip, *ip, 0);
		if (pc != NULL)
			*ic = pf_cksum_fixup(*ic, opc, *pc, 0);
	}
	/* Change inner ip address, fix inner ip and icmp checksums. */
	PF_ACPY(ia, na, af);
	switch (af) {
#ifdef INET
	case AF_INET: {
		u_int32_t	 oh2c = *h2c;

		*h2c = pf_cksum_fixup(pf_cksum_fixup(*h2c,
		    oia.addr16[0], ia->addr16[0], 0),
		    oia.addr16[1], ia->addr16[1], 0);
		*ic = pf_cksum_fixup(pf_cksum_fixup(*ic,
		    oia.addr16[0], ia->addr16[0], 0),
		    oia.addr16[1], ia->addr16[1], 0);
		*ic = pf_cksum_fixup(*ic, oh2c, *h2c, 0);
		break;
	}
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		*ic = pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
		    pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
		    pf_cksum_fixup(pf_cksum_fixup(*ic,
		    oia.addr16[0], ia->addr16[0], u),
		    oia.addr16[1], ia->addr16[1], u),
		    oia.addr16[2], ia->addr16[2], u),
		    oia.addr16[3], ia->addr16[3], u),
		    oia.addr16[4], ia->addr16[4], u),
		    oia.addr16[5], ia->addr16[5], u),
		    oia.addr16[6], ia->addr16[6], u),
		    oia.addr16[7], ia->addr16[7], u);
		break;
#endif /* INET6 */
	}
	/* Outer ip address, fix outer ip or icmpv6 checksum, if necessary. */
	if (oa) {
		PF_ACPY(oa, na, af);
		switch (af) {
#ifdef INET
		case AF_INET:
			*hc = pf_cksum_fixup(pf_cksum_fixup(*hc,
			    ooa.addr16[0], oa->addr16[0], 0),
			    ooa.addr16[1], oa->addr16[1], 0);
			break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			*ic = pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
			    pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
			    pf_cksum_fixup(pf_cksum_fixup(*ic,
			    ooa.addr16[0], oa->addr16[0], u),
			    ooa.addr16[1], oa->addr16[1], u),
			    ooa.addr16[2], oa->addr16[2], u),
			    ooa.addr16[3], oa->addr16[3], u),
			    ooa.addr16[4], oa->addr16[4], u),
			    ooa.addr16[5], oa->addr16[5], u),
			    ooa.addr16[6], oa->addr16[6], u),
			    ooa.addr16[7], oa->addr16[7], u);
			break;
#endif /* INET6 */
		}
	}
}


/*
 * Need to modulate the sequence numbers in the TCP SACK option
 * (credits to Krzysztof Pfaff for report and patch)
 */
int
pf_modulate_sack(struct mbuf *m, int off, struct pf_pdesc *pd,
    struct tcphdr *th, struct pf_state_peer *dst)
{
	int hlen = (th->th_off << 2) - sizeof(*th), thoptlen = hlen;
	u_int8_t opts[TCP_MAXOLEN], *opt = opts;
	int copyback = 0, i, olen;
	struct raw_sackblock sack;

#define TCPOLEN_SACKLEN	(TCPOLEN_SACK + 2)
	if (hlen < TCPOLEN_SACKLEN ||
	    !pf_pull_hdr(m, off + sizeof(*th), opts, hlen, NULL, NULL, pd->af))
		return 0;

	while (hlen >= TCPOLEN_SACKLEN) {
		olen = opt[1];
		switch (*opt) {
		case TCPOPT_EOL:	/* FALLTHROUGH */
		case TCPOPT_NOP:
			opt++;
			hlen--;
			break;
		case TCPOPT_SACK:
			if (olen > hlen)
				olen = hlen;
			if (olen >= TCPOLEN_SACKLEN) {
				for (i = 2; i + TCPOLEN_SACK <= olen;
				    i += TCPOLEN_SACK) {
					memcpy(&sack, &opt[i], sizeof(sack));
					pf_change_a(&sack.rblk_start, &th->th_sum,
					    htonl(ntohl(sack.rblk_start) -
					    dst->seqdiff), 0);
					pf_change_a(&sack.rblk_end, &th->th_sum,
					    htonl(ntohl(sack.rblk_end) -
					    dst->seqdiff), 0);
					memcpy(&opt[i], &sack, sizeof(sack));
				}
				copyback = 1;
			}
			/* FALLTHROUGH */
		default:
			if (olen < 2)
				olen = 2;
			hlen -= olen;
			opt += olen;
		}
	}

	if (copyback)
		m_copyback(m, off + sizeof(*th), thoptlen, opts);
	return (copyback);
}

void
pf_send_tcp(const struct pf_rule *r, sa_family_t af,
    const struct pf_addr *saddr, const struct pf_addr *daddr,
    u_int16_t sport, u_int16_t dport, u_int32_t seq, u_int32_t ack,
    u_int8_t flags, u_int16_t win, u_int16_t mss, u_int8_t ttl, int tag,
    u_int16_t rtag, struct ether_header *eh, struct ifnet *ifp)
{
	struct mbuf	*m;
	int		 len = 0, tlen;
#ifdef INET
	struct ip	*h = NULL;
#endif /* INET */
#ifdef INET6
	struct ip6_hdr	*h6 = NULL;
#endif /* INET6 */
	struct tcphdr	*th = NULL;
	char		*opt;

	ASSERT_LWKT_TOKEN_HELD(&pf_token);

	/* maximum segment size tcp option */
	tlen = sizeof(struct tcphdr);
	if (mss)
		tlen += 4;

	switch (af) {
#ifdef INET
	case AF_INET:
		len = sizeof(struct ip) + tlen;
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		len = sizeof(struct ip6_hdr) + tlen;
		break;
#endif /* INET6 */
	}

	/*
	 * Create outgoing mbuf.
	 *
	 * DragonFly doesn't zero the auxillary pkghdr fields, only fw_flags,
	 * so make sure pf.flags is clear.
	 */
	m = m_gethdr(M_NOWAIT, MT_HEADER);
	if (m == NULL) {
		return;
	}
	if (tag)
		m->m_pkthdr.fw_flags |= PF_MBUF_TAGGED;
	m->m_pkthdr.pf.flags = 0;
	m->m_pkthdr.pf.tag = rtag;
	/* XXX Recheck when upgrading to > 4.4 */
	m->m_pkthdr.pf.statekey = NULL;
	if (r != NULL && r->rtableid >= 0)
		m->m_pkthdr.pf.rtableid = r->rtableid;

#ifdef ALTQ
	if (r != NULL && r->qid) {
		m->m_pkthdr.fw_flags |= PF_MBUF_STRUCTURE;
		m->m_pkthdr.pf.qid = r->qid;
		m->m_pkthdr.pf.ecn_af = af;
		m->m_pkthdr.pf.hdr = mtod(m, struct ip *);
	}
#endif /* ALTQ */
	m->m_data += max_linkhdr;
	m->m_pkthdr.len = m->m_len = len;
	m->m_pkthdr.rcvif = NULL;
	bzero(m->m_data, len);
	switch (af) {
#ifdef INET
	case AF_INET:
		h = mtod(m, struct ip *);

		/* IP header fields included in the TCP checksum */
		h->ip_p = IPPROTO_TCP;
		h->ip_len = htons(tlen);
		h->ip_src.s_addr = saddr->v4.s_addr;
		h->ip_dst.s_addr = daddr->v4.s_addr;

		th = (struct tcphdr *)((caddr_t)h + sizeof(struct ip));
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		h6 = mtod(m, struct ip6_hdr *);

		/* IP header fields included in the TCP checksum */
		h6->ip6_nxt = IPPROTO_TCP;
		h6->ip6_plen = htons(tlen);
		memcpy(&h6->ip6_src, &saddr->v6, sizeof(struct in6_addr));
		memcpy(&h6->ip6_dst, &daddr->v6, sizeof(struct in6_addr));

		th = (struct tcphdr *)((caddr_t)h6 + sizeof(struct ip6_hdr));
		break;
#endif /* INET6 */
	}

	/* TCP header */
	th->th_sport = sport;
	th->th_dport = dport;
	th->th_seq = htonl(seq);
	th->th_ack = htonl(ack);
	th->th_off = tlen >> 2;
	th->th_flags = flags;
	th->th_win = htons(win);

	if (mss) {
		opt = (char *)(th + 1);
		opt[0] = TCPOPT_MAXSEG;
		opt[1] = 4;
		mss = htons(mss);
		bcopy((caddr_t)&mss, (caddr_t)(opt + 2), 2);
	}

	switch (af) {
#ifdef INET
	case AF_INET:
		/* TCP checksum */
		th->th_sum = in_cksum(m, len);

		/* Finish the IP header */
		h->ip_v = 4;
		h->ip_hl = sizeof(*h) >> 2;
		h->ip_tos = IPTOS_LOWDELAY;
		h->ip_len = htons(len);
		h->ip_off = path_mtu_discovery ? htons(IP_DF) : 0;
		h->ip_ttl = ttl ? ttl : ip_defttl;
		h->ip_sum = 0;
		if (eh == NULL) {
			lwkt_reltoken(&pf_token);
			ip_output(m, NULL, NULL, 0, NULL, NULL);
			lwkt_gettoken(&pf_token);
		} else {
			struct route		 ro;
			struct rtentry		 rt;
			struct ether_header	*e = (void *)ro.ro_dst.sa_data;

			if (ifp == NULL) {
				m_freem(m);
				return;
			}
			rt.rt_ifp = ifp;
			ro.ro_rt = &rt;
			ro.ro_dst.sa_len = sizeof(ro.ro_dst);
			ro.ro_dst.sa_family = pseudo_AF_HDRCMPLT;
			bcopy(eh->ether_dhost, e->ether_shost, ETHER_ADDR_LEN);
			bcopy(eh->ether_shost, e->ether_dhost, ETHER_ADDR_LEN);
			e->ether_type = eh->ether_type;
			/* XXX_IMPORT: later */
			lwkt_reltoken(&pf_token);
			ip_output(m, NULL, &ro, 0, NULL, NULL);
			lwkt_gettoken(&pf_token);
		}
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		/* TCP checksum */
		th->th_sum = in6_cksum(m, IPPROTO_TCP,
		    sizeof(struct ip6_hdr), tlen);

		h6->ip6_vfc |= IPV6_VERSION;
		h6->ip6_hlim = IPV6_DEFHLIM;

		lwkt_reltoken(&pf_token);
		ip6_output(m, NULL, NULL, 0, NULL, NULL, NULL);
		lwkt_gettoken(&pf_token);
		break;
#endif /* INET6 */
	}
}

void
pf_send_icmp(struct mbuf *m, u_int8_t type, u_int8_t code, sa_family_t af,
    struct pf_rule *r)
{
	struct mbuf	*m0;

	/*
	 * DragonFly doesn't zero the auxillary pkghdr fields, only fw_flags,
	 * so make sure pf.flags is clear.
	 */
	if ((m0 = m_copy(m, 0, M_COPYALL)) == NULL)
		return;

	m0->m_pkthdr.fw_flags |= PF_MBUF_TAGGED;
	m0->m_pkthdr.pf.flags = 0;
	/* XXX Re-Check when Upgrading to > 4.4 */
	m0->m_pkthdr.pf.statekey = NULL;

	if (r->rtableid >= 0)
		m0->m_pkthdr.pf.rtableid = r->rtableid;

#ifdef ALTQ
	if (r->qid) {
		m->m_pkthdr.fw_flags |= PF_MBUF_STRUCTURE;
		m0->m_pkthdr.pf.qid = r->qid;
		m0->m_pkthdr.pf.ecn_af = af;
		m0->m_pkthdr.pf.hdr = mtod(m0, struct ip *);
	}
#endif /* ALTQ */

	switch (af) {
#ifdef INET
	case AF_INET:
		icmp_error(m0, type, code, 0, 0);
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		icmp6_error(m0, type, code, 0);
		break;
#endif /* INET6 */
	}
}

/*
 * Return 1 if the addresses a and b match (with mask m), otherwise return 0.
 * If n is 0, they match if they are equal. If n is != 0, they match if they
 * are different.
 */
int
pf_match_addr(u_int8_t n, struct pf_addr *a, struct pf_addr *m,
    struct pf_addr *b, sa_family_t af)
{
	int	match = 0;

	switch (af) {
#ifdef INET
	case AF_INET:
		if ((a->addr32[0] & m->addr32[0]) ==
		    (b->addr32[0] & m->addr32[0]))
			match++;
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		if (((a->addr32[0] & m->addr32[0]) ==
		     (b->addr32[0] & m->addr32[0])) &&
		    ((a->addr32[1] & m->addr32[1]) ==
		     (b->addr32[1] & m->addr32[1])) &&
		    ((a->addr32[2] & m->addr32[2]) ==
		     (b->addr32[2] & m->addr32[2])) &&
		    ((a->addr32[3] & m->addr32[3]) ==
		     (b->addr32[3] & m->addr32[3])))
			match++;
		break;
#endif /* INET6 */
	}
	if (match) {
		if (n)
			return (0);
		else
			return (1);
	} else {
		if (n)
			return (1);
		else
			return (0);
	}
}

/*
 * Return 1 if b <= a <= e, otherwise return 0.
 */
int
pf_match_addr_range(struct pf_addr *b, struct pf_addr *e,
    struct pf_addr *a, sa_family_t af)
{
	switch (af) {
#ifdef INET
	case AF_INET:
		if ((a->addr32[0] < b->addr32[0]) ||
		    (a->addr32[0] > e->addr32[0]))
			return (0);
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6: {
		int	i;

		/* check a >= b */
		for (i = 0; i < 4; ++i)
			if (a->addr32[i] > b->addr32[i])
				break;
			else if (a->addr32[i] < b->addr32[i])
				return (0);
		/* check a <= e */
		for (i = 0; i < 4; ++i)
			if (a->addr32[i] < e->addr32[i])
				break;
			else if (a->addr32[i] > e->addr32[i])
				return (0);
		break;
	}
#endif /* INET6 */
	}
	return (1);
}

int
pf_match(u_int8_t op, u_int32_t a1, u_int32_t a2, u_int32_t p)
{
	switch (op) {
	case PF_OP_IRG:
		return ((p > a1) && (p < a2));
	case PF_OP_XRG:
		return ((p < a1) || (p > a2));
	case PF_OP_RRG:
		return ((p >= a1) && (p <= a2));
	case PF_OP_EQ:
		return (p == a1);
	case PF_OP_NE:
		return (p != a1);
	case PF_OP_LT:
		return (p < a1);
	case PF_OP_LE:
		return (p <= a1);
	case PF_OP_GT:
		return (p > a1);
	case PF_OP_GE:
		return (p >= a1);
	}
	return (0); /* never reached */
}

int
pf_match_port(u_int8_t op, u_int16_t a1, u_int16_t a2, u_int16_t p)
{
	a1 = ntohs(a1);
	a2 = ntohs(a2);
	p = ntohs(p);
	return (pf_match(op, a1, a2, p));
}

int
pf_match_uid(u_int8_t op, uid_t a1, uid_t a2, uid_t u)
{
	if (u == UID_MAX && op != PF_OP_EQ && op != PF_OP_NE)
		return (0);
	return (pf_match(op, a1, a2, u));
}

int
pf_match_gid(u_int8_t op, gid_t a1, gid_t a2, gid_t g)
{
	if (g == GID_MAX && op != PF_OP_EQ && op != PF_OP_NE)
		return (0);
	return (pf_match(op, a1, a2, g));
}

int
pf_match_tag(struct mbuf *m, struct pf_rule *r, int *tag)
{
	if (*tag == -1)
		*tag = m->m_pkthdr.pf.tag;

	return ((!r->match_tag_not && r->match_tag == *tag) ||
	    (r->match_tag_not && r->match_tag != *tag));
}

int
pf_tag_packet(struct mbuf *m, int tag, int rtableid)
{
	if (tag <= 0 && rtableid < 0)
		return (0);

	if (tag > 0)
		m->m_pkthdr.pf.tag = tag;
	if (rtableid >= 0)
		m->m_pkthdr.pf.rtableid = rtableid;

	return (0);
}

void
pf_step_into_anchor(int *depth, struct pf_ruleset **rs, int n,
    struct pf_rule **r, struct pf_rule **a, int *match)
{
	struct pf_anchor_stackframe	*f;

	(*r)->anchor->match = 0;
	if (match)
		*match = 0;
	if (*depth >= NELEM(pf_anchor_stack)) {
		kprintf("pf_step_into_anchor: stack overflow\n");
		*r = TAILQ_NEXT(*r, entries);
		return;
	} else if (*depth == 0 && a != NULL)
		*a = *r;
	f = pf_anchor_stack + (*depth)++;
	f->rs = *rs;
	f->r = *r;
	if ((*r)->anchor_wildcard) {
		f->parent = &(*r)->anchor->children;
		if ((f->child = RB_MIN(pf_anchor_node, f->parent)) ==
		    NULL) {
			*r = NULL;
			return;
		}
		*rs = &f->child->ruleset;
	} else {
		f->parent = NULL;
		f->child = NULL;
		*rs = &(*r)->anchor->ruleset;
	}
	*r = TAILQ_FIRST((*rs)->rules[n].active.ptr);
}

int
pf_step_out_of_anchor(int *depth, struct pf_ruleset **rs, int n,
    struct pf_rule **r, struct pf_rule **a, int *match)
{
	struct pf_anchor_stackframe	*f;
	int quick = 0;

	do {
		if (*depth <= 0)
			break;
		f = pf_anchor_stack + *depth - 1;
		if (f->parent != NULL && f->child != NULL) {
			if (f->child->match ||
			    (match != NULL && *match)) {
				f->r->anchor->match = 1;
				*match = 0;
			}
			f->child = RB_NEXT(pf_anchor_node, f->parent, f->child);
			if (f->child != NULL) {
				*rs = &f->child->ruleset;
				*r = TAILQ_FIRST((*rs)->rules[n].active.ptr);
				if (*r == NULL)
					continue;
				else
					break;
			}
		}
		(*depth)--;
		if (*depth == 0 && a != NULL)
			*a = NULL;
		*rs = f->rs;
		if (f->r->anchor->match || (match != NULL && *match))
			quick = f->r->quick;
		*r = TAILQ_NEXT(f->r, entries);
	} while (*r == NULL);

	return (quick);
}

#ifdef INET6
void
pf_poolmask(struct pf_addr *naddr, struct pf_addr *raddr,
    struct pf_addr *rmask, struct pf_addr *saddr, sa_family_t af)
{
	switch (af) {
#ifdef INET
	case AF_INET:
		naddr->addr32[0] = (raddr->addr32[0] & rmask->addr32[0]) |
		((rmask->addr32[0] ^ 0xffffffff ) & saddr->addr32[0]);
		break;
#endif /* INET */
	case AF_INET6:
		naddr->addr32[0] = (raddr->addr32[0] & rmask->addr32[0]) |
		((rmask->addr32[0] ^ 0xffffffff ) & saddr->addr32[0]);
		naddr->addr32[1] = (raddr->addr32[1] & rmask->addr32[1]) |
		((rmask->addr32[1] ^ 0xffffffff ) & saddr->addr32[1]);
		naddr->addr32[2] = (raddr->addr32[2] & rmask->addr32[2]) |
		((rmask->addr32[2] ^ 0xffffffff ) & saddr->addr32[2]);
		naddr->addr32[3] = (raddr->addr32[3] & rmask->addr32[3]) |
		((rmask->addr32[3] ^ 0xffffffff ) & saddr->addr32[3]);
		break;
	}
}

void
pf_addr_inc(struct pf_addr *addr, sa_family_t af)
{
	switch (af) {
#ifdef INET
	case AF_INET:
		addr->addr32[0] = htonl(ntohl(addr->addr32[0]) + 1);
		break;
#endif /* INET */
	case AF_INET6:
		if (addr->addr32[3] == 0xffffffff) {
			addr->addr32[3] = 0;
			if (addr->addr32[2] == 0xffffffff) {
				addr->addr32[2] = 0;
				if (addr->addr32[1] == 0xffffffff) {
					addr->addr32[1] = 0;
					addr->addr32[0] =
					    htonl(ntohl(addr->addr32[0]) + 1);
				} else
					addr->addr32[1] =
					    htonl(ntohl(addr->addr32[1]) + 1);
			} else
				addr->addr32[2] =
				    htonl(ntohl(addr->addr32[2]) + 1);
		} else
			addr->addr32[3] =
			    htonl(ntohl(addr->addr32[3]) + 1);
		break;
	}
}
#endif /* INET6 */

#define mix(a,b,c) \
	do {					\
		a -= b; a -= c; a ^= (c >> 13);	\
		b -= c; b -= a; b ^= (a << 8);	\
		c -= a; c -= b; c ^= (b >> 13);	\
		a -= b; a -= c; a ^= (c >> 12);	\
		b -= c; b -= a; b ^= (a << 16);	\
		c -= a; c -= b; c ^= (b >> 5);	\
		a -= b; a -= c; a ^= (c >> 3);	\
		b -= c; b -= a; b ^= (a << 10);	\
		c -= a; c -= b; c ^= (b >> 15);	\
	} while (0)

/*
 * hash function based on bridge_hash in if_bridge.c
 */
void
pf_hash(struct pf_addr *inaddr, struct pf_addr *hash,
    struct pf_poolhashkey *key, sa_family_t af)
{
	u_int32_t	a = 0x9e3779b9, b = 0x9e3779b9, c = key->key32[0];

	switch (af) {
#ifdef INET
	case AF_INET:
		a += inaddr->addr32[0];
		b += key->key32[1];
		mix(a, b, c);
		hash->addr32[0] = c + key->key32[2];
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		a += inaddr->addr32[0];
		b += inaddr->addr32[2];
		mix(a, b, c);
		hash->addr32[0] = c;
		a += inaddr->addr32[1];
		b += inaddr->addr32[3];
		c += key->key32[1];
		mix(a, b, c);
		hash->addr32[1] = c;
		a += inaddr->addr32[2];
		b += inaddr->addr32[1];
		c += key->key32[2];
		mix(a, b, c);
		hash->addr32[2] = c;
		a += inaddr->addr32[3];
		b += inaddr->addr32[0];
		c += key->key32[3];
		mix(a, b, c);
		hash->addr32[3] = c;
		break;
#endif /* INET6 */
	}
}

int
pf_map_addr(sa_family_t af, struct pf_rule *r, struct pf_addr *saddr,
    struct pf_addr *naddr, struct pf_addr *init_addr, struct pf_src_node **sn)
{
	unsigned char		 hash[16];
	struct pf_pool		*rpool = &r->rpool;
	struct pf_pooladdr	*acur = rpool->cur;
	struct pf_pooladdr	*cur;
	struct pf_addr		*raddr;
	struct pf_addr		*rmask;
	struct pf_addr		counter;
	struct pf_src_node	 k;
	int cpu = mycpu->gd_cpuid;
	int tblidx;

	bzero(hash, sizeof(hash));	/* avoid gcc warnings */

	/*
	 * NOTE! rpool->cur and rpool->tblidx can be iterators and thus
	 *	 may represent a SMP race due to the shared nature of the
	 *	 rpool structure.  We allow the race and ensure that updates
	 *	 do not create a fatal condition.
	 */
	cpu_ccfence();
	cur = acur;
	raddr = &cur->addr.v.a.addr;
	rmask = &cur->addr.v.a.mask;

	if (*sn == NULL && r->rpool.opts & PF_POOL_STICKYADDR &&
	    (r->rpool.opts & PF_POOL_TYPEMASK) != PF_POOL_NONE) {
		k.af = af;
		PF_ACPY(&k.addr, saddr, af);
		if (r->rule_flag & PFRULE_RULESRCTRACK ||
		    r->rpool.opts & PF_POOL_STICKYADDR)
			k.rule.ptr = r;
		else
			k.rule.ptr = NULL;
		PF_INC_SCOUNTER(SCNT_SRC_NODE_SEARCH);
		*sn = RB_FIND(pf_src_tree, &tree_src_tracking[cpu], &k);
		if (*sn != NULL && !PF_AZERO(&(*sn)->raddr, af)) {
			PF_ACPY(naddr, &(*sn)->raddr, af);
			if (pf_status.debug >= PF_DEBUG_MISC) {
				kprintf("pf_map_addr: src tracking maps ");
				pf_print_host(&k.addr, 0, af);
				kprintf(" to ");
				pf_print_host(naddr, 0, af);
				kprintf("\n");
			}
			return (0);
		}
	}

	if (cur->addr.type == PF_ADDR_NOROUTE)
		return (1);
	if (cur->addr.type == PF_ADDR_DYNIFTL) {
		switch (af) {
#ifdef INET
		case AF_INET:
			if (cur->addr.p.dyn->pfid_acnt4 < 1 &&
			    (rpool->opts & PF_POOL_TYPEMASK) !=
			    PF_POOL_ROUNDROBIN)
				return (1);
			raddr = &cur->addr.p.dyn->pfid_addr4;
			rmask = &cur->addr.p.dyn->pfid_mask4;
			break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			if (cur->addr.p.dyn->pfid_acnt6 < 1 &&
			    (rpool->opts & PF_POOL_TYPEMASK) !=
			    PF_POOL_ROUNDROBIN)
				return (1);
			raddr = &cur->addr.p.dyn->pfid_addr6;
			rmask = &cur->addr.p.dyn->pfid_mask6;
			break;
#endif /* INET6 */
		}
	} else if (cur->addr.type == PF_ADDR_TABLE) {
		if ((rpool->opts & PF_POOL_TYPEMASK) != PF_POOL_ROUNDROBIN)
			return (1); /* unsupported */
	} else {
		raddr = &cur->addr.v.a.addr;
		rmask = &cur->addr.v.a.mask;
	}

	switch (rpool->opts & PF_POOL_TYPEMASK) {
	case PF_POOL_NONE:
		PF_ACPY(naddr, raddr, af);
		break;
	case PF_POOL_BITMASK:
		PF_POOLMASK(naddr, raddr, rmask, saddr, af);
		break;
	case PF_POOL_RANDOM:
		if (init_addr != NULL && PF_AZERO(init_addr, af)) {
			switch (af) {
#ifdef INET
			case AF_INET:
				counter.addr32[0] = htonl(karc4random());
				break;
#endif /* INET */
#ifdef INET6
			case AF_INET6:
				if (rmask->addr32[3] != 0xffffffff)
					counter.addr32[3] =
						htonl(karc4random());
				else
					break;
				if (rmask->addr32[2] != 0xffffffff)
					counter.addr32[2] =
						htonl(karc4random());
				else
					break;
				if (rmask->addr32[1] != 0xffffffff)
					counter.addr32[1] =
						htonl(karc4random());
				else
					break;
				if (rmask->addr32[0] != 0xffffffff)
					counter.addr32[0] =
						htonl(karc4random());
				break;
#endif /* INET6 */
			}
			PF_POOLMASK(naddr, raddr, rmask, &counter, af);
			PF_ACPY(init_addr, naddr, af);

		} else {
			counter = rpool->counter;
			cpu_ccfence();
			PF_AINC(&counter, af);
			PF_POOLMASK(naddr, raddr, rmask, &counter, af);
			rpool->counter = counter;
		}
		break;
	case PF_POOL_SRCHASH:
		pf_hash(saddr, (struct pf_addr *)&hash, &rpool->key, af);
		PF_POOLMASK(naddr, raddr, rmask, (struct pf_addr *)&hash, af);
		break;
	case PF_POOL_ROUNDROBIN:
		tblidx = rpool->tblidx;
		counter = rpool->counter;
		if (cur->addr.type == PF_ADDR_TABLE) {
			if (!pfr_pool_get(cur->addr.p.tbl,
			    &tblidx, &counter,
			    &raddr, &rmask, af)) {
				goto get_addr;
			}
		} else if (cur->addr.type == PF_ADDR_DYNIFTL) {
			if (!pfr_pool_get(cur->addr.p.dyn->pfid_kt,
			    &tblidx, &counter,
			    &raddr, &rmask, af)) {
				goto get_addr;
			}
		} else if (pf_match_addr(0, raddr, rmask,
					 &counter, af)) {
			goto get_addr;
		}

	try_next:
		if ((cur = TAILQ_NEXT(cur, entries)) == NULL)
			cur = TAILQ_FIRST(&rpool->list);
		if (cur->addr.type == PF_ADDR_TABLE) {
			tblidx = -1;
			if (pfr_pool_get(cur->addr.p.tbl,
			    &tblidx, &counter,
			    &raddr, &rmask, af)) {
				/* table contains no address of type 'af' */
				if (cur != acur)
					goto try_next;
				return (1);
			}
		} else if (cur->addr.type == PF_ADDR_DYNIFTL) {
			tblidx = -1;
			if (pfr_pool_get(cur->addr.p.dyn->pfid_kt,
			    &tblidx, &counter,
			    &raddr, &rmask, af)) {
				/* table contains no address of type 'af' */
				if (cur != acur)
					goto try_next;
				return (1);
			}
		} else {
			raddr = &cur->addr.v.a.addr;
			rmask = &cur->addr.v.a.mask;
			PF_ACPY(&counter, raddr, af);
		}

	get_addr:
		rpool->cur = cur;
		rpool->tblidx = tblidx;
		PF_ACPY(naddr, &counter, af);
		if (init_addr != NULL && PF_AZERO(init_addr, af))
			PF_ACPY(init_addr, naddr, af);
		PF_AINC(&counter, af);
		rpool->counter = counter;
		break;
	}
	if (*sn != NULL)
		PF_ACPY(&(*sn)->raddr, naddr, af);

	if (pf_status.debug >= PF_DEBUG_MISC &&
	    (rpool->opts & PF_POOL_TYPEMASK) != PF_POOL_NONE) {
		kprintf("pf_map_addr: selected address ");
		pf_print_host(naddr, 0, af);
		kprintf("\n");
	}

	return (0);
}

int
pf_get_sport(struct pf_pdesc *pd, sa_family_t af,
	     u_int8_t proto, struct pf_rule *r,
	     struct pf_addr *saddr, struct pf_addr *daddr,
	     u_int16_t sport, u_int16_t dport,
	     struct pf_addr *naddr, u_int16_t *nport,
	     u_int16_t low, u_int16_t high, struct pf_src_node **sn)
{
	struct pf_state_key_cmp	key;
	struct pf_addr		init_addr;
	u_int16_t		cut;
	u_int32_t		hash_base = 0;
	int			do_hash = 0;

	bzero(&init_addr, sizeof(init_addr));
	if (pf_map_addr(af, r, saddr, naddr, &init_addr, sn))
		return (1);

	if (proto == IPPROTO_ICMP) {
		low = 1;
		high = 65535;
	}

	bzero(&key, sizeof(key));
	key.af = af;
	key.proto = proto;
	key.port[0] = dport;
	PF_ACPY(&key.addr[0], daddr, key.af);

	do {
		PF_ACPY(&key.addr[1], naddr, key.af);

		/*
		 * We want to select a port that calculates to a toeplitz hash
		 * that masks to the same cpu, otherwise the response may
		 * not see the new state.
		 *
		 * We can still do this even if the kernel is disregarding
		 * the hash and vectoring the packets to a specific cpu,
		 * but it will reduce the number of ports we can use.
		 */
		switch(af) {
		case AF_INET:
			if (proto == IPPROTO_TCP) {
				do_hash = 1;
				hash_base = toeplitz_piecemeal_port(dport) ^
				    toeplitz_piecemeal_addr(daddr->v4.s_addr) ^
				    toeplitz_piecemeal_addr(naddr->v4.s_addr);
			}
			break;
		case AF_INET6:
			/* XXX TODO XXX */
		default:
			/* XXX TODO XXX */
			break;
		}

		/*
		 * port search; start random, step;
		 * similar 2 portloop in in_pcbbind
		 *
		 * WARNING! We try to match such that the kernel will
		 *	    dispatch the translated host/port to the same
		 *	    cpu, but this might not be possible.
		 *
		 *	    In the case where the port is fixed, or for the
		 *	    UDP case (whos toeplitz does not incorporate the
		 *	    port), we set not_cpu_localized which ultimately
		 *	    causes the pf_state_tree element
		 *
		 * XXX fixed ports present a problem for cpu localization.
		 */
		if (!(proto == IPPROTO_TCP ||
		      proto == IPPROTO_UDP ||
		      proto == IPPROTO_ICMP)) {
			/*
			 * non-specific protocol, leave port intact.
			 */
			key.port[1] = sport;
			if (pf_find_state_all(&key, PF_IN, NULL) == NULL) {
				*nport = sport;
				pd->not_cpu_localized = 1;
				return (0);
			}
		} else if (low == 0 && high == 0) {
			/*
			 * static-port same as originator.
			 */
			key.port[1] = sport;
			if (pf_find_state_all(&key, PF_IN, NULL) == NULL) {
				*nport = sport;
				pd->not_cpu_localized = 1;
				return (0);
			}
		} else if (low == high) {
			/*
			 * specific port as specified.
			 */
			key.port[1] = htons(low);
			if (pf_find_state_all(&key, PF_IN, NULL) == NULL) {
				*nport = htons(low);
				pd->not_cpu_localized = 1;
				return (0);
			}
		} else {
			/*
			 * normal dynamic port
			 */
			u_int16_t tmp;

			if (low > high) {
				tmp = low;
				low = high;
				high = tmp;
			}
			/* low < high */
			cut = htonl(karc4random()) % (1 + high - low) + low;
			/* low <= cut <= high */
			for (tmp = cut; tmp <= high; ++(tmp)) {
				key.port[1] = htons(tmp);
				if (do_hash) {
					uint32_t hash;

					hash = hash_base ^
					toeplitz_piecemeal_port(key.port[1]);
					if (netisr_hashcpu(hash) != mycpuid)
						continue;
				}
				if (pf_find_state_all(&key, PF_IN, NULL) ==
				    NULL && !in_baddynamic(tmp, proto)) {
					if (proto == IPPROTO_UDP)
						pd->not_cpu_localized = 1;
					*nport = htons(tmp);
					return (0);
				}
			}
			for (tmp = cut - 1; tmp >= low; --(tmp)) {
				key.port[1] = htons(tmp);
				if (do_hash) {
					uint32_t hash;

					hash = hash_base ^
					toeplitz_piecemeal_port(key.port[1]);
					if (netisr_hashcpu(hash) != mycpuid)
						continue;
				}
				if (pf_find_state_all(&key, PF_IN, NULL) ==
				    NULL && !in_baddynamic(tmp, proto)) {
					if (proto == IPPROTO_UDP)
						pd->not_cpu_localized = 1;
					*nport = htons(tmp);
					return (0);
				}
			}
		}

		/*
		 * Next address
		 */
		switch (r->rpool.opts & PF_POOL_TYPEMASK) {
		case PF_POOL_RANDOM:
		case PF_POOL_ROUNDROBIN:
			if (pf_map_addr(af, r, saddr, naddr, &init_addr, sn))
				return (1);
			break;
		case PF_POOL_NONE:
		case PF_POOL_SRCHASH:
		case PF_POOL_BITMASK:
		default:
			return (1);
		}
	} while (! PF_AEQ(&init_addr, naddr, af) );
	return (1);					/* none available */
}

struct pf_rule *
pf_match_translation(struct pf_pdesc *pd, struct mbuf *m, int off,
    int direction, struct pfi_kif *kif, struct pf_addr *saddr, u_int16_t sport,
    struct pf_addr *daddr, u_int16_t dport, int rs_num)
{
	struct pf_rule		*r, *rm = NULL;
	struct pf_ruleset	*ruleset = NULL;
	int			 tag = -1;
	int			 rtableid = -1;
	int			 asd = 0;

	r = TAILQ_FIRST(pf_main_ruleset.rules[rs_num].active.ptr);
	while (r && rm == NULL) {
		struct pf_rule_addr	*src = NULL, *dst = NULL;
		struct pf_addr_wrap	*xdst = NULL;
		struct pf_pooladdr	*cur;

		if (r->action == PF_BINAT && direction == PF_IN) {
			src = &r->dst;
			cur = r->rpool.cur;	/* SMP race possible */
			cpu_ccfence();
			if (cur)
				xdst = &cur->addr;
		} else {
			src = &r->src;
			dst = &r->dst;
		}

		r->evaluations++;
		if (pfi_kif_match(r->kif, kif) == r->ifnot)
			r = r->skip[PF_SKIP_IFP].ptr;
		else if (r->direction && r->direction != direction)
			r = r->skip[PF_SKIP_DIR].ptr;
		else if (r->af && r->af != pd->af)
			r = r->skip[PF_SKIP_AF].ptr;
		else if (r->proto && r->proto != pd->proto)
			r = r->skip[PF_SKIP_PROTO].ptr;
		else if (PF_MISMATCHAW(&src->addr, saddr, pd->af,
		    src->neg, kif))
			r = r->skip[src == &r->src ? PF_SKIP_SRC_ADDR :
			    PF_SKIP_DST_ADDR].ptr;
		else if (src->port_op && !pf_match_port(src->port_op,
		    src->port[0], src->port[1], sport))
			r = r->skip[src == &r->src ? PF_SKIP_SRC_PORT :
			    PF_SKIP_DST_PORT].ptr;
		else if (dst != NULL &&
		    PF_MISMATCHAW(&dst->addr, daddr, pd->af, dst->neg, NULL))
			r = r->skip[PF_SKIP_DST_ADDR].ptr;
		else if (xdst != NULL && PF_MISMATCHAW(xdst, daddr, pd->af,
		    0, NULL))
			r = TAILQ_NEXT(r, entries);
		else if (dst != NULL && dst->port_op &&
		    !pf_match_port(dst->port_op, dst->port[0],
		    dst->port[1], dport))
			r = r->skip[PF_SKIP_DST_PORT].ptr;
		else if (r->match_tag && !pf_match_tag(m, r, &tag))
			r = TAILQ_NEXT(r, entries);
		else if (r->os_fingerprint != PF_OSFP_ANY && (pd->proto !=
		    IPPROTO_TCP || !pf_osfp_match(pf_osfp_fingerprint(pd, m,
		    off, pd->hdr.tcp), r->os_fingerprint)))
			r = TAILQ_NEXT(r, entries);
		else {
			if (r->tag)
				tag = r->tag;
			if (r->rtableid >= 0)
				rtableid = r->rtableid;
			if (r->anchor == NULL) {
				rm = r;
			} else
				pf_step_into_anchor(&asd, &ruleset, rs_num,
				    &r, NULL, NULL);
		}
		if (r == NULL)
			pf_step_out_of_anchor(&asd, &ruleset, rs_num, &r,
			    NULL, NULL);
	}
	if (pf_tag_packet(m, tag, rtableid))
		return (NULL);
	if (rm != NULL && (rm->action == PF_NONAT ||
	    rm->action == PF_NORDR || rm->action == PF_NOBINAT))
		return (NULL);
	return (rm);
}

struct pf_rule *
pf_get_translation(struct pf_pdesc *pd, struct mbuf *m, int off, int direction,
    struct pfi_kif *kif, struct pf_src_node **sn,
    struct pf_state_key **skw, struct pf_state_key **sks,
    struct pf_state_key **skp, struct pf_state_key **nkp,
    struct pf_addr *saddr, struct pf_addr *daddr,
    u_int16_t sport, u_int16_t dport)
{
	struct pf_rule	*r = NULL;

	if (direction == PF_OUT) {
		r = pf_match_translation(pd, m, off, direction, kif, saddr,
		    sport, daddr, dport, PF_RULESET_BINAT);
		if (r == NULL)
			r = pf_match_translation(pd, m, off, direction, kif,
			    saddr, sport, daddr, dport, PF_RULESET_NAT);
	} else {
		r = pf_match_translation(pd, m, off, direction, kif, saddr,
		    sport, daddr, dport, PF_RULESET_RDR);
		if (r == NULL)
			r = pf_match_translation(pd, m, off, direction, kif,
			    saddr, sport, daddr, dport, PF_RULESET_BINAT);
	}

	if (r != NULL) {
		struct pf_addr	*naddr;
		u_int16_t	*nport;

		if (pf_state_key_setup(pd, r, skw, sks, skp, nkp,
		    saddr, daddr, sport, dport))
			return r;

		/* XXX We only modify one side for now. */
		naddr = &(*nkp)->addr[1];
		nport = &(*nkp)->port[1];

		/*
		 * NOTE: Currently all translations will clear
		 *	 BRIDGE_MBUF_TAGGED, telling the bridge to
		 *	 ignore the original input encapsulation.
		 */
		switch (r->action) {
		case PF_NONAT:
		case PF_NOBINAT:
		case PF_NORDR:
			return (NULL);
		case PF_NAT:
			m->m_pkthdr.fw_flags &= ~BRIDGE_MBUF_TAGGED;
			if (pf_get_sport(pd, pd->af, pd->proto, r,
			    saddr, daddr, sport, dport,
			    naddr, nport, r->rpool.proxy_port[0],
			    r->rpool.proxy_port[1], sn)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: NAT proxy port allocation "
				    "(%u-%u) failed\n",
				    r->rpool.proxy_port[0],
				    r->rpool.proxy_port[1]));
				return (NULL);
			}
			break;
		case PF_BINAT:
			m->m_pkthdr.fw_flags &= ~BRIDGE_MBUF_TAGGED;
			switch (direction) {
			case PF_OUT:
				if (r->rpool.cur->addr.type == PF_ADDR_DYNIFTL){
					switch (pd->af) {
#ifdef INET
					case AF_INET:
						if (r->rpool.cur->addr.p.dyn->
						    pfid_acnt4 < 1)
							return (NULL);
						PF_POOLMASK(naddr,
						    &r->rpool.cur->addr.p.dyn->
						    pfid_addr4,
						    &r->rpool.cur->addr.p.dyn->
						    pfid_mask4,
						    saddr, AF_INET);
						break;
#endif /* INET */
#ifdef INET6
					case AF_INET6:
						if (r->rpool.cur->addr.p.dyn->
						    pfid_acnt6 < 1)
							return (NULL);
						PF_POOLMASK(naddr,
						    &r->rpool.cur->addr.p.dyn->
						    pfid_addr6,
						    &r->rpool.cur->addr.p.dyn->
						    pfid_mask6,
						    saddr, AF_INET6);
						break;
#endif /* INET6 */
					}
				} else
					PF_POOLMASK(naddr,
					    &r->rpool.cur->addr.v.a.addr,
					    &r->rpool.cur->addr.v.a.mask,
					    saddr, pd->af);
				break;
			case PF_IN:
				if (r->src.addr.type == PF_ADDR_DYNIFTL) {
					switch (pd->af) {
#ifdef INET
					case AF_INET:
						if (r->src.addr.p.dyn->
						    pfid_acnt4 < 1)
							return (NULL);
						PF_POOLMASK(naddr,
						    &r->src.addr.p.dyn->
						    pfid_addr4,
						    &r->src.addr.p.dyn->
						    pfid_mask4,
						    daddr, AF_INET);
						break;
#endif /* INET */
#ifdef INET6
					case AF_INET6:
						if (r->src.addr.p.dyn->
						    pfid_acnt6 < 1)
							return (NULL);
						PF_POOLMASK(naddr,
						    &r->src.addr.p.dyn->
						    pfid_addr6,
						    &r->src.addr.p.dyn->
						    pfid_mask6,
						    daddr, AF_INET6);
						break;
#endif /* INET6 */
					}
				} else
					PF_POOLMASK(naddr,
					    &r->src.addr.v.a.addr,
					    &r->src.addr.v.a.mask, daddr,
					    pd->af);
				break;
			}
			break;
		case PF_RDR: {
			m->m_pkthdr.fw_flags &= ~BRIDGE_MBUF_TAGGED;
			if (pf_map_addr(pd->af, r, saddr, naddr, NULL, sn))
				return (NULL);
			if ((r->rpool.opts & PF_POOL_TYPEMASK) ==
			    PF_POOL_BITMASK)
				PF_POOLMASK(naddr, naddr,
				    &r->rpool.cur->addr.v.a.mask, daddr,
				    pd->af);

			if (r->rpool.proxy_port[1]) {
				u_int32_t	tmp_nport;

				tmp_nport = ((ntohs(dport) -
				    ntohs(r->dst.port[0])) %
				    (r->rpool.proxy_port[1] -
				    r->rpool.proxy_port[0] + 1)) +
				    r->rpool.proxy_port[0];

				/* wrap around if necessary */
				if (tmp_nport > 65535)
					tmp_nport -= 65535;
				*nport = htons((u_int16_t)tmp_nport);
			} else if (r->rpool.proxy_port[0]) {
				*nport = htons(r->rpool.proxy_port[0]);
			}
			pd->not_cpu_localized = 1;
			break;
		}
		default:
			return (NULL);
		}
	}

	return (r);
}

struct netmsg_hashlookup {
	struct netmsg_base	base;
	struct inpcb		**nm_pinp;
	struct inpcbinfo    	*nm_pcbinfo;
	struct pf_addr		*nm_saddr;
	struct pf_addr		*nm_daddr;
	uint16_t		nm_sport;
	uint16_t		nm_dport;
	sa_family_t		nm_af;
};

#ifdef PF_SOCKET_LOOKUP_DOMSG
static void
in_pcblookup_hash_handler(netmsg_t msg)
{
	struct netmsg_hashlookup *rmsg = (struct netmsg_hashlookup *)msg;

	if (rmsg->nm_af == AF_INET)
		*rmsg->nm_pinp = in_pcblookup_hash(rmsg->nm_pcbinfo,
		    rmsg->nm_saddr->v4, rmsg->nm_sport, rmsg->nm_daddr->v4,
		    rmsg->nm_dport, INPLOOKUP_WILDCARD, NULL);
#ifdef INET6
	else
		*rmsg->nm_pinp = in6_pcblookup_hash(rmsg->nm_pcbinfo,
		    &rmsg->nm_saddr->v6, rmsg->nm_sport, &rmsg->nm_daddr->v6,
		    rmsg->nm_dport, INPLOOKUP_WILDCARD, NULL);
#endif /* INET6 */
	lwkt_replymsg(&rmsg->base.lmsg, 0);
}
#endif	/* PF_SOCKET_LOOKUP_DOMSG */

int
pf_socket_lookup(int direction, struct pf_pdesc *pd)
{
	struct pf_addr		*saddr, *daddr;
	u_int16_t		 sport, dport;
	struct inpcbinfo	*pi;
	struct inpcb		*inp;
	struct netmsg_hashlookup *msg = NULL;
#ifdef PF_SOCKET_LOOKUP_DOMSG
	struct netmsg_hashlookup msg0;
#endif
	int			 pi_cpu = 0;

	if (pd == NULL)
		return (-1);
	pd->lookup.uid = UID_MAX;
	pd->lookup.gid = GID_MAX;
	pd->lookup.pid = NO_PID;
	if (direction == PF_IN) {
		saddr = pd->src;
		daddr = pd->dst;
	} else {
		saddr = pd->dst;
		daddr = pd->src;
	}
	switch (pd->proto) {
	case IPPROTO_TCP:
		if (pd->hdr.tcp == NULL)
			return (-1);
		sport = pd->hdr.tcp->th_sport;
		dport = pd->hdr.tcp->th_dport;

		pi_cpu = tcp_addrcpu(saddr->v4.s_addr, sport, daddr->v4.s_addr, dport);
		pi = &tcbinfo[pi_cpu];
		/*
		 * Our netstack runs lockless on MP systems
		 * (only for TCP connections at the moment).
		 * 
		 * As we are not allowed to read another CPU's tcbinfo,
		 * we have to ask that CPU via remote call to search the
		 * table for us.
		 * 
		 * Prepare a msg iff data belongs to another CPU.
		 */
		if (pi_cpu != mycpu->gd_cpuid) {
#ifdef PF_SOCKET_LOOKUP_DOMSG
			/*
			 * NOTE:
			 *
			 * Following lwkt_domsg() is dangerous and could
			 * lockup the network system, e.g.
			 *
			 * On 2 CPU system:
			 * netisr0 domsg to netisr1 (due to lookup)
			 * netisr1 domsg to netisr0 (due to lookup)
			 *
			 * We simply return -1 here, since we are probably
			 * called before NAT, so the TCP packet should
			 * already be on the correct CPU.
			 */
			msg = &msg0;
			netmsg_init(&msg->base, NULL, &curthread->td_msgport,
				    0, in_pcblookup_hash_handler);
			msg->nm_pinp = &inp;
			msg->nm_pcbinfo = pi;
			msg->nm_saddr = saddr;
			msg->nm_sport = sport;
			msg->nm_daddr = daddr;
			msg->nm_dport = dport;
			msg->nm_af = pd->af;
#else	/* !PF_SOCKET_LOOKUP_DOMSG */
			kprintf("pf_socket_lookup: tcp packet not on the "
				"correct cpu %d, cur cpu %d\n",
				pi_cpu, mycpuid);
			print_backtrace(-1);
			return -1;
#endif	/* PF_SOCKET_LOOKUP_DOMSG */
		}
		break;
	case IPPROTO_UDP:
		if (pd->hdr.udp == NULL)
			return (-1);
		sport = pd->hdr.udp->uh_sport;
		dport = pd->hdr.udp->uh_dport;
		pi = &udbinfo[mycpuid];
		break;
	default:
		return (-1);
	}
	if (direction != PF_IN) {
		u_int16_t	p;

		p = sport;
		sport = dport;
		dport = p;
	}
	switch (pd->af) {
#ifdef INET6
	case AF_INET6:
		/*
		 * Query other CPU, second part
		 * 
		 * msg only gets initialized when:
		 * 1) packet is TCP
		 * 2) the info belongs to another CPU
		 *
		 * Use some switch/case magic to avoid code duplication.
		 */
		if (msg == NULL) {
			inp = in6_pcblookup_hash(pi, &saddr->v6, sport,
			    &daddr->v6, dport, INPLOOKUP_WILDCARD, NULL);

			if (inp == NULL)
				return (-1);
			break;
		}
		/* FALLTHROUGH if SMP and on other CPU */
#endif /* INET6 */
	case AF_INET:
		if (msg != NULL) {
			lwkt_domsg(netisr_cpuport(pi_cpu),
				     &msg->base.lmsg, 0);
		} else
		{
			inp = in_pcblookup_hash(pi, saddr->v4, sport, daddr->v4,
			    dport, INPLOOKUP_WILDCARD, NULL);
		}
		if (inp == NULL)
			return (-1);
		break;

	default:
		return (-1);
	}
	pd->lookup.uid = inp->inp_socket->so_cred->cr_uid;
	pd->lookup.gid = inp->inp_socket->so_cred->cr_groups[0];
	return (1);
}

u_int8_t
pf_get_wscale(struct mbuf *m, int off, u_int16_t th_off, sa_family_t af)
{
	int		 hlen;
	u_int8_t	 hdr[60];
	u_int8_t	*opt, optlen;
	u_int8_t	 wscale = 0;

	hlen = th_off << 2;		/* hlen <= sizeof(hdr) */
	if (hlen <= sizeof(struct tcphdr))
		return (0);
	if (!pf_pull_hdr(m, off, hdr, hlen, NULL, NULL, af))
		return (0);
	opt = hdr + sizeof(struct tcphdr);
	hlen -= sizeof(struct tcphdr);
	while (hlen >= 3) {
		switch (*opt) {
		case TCPOPT_EOL:
		case TCPOPT_NOP:
			++opt;
			--hlen;
			break;
		case TCPOPT_WINDOW:
			wscale = opt[2];
			if (wscale > TCP_MAX_WINSHIFT)
				wscale = TCP_MAX_WINSHIFT;
			wscale |= PF_WSCALE_FLAG;
			/* FALLTHROUGH */
		default:
			optlen = opt[1];
			if (optlen < 2)
				optlen = 2;
			hlen -= optlen;
			opt += optlen;
			break;
		}
	}
	return (wscale);
}

u_int16_t
pf_get_mss(struct mbuf *m, int off, u_int16_t th_off, sa_family_t af)
{
	int		 hlen;
	u_int8_t	 hdr[60];
	u_int8_t	*opt, optlen;
	u_int16_t	 mss = tcp_mssdflt;

	hlen = th_off << 2;	/* hlen <= sizeof(hdr) */
	if (hlen <= sizeof(struct tcphdr))
		return (0);
	if (!pf_pull_hdr(m, off, hdr, hlen, NULL, NULL, af))
		return (0);
	opt = hdr + sizeof(struct tcphdr);
	hlen -= sizeof(struct tcphdr);
	while (hlen >= TCPOLEN_MAXSEG) {
		switch (*opt) {
		case TCPOPT_EOL:
		case TCPOPT_NOP:
			++opt;
			--hlen;
			break;
		case TCPOPT_MAXSEG:
			bcopy((caddr_t)(opt + 2), (caddr_t)&mss, 2);
			/* FALLTHROUGH */
		default:
			optlen = opt[1];
			if (optlen < 2)
				optlen = 2;
			hlen -= optlen;
			opt += optlen;
			break;
		}
	}
	return (mss);
}

u_int16_t
pf_calc_mss(struct pf_addr *addr, sa_family_t af, u_int16_t offer)
{
#ifdef INET
	struct sockaddr_in	*dst;
	struct route		 ro;
#endif /* INET */
#ifdef INET6
	struct sockaddr_in6	*dst6;
	struct route_in6	 ro6;
#endif /* INET6 */
	struct rtentry		*rt = NULL;
	int			 hlen = 0;
	u_int16_t		 mss = tcp_mssdflt;

	switch (af) {
#ifdef INET
	case AF_INET:
		hlen = sizeof(struct ip);
		bzero(&ro, sizeof(ro));
		dst = (struct sockaddr_in *)&ro.ro_dst;
		dst->sin_family = AF_INET;
		dst->sin_len = sizeof(*dst);
		dst->sin_addr = addr->v4;
		rtalloc_ign(&ro, (RTF_CLONING | RTF_PRCLONING));
		rt = ro.ro_rt;
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		hlen = sizeof(struct ip6_hdr);
		bzero(&ro6, sizeof(ro6));
		dst6 = (struct sockaddr_in6 *)&ro6.ro_dst;
		dst6->sin6_family = AF_INET6;
		dst6->sin6_len = sizeof(*dst6);
		dst6->sin6_addr = addr->v6;
		rtalloc_ign((struct route *)&ro6, (RTF_CLONING | RTF_PRCLONING));
		rt = ro6.ro_rt;
		break;
#endif /* INET6 */
	}

	if (rt && rt->rt_ifp) {
		mss = rt->rt_ifp->if_mtu - hlen - sizeof(struct tcphdr);
		mss = max(tcp_mssdflt, mss);
		RTFREE(rt);
	}
	mss = min(mss, offer);
	mss = max(mss, 64);		/* sanity - at least max opt space */
	return (mss);
}

void
pf_set_rt_ifp(struct pf_state *s, struct pf_addr *saddr)
{
	struct pf_rule *r = s->rule.ptr;

	s->rt_kif = NULL;
	if (!r->rt || r->rt == PF_FASTROUTE)
		return;
	switch (s->key[PF_SK_WIRE]->af) {
#ifdef INET
	case AF_INET:
		pf_map_addr(AF_INET, r, saddr, &s->rt_addr, NULL,
		    &s->nat_src_node);
		s->rt_kif = r->rpool.cur->kif;
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		pf_map_addr(AF_INET6, r, saddr, &s->rt_addr, NULL,
		    &s->nat_src_node);
		s->rt_kif = r->rpool.cur->kif;
		break;
#endif /* INET6 */
	}
}

u_int32_t
pf_tcp_iss(struct pf_pdesc *pd)
{
	MD5_CTX ctx;
	u_int32_t digest[4];

	if (pf_tcp_secret_init == 0) {
		lwkt_gettoken(&pf_gtoken);
		if (pf_tcp_secret_init == 0) {
			karc4random_buf(pf_tcp_secret, sizeof(pf_tcp_secret));
			MD5Init(&pf_tcp_secret_ctx);
			MD5Update(&pf_tcp_secret_ctx, pf_tcp_secret,
			    sizeof(pf_tcp_secret));
			pf_tcp_secret_init = 1;
		}
		lwkt_reltoken(&pf_gtoken);
	}
	ctx = pf_tcp_secret_ctx;

	MD5Update(&ctx, (char *)&pd->hdr.tcp->th_sport, sizeof(u_short));
	MD5Update(&ctx, (char *)&pd->hdr.tcp->th_dport, sizeof(u_short));
	if (pd->af == AF_INET6) {
		MD5Update(&ctx, (char *)&pd->src->v6, sizeof(struct in6_addr));
		MD5Update(&ctx, (char *)&pd->dst->v6, sizeof(struct in6_addr));
	} else {
		MD5Update(&ctx, (char *)&pd->src->v4, sizeof(struct in_addr));
		MD5Update(&ctx, (char *)&pd->dst->v4, sizeof(struct in_addr));
	}
	MD5Final((u_char *)digest, &ctx);
	pf_tcp_iss_off += 4096;

	return (digest[0] + pd->hdr.tcp->th_seq + pf_tcp_iss_off);
}

int
pf_test_rule(struct pf_rule **rm, struct pf_state **sm, int direction,
    struct pfi_kif *kif, struct mbuf *m, int off, void *h,
    struct pf_pdesc *pd, struct pf_rule **am, struct pf_ruleset **rsm,
    struct ifqueue *ifq, struct inpcb *inp)
{
	struct pf_rule		*nr = NULL;
	struct pf_addr		*saddr = pd->src, *daddr = pd->dst;
	sa_family_t		 af = pd->af;
	struct pf_rule		*r, *a = NULL;
	struct pf_ruleset	*ruleset = NULL;
	struct pf_src_node	*nsn = NULL;
	struct tcphdr		*th = pd->hdr.tcp;
	struct pf_state_key	*skw = NULL, *sks = NULL;
	struct pf_state_key	*sk = NULL, *nk = NULL;
	u_short			 reason;
	int			 rewrite = 0, hdrlen = 0;
	int			 tag = -1, rtableid = -1;
	int			 asd = 0;
	int			 match = 0;
	int			 state_icmp = 0;
	u_int16_t		 sport = 0, dport = 0;
	u_int16_t		 bproto_sum = 0, bip_sum = 0;
	u_int8_t		 icmptype = 0, icmpcode = 0;


	if (direction == PF_IN && pf_check_congestion(ifq)) {
		REASON_SET(&reason, PFRES_CONGEST);
		return (PF_DROP);
	}

	if (inp != NULL)
		pd->lookup.done = pf_socket_lookup(direction, pd);
	else if (debug_pfugidhack) { 
		DPFPRINTF(PF_DEBUG_MISC, ("pf: unlocked lookup\n"));
		pd->lookup.done = pf_socket_lookup(direction, pd);
	}

	switch (pd->proto) {
	case IPPROTO_TCP:
		sport = th->th_sport;
		dport = th->th_dport;
		hdrlen = sizeof(*th);
		break;
	case IPPROTO_UDP:
		sport = pd->hdr.udp->uh_sport;
		dport = pd->hdr.udp->uh_dport;
		hdrlen = sizeof(*pd->hdr.udp);
		break;
#ifdef INET
	case IPPROTO_ICMP:
		if (pd->af != AF_INET)
			break;
		sport = dport = pd->hdr.icmp->icmp_id;
		hdrlen = sizeof(*pd->hdr.icmp);
		icmptype = pd->hdr.icmp->icmp_type;
		icmpcode = pd->hdr.icmp->icmp_code;

		if (icmptype == ICMP_UNREACH ||
		    icmptype == ICMP_SOURCEQUENCH ||
		    icmptype == ICMP_REDIRECT ||
		    icmptype == ICMP_TIMXCEED ||
		    icmptype == ICMP_PARAMPROB)
			state_icmp++;
		break;
#endif /* INET */
#ifdef INET6
	case IPPROTO_ICMPV6:
		if (af != AF_INET6)
			break;
		sport = dport = pd->hdr.icmp6->icmp6_id;
		hdrlen = sizeof(*pd->hdr.icmp6);
		icmptype = pd->hdr.icmp6->icmp6_type;
		icmpcode = pd->hdr.icmp6->icmp6_code;

		if (icmptype == ICMP6_DST_UNREACH ||
		    icmptype == ICMP6_PACKET_TOO_BIG ||
		    icmptype == ICMP6_TIME_EXCEEDED ||
		    icmptype == ICMP6_PARAM_PROB)
			state_icmp++;
		break;
#endif /* INET6 */
	default:
		sport = dport = hdrlen = 0;
		break;
	}

	r = TAILQ_FIRST(pf_main_ruleset.rules[PF_RULESET_FILTER].active.ptr);

	/* check packet for BINAT/NAT/RDR */
	if ((nr = pf_get_translation(pd, m, off, direction, kif, &nsn,
	    &skw, &sks, &sk, &nk, saddr, daddr, sport, dport)) != NULL) {
		if (nk == NULL || sk == NULL) {
			REASON_SET(&reason, PFRES_MEMORY);
			goto cleanup;
		}

		if (pd->ip_sum)
			bip_sum = *pd->ip_sum;

		m->m_flags &= ~M_HASH;
		switch (pd->proto) {
		case IPPROTO_TCP:
			bproto_sum = th->th_sum;
			pd->proto_sum = &th->th_sum;

			if (PF_ANEQ(saddr, &nk->addr[pd->sidx], af) ||
			    nk->port[pd->sidx] != sport) {
				pf_change_ap(saddr, &th->th_sport, pd->ip_sum,
				    &th->th_sum, &nk->addr[pd->sidx],
				    nk->port[pd->sidx], 0, af);
				pd->sport = &th->th_sport;
				sport = th->th_sport;
			}

			if (PF_ANEQ(daddr, &nk->addr[pd->didx], af) ||
			    nk->port[pd->didx] != dport) {
				pf_change_ap(daddr, &th->th_dport, pd->ip_sum,
				    &th->th_sum, &nk->addr[pd->didx],
				    nk->port[pd->didx], 0, af);
				dport = th->th_dport;
				pd->dport = &th->th_dport;
			}
			rewrite++;
			break;
		case IPPROTO_UDP:
			bproto_sum = pd->hdr.udp->uh_sum;
			pd->proto_sum = &pd->hdr.udp->uh_sum;

			if (PF_ANEQ(saddr, &nk->addr[pd->sidx], af) ||
			    nk->port[pd->sidx] != sport) {
				pf_change_ap(saddr, &pd->hdr.udp->uh_sport,
				    pd->ip_sum, &pd->hdr.udp->uh_sum,
				    &nk->addr[pd->sidx],
				    nk->port[pd->sidx], 1, af);
				sport = pd->hdr.udp->uh_sport;
				pd->sport = &pd->hdr.udp->uh_sport;
			}

			if (PF_ANEQ(daddr, &nk->addr[pd->didx], af) ||
			    nk->port[pd->didx] != dport) {
				pf_change_ap(daddr, &pd->hdr.udp->uh_dport,
				    pd->ip_sum, &pd->hdr.udp->uh_sum,
				    &nk->addr[pd->didx],
				    nk->port[pd->didx], 1, af);
				dport = pd->hdr.udp->uh_dport;
				pd->dport = &pd->hdr.udp->uh_dport;
			}
			rewrite++;
			break;
#ifdef INET
		case IPPROTO_ICMP:
			nk->port[0] = nk->port[1];
			if (PF_ANEQ(saddr, &nk->addr[pd->sidx], AF_INET))
				pf_change_a(&saddr->v4.s_addr, pd->ip_sum,
				    nk->addr[pd->sidx].v4.s_addr, 0);

			if (PF_ANEQ(daddr, &nk->addr[pd->didx], AF_INET))
				pf_change_a(&daddr->v4.s_addr, pd->ip_sum,
				    nk->addr[pd->didx].v4.s_addr, 0);

			if (nk->port[1] != pd->hdr.icmp->icmp_id) {
				pd->hdr.icmp->icmp_cksum = pf_cksum_fixup(
				    pd->hdr.icmp->icmp_cksum, sport,
				    nk->port[1], 0);
				pd->hdr.icmp->icmp_id = nk->port[1];
				pd->sport = &pd->hdr.icmp->icmp_id;
			}
			m_copyback(m, off, ICMP_MINLEN, (caddr_t)pd->hdr.icmp);
			break;
#endif /* INET */
#ifdef INET6
		case IPPROTO_ICMPV6:
			nk->port[0] = nk->port[1];
			if (PF_ANEQ(saddr, &nk->addr[pd->sidx], AF_INET6))
				pf_change_a6(saddr, &pd->hdr.icmp6->icmp6_cksum,
				    &nk->addr[pd->sidx], 0);

			if (PF_ANEQ(daddr, &nk->addr[pd->didx], AF_INET6))
				pf_change_a6(daddr, &pd->hdr.icmp6->icmp6_cksum,
				    &nk->addr[pd->didx], 0);
			rewrite++;
			break;
#endif /* INET */
		default:
			switch (af) {
#ifdef INET
			case AF_INET:
				if (PF_ANEQ(saddr,
				    &nk->addr[pd->sidx], AF_INET))
					pf_change_a(&saddr->v4.s_addr,
					    pd->ip_sum,
					    nk->addr[pd->sidx].v4.s_addr, 0);

				if (PF_ANEQ(daddr,
				    &nk->addr[pd->didx], AF_INET))
					pf_change_a(&daddr->v4.s_addr,
					    pd->ip_sum,
					    nk->addr[pd->didx].v4.s_addr, 0);
				break;
#endif /* INET */
#ifdef INET6
			case AF_INET6:
				if (PF_ANEQ(saddr,
				    &nk->addr[pd->sidx], AF_INET6))
					PF_ACPY(saddr, &nk->addr[pd->sidx], af);

				if (PF_ANEQ(daddr,
				    &nk->addr[pd->didx], AF_INET6))
					PF_ACPY(saddr, &nk->addr[pd->didx], af);
				break;
#endif /* INET */
			}
			break;
		}
		if (nr->natpass)
			r = NULL;
		pd->nat_rule = nr;
	}

	while (r != NULL) {
		r->evaluations++;
		if (pfi_kif_match(r->kif, kif) == r->ifnot)
			r = r->skip[PF_SKIP_IFP].ptr;
		else if (r->direction && r->direction != direction)
			r = r->skip[PF_SKIP_DIR].ptr;
		else if (r->af && r->af != af)
			r = r->skip[PF_SKIP_AF].ptr;
		else if (r->proto && r->proto != pd->proto)
			r = r->skip[PF_SKIP_PROTO].ptr;
		else if (PF_MISMATCHAW(&r->src.addr, saddr, af,
		    r->src.neg, kif))
			r = r->skip[PF_SKIP_SRC_ADDR].ptr;
		/* tcp/udp only. port_op always 0 in other cases */
		else if (r->src.port_op && !pf_match_port(r->src.port_op,
		    r->src.port[0], r->src.port[1], sport))
			r = r->skip[PF_SKIP_SRC_PORT].ptr;
		else if (PF_MISMATCHAW(&r->dst.addr, daddr, af,
		    r->dst.neg, NULL))
			r = r->skip[PF_SKIP_DST_ADDR].ptr;
		/* tcp/udp only. port_op always 0 in other cases */
		else if (r->dst.port_op && !pf_match_port(r->dst.port_op,
		    r->dst.port[0], r->dst.port[1], dport))
			r = r->skip[PF_SKIP_DST_PORT].ptr;
		/* icmp only. type always 0 in other cases */
		else if (r->type && r->type != icmptype + 1)
			r = TAILQ_NEXT(r, entries);
		/* icmp only. type always 0 in other cases */
		else if (r->code && r->code != icmpcode + 1)
			r = TAILQ_NEXT(r, entries);
		else if (r->tos && !(r->tos == pd->tos))
			r = TAILQ_NEXT(r, entries);
		else if (r->rule_flag & PFRULE_FRAGMENT)
			r = TAILQ_NEXT(r, entries);
		else if (pd->proto == IPPROTO_TCP &&
		    (r->flagset & th->th_flags) != r->flags)
			r = TAILQ_NEXT(r, entries);
		/* tcp/udp only. uid.op always 0 in other cases */
		else if (r->uid.op && (pd->lookup.done || (pd->lookup.done =
		    pf_socket_lookup(direction, pd), 1)) &&
		    !pf_match_uid(r->uid.op, r->uid.uid[0], r->uid.uid[1],
		    pd->lookup.uid))
			r = TAILQ_NEXT(r, entries);
		/* tcp/udp only. gid.op always 0 in other cases */
		else if (r->gid.op && (pd->lookup.done || (pd->lookup.done =
		    pf_socket_lookup(direction, pd), 1)) &&
		    !pf_match_gid(r->gid.op, r->gid.gid[0], r->gid.gid[1],
		    pd->lookup.gid))
			r = TAILQ_NEXT(r, entries);
		else if (r->prob &&
		  r->prob <= karc4random())
			r = TAILQ_NEXT(r, entries);
		else if (r->match_tag && !pf_match_tag(m, r, &tag))
			r = TAILQ_NEXT(r, entries);
		else if (r->os_fingerprint != PF_OSFP_ANY &&
		    (pd->proto != IPPROTO_TCP || !pf_osfp_match(
		    pf_osfp_fingerprint(pd, m, off, th),
		    r->os_fingerprint)))
			r = TAILQ_NEXT(r, entries);
		else {
			if (r->tag)
				tag = r->tag;
			if (r->rtableid >= 0)
				rtableid = r->rtableid;
			if (r->anchor == NULL) {
				match = 1;
				*rm = r;
				*am = a;
				*rsm = ruleset;
				if ((*rm)->quick)
					break;
				r = TAILQ_NEXT(r, entries);
			} else
				pf_step_into_anchor(&asd, &ruleset,
				    PF_RULESET_FILTER, &r, &a, &match);
		}
		if (r == NULL && pf_step_out_of_anchor(&asd, &ruleset,
		    PF_RULESET_FILTER, &r, &a, &match))
			break;
	}
	r = *rm;
	a = *am;
	ruleset = *rsm;

	REASON_SET(&reason, PFRES_MATCH);

	if (r->log || (nr != NULL && nr->log)) {
		if (rewrite)
			m_copyback(m, off, hdrlen, pd->hdr.any);
		PFLOG_PACKET(kif, h, m, af, direction, reason, r->log ? r : nr,
		    a, ruleset, pd);
	}

	if ((r->action == PF_DROP) &&
	    ((r->rule_flag & PFRULE_RETURNRST) ||
	    (r->rule_flag & PFRULE_RETURNICMP) ||
	    (r->rule_flag & PFRULE_RETURN))) {
		/* undo NAT changes, if they have taken place */
		if (nr != NULL) {
			PF_ACPY(saddr, &sk->addr[pd->sidx], af);
			PF_ACPY(daddr, &sk->addr[pd->didx], af);
			if (pd->sport)
				*pd->sport = sk->port[pd->sidx];
			if (pd->dport)
				*pd->dport = sk->port[pd->didx];
			if (pd->proto_sum)
				*pd->proto_sum = bproto_sum;
			if (pd->ip_sum)
				*pd->ip_sum = bip_sum;
			m_copyback(m, off, hdrlen, pd->hdr.any);
		}
		if (pd->proto == IPPROTO_TCP &&
		    ((r->rule_flag & PFRULE_RETURNRST) ||
		    (r->rule_flag & PFRULE_RETURN)) &&
		    !(th->th_flags & TH_RST)) {
			u_int32_t	 ack = ntohl(th->th_seq) + pd->p_len;
			int		 len = 0;
			struct ip	*h4;
#ifdef INET6
			struct ip6_hdr	*h6;
#endif
			switch (af) {
			case AF_INET:
				h4 = mtod(m, struct ip *);
				len = ntohs(h4->ip_len) - off;
				break;
#ifdef INET6
			case AF_INET6:
				h6 = mtod(m, struct ip6_hdr *);
				len = h6->ip6_plen - (off - sizeof(*h6));
				break;
#endif
			}

			if (pf_check_proto_cksum(m, off, len, IPPROTO_TCP, af))
				REASON_SET(&reason, PFRES_PROTCKSUM);
			else {
				if (th->th_flags & TH_SYN)
					ack++;
				if (th->th_flags & TH_FIN)
					ack++;
				pf_send_tcp(r, af, pd->dst,
				    pd->src, th->th_dport, th->th_sport,
				    ntohl(th->th_ack), ack, TH_RST|TH_ACK, 0, 0,
				    r->return_ttl, 1, 0, pd->eh, kif->pfik_ifp);
			}
		} else if (pd->proto != IPPROTO_ICMP && af == AF_INET &&
		    r->return_icmp)
			pf_send_icmp(m, r->return_icmp >> 8,
			    r->return_icmp & 255, af, r);
		else if (pd->proto != IPPROTO_ICMPV6 && af == AF_INET6 &&
		    r->return_icmp6)
			pf_send_icmp(m, r->return_icmp6 >> 8,
			    r->return_icmp6 & 255, af, r);
	}

	if (r->action == PF_DROP)
		goto cleanup;

	if (pf_tag_packet(m, tag, rtableid)) {
		REASON_SET(&reason, PFRES_MEMORY);
		goto cleanup;
	}

	if (!state_icmp && (r->keep_state || nr != NULL ||
	    (pd->flags & PFDESC_TCP_NORM))) {
		int action;
		action = pf_create_state(r, nr, a, pd, nsn, skw, sks, nk, sk, m,
		    off, sport, dport, &rewrite, kif, sm, tag, bproto_sum,
		    bip_sum, hdrlen);
		if (action != PF_PASS)
			return (action);
	}

	/* copy back packet headers if we performed NAT operations */
	if (rewrite)
		m_copyback(m, off, hdrlen, pd->hdr.any);

	return (PF_PASS);

cleanup:
	if (sk != NULL)
		kfree(sk, M_PFSTATEKEYPL);
	if (nk != NULL)
		kfree(nk, M_PFSTATEKEYPL);
	return (PF_DROP);
}

static __inline int
pf_create_state(struct pf_rule *r, struct pf_rule *nr, struct pf_rule *a,
    struct pf_pdesc *pd, struct pf_src_node *nsn, struct pf_state_key *skw,
    struct pf_state_key *sks, struct pf_state_key *nk, struct pf_state_key *sk,
    struct mbuf *m, int off, u_int16_t sport, u_int16_t dport, int *rewrite,
    struct pfi_kif *kif, struct pf_state **sm, int tag, u_int16_t bproto_sum,
    u_int16_t bip_sum, int hdrlen)
{
	struct pf_state		*s = NULL;
	struct pf_src_node	*sn = NULL;
	struct tcphdr		*th = pd->hdr.tcp;
	u_int16_t		 mss = tcp_mssdflt;
	u_short			 reason;
	int cpu = mycpu->gd_cpuid;

	/* check maximums */
	if (r->max_states && (r->states_cur >= r->max_states)) {
		PF_INC_LCOUNTER(LCNT_STATES);
		REASON_SET(&reason, PFRES_MAXSTATES);
		return (PF_DROP);
	}
	/* src node for filter rule */
	if ((r->rule_flag & PFRULE_SRCTRACK ||
	    r->rpool.opts & PF_POOL_STICKYADDR) &&
	    pf_insert_src_node(&sn, r, pd->src, pd->af) != 0) {
		REASON_SET(&reason, PFRES_SRCLIMIT);
		goto csfailed;
	}
	/* src node for translation rule */
	if (nr != NULL && (nr->rpool.opts & PF_POOL_STICKYADDR) &&
	    pf_insert_src_node(&nsn, nr, &sk->addr[pd->sidx], pd->af)) {
		REASON_SET(&reason, PFRES_SRCLIMIT);
		goto csfailed;
	}
	s = kmalloc(sizeof(struct pf_state), M_PFSTATEPL, M_NOWAIT|M_ZERO);
	if (s == NULL) {
		REASON_SET(&reason, PFRES_MEMORY);
		goto csfailed;
	}
	lockinit(&s->lk, "pfstlk", 0, 0);
	s->id = 0; /* XXX Do we really need that? not in OpenBSD */
	s->creatorid = 0;
	s->rule.ptr = r;
	s->nat_rule.ptr = nr;
	s->anchor.ptr = a;
	s->state_flags = PFSTATE_CREATEINPROG;
	STATE_INC_COUNTERS(s);
	if (r->allow_opts)
		s->state_flags |= PFSTATE_ALLOWOPTS;
	if (r->rule_flag & PFRULE_STATESLOPPY)
		s->state_flags |= PFSTATE_SLOPPY;
	if (pd->not_cpu_localized)
		s->state_flags |= PFSTATE_STACK_GLOBAL;

	s->log = r->log & PF_LOG_ALL;
	if (nr != NULL)
		s->log |= nr->log & PF_LOG_ALL;
	switch (pd->proto) {
	case IPPROTO_TCP:
		s->src.seqlo = ntohl(th->th_seq);
		s->src.seqhi = s->src.seqlo + pd->p_len + 1;
		if ((th->th_flags & (TH_SYN|TH_ACK)) == TH_SYN &&
		    r->keep_state == PF_STATE_MODULATE) {
			/* Generate sequence number modulator */
			if ((s->src.seqdiff = pf_tcp_iss(pd) - s->src.seqlo) ==
			    0)
				s->src.seqdiff = 1;
			pf_change_a(&th->th_seq, &th->th_sum,
			    htonl(s->src.seqlo + s->src.seqdiff), 0);
			*rewrite = 1;
		} else
			s->src.seqdiff = 0;
		if (th->th_flags & TH_SYN) {
			s->src.seqhi++;
			s->src.wscale = pf_get_wscale(m, off,
			    th->th_off, pd->af);
		}
		s->src.max_win = MAX(ntohs(th->th_win), 1);
		if (s->src.wscale & PF_WSCALE_MASK) {
			/* Remove scale factor from initial window */
			int win = s->src.max_win;
			win += 1 << (s->src.wscale & PF_WSCALE_MASK);
			s->src.max_win = (win - 1) >>
			    (s->src.wscale & PF_WSCALE_MASK);
		}
		if (th->th_flags & TH_FIN)
			s->src.seqhi++;
		s->dst.seqhi = 1;
		s->dst.max_win = 1;
		s->src.state = TCPS_SYN_SENT;
		s->dst.state = TCPS_CLOSED;
		s->timeout = PFTM_TCP_FIRST_PACKET;
		break;
	case IPPROTO_UDP:
		s->src.state = PFUDPS_SINGLE;
		s->dst.state = PFUDPS_NO_TRAFFIC;
		s->timeout = PFTM_UDP_FIRST_PACKET;
		break;
	case IPPROTO_ICMP:
#ifdef INET6
	case IPPROTO_ICMPV6:
#endif
		s->timeout = PFTM_ICMP_FIRST_PACKET;
		break;
	default:
		s->src.state = PFOTHERS_SINGLE;
		s->dst.state = PFOTHERS_NO_TRAFFIC;
		s->timeout = PFTM_OTHER_FIRST_PACKET;
	}

	s->creation = time_second;
	s->expire = time_second;

	if (sn != NULL) {
		s->src_node = sn;
		s->src_node->states++;
	}
	if (nsn != NULL) {
		/* XXX We only modify one side for now. */
		PF_ACPY(&nsn->raddr, &nk->addr[1], pd->af);
		s->nat_src_node = nsn;
		s->nat_src_node->states++;
	}
	if (pd->proto == IPPROTO_TCP) {
		if ((pd->flags & PFDESC_TCP_NORM) && pf_normalize_tcp_init(m,
		    off, pd, th, &s->src, &s->dst)) {
			REASON_SET(&reason, PFRES_MEMORY);
			pf_src_tree_remove_state(s);
			STATE_DEC_COUNTERS(s);
			kfree(s, M_PFSTATEPL);
			return (PF_DROP);
		}
		if ((pd->flags & PFDESC_TCP_NORM) && s->src.scrub &&
		    pf_normalize_tcp_stateful(m, off, pd, &reason, th, s,
		    &s->src, &s->dst, rewrite)) {
			/* This really shouldn't happen!!! */
			DPFPRINTF(PF_DEBUG_URGENT,
			    ("pf_normalize_tcp_stateful failed on first pkt"));
			pf_normalize_tcp_cleanup(s);
			pf_src_tree_remove_state(s);
			STATE_DEC_COUNTERS(s);
			kfree(s, M_PFSTATEPL);
			return (PF_DROP);
		}
	}
	s->direction = pd->dir;

	if (sk == NULL && pf_state_key_setup(pd, nr, &skw, &sks, &sk, &nk,
					     pd->src, pd->dst, sport, dport)) {
		REASON_SET(&reason, PFRES_MEMORY);
		goto csfailed;
	}

	if (pf_state_insert(BOUND_IFACE(r, kif), skw, sks, s)) {
		if (pd->proto == IPPROTO_TCP)
			pf_normalize_tcp_cleanup(s);
		REASON_SET(&reason, PFRES_STATEINS);
		pf_src_tree_remove_state(s);
		STATE_DEC_COUNTERS(s);
		kfree(s, M_PFSTATEPL);
		return (PF_DROP);
	} else
		*sm = s;

	pf_set_rt_ifp(s, pd->src);	/* needs s->state_key set */
	if (tag > 0) {
		pf_tag_ref(tag);
		s->tag = tag;
	}
	if (pd->proto == IPPROTO_TCP && (th->th_flags & (TH_SYN|TH_ACK)) ==
	    TH_SYN && r->keep_state == PF_STATE_SYNPROXY) {
		s->src.state = PF_TCPS_PROXY_SRC;
		/* undo NAT changes, if they have taken place */
		if (nr != NULL) {
			struct pf_state_key *skt = s->key[PF_SK_WIRE];
			if (pd->dir == PF_OUT)
				skt = s->key[PF_SK_STACK];
			PF_ACPY(pd->src, &skt->addr[pd->sidx], pd->af);
			PF_ACPY(pd->dst, &skt->addr[pd->didx], pd->af);
			if (pd->sport)
				*pd->sport = skt->port[pd->sidx];
			if (pd->dport)
				*pd->dport = skt->port[pd->didx];
			if (pd->proto_sum)
				*pd->proto_sum = bproto_sum;
			if (pd->ip_sum)
				*pd->ip_sum = bip_sum;
			m->m_flags &= ~M_HASH;
			m_copyback(m, off, hdrlen, pd->hdr.any);
		}
		s->src.seqhi = htonl(karc4random());
		/* Find mss option */
		mss = pf_get_mss(m, off, th->th_off, pd->af);
		mss = pf_calc_mss(pd->src, pd->af, mss);
		mss = pf_calc_mss(pd->dst, pd->af, mss);
		s->src.mss = mss;
		s->state_flags &= ~PFSTATE_CREATEINPROG;
		pf_send_tcp(r, pd->af, pd->dst, pd->src, th->th_dport,
			    th->th_sport, s->src.seqhi, ntohl(th->th_seq) + 1,
			    TH_SYN|TH_ACK, 0, s->src.mss, 0, 1, 0, NULL, NULL);
		REASON_SET(&reason, PFRES_SYNPROXY);
		return (PF_SYNPROXY_DROP);
	}

	s->state_flags &= ~PFSTATE_CREATEINPROG;
	return (PF_PASS);

csfailed:
	if (sk != NULL)
		kfree(sk, M_PFSTATEKEYPL);
	if (nk != NULL)
		kfree(nk, M_PFSTATEKEYPL);

	if (sn != NULL && sn->states == 0 && sn->expire == 0) {
		RB_REMOVE(pf_src_tree, &tree_src_tracking[cpu], sn);
		PF_INC_SCOUNTER(SCNT_SRC_NODE_REMOVALS);
		atomic_add_int(&pf_status.src_nodes, -1);
		kfree(sn, M_PFSRCTREEPL);
	}
	if (nsn != sn && nsn != NULL && nsn->states == 0 && nsn->expire == 0) {
		RB_REMOVE(pf_src_tree, &tree_src_tracking[cpu], nsn);
		PF_INC_SCOUNTER(SCNT_SRC_NODE_REMOVALS);
		atomic_add_int(&pf_status.src_nodes, -1);
		kfree(nsn, M_PFSRCTREEPL);
	}
	if (s) {
		pf_src_tree_remove_state(s);
		STATE_DEC_COUNTERS(s);
		kfree(s, M_PFSTATEPL);
	}

	return (PF_DROP);
}

int
pf_test_fragment(struct pf_rule **rm, int direction, struct pfi_kif *kif,
    struct mbuf *m, void *h, struct pf_pdesc *pd, struct pf_rule **am,
    struct pf_ruleset **rsm)
{
	struct pf_rule		*r, *a = NULL;
	struct pf_ruleset	*ruleset = NULL;
	sa_family_t		 af = pd->af;
	u_short			 reason;
	int			 tag = -1;
	int			 asd = 0;
	int			 match = 0;

	r = TAILQ_FIRST(pf_main_ruleset.rules[PF_RULESET_FILTER].active.ptr);
	while (r != NULL) {
		r->evaluations++;
		if (pfi_kif_match(r->kif, kif) == r->ifnot)
			r = r->skip[PF_SKIP_IFP].ptr;
		else if (r->direction && r->direction != direction)
			r = r->skip[PF_SKIP_DIR].ptr;
		else if (r->af && r->af != af)
			r = r->skip[PF_SKIP_AF].ptr;
		else if (r->proto && r->proto != pd->proto)
			r = r->skip[PF_SKIP_PROTO].ptr;
		else if (PF_MISMATCHAW(&r->src.addr, pd->src, af,
		    r->src.neg, kif))
			r = r->skip[PF_SKIP_SRC_ADDR].ptr;
		else if (PF_MISMATCHAW(&r->dst.addr, pd->dst, af,
		    r->dst.neg, NULL))
			r = r->skip[PF_SKIP_DST_ADDR].ptr;
		else if (r->tos && !(r->tos == pd->tos))
			r = TAILQ_NEXT(r, entries);
		else if (r->os_fingerprint != PF_OSFP_ANY)
			r = TAILQ_NEXT(r, entries);
		else if (pd->proto == IPPROTO_UDP &&
		    (r->src.port_op || r->dst.port_op))
			r = TAILQ_NEXT(r, entries);
		else if (pd->proto == IPPROTO_TCP &&
		    (r->src.port_op || r->dst.port_op || r->flagset))
			r = TAILQ_NEXT(r, entries);
		else if ((pd->proto == IPPROTO_ICMP ||
		    pd->proto == IPPROTO_ICMPV6) &&
		    (r->type || r->code))
			r = TAILQ_NEXT(r, entries);
		else if (r->prob && r->prob <= karc4random())
			r = TAILQ_NEXT(r, entries);
		else if (r->match_tag && !pf_match_tag(m, r, &tag))
			r = TAILQ_NEXT(r, entries);
		else {
			if (r->anchor == NULL) {
				match = 1;
				*rm = r;
				*am = a;
				*rsm = ruleset;
				if ((*rm)->quick)
					break;
				r = TAILQ_NEXT(r, entries);
			} else
				pf_step_into_anchor(&asd, &ruleset,
				    PF_RULESET_FILTER, &r, &a, &match);
		}
		if (r == NULL && pf_step_out_of_anchor(&asd, &ruleset,
		    PF_RULESET_FILTER, &r, &a, &match))
			break;
	}
	r = *rm;
	a = *am;
	ruleset = *rsm;

	REASON_SET(&reason, PFRES_MATCH);

	if (r->log)
		PFLOG_PACKET(kif, h, m, af, direction, reason, r, a, ruleset,
		    pd);

	if (r->action != PF_PASS)
		return (PF_DROP);

	if (pf_tag_packet(m, tag, -1)) {
		REASON_SET(&reason, PFRES_MEMORY);
		return (PF_DROP);
	}

	return (PF_PASS);
}

/*
 * Called with state locked
 */
int
pf_tcp_track_full(struct pf_state_peer *src, struct pf_state_peer *dst,
	struct pf_state **state, struct pfi_kif *kif, struct mbuf *m, int off,
	struct pf_pdesc *pd, u_short *reason, int *copyback)
{
	struct tcphdr		*th = pd->hdr.tcp;
	u_int16_t		 win = ntohs(th->th_win);
	u_int32_t		 ack, end, seq, orig_seq;
	u_int8_t		 sws, dws;
	int			 ackskew;

	if (src->wscale && dst->wscale && !(th->th_flags & TH_SYN)) {
		sws = src->wscale & PF_WSCALE_MASK;
		dws = dst->wscale & PF_WSCALE_MASK;
	} else {
		sws = dws = 0;
	}

	/*
	 * Sequence tracking algorithm from Guido van Rooij's paper:
	 *   http://www.madison-gurkha.com/publications/tcp_filtering/
	 *	tcp_filtering.ps
	 */

	orig_seq = seq = ntohl(th->th_seq);
	if (src->seqlo == 0) {
		/* First packet from this end. Set its state */

		if ((pd->flags & PFDESC_TCP_NORM || dst->scrub) &&
		    src->scrub == NULL) {
			if (pf_normalize_tcp_init(m, off, pd, th, src, dst)) {
				REASON_SET(reason, PFRES_MEMORY);
				return (PF_DROP);
			}
		}

		/* Deferred generation of sequence number modulator */
		if (dst->seqdiff && !src->seqdiff) {
			/* use random iss for the TCP server */
			while ((src->seqdiff = karc4random() - seq) == 0)
				;
			ack = ntohl(th->th_ack) - dst->seqdiff;
			pf_change_a(&th->th_seq, &th->th_sum, htonl(seq +
			    src->seqdiff), 0);
			pf_change_a(&th->th_ack, &th->th_sum, htonl(ack), 0);
			*copyback = 1;
		} else {
			ack = ntohl(th->th_ack);
		}

		end = seq + pd->p_len;
		if (th->th_flags & TH_SYN) {
			end++;
			(*state)->sync_flags |= PFSTATE_GOT_SYN2;
			if (dst->wscale & PF_WSCALE_FLAG) {
				src->wscale = pf_get_wscale(m, off, th->th_off,
				    pd->af);
				if (src->wscale & PF_WSCALE_FLAG) {
					/* Remove scale factor from initial
					 * window */
					sws = src->wscale & PF_WSCALE_MASK;
					win = ((u_int32_t)win + (1 << sws) - 1)
					    >> sws;
					dws = dst->wscale & PF_WSCALE_MASK;
				} else {
					/* fixup other window */
					dst->max_win <<= dst->wscale &
					    PF_WSCALE_MASK;
					/* in case of a retrans SYN|ACK */
					dst->wscale = 0;
				}
			}
		}
		if (th->th_flags & TH_FIN)
			end++;

		src->seqlo = seq;
		if (src->state < TCPS_SYN_SENT)
			src->state = TCPS_SYN_SENT;

		/*
		 * May need to slide the window (seqhi may have been set by
		 * the crappy stack check or if we picked up the connection
		 * after establishment)
		 */
		if (src->seqhi == 1 ||
		    SEQ_GEQ(end + MAX(1, dst->max_win << dws), src->seqhi))
			src->seqhi = end + MAX(1, dst->max_win << dws);
		if (win > src->max_win)
			src->max_win = win;

	} else {
		ack = ntohl(th->th_ack) - dst->seqdiff;
		if (src->seqdiff) {
			/* Modulate sequence numbers */
			pf_change_a(&th->th_seq, &th->th_sum, htonl(seq +
			    src->seqdiff), 0);
			pf_change_a(&th->th_ack, &th->th_sum, htonl(ack), 0);
			*copyback = 1;
		}
		end = seq + pd->p_len;
		if (th->th_flags & TH_SYN)
			end++;
		if (th->th_flags & TH_FIN)
			end++;
	}

	if ((th->th_flags & TH_ACK) == 0) {
		/* Let it pass through the ack skew check */
		ack = dst->seqlo;
	} else if ((ack == 0 &&
	    (th->th_flags & (TH_ACK|TH_RST)) == (TH_ACK|TH_RST)) ||
	    /* broken tcp stacks do not set ack */
	    (dst->state < TCPS_SYN_SENT)) {
		/*
		 * Many stacks (ours included) will set the ACK number in an
		 * FIN|ACK if the SYN times out -- no sequence to ACK.
		 */
		ack = dst->seqlo;
	}

	if (seq == end) {
		/* Ease sequencing restrictions on no data packets */
		seq = src->seqlo;
		end = seq;
	}

	ackskew = dst->seqlo - ack;


	/*
	 * Need to demodulate the sequence numbers in any TCP SACK options
	 * (Selective ACK). We could optionally validate the SACK values
	 * against the current ACK window, either forwards or backwards, but
	 * I'm not confident that SACK has been implemented properly
	 * everywhere. It wouldn't surprise me if several stacks accidently
	 * SACK too far backwards of previously ACKed data. There really aren't
	 * any security implications of bad SACKing unless the target stack
	 * doesn't validate the option length correctly. Someone trying to
	 * spoof into a TCP connection won't bother blindly sending SACK
	 * options anyway.
	 */
	if (dst->seqdiff && (th->th_off << 2) > sizeof(struct tcphdr)) {
		if (pf_modulate_sack(m, off, pd, th, dst))
			*copyback = 1;
	}


#define MAXACKWINDOW (0xffff + 1500)	/* 1500 is an arbitrary fudge factor */
	if (SEQ_GEQ(src->seqhi, end) &&
	    /* Last octet inside other's window space */
	    SEQ_GEQ(seq, src->seqlo - (dst->max_win << dws)) &&
	    /* Retrans: not more than one window back */
	    (ackskew >= -MAXACKWINDOW) &&
	    /* Acking not more than one reassembled fragment backwards */
	    (ackskew <= (MAXACKWINDOW << sws)) &&
	    /* Acking not more than one window forward */
	    ((th->th_flags & TH_RST) == 0 || orig_seq == src->seqlo ||
	    (orig_seq == src->seqlo + 1) || (orig_seq + 1 == src->seqlo) ||
	    (pd->flags & PFDESC_IP_REAS) == 0)) {
	    /* Require an exact/+1 sequence match on resets when possible */

		if (dst->scrub || src->scrub) {
			if (pf_normalize_tcp_stateful(m, off, pd, reason, th,
			    *state, src, dst, copyback))
				return (PF_DROP);
		}

		/* update max window */
		if (src->max_win < win)
			src->max_win = win;
		/* synchronize sequencing */
		if (SEQ_GT(end, src->seqlo))
			src->seqlo = end;
		/* slide the window of what the other end can send */
		if (SEQ_GEQ(ack + (win << sws), dst->seqhi))
			dst->seqhi = ack + MAX((win << sws), 1);


		/* update states */
		if (th->th_flags & TH_SYN)
			if (src->state < TCPS_SYN_SENT)
				src->state = TCPS_SYN_SENT;
		if (th->th_flags & TH_FIN)
			if (src->state < TCPS_CLOSING)
				src->state = TCPS_CLOSING;
		if (th->th_flags & TH_ACK) {
			if (dst->state == TCPS_SYN_SENT) {
				dst->state = TCPS_ESTABLISHED;
				if (src->state == TCPS_ESTABLISHED &&
				    (*state)->src_node != NULL &&
				    pf_src_connlimit(*state)) {
					REASON_SET(reason, PFRES_SRCLIMIT);
					return (PF_DROP);
				}
			} else if (dst->state == TCPS_CLOSING)
				dst->state = TCPS_FIN_WAIT_2;
		}
		if (th->th_flags & TH_RST)
			src->state = dst->state = TCPS_TIME_WAIT;

		/* update expire time */
		(*state)->expire = time_second;
		if (src->state >= TCPS_FIN_WAIT_2 &&
		    dst->state >= TCPS_FIN_WAIT_2)
			(*state)->timeout = PFTM_TCP_CLOSED;
		else if (src->state >= TCPS_CLOSING &&
		    dst->state >= TCPS_CLOSING)
			(*state)->timeout = PFTM_TCP_FIN_WAIT;
		else if (src->state < TCPS_ESTABLISHED ||
		    dst->state < TCPS_ESTABLISHED)
			(*state)->timeout = PFTM_TCP_OPENING;
		else if (src->state >= TCPS_CLOSING ||
		    dst->state >= TCPS_CLOSING)
			(*state)->timeout = PFTM_TCP_CLOSING;
		else if ((th->th_flags & TH_SYN) &&
			 ((*state)->state_flags & PFSTATE_SLOPPY))
			(*state)->timeout = PFTM_TCP_FIRST_PACKET;
		else
			(*state)->timeout = PFTM_TCP_ESTABLISHED;

		/* Fall through to PASS packet */

	} else if ((dst->state < TCPS_SYN_SENT ||
		dst->state >= TCPS_FIN_WAIT_2 ||
		src->state >= TCPS_FIN_WAIT_2) &&
	    SEQ_GEQ(src->seqhi + MAXACKWINDOW, end) &&
	    /* Within a window forward of the originating packet */
	    SEQ_GEQ(seq, src->seqlo - MAXACKWINDOW)) {
	    /* Within a window backward of the originating packet */

		/*
		 * This currently handles three situations:
		 *  1) Stupid stacks will shotgun SYNs before their peer
		 *     replies.
		 *  2) When PF catches an already established stream (the
		 *     firewall rebooted, the state table was flushed, routes
		 *     changed...)
		 *  3) Packets get funky immediately after the connection
		 *     closes (this should catch Solaris spurious ACK|FINs
		 *     that web servers like to spew after a close)
		 *
		 * This must be a little more careful than the above code
		 * since packet floods will also be caught here. We don't
		 * update the TTL here to mitigate the damage of a packet
		 * flood and so the same code can handle awkward establishment
		 * and a loosened connection close.
		 * In the establishment case, a correct peer response will
		 * validate the connection, go through the normal state code
		 * and keep updating the state TTL.
		 */

		if (pf_status.debug >= PF_DEBUG_MISC) {
			kprintf("pf: loose state match: ");
			pf_print_state(*state);
			pf_print_flags(th->th_flags);
			kprintf(" seq=%u (%u) ack=%u len=%u ackskew=%d "
			    "pkts=%llu:%llu dir=%s,%s\n", seq, orig_seq, ack, pd->p_len,
			    ackskew, (unsigned long long)(*state)->packets[0],
			    (unsigned long long)(*state)->packets[1],
			    pd->dir == PF_IN ? "in" : "out",
			    pd->dir == (*state)->direction ? "fwd" : "rev");
		}

		if (dst->scrub || src->scrub) {
			if (pf_normalize_tcp_stateful(m, off, pd, reason, th,
			    *state, src, dst, copyback))
				return (PF_DROP);
		}

		/* update max window */
		if (src->max_win < win)
			src->max_win = win;
		/* synchronize sequencing */
		if (SEQ_GT(end, src->seqlo))
			src->seqlo = end;
		/* slide the window of what the other end can send */
		if (SEQ_GEQ(ack + (win << sws), dst->seqhi))
			dst->seqhi = ack + MAX((win << sws), 1);

		/*
		 * Cannot set dst->seqhi here since this could be a shotgunned
		 * SYN and not an already established connection.
		 */

		if (th->th_flags & TH_FIN)
			if (src->state < TCPS_CLOSING)
				src->state = TCPS_CLOSING;
		if (th->th_flags & TH_RST)
			src->state = dst->state = TCPS_TIME_WAIT;

		/* Fall through to PASS packet */

	} else if ((*state)->pickup_mode == PF_PICKUPS_HASHONLY ||
		    ((*state)->pickup_mode == PF_PICKUPS_ENABLED &&
		     ((*state)->sync_flags & PFSTATE_GOT_SYN_MASK) !=
		      PFSTATE_GOT_SYN_MASK)) {
		/*
		 * If pickup mode is hash only, do not fail on sequence checks.
		 *
		 * If pickup mode is enabled and we did not see the SYN in
		 * both direction, do not fail on sequence checks because
		 * we do not have complete information on window scale.
		 *
		 * Adjust expiration and fall through to PASS packet.
		 * XXX Add a FIN check to reduce timeout?
		 */
		(*state)->expire = time_second;
	} else  {
		/*
		 * Failure processing
		 */
		if ((*state)->dst.state == TCPS_SYN_SENT &&
		    (*state)->src.state == TCPS_SYN_SENT) {
			/* Send RST for state mismatches during handshake */
			if (!(th->th_flags & TH_RST))
				pf_send_tcp((*state)->rule.ptr, pd->af,
				    pd->dst, pd->src, th->th_dport,
				    th->th_sport, ntohl(th->th_ack), 0,
				    TH_RST, 0, 0,
				    (*state)->rule.ptr->return_ttl, 1, 0,
				    pd->eh, kif->pfik_ifp);
			src->seqlo = 0;
			src->seqhi = 1;
			src->max_win = 1;
		} else if (pf_status.debug >= PF_DEBUG_MISC) {
			kprintf("pf: BAD state: ");
			pf_print_state(*state);
			pf_print_flags(th->th_flags);
			kprintf(" seq=%u (%u) ack=%u len=%u ackskew=%d "
			    "pkts=%llu:%llu dir=%s,%s\n",
			    seq, orig_seq, ack, pd->p_len, ackskew,
			    (unsigned long long)(*state)->packets[0],
				(unsigned long long)(*state)->packets[1],
			    pd->dir == PF_IN ? "in" : "out",
			    pd->dir == (*state)->direction ? "fwd" : "rev");
			kprintf("pf: State failure on: %c %c %c %c | %c %c\n",
			    SEQ_GEQ(src->seqhi, end) ? ' ' : '1',
			    SEQ_GEQ(seq, src->seqlo - (dst->max_win << dws)) ?
			    ' ': '2',
			    (ackskew >= -MAXACKWINDOW) ? ' ' : '3',
			    (ackskew <= (MAXACKWINDOW << sws)) ? ' ' : '4',
			    SEQ_GEQ(src->seqhi + MAXACKWINDOW, end) ?' ' :'5',
			    SEQ_GEQ(seq, src->seqlo - MAXACKWINDOW) ?' ' :'6');
		}
		REASON_SET(reason, PFRES_BADSTATE);
		return (PF_DROP);
	}

	return (PF_PASS);
}

/*
 * Called with state locked
 */
int
pf_tcp_track_sloppy(struct pf_state_peer *src, struct pf_state_peer *dst,
	struct pf_state **state, struct pf_pdesc *pd, u_short *reason)
{
	struct tcphdr		*th = pd->hdr.tcp;

	if (th->th_flags & TH_SYN)
		if (src->state < TCPS_SYN_SENT)
			src->state = TCPS_SYN_SENT;
	if (th->th_flags & TH_FIN)
		if (src->state < TCPS_CLOSING)
			src->state = TCPS_CLOSING;
	if (th->th_flags & TH_ACK) {
		if (dst->state == TCPS_SYN_SENT) {
			dst->state = TCPS_ESTABLISHED;
			if (src->state == TCPS_ESTABLISHED &&
			    (*state)->src_node != NULL &&
			    pf_src_connlimit(*state)) {
				REASON_SET(reason, PFRES_SRCLIMIT);
				return (PF_DROP);
			}
		} else if (dst->state == TCPS_CLOSING) {
			dst->state = TCPS_FIN_WAIT_2;
		} else if (src->state == TCPS_SYN_SENT &&
		    dst->state < TCPS_SYN_SENT) {
			/*
			 * Handle a special sloppy case where we only see one
			 * half of the connection. If there is a ACK after
			 * the initial SYN without ever seeing a packet from
			 * the destination, set the connection to established.
			 */
			dst->state = src->state = TCPS_ESTABLISHED;
			if ((*state)->src_node != NULL &&
			    pf_src_connlimit(*state)) {
				REASON_SET(reason, PFRES_SRCLIMIT);
				return (PF_DROP);
			}
		} else if (src->state == TCPS_CLOSING &&
		    dst->state == TCPS_ESTABLISHED &&
		    dst->seqlo == 0) {
			/*
			 * Handle the closing of half connections where we
			 * don't see the full bidirectional FIN/ACK+ACK
			 * handshake.
			 */
			dst->state = TCPS_CLOSING;
		}
	}
	if (th->th_flags & TH_RST)
		src->state = dst->state = TCPS_TIME_WAIT;

	/* update expire time */
	(*state)->expire = time_second;
	if (src->state >= TCPS_FIN_WAIT_2 &&
	    dst->state >= TCPS_FIN_WAIT_2)
		(*state)->timeout = PFTM_TCP_CLOSED;
	else if (src->state >= TCPS_CLOSING &&
	    dst->state >= TCPS_CLOSING)
		(*state)->timeout = PFTM_TCP_FIN_WAIT;
	else if (src->state < TCPS_ESTABLISHED ||
	    dst->state < TCPS_ESTABLISHED)
		(*state)->timeout = PFTM_TCP_OPENING;
	else if (src->state >= TCPS_CLOSING ||
	    dst->state >= TCPS_CLOSING)
		(*state)->timeout = PFTM_TCP_CLOSING;
	else if ((th->th_flags & TH_SYN) &&
		 ((*state)->state_flags & PFSTATE_SLOPPY))
		(*state)->timeout = PFTM_TCP_FIRST_PACKET;
	else
		(*state)->timeout = PFTM_TCP_ESTABLISHED;

	return (PF_PASS);
}

/*
 * Test TCP connection state.  Caller must hold the state locked.
 */
int
pf_test_state_tcp(struct pf_state **state, int direction, struct pfi_kif *kif,
		  struct mbuf *m, int off, void *h, struct pf_pdesc *pd,
		  u_short *reason)
{
	struct pf_state_key_cmp	 key;
	struct tcphdr		*th = pd->hdr.tcp;
	int			 copyback = 0;
	int			 error;
	struct pf_state_peer	*src, *dst;
	struct pf_state_key	*sk;

	bzero(&key, sizeof(key));
	key.af = pd->af;
	key.proto = IPPROTO_TCP;
	if (direction == PF_IN)	{	/* wire side, straight */
		PF_ACPY(&key.addr[0], pd->src, key.af);
		PF_ACPY(&key.addr[1], pd->dst, key.af);
		key.port[0] = th->th_sport;
		key.port[1] = th->th_dport;
		if (pf_status.debug >= PF_DEBUG_MISC) {
			kprintf("test-tcp IN (%08x:%d) -> (%08x:%d)\n",
				ntohl(key.addr[0].addr32[0]),
				ntohs(key.port[0]),
				ntohl(key.addr[1].addr32[0]),
				ntohs(key.port[1]));
		}
	} else {			/* stack side, reverse */
		PF_ACPY(&key.addr[1], pd->src, key.af);
		PF_ACPY(&key.addr[0], pd->dst, key.af);
		key.port[1] = th->th_sport;
		key.port[0] = th->th_dport;
		if (pf_status.debug >= PF_DEBUG_MISC) {
			kprintf("test-tcp OUT (%08x:%d) <- (%08x:%d)\n",
				ntohl(key.addr[0].addr32[0]),
				ntohs(key.port[0]),
				ntohl(key.addr[1].addr32[0]),
				ntohs(key.port[1]));
		}
	}

	STATE_LOOKUP(kif, &key, direction, *state, m);
	lockmgr(&(*state)->lk, LK_EXCLUSIVE);

	if (direction == (*state)->direction) {
		src = &(*state)->src;
		dst = &(*state)->dst;
	} else {
		src = &(*state)->dst;
		dst = &(*state)->src;
	}

	sk = (*state)->key[pd->didx];

	if ((*state)->src.state == PF_TCPS_PROXY_SRC) {
		if (direction != (*state)->direction) {
			REASON_SET(reason, PFRES_SYNPROXY);
			FAIL (PF_SYNPROXY_DROP);
		}
		if (th->th_flags & TH_SYN) {
			if (ntohl(th->th_seq) != (*state)->src.seqlo) {
				REASON_SET(reason, PFRES_SYNPROXY);
				FAIL (PF_DROP);
			}
			pf_send_tcp((*state)->rule.ptr, pd->af, pd->dst,
			    pd->src, th->th_dport, th->th_sport,
			    (*state)->src.seqhi, ntohl(th->th_seq) + 1,
			    TH_SYN|TH_ACK, 0, (*state)->src.mss, 0, 1,
			    0, NULL, NULL);
			REASON_SET(reason, PFRES_SYNPROXY);
			FAIL (PF_SYNPROXY_DROP);
		} else if (!(th->th_flags & TH_ACK) ||
		    (ntohl(th->th_ack) != (*state)->src.seqhi + 1) ||
		    (ntohl(th->th_seq) != (*state)->src.seqlo + 1)) {
			REASON_SET(reason, PFRES_SYNPROXY);
			FAIL (PF_DROP);
		} else if ((*state)->src_node != NULL &&
		    pf_src_connlimit(*state)) {
			REASON_SET(reason, PFRES_SRCLIMIT);
			FAIL (PF_DROP);
		} else
			(*state)->src.state = PF_TCPS_PROXY_DST;
	}
	if ((*state)->src.state == PF_TCPS_PROXY_DST) {
		if (direction == (*state)->direction) {
			if (((th->th_flags & (TH_SYN|TH_ACK)) != TH_ACK) ||
			    (ntohl(th->th_ack) != (*state)->src.seqhi + 1) ||
			    (ntohl(th->th_seq) != (*state)->src.seqlo + 1)) {
				REASON_SET(reason, PFRES_SYNPROXY);
				FAIL (PF_DROP);
			}
			(*state)->src.max_win = MAX(ntohs(th->th_win), 1);
			if ((*state)->dst.seqhi == 1)
				(*state)->dst.seqhi = htonl(karc4random());
			pf_send_tcp((*state)->rule.ptr, pd->af,
			    &sk->addr[pd->sidx], &sk->addr[pd->didx],
			    sk->port[pd->sidx], sk->port[pd->didx],
			    (*state)->dst.seqhi, 0, TH_SYN, 0,
			    (*state)->src.mss, 0, 0, (*state)->tag, NULL, NULL);
			REASON_SET(reason, PFRES_SYNPROXY);
			FAIL (PF_SYNPROXY_DROP);
		} else if (((th->th_flags & (TH_SYN|TH_ACK)) !=
		    (TH_SYN|TH_ACK)) ||
		    (ntohl(th->th_ack) != (*state)->dst.seqhi + 1)) {
			REASON_SET(reason, PFRES_SYNPROXY);
			FAIL (PF_DROP);
		} else {
			(*state)->dst.max_win = MAX(ntohs(th->th_win), 1);
			(*state)->dst.seqlo = ntohl(th->th_seq);
			pf_send_tcp((*state)->rule.ptr, pd->af, pd->dst,
			    pd->src, th->th_dport, th->th_sport,
			    ntohl(th->th_ack), ntohl(th->th_seq) + 1,
			    TH_ACK, (*state)->src.max_win, 0, 0, 0,
			    (*state)->tag, NULL, NULL);
			pf_send_tcp((*state)->rule.ptr, pd->af,
			    &sk->addr[pd->sidx], &sk->addr[pd->didx],
			    sk->port[pd->sidx], sk->port[pd->didx],
			    (*state)->src.seqhi + 1, (*state)->src.seqlo + 1,
			    TH_ACK, (*state)->dst.max_win, 0, 0, 1,
			    0, NULL, NULL);
			(*state)->src.seqdiff = (*state)->dst.seqhi -
			    (*state)->src.seqlo;
			(*state)->dst.seqdiff = (*state)->src.seqhi -
			    (*state)->dst.seqlo;
			(*state)->src.seqhi = (*state)->src.seqlo +
			    (*state)->dst.max_win;
			(*state)->dst.seqhi = (*state)->dst.seqlo +
			    (*state)->src.max_win;
			(*state)->src.wscale = (*state)->dst.wscale = 0;
			(*state)->src.state = (*state)->dst.state =
			    TCPS_ESTABLISHED;
			REASON_SET(reason, PFRES_SYNPROXY);
			FAIL (PF_SYNPROXY_DROP);
		}
	}

	/*
	 * Check for connection (addr+port pair) reuse.  We can't actually
	 * unlink the state if we don't own it.
	 */
	if (((th->th_flags & (TH_SYN|TH_ACK)) == TH_SYN) &&
	    dst->state >= TCPS_FIN_WAIT_2 &&
	    src->state >= TCPS_FIN_WAIT_2) {
		if (pf_status.debug >= PF_DEBUG_MISC) {
			kprintf("pf: state reuse ");
			pf_print_state(*state);
			pf_print_flags(th->th_flags);
			kprintf("\n");
		}
		/* XXX make sure it's the same direction ?? */
		(*state)->src.state = (*state)->dst.state = TCPS_CLOSED;
		if ((*state)->cpuid == mycpu->gd_cpuid) {
			pf_unlink_state(*state);
			*state = NULL;
		} else {
			(*state)->timeout = PFTM_PURGE;
		}
		FAIL (PF_DROP);
	}

	if ((*state)->state_flags & PFSTATE_SLOPPY) {
		if (pf_tcp_track_sloppy(src, dst, state, pd,
					reason) == PF_DROP) {
			FAIL (PF_DROP);
		}
	} else {
		if (pf_tcp_track_full(src, dst, state, kif, m, off, pd,
				      reason, &copyback) == PF_DROP) {
			FAIL (PF_DROP);
		}
	}

	/* translate source/destination address, if necessary */
	if ((*state)->key[PF_SK_WIRE] != (*state)->key[PF_SK_STACK]) {
		struct pf_state_key *nk = (*state)->key[pd->didx];

		if (PF_ANEQ(pd->src, &nk->addr[pd->sidx], pd->af) ||
		    nk->port[pd->sidx] != th->th_sport)  {
			/*
			 * The translated source address may be completely
			 * unrelated to the saved link header, make sure
			 * a bridge doesn't try to use it.
			 */
			m->m_pkthdr.fw_flags &= ~BRIDGE_MBUF_TAGGED;
			pf_change_ap(pd->src, &th->th_sport, pd->ip_sum,
			    &th->th_sum, &nk->addr[pd->sidx],
			    nk->port[pd->sidx], 0, pd->af);
		}

		if (PF_ANEQ(pd->dst, &nk->addr[pd->didx], pd->af) ||
		    nk->port[pd->didx] != th->th_dport) {
			/*
			 * If we don't redispatch the packet will go into
			 * the protocol stack on the wrong cpu for the
			 * post-translated address.
			 */
			pf_change_ap(pd->dst, &th->th_dport, pd->ip_sum,
			    &th->th_sum, &nk->addr[pd->didx],
			    nk->port[pd->didx], 0, pd->af);
		}
		copyback = 1;
	}

	/* Copyback sequence modulation or stateful scrub changes if needed */
	if (copyback) {
		m->m_flags &= ~M_HASH;
		m_copyback(m, off, sizeof(*th), (caddr_t)th);
	}

	pfsync_update_state(*state);
	error = PF_PASS;
done:
	if (*state)
		lockmgr(&(*state)->lk, LK_RELEASE);
	return (error);
}

/*
 * Test UDP connection state.  Caller must hold the state locked.
 */
int
pf_test_state_udp(struct pf_state **state, int direction, struct pfi_kif *kif,
		  struct mbuf *m, int off, void *h, struct pf_pdesc *pd)
{
	struct pf_state_peer	*src, *dst;
	struct pf_state_key_cmp	 key;
	struct udphdr		*uh = pd->hdr.udp;

	bzero(&key, sizeof(key));
	key.af = pd->af;
	key.proto = IPPROTO_UDP;
	if (direction == PF_IN)	{	/* wire side, straight */
		PF_ACPY(&key.addr[0], pd->src, key.af);
		PF_ACPY(&key.addr[1], pd->dst, key.af);
		key.port[0] = uh->uh_sport;
		key.port[1] = uh->uh_dport;
	} else {			/* stack side, reverse */
		PF_ACPY(&key.addr[1], pd->src, key.af);
		PF_ACPY(&key.addr[0], pd->dst, key.af);
		key.port[1] = uh->uh_sport;
		key.port[0] = uh->uh_dport;
	}

	STATE_LOOKUP(kif, &key, direction, *state, m);
	lockmgr(&(*state)->lk, LK_EXCLUSIVE);

	if (direction == (*state)->direction) {
		src = &(*state)->src;
		dst = &(*state)->dst;
	} else {
		src = &(*state)->dst;
		dst = &(*state)->src;
	}

	/* update states */
	if (src->state < PFUDPS_SINGLE)
		src->state = PFUDPS_SINGLE;
	if (dst->state == PFUDPS_SINGLE)
		dst->state = PFUDPS_MULTIPLE;

	/* update expire time */
	(*state)->expire = time_second;
	if (src->state == PFUDPS_MULTIPLE && dst->state == PFUDPS_MULTIPLE)
		(*state)->timeout = PFTM_UDP_MULTIPLE;
	else
		(*state)->timeout = PFTM_UDP_SINGLE;

	/* translate source/destination address, if necessary */
	if ((*state)->key[PF_SK_WIRE] != (*state)->key[PF_SK_STACK]) {
		struct pf_state_key *nk = (*state)->key[pd->didx];

		if (PF_ANEQ(pd->src, &nk->addr[pd->sidx], pd->af) ||
		    nk->port[pd->sidx] != uh->uh_sport) {
			/*
			 * The translated source address may be completely
			 * unrelated to the saved link header, make sure
			 * a bridge doesn't try to use it.
			 */
			m->m_pkthdr.fw_flags &= ~BRIDGE_MBUF_TAGGED;
			m->m_flags &= ~M_HASH;
			pf_change_ap(pd->src, &uh->uh_sport, pd->ip_sum,
			    &uh->uh_sum, &nk->addr[pd->sidx],
			    nk->port[pd->sidx], 1, pd->af);
		}

		if (PF_ANEQ(pd->dst, &nk->addr[pd->didx], pd->af) ||
		    nk->port[pd->didx] != uh->uh_dport) {
			/*
			 * If we don't redispatch the packet will go into
			 * the protocol stack on the wrong cpu for the
			 * post-translated address.
			 */
			m->m_flags &= ~M_HASH;
			pf_change_ap(pd->dst, &uh->uh_dport, pd->ip_sum,
			    &uh->uh_sum, &nk->addr[pd->didx],
			    nk->port[pd->didx], 1, pd->af);
		}
		m_copyback(m, off, sizeof(*uh), (caddr_t)uh);
	}

	pfsync_update_state(*state);
	lockmgr(&(*state)->lk, LK_RELEASE);
	return (PF_PASS);
}

/*
 * Test ICMP connection state.  Caller must hold the state locked.
 */
int
pf_test_state_icmp(struct pf_state **state, int direction, struct pfi_kif *kif,
		   struct mbuf *m, int off, void *h, struct pf_pdesc *pd,
		   u_short *reason)
{
	struct pf_addr	*saddr = pd->src, *daddr = pd->dst;
	u_int16_t	 icmpid = 0, *icmpsum = NULL;
	u_int8_t	 icmptype = 0;
	int		 state_icmp = 0;
	int		 error;
	struct pf_state_key_cmp key;

	bzero(&key, sizeof(key));

	switch (pd->proto) {
#ifdef INET
	case IPPROTO_ICMP:
		icmptype = pd->hdr.icmp->icmp_type;
		icmpid = pd->hdr.icmp->icmp_id;
		icmpsum = &pd->hdr.icmp->icmp_cksum;

		if (icmptype == ICMP_UNREACH ||
		    icmptype == ICMP_SOURCEQUENCH ||
		    icmptype == ICMP_REDIRECT ||
		    icmptype == ICMP_TIMXCEED ||
		    icmptype == ICMP_PARAMPROB)
			state_icmp++;
		break;
#endif /* INET */
#ifdef INET6
	case IPPROTO_ICMPV6:
		icmptype = pd->hdr.icmp6->icmp6_type;
		icmpid = pd->hdr.icmp6->icmp6_id;
		icmpsum = &pd->hdr.icmp6->icmp6_cksum;

		if (icmptype == ICMP6_DST_UNREACH ||
		    icmptype == ICMP6_PACKET_TOO_BIG ||
		    icmptype == ICMP6_TIME_EXCEEDED ||
		    icmptype == ICMP6_PARAM_PROB)
			state_icmp++;
		break;
#endif /* INET6 */
	}

	if (!state_icmp) {

		/*
		 * ICMP query/reply message not related to a TCP/UDP packet.
		 * Search for an ICMP state.
		 */
		key.af = pd->af;
		key.proto = pd->proto;
		key.port[0] = key.port[1] = icmpid;
		if (direction == PF_IN)	{	/* wire side, straight */
			PF_ACPY(&key.addr[0], pd->src, key.af);
			PF_ACPY(&key.addr[1], pd->dst, key.af);
		} else {			/* stack side, reverse */
			PF_ACPY(&key.addr[1], pd->src, key.af);
			PF_ACPY(&key.addr[0], pd->dst, key.af);
		}

		STATE_LOOKUP(kif, &key, direction, *state, m);
		lockmgr(&(*state)->lk, LK_EXCLUSIVE);

		(*state)->expire = time_second;
		(*state)->timeout = PFTM_ICMP_ERROR_REPLY;

		/* translate source/destination address, if necessary */
		if ((*state)->key[PF_SK_WIRE] != (*state)->key[PF_SK_STACK]) {
			struct pf_state_key *nk = (*state)->key[pd->didx];

			switch (pd->af) {
#ifdef INET
			case AF_INET:
				if (PF_ANEQ(pd->src,
				    &nk->addr[pd->sidx], AF_INET))
					pf_change_a(&saddr->v4.s_addr,
					    pd->ip_sum,
					    nk->addr[pd->sidx].v4.s_addr, 0);

				if (PF_ANEQ(pd->dst, &nk->addr[pd->didx],
				    AF_INET))
					pf_change_a(&daddr->v4.s_addr,
					    pd->ip_sum,
					    nk->addr[pd->didx].v4.s_addr, 0);

				if (nk->port[0] !=
				    pd->hdr.icmp->icmp_id) {
					pd->hdr.icmp->icmp_cksum =
					    pf_cksum_fixup(
					    pd->hdr.icmp->icmp_cksum, icmpid,
					    nk->port[pd->sidx], 0);
					pd->hdr.icmp->icmp_id =
					    nk->port[pd->sidx];
				}

				m->m_flags &= ~M_HASH;
				m_copyback(m, off, ICMP_MINLEN,
				    (caddr_t)pd->hdr.icmp);
				break;
#endif /* INET */
#ifdef INET6
			case AF_INET6:
				if (PF_ANEQ(pd->src,
				    &nk->addr[pd->sidx], AF_INET6))
					pf_change_a6(saddr,
					    &pd->hdr.icmp6->icmp6_cksum,
					    &nk->addr[pd->sidx], 0);

				if (PF_ANEQ(pd->dst,
				    &nk->addr[pd->didx], AF_INET6))
					pf_change_a6(daddr,
					    &pd->hdr.icmp6->icmp6_cksum,
					    &nk->addr[pd->didx], 0);

				m->m_flags &= ~M_HASH;
				m_copyback(m, off,
					sizeof(struct icmp6_hdr),
					(caddr_t)pd->hdr.icmp6);
				break;
#endif /* INET6 */
			}
		}
	} else {
		/*
		 * ICMP error message in response to a TCP/UDP packet.
		 * Extract the inner TCP/UDP header and search for that state.
		 */

		struct pf_pdesc	pd2;
#ifdef INET
		struct ip	h2;
#endif /* INET */
#ifdef INET6
		struct ip6_hdr	h2_6;
		int		terminal = 0;
#endif /* INET6 */
		int		ipoff2;
		int		off2;

		pd2.not_cpu_localized = 1;
		pd2.af = pd->af;
		/* Payload packet is from the opposite direction. */
		pd2.sidx = (direction == PF_IN) ? 1 : 0;
		pd2.didx = (direction == PF_IN) ? 0 : 1;
		switch (pd->af) {
#ifdef INET
		case AF_INET:
			/* offset of h2 in mbuf chain */
			ipoff2 = off + ICMP_MINLEN;

			if (!pf_pull_hdr(m, ipoff2, &h2, sizeof(h2),
			    NULL, reason, pd2.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: ICMP error message too short "
				    "(ip)\n"));
				FAIL (PF_DROP);
			}
			/*
			 * ICMP error messages don't refer to non-first
			 * fragments
			 */
			if (h2.ip_off & htons(IP_OFFMASK)) {
				REASON_SET(reason, PFRES_FRAG);
				FAIL (PF_DROP);
			}

			/* offset of protocol header that follows h2 */
			off2 = ipoff2 + (h2.ip_hl << 2);

			pd2.proto = h2.ip_p;
			pd2.src = (struct pf_addr *)&h2.ip_src;
			pd2.dst = (struct pf_addr *)&h2.ip_dst;
			pd2.ip_sum = &h2.ip_sum;
			break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			ipoff2 = off + sizeof(struct icmp6_hdr);

			if (!pf_pull_hdr(m, ipoff2, &h2_6, sizeof(h2_6),
			    NULL, reason, pd2.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: ICMP error message too short "
				    "(ip6)\n"));
				FAIL (PF_DROP);
			}
			pd2.proto = h2_6.ip6_nxt;
			pd2.src = (struct pf_addr *)&h2_6.ip6_src;
			pd2.dst = (struct pf_addr *)&h2_6.ip6_dst;
			pd2.ip_sum = NULL;
			off2 = ipoff2 + sizeof(h2_6);
			do {
				switch (pd2.proto) {
				case IPPROTO_FRAGMENT:
					/*
					 * ICMPv6 error messages for
					 * non-first fragments
					 */
					REASON_SET(reason, PFRES_FRAG);
					FAIL (PF_DROP);
				case IPPROTO_AH:
				case IPPROTO_HOPOPTS:
				case IPPROTO_ROUTING:
				case IPPROTO_DSTOPTS: {
					/* get next header and header length */
					struct ip6_ext opt6;

					if (!pf_pull_hdr(m, off2, &opt6,
					    sizeof(opt6), NULL, reason,
					    pd2.af)) {
						DPFPRINTF(PF_DEBUG_MISC,
						    ("pf: ICMPv6 short opt\n"));
						FAIL (PF_DROP);
					}
					if (pd2.proto == IPPROTO_AH)
						off2 += (opt6.ip6e_len + 2) * 4;
					else
						off2 += (opt6.ip6e_len + 1) * 8;
					pd2.proto = opt6.ip6e_nxt;
					/* goto the next header */
					break;
				}
				default:
					terminal++;
					break;
				}
			} while (!terminal);
			break;
#endif /* INET6 */
		default:
			DPFPRINTF(PF_DEBUG_MISC,
			    ("pf: ICMP AF %d unknown (ip6)\n", pd->af));
			FAIL (PF_DROP);
			break;
		}

		switch (pd2.proto) {
		case IPPROTO_TCP: {
			struct tcphdr		 th;
			u_int32_t		 seq;
			struct pf_state_peer	*src, *dst;
			u_int8_t		 dws;
			int			 copyback = 0;

			/*
			 * Only the first 8 bytes of the TCP header can be
			 * expected. Don't access any TCP header fields after
			 * th_seq, an ackskew test is not possible.
			 */
			if (!pf_pull_hdr(m, off2, &th, 8, NULL, reason,
			    pd2.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: ICMP error message too short "
				    "(tcp)\n"));
				FAIL (PF_DROP);
			}

			key.af = pd2.af;
			key.proto = IPPROTO_TCP;
			PF_ACPY(&key.addr[pd2.sidx], pd2.src, key.af);
			PF_ACPY(&key.addr[pd2.didx], pd2.dst, key.af);
			key.port[pd2.sidx] = th.th_sport;
			key.port[pd2.didx] = th.th_dport;

			STATE_LOOKUP(kif, &key, direction, *state, m);
			lockmgr(&(*state)->lk, LK_EXCLUSIVE);

			if (direction == (*state)->direction) {
				src = &(*state)->dst;
				dst = &(*state)->src;
			} else {
				src = &(*state)->src;
				dst = &(*state)->dst;
			}

			if (src->wscale && dst->wscale)
				dws = dst->wscale & PF_WSCALE_MASK;
			else
				dws = 0;

			/* Demodulate sequence number */
			seq = ntohl(th.th_seq) - src->seqdiff;
			if (src->seqdiff) {
				pf_change_a(&th.th_seq, icmpsum,
				    htonl(seq), 0);
				copyback = 1;
			}

			if (!((*state)->state_flags & PFSTATE_SLOPPY) &&
			    (!SEQ_GEQ(src->seqhi, seq) ||
			    !SEQ_GEQ(seq, src->seqlo - (dst->max_win << dws)))) {
				if (pf_status.debug >= PF_DEBUG_MISC) {
					kprintf("pf: BAD ICMP %d:%d ",
					    icmptype, pd->hdr.icmp->icmp_code);
					pf_print_host(pd->src, 0, pd->af);
					kprintf(" -> ");
					pf_print_host(pd->dst, 0, pd->af);
					kprintf(" state: ");
					pf_print_state(*state);
					kprintf(" seq=%u\n", seq);
				}
				REASON_SET(reason, PFRES_BADSTATE);
				FAIL (PF_DROP);
			} else {
				if (pf_status.debug >= PF_DEBUG_MISC) {
					kprintf("pf: OK ICMP %d:%d ",
					    icmptype, pd->hdr.icmp->icmp_code);
					pf_print_host(pd->src, 0, pd->af);
					kprintf(" -> ");
					pf_print_host(pd->dst, 0, pd->af);
					kprintf(" state: ");
					pf_print_state(*state);
					kprintf(" seq=%u\n", seq);
				}
			}

			/* translate source/destination address, if necessary */
			if ((*state)->key[PF_SK_WIRE] !=
			    (*state)->key[PF_SK_STACK]) {
				struct pf_state_key *nk =
				    (*state)->key[pd->didx];

				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af) ||
				    nk->port[pd2.sidx] != th.th_sport)
					pf_change_icmp(pd2.src, &th.th_sport,
					    daddr, &nk->addr[pd2.sidx],
					    nk->port[pd2.sidx], NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, pd2.af);

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af) ||
				    nk->port[pd2.didx] != th.th_dport)
					pf_change_icmp(pd2.dst, &th.th_dport,
					    NULL, /* XXX Inbound NAT? */
					    &nk->addr[pd2.didx],
					    nk->port[pd2.didx], NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, pd2.af);
				copyback = 1;
			}

			if (copyback) {
				switch (pd2.af) {
#ifdef INET
				case AF_INET:
					m_copyback(m, off, ICMP_MINLEN,
					    (caddr_t)pd->hdr.icmp);
					m_copyback(m, ipoff2, sizeof(h2),
					    (caddr_t)&h2);
					break;
#endif /* INET */
#ifdef INET6
				case AF_INET6:
					m_copyback(m, off,
					    sizeof(struct icmp6_hdr),
					    (caddr_t)pd->hdr.icmp6);
					m_copyback(m, ipoff2, sizeof(h2_6),
					    (caddr_t)&h2_6);
					break;
#endif /* INET6 */
				}
				m->m_flags &= ~M_HASH;
				m_copyback(m, off2, 8, (caddr_t)&th);
			}
			break;
		}
		case IPPROTO_UDP: {
			struct udphdr		uh;

			if (!pf_pull_hdr(m, off2, &uh, sizeof(uh),
			    NULL, reason, pd2.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: ICMP error message too short "
				    "(udp)\n"));
				return (PF_DROP);
			}

			key.af = pd2.af;
			key.proto = IPPROTO_UDP;
			PF_ACPY(&key.addr[pd2.sidx], pd2.src, key.af);
			PF_ACPY(&key.addr[pd2.didx], pd2.dst, key.af);
			key.port[pd2.sidx] = uh.uh_sport;
			key.port[pd2.didx] = uh.uh_dport;

			STATE_LOOKUP(kif, &key, direction, *state, m);
			lockmgr(&(*state)->lk, LK_EXCLUSIVE);

			/* translate source/destination address, if necessary */
			if ((*state)->key[PF_SK_WIRE] !=
			    (*state)->key[PF_SK_STACK]) {
				struct pf_state_key *nk =
				    (*state)->key[pd->didx];

				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af) ||
				    nk->port[pd2.sidx] != uh.uh_sport)
					pf_change_icmp(pd2.src, &uh.uh_sport,
					    daddr, &nk->addr[pd2.sidx],
					    nk->port[pd2.sidx], &uh.uh_sum,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 1, pd2.af);

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af) ||
				    nk->port[pd2.didx] != uh.uh_dport)
					pf_change_icmp(pd2.dst, &uh.uh_dport,
					    NULL, /* XXX Inbound NAT? */
					    &nk->addr[pd2.didx],
					    nk->port[pd2.didx], &uh.uh_sum,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 1, pd2.af);

				switch (pd2.af) {
#ifdef INET
				case AF_INET:
					m_copyback(m, off, ICMP_MINLEN,
					    (caddr_t)pd->hdr.icmp);
					m_copyback(m, ipoff2, sizeof(h2), (caddr_t)&h2);
					break;
#endif /* INET */
#ifdef INET6
				case AF_INET6:
					m_copyback(m, off,
					    sizeof(struct icmp6_hdr),
					    (caddr_t)pd->hdr.icmp6);
					m_copyback(m, ipoff2, sizeof(h2_6),
					    (caddr_t)&h2_6);
					break;
#endif /* INET6 */
				}
				m->m_flags &= ~M_HASH;
				m_copyback(m, off2, sizeof(uh), (caddr_t)&uh);
			}
			break;
		}
#ifdef INET
		case IPPROTO_ICMP: {
			struct icmp		iih;

			if (!pf_pull_hdr(m, off2, &iih, ICMP_MINLEN,
			    NULL, reason, pd2.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: ICMP error message too short i"
				    "(icmp)\n"));
				return (PF_DROP);
			}

			key.af = pd2.af;
			key.proto = IPPROTO_ICMP;
			PF_ACPY(&key.addr[pd2.sidx], pd2.src, key.af);
			PF_ACPY(&key.addr[pd2.didx], pd2.dst, key.af);
			key.port[0] = key.port[1] = iih.icmp_id;

			STATE_LOOKUP(kif, &key, direction, *state, m);
			lockmgr(&(*state)->lk, LK_EXCLUSIVE);

			/* translate source/destination address, if necessary */
			if ((*state)->key[PF_SK_WIRE] !=
			    (*state)->key[PF_SK_STACK]) {
				struct pf_state_key *nk =
				    (*state)->key[pd->didx];

				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af) ||
				    nk->port[pd2.sidx] != iih.icmp_id)
					pf_change_icmp(pd2.src, &iih.icmp_id,
					    daddr, &nk->addr[pd2.sidx],
					    nk->port[pd2.sidx], NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, AF_INET);

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af) ||
				    nk->port[pd2.didx] != iih.icmp_id)
					pf_change_icmp(pd2.dst, &iih.icmp_id,
					    NULL, /* XXX Inbound NAT? */
					    &nk->addr[pd2.didx],
					    nk->port[pd2.didx], NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, AF_INET);

				m_copyback(m, off, ICMP_MINLEN, (caddr_t)pd->hdr.icmp);
				m_copyback(m, ipoff2, sizeof(h2), (caddr_t)&h2);
				m_copyback(m, off2, ICMP_MINLEN, (caddr_t)&iih);
				m->m_flags &= ~M_HASH;
			}
			break;
		}
#endif /* INET */
#ifdef INET6
		case IPPROTO_ICMPV6: {
			struct icmp6_hdr	iih;

			if (!pf_pull_hdr(m, off2, &iih,
			    sizeof(struct icmp6_hdr), NULL, reason, pd2.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: ICMP error message too short "
				    "(icmp6)\n"));
				FAIL (PF_DROP);
			}

			key.af = pd2.af;
			key.proto = IPPROTO_ICMPV6;
			PF_ACPY(&key.addr[pd2.sidx], pd2.src, key.af);
			PF_ACPY(&key.addr[pd2.didx], pd2.dst, key.af);
			key.port[0] = key.port[1] = iih.icmp6_id;

			STATE_LOOKUP(kif, &key, direction, *state, m);
			lockmgr(&(*state)->lk, LK_EXCLUSIVE);

			/* translate source/destination address, if necessary */
			if ((*state)->key[PF_SK_WIRE] !=
			    (*state)->key[PF_SK_STACK]) {
				struct pf_state_key *nk =
				    (*state)->key[pd->didx];

				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af) ||
				    nk->port[pd2.sidx] != iih.icmp6_id)
					pf_change_icmp(pd2.src, &iih.icmp6_id,
					    daddr, &nk->addr[pd2.sidx],
					    nk->port[pd2.sidx], NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, AF_INET6);

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af) ||
				    nk->port[pd2.didx] != iih.icmp6_id)
					pf_change_icmp(pd2.dst, &iih.icmp6_id,
					    NULL, /* XXX Inbound NAT? */
					    &nk->addr[pd2.didx],
					    nk->port[pd2.didx], NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, AF_INET6);

				m_copyback(m, off, sizeof(struct icmp6_hdr),
				    (caddr_t)pd->hdr.icmp6);
				m_copyback(m, ipoff2, sizeof(h2_6), (caddr_t)&h2_6);
				m_copyback(m, off2, sizeof(struct icmp6_hdr),
				    (caddr_t)&iih);
				m->m_flags &= ~M_HASH;
			}
			break;
		}
#endif /* INET6 */
		default: {
			key.af = pd2.af;
			key.proto = pd2.proto;
			PF_ACPY(&key.addr[pd2.sidx], pd2.src, key.af);
			PF_ACPY(&key.addr[pd2.didx], pd2.dst, key.af);
			key.port[0] = key.port[1] = 0;

			STATE_LOOKUP(kif, &key, direction, *state, m);
			lockmgr(&(*state)->lk, LK_EXCLUSIVE);

			/* translate source/destination address, if necessary */
			if ((*state)->key[PF_SK_WIRE] !=
			    (*state)->key[PF_SK_STACK]) {
				struct pf_state_key *nk =
				    (*state)->key[pd->didx];

				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af))
					pf_change_icmp(pd2.src, NULL, daddr,
					    &nk->addr[pd2.sidx], 0, NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, pd2.af);

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af))
					pf_change_icmp(pd2.src, NULL,
					    NULL, /* XXX Inbound NAT? */
					    &nk->addr[pd2.didx], 0, NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, pd2.af);

				switch (pd2.af) {
#ifdef INET
				case AF_INET:
					m_copyback(m, off, ICMP_MINLEN,
					    (caddr_t)pd->hdr.icmp);
					m_copyback(m, ipoff2, sizeof(h2), (caddr_t)&h2);
					m->m_flags &= ~M_HASH;
					break;
#endif /* INET */
#ifdef INET6
				case AF_INET6:
					m_copyback(m, off,
					    sizeof(struct icmp6_hdr),
					    (caddr_t)pd->hdr.icmp6);
					m_copyback(m, ipoff2, sizeof(h2_6),
					    (caddr_t)&h2_6);
					m->m_flags &= ~M_HASH;
					break;
#endif /* INET6 */
				}
			}
			break;
		}
		}
	}

	pfsync_update_state(*state);
	error = PF_PASS;
done:
	if (*state)
		lockmgr(&(*state)->lk, LK_RELEASE);
	return (error);
}

/*
 * Test other connection state.  Caller must hold the state locked.
 */
int
pf_test_state_other(struct pf_state **state, int direction, struct pfi_kif *kif,
		    struct mbuf *m, struct pf_pdesc *pd)
{
	struct pf_state_peer	*src, *dst;
	struct pf_state_key_cmp	 key;

	bzero(&key, sizeof(key));
	key.af = pd->af;
	key.proto = pd->proto;
	if (direction == PF_IN)	{
		PF_ACPY(&key.addr[0], pd->src, key.af);
		PF_ACPY(&key.addr[1], pd->dst, key.af);
		key.port[0] = key.port[1] = 0;
	} else {
		PF_ACPY(&key.addr[1], pd->src, key.af);
		PF_ACPY(&key.addr[0], pd->dst, key.af);
		key.port[1] = key.port[0] = 0;
	}

	STATE_LOOKUP(kif, &key, direction, *state, m);
	lockmgr(&(*state)->lk, LK_EXCLUSIVE);

	if (direction == (*state)->direction) {
		src = &(*state)->src;
		dst = &(*state)->dst;
	} else {
		src = &(*state)->dst;
		dst = &(*state)->src;
	}

	/* update states */
	if (src->state < PFOTHERS_SINGLE)
		src->state = PFOTHERS_SINGLE;
	if (dst->state == PFOTHERS_SINGLE)
		dst->state = PFOTHERS_MULTIPLE;

	/* update expire time */
	(*state)->expire = time_second;
	if (src->state == PFOTHERS_MULTIPLE && dst->state == PFOTHERS_MULTIPLE)
		(*state)->timeout = PFTM_OTHER_MULTIPLE;
	else
		(*state)->timeout = PFTM_OTHER_SINGLE;

	/* translate source/destination address, if necessary */
	if ((*state)->key[PF_SK_WIRE] != (*state)->key[PF_SK_STACK]) {
		struct pf_state_key *nk = (*state)->key[pd->didx];

		KKASSERT(nk);
		KKASSERT(pd);
		KKASSERT(pd->src);
		KKASSERT(pd->dst);
		switch (pd->af) {
#ifdef INET
		case AF_INET:
			if (PF_ANEQ(pd->src, &nk->addr[pd->sidx], AF_INET))
				pf_change_a(&pd->src->v4.s_addr,
				    pd->ip_sum,
				    nk->addr[pd->sidx].v4.s_addr,
				    0);


			if (PF_ANEQ(pd->dst, &nk->addr[pd->didx], AF_INET))
				pf_change_a(&pd->dst->v4.s_addr,
				    pd->ip_sum,
				    nk->addr[pd->didx].v4.s_addr,
				    0);

			break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			if (PF_ANEQ(pd->src, &nk->addr[pd->sidx], AF_INET6))
				PF_ACPY(pd->src, &nk->addr[pd->sidx], pd->af);

			if (PF_ANEQ(pd->dst, &nk->addr[pd->didx], AF_INET6))
				PF_ACPY(pd->dst, &nk->addr[pd->didx], pd->af);
#endif /* INET6 */
		}
	}

	pfsync_update_state(*state);
	lockmgr(&(*state)->lk, LK_RELEASE);
	return (PF_PASS);
}

/*
 * ipoff and off are measured from the start of the mbuf chain.
 * h must be at "ipoff" on the mbuf chain.
 */
void *
pf_pull_hdr(struct mbuf *m, int off, void *p, int len,
    u_short *actionp, u_short *reasonp, sa_family_t af)
{
	switch (af) {
#ifdef INET
	case AF_INET: {
		struct ip	*h = mtod(m, struct ip *);
		u_int16_t	 fragoff = (ntohs(h->ip_off) & IP_OFFMASK) << 3;

		if (fragoff) {
			if (fragoff >= len)
				ACTION_SET(actionp, PF_PASS);
			else {
				ACTION_SET(actionp, PF_DROP);
				REASON_SET(reasonp, PFRES_FRAG);
			}
			return (NULL);
		}
		if (m->m_pkthdr.len < off + len ||
		    ntohs(h->ip_len) < off + len) {
			ACTION_SET(actionp, PF_DROP);
			REASON_SET(reasonp, PFRES_SHORT);
			return (NULL);
		}
		break;
	}
#endif /* INET */
#ifdef INET6
	case AF_INET6: {
		struct ip6_hdr	*h = mtod(m, struct ip6_hdr *);

		if (m->m_pkthdr.len < off + len ||
		    (ntohs(h->ip6_plen) + sizeof(struct ip6_hdr)) <
		    (unsigned)(off + len)) {
			ACTION_SET(actionp, PF_DROP);
			REASON_SET(reasonp, PFRES_SHORT);
			return (NULL);
		}
		break;
	}
#endif /* INET6 */
	}
	m_copydata(m, off, len, p);
	return (p);
}

int
pf_routable(struct pf_addr *addr, sa_family_t af, struct pfi_kif *kif)
{
	struct sockaddr_in	*dst;
	int			 ret = 1;
	int			 check_mpath;
#ifdef INET6
	struct sockaddr_in6	*dst6;
	struct route_in6	 ro;
#else
	struct route		 ro;
#endif
	struct radix_node	*rn;
	struct rtentry		*rt;
	struct ifnet		*ifp;

	check_mpath = 0;
	bzero(&ro, sizeof(ro));
	switch (af) {
	case AF_INET:
		dst = satosin(&ro.ro_dst);
		dst->sin_family = AF_INET;
		dst->sin_len = sizeof(*dst);
		dst->sin_addr = addr->v4;
		break;
#ifdef INET6
	case AF_INET6:
		/*
		 * Skip check for addresses with embedded interface scope,
		 * as they would always match anyway.
		 */
		if (IN6_IS_SCOPE_EMBED(&addr->v6))
			goto out;
		dst6 = (struct sockaddr_in6 *)&ro.ro_dst;
		dst6->sin6_family = AF_INET6;
		dst6->sin6_len = sizeof(*dst6);
		dst6->sin6_addr = addr->v6;
		break;
#endif /* INET6 */
	default:
		return (0);
	}

	/* Skip checks for ipsec interfaces */
	if (kif != NULL && kif->pfik_ifp->if_type == IFT_ENC)
		goto out;

	rtalloc_ign((struct route *)&ro, 0);

	if (ro.ro_rt != NULL) {
		/* No interface given, this is a no-route check */
		if (kif == NULL)
			goto out;

		if (kif->pfik_ifp == NULL) {
			ret = 0;
			goto out;
		}

		/* Perform uRPF check if passed input interface */
		ret = 0;
		rn = (struct radix_node *)ro.ro_rt;
		do {
			rt = (struct rtentry *)rn;
			ifp = rt->rt_ifp;

			if (kif->pfik_ifp == ifp)
				ret = 1;
			rn = NULL;
		} while (check_mpath == 1 && rn != NULL && ret == 0);
	} else
		ret = 0;
out:
	if (ro.ro_rt != NULL)
		RTFREE(ro.ro_rt);
	return (ret);
}

int
pf_rtlabel_match(struct pf_addr *addr, sa_family_t af, struct pf_addr_wrap *aw)
{
	struct sockaddr_in	*dst;
#ifdef INET6
	struct sockaddr_in6	*dst6;
	struct route_in6	 ro;
#else
	struct route		 ro;
#endif
	int			 ret = 0;

	ASSERT_LWKT_TOKEN_HELD(&pf_token);

	bzero(&ro, sizeof(ro));
	switch (af) {
	case AF_INET:
		dst = satosin(&ro.ro_dst);
		dst->sin_family = AF_INET;
		dst->sin_len = sizeof(*dst);
		dst->sin_addr = addr->v4;
		break;
#ifdef INET6
	case AF_INET6:
		dst6 = (struct sockaddr_in6 *)&ro.ro_dst;
		dst6->sin6_family = AF_INET6;
		dst6->sin6_len = sizeof(*dst6);
		dst6->sin6_addr = addr->v6;
		break;
#endif /* INET6 */
	default:
		return (0);
	}

rtalloc_ign((struct route *)&ro, (RTF_CLONING | RTF_PRCLONING));

	if (ro.ro_rt != NULL) {
		RTFREE(ro.ro_rt);
	}

	return (ret);
}

#ifdef INET
void
pf_route(struct mbuf **m, struct pf_rule *r, int dir, struct ifnet *oifp,
    struct pf_state *s, struct pf_pdesc *pd)
{
	struct mbuf		*m0, *m1;
	struct route		 iproute;
	struct route		*ro = NULL;
	struct sockaddr_in	*dst;
	struct ip		*ip;
	struct ifnet		*ifp = NULL;
	struct pf_addr		 naddr;
	struct pf_src_node	*sn = NULL;
	int			 error = 0;
	int sw_csum;

	ASSERT_LWKT_TOKEN_HELD(&pf_token);

	if (m == NULL || *m == NULL || r == NULL ||
	    (dir != PF_IN && dir != PF_OUT) || oifp == NULL)
		panic("pf_route: invalid parameters");

	if (((*m)->m_pkthdr.fw_flags & PF_MBUF_ROUTED) == 0) {
		(*m)->m_pkthdr.fw_flags |= PF_MBUF_ROUTED;
		(*m)->m_pkthdr.pf.routed = 1;
	} else {
		if ((*m)->m_pkthdr.pf.routed++ > 3) {
			m0 = *m;
			*m = NULL;
			goto bad;
		}
	}

	if (r->rt == PF_DUPTO) {
		if ((m0 = m_dup(*m, M_NOWAIT)) == NULL) {
			return;
		}
	} else {
		if ((r->rt == PF_REPLYTO) == (r->direction == dir)) {
			return;
		}
		m0 = *m;
	}

	if (m0->m_len < sizeof(struct ip)) {
		DPFPRINTF(PF_DEBUG_URGENT,
		    ("pf_route: m0->m_len < sizeof(struct ip)\n"));
		goto bad;
	}

	ip = mtod(m0, struct ip *);

	ro = &iproute;
	bzero((caddr_t)ro, sizeof(*ro));
	dst = satosin(&ro->ro_dst);
	dst->sin_family = AF_INET;
	dst->sin_len = sizeof(*dst);
	dst->sin_addr = ip->ip_dst;

	if (r->rt == PF_FASTROUTE) {
		rtalloc(ro);
		if (ro->ro_rt == 0) {
			ipstat.ips_noroute++;
			goto bad;
		}

		ifp = ro->ro_rt->rt_ifp;
		ro->ro_rt->rt_use++;

		if (ro->ro_rt->rt_flags & RTF_GATEWAY)
			dst = satosin(ro->ro_rt->rt_gateway);
	} else {
		if (TAILQ_EMPTY(&r->rpool.list)) {
			DPFPRINTF(PF_DEBUG_URGENT,
			    ("pf_route: TAILQ_EMPTY(&r->rpool.list)\n"));
			goto bad;
		}
		if (s == NULL) {
			pf_map_addr(AF_INET, r, (struct pf_addr *)&ip->ip_src,
			    &naddr, NULL, &sn);
			if (!PF_AZERO(&naddr, AF_INET))
				dst->sin_addr.s_addr = naddr.v4.s_addr;
			ifp = r->rpool.cur->kif ?
			    r->rpool.cur->kif->pfik_ifp : NULL;
		} else {
			if (!PF_AZERO(&s->rt_addr, AF_INET))
				dst->sin_addr.s_addr =
				    s->rt_addr.v4.s_addr;
			ifp = s->rt_kif ? s->rt_kif->pfik_ifp : NULL;
		}
	}
	if (ifp == NULL)
		goto bad;

	if (oifp != ifp) {
		if (pf_test(PF_OUT, ifp, &m0, NULL, NULL) != PF_PASS) {
			goto bad;
		} else if (m0 == NULL) {
			goto done;
		}
		if (m0->m_len < sizeof(struct ip)) {
			DPFPRINTF(PF_DEBUG_URGENT,
			    ("pf_route: m0->m_len < sizeof(struct ip)\n"));
			goto bad;
		}
		ip = mtod(m0, struct ip *);
	}

	/* Copied from FreeBSD 5.1-CURRENT ip_output. */
	m0->m_pkthdr.csum_flags |= CSUM_IP;
	sw_csum = m0->m_pkthdr.csum_flags & ~ifp->if_hwassist;
	if (sw_csum & CSUM_DELAY_DATA) {
		in_delayed_cksum(m0);
		sw_csum &= ~CSUM_DELAY_DATA;
	}
	m0->m_pkthdr.csum_flags &= ifp->if_hwassist;
	m0->m_pkthdr.csum_iphlen = (ip->ip_hl << 2);

	/*
	 * WARNING!  We cannot fragment if the packet was modified from an
	 *	     original which expected to be using TSO.  In this
	 *	     situation we pray that the target interface is
	 *	     compatible with the originating interface.
	 */
	if (ntohs(ip->ip_len) <= ifp->if_mtu ||
	    (m0->m_pkthdr.csum_flags & CSUM_TSO) ||
	    ((ifp->if_hwassist & CSUM_FRAGMENT) &&
		(ip->ip_off & htons(IP_DF)) == 0)) {
		ip->ip_sum = 0;
		if (sw_csum & CSUM_DELAY_IP) {
			/* From KAME */
			if (ip->ip_v == IPVERSION &&
			    (ip->ip_hl << 2) == sizeof(*ip)) {
				ip->ip_sum = in_cksum_hdr(ip);
			} else {
				ip->ip_sum = in_cksum(m0, ip->ip_hl << 2);
			}
		}
		lwkt_reltoken(&pf_token);
		error = ifp->if_output(ifp, m0, sintosa(dst), ro->ro_rt);
		lwkt_gettoken(&pf_token);
		goto done;
	}

	/*
	 * Too large for interface; fragment if possible.
	 * Must be able to put at least 8 bytes per fragment.
	 */
	if (ip->ip_off & htons(IP_DF)) {
		ipstat.ips_cantfrag++;
		if (r->rt != PF_DUPTO) {
			icmp_error(m0, ICMP_UNREACH, ICMP_UNREACH_NEEDFRAG, 0,
				   ifp->if_mtu);
			goto done;
		} else
			goto bad;
	}

	m1 = m0;
	error = ip_fragment(ip, &m0, ifp->if_mtu, ifp->if_hwassist, sw_csum);
	if (error) {
		goto bad;
	}

	for (m0 = m1; m0; m0 = m1) {
		m1 = m0->m_nextpkt;
		m0->m_nextpkt = 0;
		if (error == 0) {
			lwkt_reltoken(&pf_token);
			error = (*ifp->if_output)(ifp, m0, sintosa(dst),
						  NULL);
			lwkt_gettoken(&pf_token);
		} else
			m_freem(m0);
	}

	if (error == 0)
		ipstat.ips_fragmented++;

done:
	if (r->rt != PF_DUPTO)
		*m = NULL;
	if (ro == &iproute && ro->ro_rt)
		RTFREE(ro->ro_rt);
	return;

bad:
	m_freem(m0);
	goto done;
}
#endif /* INET */

#ifdef INET6
void
pf_route6(struct mbuf **m, struct pf_rule *r, int dir, struct ifnet *oifp,
    struct pf_state *s, struct pf_pdesc *pd)
{
	struct mbuf		*m0;
	struct route_in6	 ip6route;
	struct route_in6	*ro;
	struct sockaddr_in6	*dst;
	struct ip6_hdr		*ip6;
	struct ifnet		*ifp = NULL;
	struct pf_addr		 naddr;
	struct pf_src_node	*sn = NULL;

	if (m == NULL || *m == NULL || r == NULL ||
	    (dir != PF_IN && dir != PF_OUT) || oifp == NULL)
		panic("pf_route6: invalid parameters");

	if (((*m)->m_pkthdr.fw_flags & PF_MBUF_ROUTED) == 0) {
		(*m)->m_pkthdr.fw_flags |= PF_MBUF_ROUTED;
		(*m)->m_pkthdr.pf.routed = 1;
	} else {
		if ((*m)->m_pkthdr.pf.routed++ > 3) {
			m0 = *m;
			*m = NULL;
			goto bad;
		}
	}

	if (r->rt == PF_DUPTO) {
		if ((m0 = m_dup(*m, M_NOWAIT)) == NULL)
			return;
	} else {
		if ((r->rt == PF_REPLYTO) == (r->direction == dir))
			return;
		m0 = *m;
	}

	if (m0->m_len < sizeof(struct ip6_hdr)) {
		DPFPRINTF(PF_DEBUG_URGENT,
		    ("pf_route6: m0->m_len < sizeof(struct ip6_hdr)\n"));
		goto bad;
	}
	ip6 = mtod(m0, struct ip6_hdr *);

	ro = &ip6route;
	bzero((caddr_t)ro, sizeof(*ro));
	dst = (struct sockaddr_in6 *)&ro->ro_dst;
	dst->sin6_family = AF_INET6;
	dst->sin6_len = sizeof(*dst);
	dst->sin6_addr = ip6->ip6_dst;

	/*
	 * DragonFly doesn't zero the auxillary pkghdr fields, only fw_flags,
	 * so make sure pf.flags is clear.
	 *
	 * Cheat. XXX why only in the v6 case???
	 */
	if (r->rt == PF_FASTROUTE) {
		m0->m_pkthdr.fw_flags |= PF_MBUF_TAGGED;
		m0->m_pkthdr.pf.flags = 0;
		/* XXX Re-Check when Upgrading to > 4.4 */
		m0->m_pkthdr.pf.statekey = NULL;
		ip6_output(m0, NULL, NULL, 0, NULL, NULL, NULL);
		return;
	}

	if (TAILQ_EMPTY(&r->rpool.list)) {
		DPFPRINTF(PF_DEBUG_URGENT,
		    ("pf_route6: TAILQ_EMPTY(&r->rpool.list)\n"));
		goto bad;
	}
	if (s == NULL) {
		pf_map_addr(AF_INET6, r, (struct pf_addr *)&ip6->ip6_src,
		    &naddr, NULL, &sn);
		if (!PF_AZERO(&naddr, AF_INET6))
			PF_ACPY((struct pf_addr *)&dst->sin6_addr,
			    &naddr, AF_INET6);
		ifp = r->rpool.cur->kif ? r->rpool.cur->kif->pfik_ifp : NULL;
	} else {
		if (!PF_AZERO(&s->rt_addr, AF_INET6))
			PF_ACPY((struct pf_addr *)&dst->sin6_addr,
			    &s->rt_addr, AF_INET6);
		ifp = s->rt_kif ? s->rt_kif->pfik_ifp : NULL;
	}
	if (ifp == NULL)
		goto bad;

	if (oifp != ifp) {
		if (pf_test6(PF_OUT, ifp, &m0, NULL, NULL) != PF_PASS) {
			goto bad;
		} else if (m0 == NULL) {
			goto done;
		}
		if (m0->m_len < sizeof(struct ip6_hdr)) {
			DPFPRINTF(PF_DEBUG_URGENT,
			    ("pf_route6: m0->m_len < sizeof(struct ip6_hdr)\n"));
			goto bad;
		}
		ip6 = mtod(m0, struct ip6_hdr *);
	}

	/*
	 * If the packet is too large for the outgoing interface,
	 * send back an icmp6 error.
	 */
	if (IN6_IS_SCOPE_EMBED(&dst->sin6_addr))
		dst->sin6_addr.s6_addr16[1] = htons(ifp->if_index);
	if ((u_long)m0->m_pkthdr.len <= ifp->if_mtu) {
		nd6_output(ifp, ifp, m0, dst, NULL);
	} else {
		in6_ifstat_inc(ifp, ifs6_in_toobig);
		if (r->rt != PF_DUPTO)
			icmp6_error(m0, ICMP6_PACKET_TOO_BIG, 0, ifp->if_mtu);
		else
			goto bad;
	}

done:
	if (r->rt != PF_DUPTO)
		*m = NULL;
	return;

bad:
	m_freem(m0);
	goto done;
}
#endif /* INET6 */


/*
 * check protocol (tcp/udp/icmp/icmp6) checksum and set mbuf flag
 *   off is the offset where the protocol header starts
 *   len is the total length of protocol header plus payload
 * returns 0 when the checksum is valid, otherwise returns 1.
 */
/*
 * XXX
 * FreeBSD supports cksum offload for the following drivers.
 * em(4), gx(4), lge(4), nge(4), ti(4), xl(4)
 * If we can make full use of it we would outperform ipfw/ipfilter in
 * very heavy traffic. 
 * I have not tested 'cause I don't have NICs that supports cksum offload.
 * (There might be problems. Typical phenomena would be
 *   1. No route message for UDP packet.
 *   2. No connection acceptance from external hosts regardless of rule set.)
 */
int
pf_check_proto_cksum(struct mbuf *m, int off, int len, u_int8_t p,
    sa_family_t af)
{
	u_int16_t sum = 0;
	int hw_assist = 0;
	struct ip *ip;

	if (off < sizeof(struct ip) || len < sizeof(struct udphdr))
		return (1);
	if (m->m_pkthdr.len < off + len)
		return (1);

	switch (p) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		if (m->m_pkthdr.csum_flags & CSUM_DATA_VALID) {
			if (m->m_pkthdr.csum_flags & CSUM_PSEUDO_HDR) {
				sum = m->m_pkthdr.csum_data;
			} else {
				ip = mtod(m, struct ip *);	
				sum = in_pseudo(ip->ip_src.s_addr,
					ip->ip_dst.s_addr, htonl((u_short)len +
					m->m_pkthdr.csum_data + p));
			}
			sum ^= 0xffff;
			++hw_assist;
		}
		break;
	case IPPROTO_ICMP:
#ifdef INET6
	case IPPROTO_ICMPV6:
#endif /* INET6 */
		break;
	default:
		return (1);
	}

	if (!hw_assist) {
		switch (af) {
		case AF_INET:
			if (p == IPPROTO_ICMP) {
				if (m->m_len < off)
					return (1);
				m->m_data += off;
				m->m_len -= off;
				sum = in_cksum(m, len);
				m->m_data -= off;
				m->m_len += off;
			} else {
				if (m->m_len < sizeof(struct ip))
					return (1);
				sum = in_cksum_range(m, p, off, len);
				if (sum == 0) {
					m->m_pkthdr.csum_flags |=
					    (CSUM_DATA_VALID |
					     CSUM_PSEUDO_HDR);
					m->m_pkthdr.csum_data = 0xffff;
				}
			}
			break;
#ifdef INET6
		case AF_INET6:
			if (m->m_len < sizeof(struct ip6_hdr))
				return (1);
			sum = in6_cksum(m, p, off, len);
			/*
			 * XXX
			 * IPv6 H/W cksum off-load not supported yet!
			 *
			 * if (sum == 0) {
			 *	m->m_pkthdr.csum_flags |=
			 *	    (CSUM_DATA_VALID|CSUM_PSEUDO_HDR);
			 *	m->m_pkthdr.csum_data = 0xffff;
			 *}
			 */
			break;
#endif /* INET6 */
		default:
			return (1);
		}
	}
	if (sum) {
		switch (p) {
		case IPPROTO_TCP:
			tcpstat.tcps_rcvbadsum++;
			break;
		case IPPROTO_UDP:
			udp_stat.udps_badsum++;
			break;
		case IPPROTO_ICMP:
			icmpstat.icps_checksum++;
			break;
#ifdef INET6
		case IPPROTO_ICMPV6:
			icmp6stat.icp6s_checksum++;
			break;
#endif /* INET6 */
		}
		return (1);
	}
	return (0);
}

struct pf_divert *
pf_find_divert(struct mbuf *m)
{
	struct m_tag    *mtag;

	if ((mtag = m_tag_find(m, PACKET_TAG_PF_DIVERT, NULL)) == NULL)
		return (NULL);

	return ((struct pf_divert *)(mtag + 1));
}

struct pf_divert *
pf_get_divert(struct mbuf *m)
{
	struct m_tag    *mtag;

	if ((mtag = m_tag_find(m, PACKET_TAG_PF_DIVERT, NULL)) == NULL) {
		mtag = m_tag_get(PACKET_TAG_PF_DIVERT, sizeof(struct pf_divert),
		    M_NOWAIT);
		if (mtag == NULL)
			return (NULL);
		bzero(mtag + 1, sizeof(struct pf_divert));
		m_tag_prepend(m, mtag);
	}

	return ((struct pf_divert *)(mtag + 1));
}

#ifdef INET

/*
 * WARNING: pf_token held shared on entry, THIS IS CPU LOCALIZED CODE
 */
int
pf_test(int dir, struct ifnet *ifp, struct mbuf **m0,
    struct ether_header *eh, struct inpcb *inp)
{
	struct pfi_kif		*kif;
	u_short			 action, reason = 0, log = 0;
	struct mbuf		*m = *m0;
	struct ip		*h = NULL;
	struct pf_rule		*a = NULL, *r = &pf_default_rule, *tr, *nr;
	struct pf_state		*s = NULL;
	struct pf_ruleset	*ruleset = NULL;
	struct pf_pdesc		 pd;
	int			 off, dirndx;
#ifdef ALTQ
	int			 pqid = 0;
#endif

	if (m->m_pkthdr.fw_flags & IPFW_MBUF_CONTINUE) {
		/* Skip us; continue in ipfw. */
		return (PF_PASS);
	}

	if (!pf_status.running)
		return (PF_PASS);

	memset(&pd, 0, sizeof(pd));
#ifdef foo
	if (ifp->if_type == IFT_CARP && ifp->if_carpdev)
		kif = (struct pfi_kif *)ifp->if_carpdev->if_pf_kif;
	else
#endif
		kif = (struct pfi_kif *)ifp->if_pf_kif;

	if (kif == NULL) {
		DPFPRINTF(PF_DEBUG_URGENT,
		    ("pf_test: kif == NULL, if_xname %s\n", ifp->if_xname));
		return (PF_DROP);
	}
	if (kif->pfik_flags & PFI_IFLAG_SKIP)
		return (PF_PASS);

#ifdef DIAGNOSTIC
	if ((m->m_flags & M_PKTHDR) == 0)
		panic("non-M_PKTHDR is passed to pf_test");
#endif /* DIAGNOSTIC */

	if (m->m_pkthdr.len < (int)sizeof(*h)) {
		action = PF_DROP;
		REASON_SET(&reason, PFRES_SHORT);
		log = 1;
		goto done;
	}

	/*
	 * DragonFly doesn't zero the auxillary pkghdr fields, only fw_flags,
	 * so make sure pf.flags is clear.
	 */
	if (m->m_pkthdr.fw_flags & PF_MBUF_TAGGED)
		return (PF_PASS);
	m->m_pkthdr.pf.flags = 0;
	/* Re-Check when updating to > 4.4 */
	m->m_pkthdr.pf.statekey = NULL;

	/* We do IP header normalization and packet reassembly here */
	if (pf_normalize_ip(m0, dir, kif, &reason, &pd) != PF_PASS) {
		action = PF_DROP;
		goto done;
	}
	m = *m0;	/* pf_normalize messes with m0 */
	h = mtod(m, struct ip *);

	off = h->ip_hl << 2;
	if (off < (int)sizeof(*h)) {
		action = PF_DROP;
		REASON_SET(&reason, PFRES_SHORT);
		log = 1;
		goto done;
	}

	pd.src = (struct pf_addr *)&h->ip_src;
	pd.dst = (struct pf_addr *)&h->ip_dst;
	pd.sport = pd.dport = NULL;
	pd.ip_sum = &h->ip_sum;
	pd.proto_sum = NULL;
	pd.proto = h->ip_p;
	pd.dir = dir;
	pd.sidx = (dir == PF_IN) ? 0 : 1;
	pd.didx = (dir == PF_IN) ? 1 : 0;
	pd.af = AF_INET;
	pd.tos = h->ip_tos;
	pd.tot_len = ntohs(h->ip_len);
	pd.eh = eh;

	/* handle fragments that didn't get reassembled by normalization */
	if (h->ip_off & htons(IP_MF | IP_OFFMASK)) {
		action = pf_test_fragment(&r, dir, kif, m, h,
		    &pd, &a, &ruleset);
		goto done;
	}

	switch (h->ip_p) {

	case IPPROTO_TCP: {
		struct tcphdr	th;

		pd.hdr.tcp = &th;
		if (!pf_pull_hdr(m, off, &th, sizeof(th),
		    &action, &reason, AF_INET)) {
			log = action != PF_PASS;
			goto done;
		}
		pd.p_len = pd.tot_len - off - (th.th_off << 2);
#ifdef ALTQ
		if ((th.th_flags & TH_ACK) && pd.p_len == 0)
			pqid = 1;
#endif
		action = pf_normalize_tcp(dir, kif, m, 0, off, h, &pd);
		if (action == PF_DROP)
			goto done;
		action = pf_test_state_tcp(&s, dir, kif, m, off, h, &pd,
					   &reason);
		if (action == PF_PASS) {
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL) {
			action = pf_test_rule(&r, &s, dir, kif,
					      m, off, h, &pd, &a,
					      &ruleset, NULL, inp);
		}
		break;
	}

	case IPPROTO_UDP: {
		struct udphdr	uh;

		pd.hdr.udp = &uh;
		if (!pf_pull_hdr(m, off, &uh, sizeof(uh),
		    &action, &reason, AF_INET)) {
			log = action != PF_PASS;
			goto done;
		}
		if (uh.uh_dport == 0 ||
		    ntohs(uh.uh_ulen) > m->m_pkthdr.len - off ||
		    ntohs(uh.uh_ulen) < sizeof(struct udphdr)) {
			action = PF_DROP;
			REASON_SET(&reason, PFRES_SHORT);
			goto done;
		}
		action = pf_test_state_udp(&s, dir, kif, m, off, h, &pd);
		if (action == PF_PASS) {
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL) {
			action = pf_test_rule(&r, &s, dir, kif,
					      m, off, h, &pd, &a,
					      &ruleset, NULL, inp);
		}
		break;
	}

	case IPPROTO_ICMP: {
		struct icmp	ih;

		pd.hdr.icmp = &ih;
		if (!pf_pull_hdr(m, off, &ih, ICMP_MINLEN,
		    &action, &reason, AF_INET)) {
			log = action != PF_PASS;
			goto done;
		}
		action = pf_test_state_icmp(&s, dir, kif, m, off, h, &pd,
					    &reason);
		if (action == PF_PASS) {
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL) {
			action = pf_test_rule(&r, &s, dir, kif,
					      m, off, h, &pd, &a,
					      &ruleset, NULL, inp);
		}
		break;
	}

	default:
		action = pf_test_state_other(&s, dir, kif, m, &pd);
		if (action == PF_PASS) {
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL) {
			action = pf_test_rule(&r, &s, dir, kif, m, off, h,
					      &pd, &a, &ruleset, NULL, inp);
		}
		break;
	}

done:
	if (action == PF_PASS && h->ip_hl > 5 &&
	    !((s && s->state_flags & PFSTATE_ALLOWOPTS) || r->allow_opts)) {
		action = PF_DROP;
		REASON_SET(&reason, PFRES_IPOPTIONS);
		log = 1;
		DPFPRINTF(PF_DEBUG_MISC,
		    ("pf: dropping packet with ip options\n"));
	}

	if ((s && s->tag) || r->rtableid)
		pf_tag_packet(m, s ? s->tag : 0, r->rtableid);

#if 0
	if (dir == PF_IN && s && s->key[PF_SK_STACK])
		m->m_pkthdr.pf.statekey = s->key[PF_SK_STACK];
#endif

#ifdef ALTQ
	/*
	 * Generate a hash code and qid request for ALTQ.  A qid of 0
	 * is allowed and will cause altq to select the default queue.
	 */
	if (action == PF_PASS) {
		m->m_pkthdr.fw_flags |= PF_MBUF_STRUCTURE;
		if (pqid || (pd.tos & IPTOS_LOWDELAY))
			m->m_pkthdr.pf.qid = r->pqid;
		else
			m->m_pkthdr.pf.qid = r->qid;
		m->m_pkthdr.pf.ecn_af = AF_INET;
		m->m_pkthdr.pf.hdr = h;
		/* add connection hash for fairq */
		if (s) {
			/* for fairq */
			m->m_pkthdr.pf.state_hash = s->hash;
			m->m_pkthdr.pf.flags |= PF_TAG_STATE_HASHED;
		}
	}
#endif /* ALTQ */

	/*
	 * connections redirected to loopback should not match sockets
	 * bound specifically to loopback due to security implications,
	 * see tcp_input() and in_pcblookup_listen().
	 */
	if (dir == PF_IN && action == PF_PASS && (pd.proto == IPPROTO_TCP ||
	    pd.proto == IPPROTO_UDP) && s != NULL && s->nat_rule.ptr != NULL &&
	    (s->nat_rule.ptr->action == PF_RDR ||
	    s->nat_rule.ptr->action == PF_BINAT) &&
	    (ntohl(pd.dst->v4.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET)
	{
		m->m_pkthdr.pf.flags |= PF_TAG_TRANSLATE_LOCALHOST;
	}

	if (dir == PF_IN && action == PF_PASS && r->divert.port) {
		struct pf_divert *divert;

		if ((divert = pf_get_divert(m))) {
			m->m_pkthdr.pf.flags |= PF_TAG_DIVERTED;
			divert->port = r->divert.port;
			divert->addr.ipv4 = r->divert.addr.v4;
		}
	}

	if (log) {
		struct pf_rule *lr;

		if (s != NULL && s->nat_rule.ptr != NULL &&
		    s->nat_rule.ptr->log & PF_LOG_ALL)
			lr = s->nat_rule.ptr;
		else
			lr = r;
		PFLOG_PACKET(kif, h, m, AF_INET, dir, reason, lr, a, ruleset,
		    &pd);
	}

	kif->pfik_bytes[0][dir == PF_OUT][action != PF_PASS] += pd.tot_len;
	kif->pfik_packets[0][dir == PF_OUT][action != PF_PASS]++;

	if (action == PF_PASS || r->action == PF_DROP) {
		dirndx = (dir == PF_OUT);
		r->packets[dirndx]++;
		r->bytes[dirndx] += pd.tot_len;
		if (a != NULL) {
			a->packets[dirndx]++;
			a->bytes[dirndx] += pd.tot_len;
		}
		if (s != NULL) {
			if (s->nat_rule.ptr != NULL) {
				s->nat_rule.ptr->packets[dirndx]++;
				s->nat_rule.ptr->bytes[dirndx] += pd.tot_len;
			}
			if (s->src_node != NULL) {
				s->src_node->packets[dirndx]++;
				s->src_node->bytes[dirndx] += pd.tot_len;
			}
			if (s->nat_src_node != NULL) {
				s->nat_src_node->packets[dirndx]++;
				s->nat_src_node->bytes[dirndx] += pd.tot_len;
			}
			dirndx = (dir == s->direction) ? 0 : 1;
			s->packets[dirndx]++;
			s->bytes[dirndx] += pd.tot_len;
		}
		tr = r;
		nr = (s != NULL) ? s->nat_rule.ptr : pd.nat_rule;
		if (nr != NULL && r == &pf_default_rule)
			tr = nr;
		if (tr->src.addr.type == PF_ADDR_TABLE)
			pfr_update_stats(tr->src.addr.p.tbl,
			    (s == NULL) ? pd.src :
			    &s->key[(s->direction == PF_IN)]->
				addr[(s->direction == PF_OUT)],
			    pd.af, pd.tot_len, dir == PF_OUT,
			    r->action == PF_PASS, tr->src.neg);
		if (tr->dst.addr.type == PF_ADDR_TABLE)
			pfr_update_stats(tr->dst.addr.p.tbl,
			    (s == NULL) ? pd.dst :
			    &s->key[(s->direction == PF_IN)]->
				addr[(s->direction == PF_IN)],
			    pd.af, pd.tot_len, dir == PF_OUT,
			    r->action == PF_PASS, tr->dst.neg);
	}


	if (action == PF_SYNPROXY_DROP) {
		m_freem(*m0);
		*m0 = NULL;
		action = PF_PASS;
	} else if (r->rt) {
		/* pf_route can free the mbuf causing *m0 to become NULL */
		pf_route(m0, r, dir, kif->pfik_ifp, s, &pd);
	}

	return (action);
}
#endif /* INET */

#ifdef INET6

/*
 * WARNING: pf_token held shared on entry, THIS IS CPU LOCALIZED CODE
 */
int
pf_test6(int dir, struct ifnet *ifp, struct mbuf **m0,
    struct ether_header *eh, struct inpcb *inp)
{
	struct pfi_kif		*kif;
	u_short			 action, reason = 0, log = 0;
	struct mbuf		*m = *m0, *n = NULL;
	struct ip6_hdr		*h = NULL;
	struct pf_rule		*a = NULL, *r = &pf_default_rule, *tr, *nr;
	struct pf_state		*s = NULL;
	struct pf_ruleset	*ruleset = NULL;
	struct pf_pdesc		 pd;
	int			 off, terminal = 0, dirndx, rh_cnt = 0;

	if (!pf_status.running)
		return (PF_PASS);

	memset(&pd, 0, sizeof(pd));
#ifdef foo
	if (ifp->if_type == IFT_CARP && ifp->if_carpdev)
		kif = (struct pfi_kif *)ifp->if_carpdev->if_pf_kif;
	else
#endif
		kif = (struct pfi_kif *)ifp->if_pf_kif;

	if (kif == NULL) {
		DPFPRINTF(PF_DEBUG_URGENT,
		    ("pf_test6: kif == NULL, if_xname %s\n", ifp->if_xname));
		return (PF_DROP);
	}
	if (kif->pfik_flags & PFI_IFLAG_SKIP)
		return (PF_PASS);

#ifdef DIAGNOSTIC
	if ((m->m_flags & M_PKTHDR) == 0)
		panic("non-M_PKTHDR is passed to pf_test6");
#endif /* DIAGNOSTIC */

	if (m->m_pkthdr.len < (int)sizeof(*h)) {
		action = PF_DROP;
		REASON_SET(&reason, PFRES_SHORT);
		log = 1;
		goto done;
	}

	/*
	 * DragonFly doesn't zero the auxillary pkghdr fields, only fw_flags,
	 * so make sure pf.flags is clear.
	 */
	if (m->m_pkthdr.fw_flags & PF_MBUF_TAGGED)
		return (PF_PASS);
	m->m_pkthdr.pf.flags = 0;
	/* Re-Check when updating to > 4.4 */
	m->m_pkthdr.pf.statekey = NULL;

	/* We do IP header normalization and packet reassembly here */
	if (pf_normalize_ip6(m0, dir, kif, &reason, &pd) != PF_PASS) {
		action = PF_DROP;
		goto done;
	}
	m = *m0;	/* pf_normalize messes with m0 */
	h = mtod(m, struct ip6_hdr *);

#if 1
	/*
	 * we do not support jumbogram yet.  if we keep going, zero ip6_plen
	 * will do something bad, so drop the packet for now.
	 */
	if (htons(h->ip6_plen) == 0) {
		action = PF_DROP;
		REASON_SET(&reason, PFRES_NORM);	/*XXX*/
		goto done;
	}
#endif

	pd.src = (struct pf_addr *)&h->ip6_src;
	pd.dst = (struct pf_addr *)&h->ip6_dst;
	pd.sport = pd.dport = NULL;
	pd.ip_sum = NULL;
	pd.proto_sum = NULL;
	pd.dir = dir;
	pd.sidx = (dir == PF_IN) ? 0 : 1;
	pd.didx = (dir == PF_IN) ? 1 : 0;
	pd.af = AF_INET6;
	pd.tos = 0;
	pd.tot_len = ntohs(h->ip6_plen) + sizeof(struct ip6_hdr);
	pd.eh = eh;

	off = ((caddr_t)h - m->m_data) + sizeof(struct ip6_hdr);
	pd.proto = h->ip6_nxt;
	do {
		switch (pd.proto) {
		case IPPROTO_FRAGMENT:
			action = pf_test_fragment(&r, dir, kif, m, h,
			    &pd, &a, &ruleset);
			if (action == PF_DROP)
				REASON_SET(&reason, PFRES_FRAG);
			goto done;
		case IPPROTO_ROUTING: {
			struct ip6_rthdr rthdr;

			if (rh_cnt++) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: IPv6 more than one rthdr\n"));
				action = PF_DROP;
				REASON_SET(&reason, PFRES_IPOPTIONS);
				log = 1;
				goto done;
			}
			if (!pf_pull_hdr(m, off, &rthdr, sizeof(rthdr), NULL,
			    &reason, pd.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: IPv6 short rthdr\n"));
				action = PF_DROP;
				REASON_SET(&reason, PFRES_SHORT);
				log = 1;
				goto done;
			}
			if (rthdr.ip6r_type == IPV6_RTHDR_TYPE_0) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: IPv6 rthdr0\n"));
				action = PF_DROP;
				REASON_SET(&reason, PFRES_IPOPTIONS);
				log = 1;
				goto done;
			}
			/* FALLTHROUGH */
		}
		case IPPROTO_AH:
		case IPPROTO_HOPOPTS:
		case IPPROTO_DSTOPTS: {
			/* get next header and header length */
			struct ip6_ext	opt6;

			if (!pf_pull_hdr(m, off, &opt6, sizeof(opt6),
			    NULL, &reason, pd.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: IPv6 short opt\n"));
				action = PF_DROP;
				log = 1;
				goto done;
			}
			if (pd.proto == IPPROTO_AH)
				off += (opt6.ip6e_len + 2) * 4;
			else
				off += (opt6.ip6e_len + 1) * 8;
			pd.proto = opt6.ip6e_nxt;
			/* goto the next header */
			break;
		}
		default:
			terminal++;
			break;
		}
	} while (!terminal);

	/* if there's no routing header, use unmodified mbuf for checksumming */
	if (!n)
		n = m;

	switch (pd.proto) {

	case IPPROTO_TCP: {
		struct tcphdr	th;

		pd.hdr.tcp = &th;
		if (!pf_pull_hdr(m, off, &th, sizeof(th),
		    &action, &reason, AF_INET6)) {
			log = action != PF_PASS;
			goto done;
		}
		pd.p_len = pd.tot_len - off - (th.th_off << 2);
		action = pf_normalize_tcp(dir, kif, m, 0, off, h, &pd);
		if (action == PF_DROP)
			goto done;
		action = pf_test_state_tcp(&s, dir, kif, m, off, h, &pd,
					   &reason);
		if (action == PF_PASS) {
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL) {
			action = pf_test_rule(&r, &s, dir, kif,
					      m, off, h, &pd, &a,
					      &ruleset, NULL, inp);
		}
		break;
	}

	case IPPROTO_UDP: {
		struct udphdr	uh;

		pd.hdr.udp = &uh;
		if (!pf_pull_hdr(m, off, &uh, sizeof(uh),
		    &action, &reason, AF_INET6)) {
			log = action != PF_PASS;
			goto done;
		}
		if (uh.uh_dport == 0 ||
		    ntohs(uh.uh_ulen) > m->m_pkthdr.len - off ||
		    ntohs(uh.uh_ulen) < sizeof(struct udphdr)) {
			action = PF_DROP;
			REASON_SET(&reason, PFRES_SHORT);
			goto done;
		}
		action = pf_test_state_udp(&s, dir, kif, m, off, h, &pd);
		if (action == PF_PASS) {
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL) {
			action = pf_test_rule(&r, &s, dir, kif,
					      m, off, h, &pd, &a,
					      &ruleset, NULL, inp);
		}
		break;
	}

	case IPPROTO_ICMPV6: {
		struct icmp6_hdr	ih;

		pd.hdr.icmp6 = &ih;
		if (!pf_pull_hdr(m, off, &ih, sizeof(ih),
		    &action, &reason, AF_INET6)) {
			log = action != PF_PASS;
			goto done;
		}
		action = pf_test_state_icmp(&s, dir, kif,
					    m, off, h, &pd, &reason);
		if (action == PF_PASS) {
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL) {
			action = pf_test_rule(&r, &s, dir, kif,
					      m, off, h, &pd, &a,
					      &ruleset, NULL, inp);
		}
		break;
	}

	default:
		action = pf_test_state_other(&s, dir, kif, m, &pd);
		if (action == PF_PASS) {
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL) {
			action = pf_test_rule(&r, &s, dir, kif, m, off, h,
					      &pd, &a, &ruleset, NULL, inp);
		}
		break;
	}

done:
	if (n != m) {
		m_freem(n);
		n = NULL;
	}

	/* handle dangerous IPv6 extension headers. */
	if (action == PF_PASS && rh_cnt &&
	    !((s && s->state_flags & PFSTATE_ALLOWOPTS) || r->allow_opts)) {
		action = PF_DROP;
		REASON_SET(&reason, PFRES_IPOPTIONS);
		log = 1;
		DPFPRINTF(PF_DEBUG_MISC,
		    ("pf: dropping packet with dangerous v6 headers\n"));
	}

	if ((s && s->tag) || r->rtableid)
		pf_tag_packet(m, s ? s->tag : 0, r->rtableid);

#if 0
	if (dir == PF_IN && s && s->key[PF_SK_STACK])
		m->m_pkthdr.pf.statekey = s->key[PF_SK_STACK];
#endif

#ifdef ALTQ
	/*
	 * Generate a hash code and qid request for ALTQ.  A qid of 0
	 * is allowed and will cause altq to select the default queue.
	 */
	if (action == PF_PASS) {
		m->m_pkthdr.fw_flags |= PF_MBUF_STRUCTURE;
		if (pd.tos & IPTOS_LOWDELAY)
			m->m_pkthdr.pf.qid = r->pqid;
		else
			m->m_pkthdr.pf.qid = r->qid;
		m->m_pkthdr.pf.ecn_af = AF_INET6;
		m->m_pkthdr.pf.hdr = h;
		if (s) {
			/* for fairq */
			m->m_pkthdr.pf.state_hash = s->hash;
			m->m_pkthdr.pf.flags |= PF_TAG_STATE_HASHED;
		}
	}
#endif /* ALTQ */

	if (dir == PF_IN && action == PF_PASS && (pd.proto == IPPROTO_TCP ||
	    pd.proto == IPPROTO_UDP) && s != NULL && s->nat_rule.ptr != NULL &&
	    (s->nat_rule.ptr->action == PF_RDR ||
	    s->nat_rule.ptr->action == PF_BINAT) &&
	    IN6_IS_ADDR_LOOPBACK(&pd.dst->v6))
	{
		m->m_pkthdr.pf.flags |= PF_TAG_TRANSLATE_LOCALHOST;
	}

	if (dir == PF_IN && action == PF_PASS && r->divert.port) {
		struct pf_divert *divert;

		if ((divert = pf_get_divert(m))) {
			m->m_pkthdr.pf.flags |= PF_TAG_DIVERTED;
			divert->port = r->divert.port;
			divert->addr.ipv6 = r->divert.addr.v6;
		}
	}

	if (log) {
		struct pf_rule *lr;

		if (s != NULL && s->nat_rule.ptr != NULL &&
		    s->nat_rule.ptr->log & PF_LOG_ALL)
			lr = s->nat_rule.ptr;
		else
			lr = r;
		PFLOG_PACKET(kif, h, m, AF_INET6, dir, reason, lr, a, ruleset,
		    &pd);
	}

	kif->pfik_bytes[1][dir == PF_OUT][action != PF_PASS] += pd.tot_len;
	kif->pfik_packets[1][dir == PF_OUT][action != PF_PASS]++;

	if (action == PF_PASS || r->action == PF_DROP) {
		dirndx = (dir == PF_OUT);
		r->packets[dirndx]++;
		r->bytes[dirndx] += pd.tot_len;
		if (a != NULL) {
			a->packets[dirndx]++;
			a->bytes[dirndx] += pd.tot_len;
		}
		if (s != NULL) {
			if (s->nat_rule.ptr != NULL) {
				s->nat_rule.ptr->packets[dirndx]++;
				s->nat_rule.ptr->bytes[dirndx] += pd.tot_len;
			}
			if (s->src_node != NULL) {
				s->src_node->packets[dirndx]++;
				s->src_node->bytes[dirndx] += pd.tot_len;
			}
			if (s->nat_src_node != NULL) {
				s->nat_src_node->packets[dirndx]++;
				s->nat_src_node->bytes[dirndx] += pd.tot_len;
			}
			dirndx = (dir == s->direction) ? 0 : 1;
			s->packets[dirndx]++;
			s->bytes[dirndx] += pd.tot_len;
		}
		tr = r;
		nr = (s != NULL) ? s->nat_rule.ptr : pd.nat_rule;
		if (nr != NULL && r == &pf_default_rule)
			tr = nr;
		if (tr->src.addr.type == PF_ADDR_TABLE)
			pfr_update_stats(tr->src.addr.p.tbl,
			    (s == NULL) ? pd.src :
			    &s->key[(s->direction == PF_IN)]->addr[0],
			    pd.af, pd.tot_len, dir == PF_OUT,
			    r->action == PF_PASS, tr->src.neg);
		if (tr->dst.addr.type == PF_ADDR_TABLE)
			pfr_update_stats(tr->dst.addr.p.tbl,
			    (s == NULL) ? pd.dst :
			    &s->key[(s->direction == PF_IN)]->addr[1],
			    pd.af, pd.tot_len, dir == PF_OUT,
			    r->action == PF_PASS, tr->dst.neg);
	}


	if (action == PF_SYNPROXY_DROP) {
		m_freem(*m0);
		*m0 = NULL;
		action = PF_PASS;
	} else if (r->rt)
		/* pf_route6 can free the mbuf causing *m0 to become NULL */
		pf_route6(m0, r, dir, kif->pfik_ifp, s, &pd);

	return (action);
}
#endif /* INET6 */

int
pf_check_congestion(struct ifqueue *ifq)
{
		return (0);
}
