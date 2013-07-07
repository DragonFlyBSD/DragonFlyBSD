/*	$OpenBSD: pf_ioctl.c,v 1.209 2008/06/29 08:42:15 mcbride Exp $ */
/*add $OpenBSD: pf_ioctl.c,v 1.212 2009/02/15 20:42:33 mbalmer Exp $ */

/*
 * Copyright (c) 2010 The DragonFly Project.  All rights reserved.
 *
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2002,2003 Henning Brauer
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
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/mbuf.h>
#include <sys/filio.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/lock.h>

#include <sys/thread2.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>

#include <net/pf/pfvar.h>
#include <sys/md5.h>

#include <net/pf/if_pflog.h>
#include <net/pf/if_pfsync.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet/in_pcb.h>
#endif /* INET6 */

#ifdef ALTQ
#include <net/altq/altq.h>
#endif

#include <machine/limits.h>
#include <net/pfil.h>
#include <sys/mutex.h>

u_int rt_numfibs = RT_NUMFIBS;

void			 pfattach(void);
struct pf_pool		*pf_get_pool(char *, u_int32_t, u_int8_t, u_int32_t,
			    u_int8_t, u_int8_t, u_int8_t);

void			 pf_mv_pool(struct pf_palist *, struct pf_palist *);
void			 pf_empty_pool(struct pf_palist *);
#ifdef ALTQ
int			 pf_begin_altq(u_int32_t *);
int			 pf_rollback_altq(u_int32_t);
int			 pf_commit_altq(u_int32_t);
int			 pf_enable_altq(struct pf_altq *);
int			 pf_disable_altq(struct pf_altq *);
#endif /* ALTQ */
int			 pf_begin_rules(u_int32_t *, int, const char *);
int			 pf_rollback_rules(u_int32_t, int, char *);
int			 pf_setup_pfsync_matching(struct pf_ruleset *);
void			 pf_hash_rule(MD5_CTX *, struct pf_rule *);
void			 pf_hash_rule_addr(MD5_CTX *, struct pf_rule_addr *);
int			 pf_commit_rules(u_int32_t, int, char *);
int			 pf_addr_setup(struct pf_ruleset *,
			    struct pf_addr_wrap *, sa_family_t);
void			 pf_addr_copyout(struct pf_addr_wrap *);

struct pf_rule		 pf_default_rule;
struct lock		 pf_consistency_lock;
#ifdef ALTQ
static int		 pf_altq_running;
#endif

#define	TAGID_MAX	 50000
TAILQ_HEAD(pf_tags, pf_tagname)	pf_tags = TAILQ_HEAD_INITIALIZER(pf_tags),
				pf_qids = TAILQ_HEAD_INITIALIZER(pf_qids);

#if (PF_QNAME_SIZE != PF_TAG_NAME_SIZE)
#error PF_QNAME_SIZE must be equal to PF_TAG_NAME_SIZE
#endif
u_int16_t		 tagname2tag(struct pf_tags *, char *);
void			 tag2tagname(struct pf_tags *, u_int16_t, char *);
void			 tag_unref(struct pf_tags *, u_int16_t);
int			 pf_rtlabel_add(struct pf_addr_wrap *);
void			 pf_rtlabel_remove(struct pf_addr_wrap *);
void			 pf_rtlabel_copyout(struct pf_addr_wrap *);

#define DPFPRINTF(n, x) if (pf_status.debug >= (n)) kprintf x

static cdev_t	pf_dev;

static MALLOC_DEFINE(M_PFRULEPL, "pfrulepl", "pf rule pool list");
static MALLOC_DEFINE(M_PFALTQPL, "pfaltqpl", "pf altq pool list");
static MALLOC_DEFINE(M_PFPOOLADDRPL, "pfpooladdrpl", "pf pool address pool list");
static MALLOC_DEFINE(M_PFFRENTPL, "pffrent", "pf frent pool list");


/*
 * XXX - These are new and need to be checked when moveing to a new version
 */
static void		 pf_clear_states(void);
static int		 pf_clear_tables(void);
static void		 pf_clear_srcnodes(void);
/*
 * XXX - These are new and need to be checked when moveing to a new version
 */
 
/*
 * Wrapper functions for pfil(9) hooks
 */
static int pf_check_in(void *arg, struct mbuf **m, struct ifnet *ifp,
		int dir);
static int pf_check_out(void *arg, struct mbuf **m, struct ifnet *ifp,
		int dir);
#ifdef INET6
static int pf_check6_in(void *arg, struct mbuf **m, struct ifnet *ifp,
		int dir);
static int pf_check6_out(void *arg, struct mbuf **m, struct ifnet *ifp,
		int dir);
#endif

static int 		 hook_pf(void);
static int 		 dehook_pf(void);
static int 		 shutdown_pf(void);
static int 		 pf_load(void);
static int 		 pf_unload(void);

d_open_t	pfopen;
d_close_t	pfclose;
d_ioctl_t	pfioctl;

static struct dev_ops pf_ops = {	    /* XXX convert to port model */
	{ PF_NAME, 73, 0 },
	.d_open =	pfopen,
	.d_close =	pfclose,
	.d_ioctl =	pfioctl
};

static volatile int pf_pfil_hooked = 0;
int pf_end_threads = 0;

int debug_pfugidhack = 0;
SYSCTL_INT(_debug, OID_AUTO, pfugidhack, CTLFLAG_RW, &debug_pfugidhack, 0,
	"Enable/disable pf user/group rules mpsafe hack");

void
pfattach(void)
{
	u_int32_t *my_timeout = pf_default_rule.timeout;


	if (!rn_inithead((void **)&pf_maskhead, NULL, 0)) {
		kprintf("pf mask radix tree create failed\n");
		return;
	}
	kmalloc_create(&pf_state_pl, "pf state pool list");
	kmalloc_raise_limit(pf_state_pl, 0);
	kmalloc_create(&pf_frent_pl, "pf fragment pool list");
	kmalloc_raise_limit(pf_frent_pl, 0);
	kmalloc_create(&pf_cent_pl, "pf cent pool list");
	kmalloc_raise_limit(pf_cent_pl, 0);
	
	pfr_initialize();
	pfi_initialize();
	pf_osfp_initialize();

	pf_pool_limits[PF_LIMIT_STATES].pp = pf_state_pl;
	pf_pool_limits[PF_LIMIT_STATES].limit = PFSTATE_HIWAT;
	pf_pool_limits[PF_LIMIT_FRAGS].pp = pf_frent_pl;
	pf_pool_limits[PF_LIMIT_FRAGS].limit = PFFRAG_FRENT_HIWAT;
	if (ctob(physmem) <= 100*1024*1024)
		pf_pool_limits[PF_LIMIT_TABLE_ENTRIES].limit =
		    PFR_KENTRY_HIWAT_SMALL;

	RB_INIT(&tree_src_tracking);
	RB_INIT(&pf_anchors);
	pf_init_ruleset(&pf_main_ruleset);
	TAILQ_INIT(&pf_altqs[0]);
	TAILQ_INIT(&pf_altqs[1]);
	TAILQ_INIT(&pf_pabuf);
	pf_altqs_active = &pf_altqs[0];
	pf_altqs_inactive = &pf_altqs[1];
	TAILQ_INIT(&state_list);

	/* default rule should never be garbage collected */
	pf_default_rule.entries.tqe_prev = &pf_default_rule.entries.tqe_next;
	pf_default_rule.action = PF_PASS;
	pf_default_rule.nr = (uint32_t)(-1);
	pf_default_rule.rtableid = -1;

	/* initialize default timeouts */
	my_timeout[PFTM_TCP_FIRST_PACKET] = 120;	/* First TCP packet */
	my_timeout[PFTM_TCP_OPENING] = 30; 		/* No response yet */
	my_timeout[PFTM_TCP_ESTABLISHED] = 24*60*60;	/* Established */
	my_timeout[PFTM_TCP_CLOSING] = 15 * 60;		/* Half closed */
	my_timeout[PFTM_TCP_FIN_WAIT] = 45;		/* Got both FINs */
	my_timeout[PFTM_TCP_CLOSED] = 90;		/* Got a RST */
	my_timeout[PFTM_UDP_FIRST_PACKET] = 60;		/* First UDP packet */
	my_timeout[PFTM_UDP_SINGLE] = 30;		/* Unidirectional */
	my_timeout[PFTM_UDP_MULTIPLE] = 60;		/* Bidirectional */
	my_timeout[PFTM_ICMP_FIRST_PACKET] = 20;	/* First ICMP packet */
	my_timeout[PFTM_ICMP_ERROR_REPLY] = 10;		/* Got error response */
	my_timeout[PFTM_OTHER_FIRST_PACKET] = 60;	/* First packet */
	my_timeout[PFTM_OTHER_SINGLE] = 30;		/* Unidirectional */
	my_timeout[PFTM_OTHER_MULTIPLE] = 60;		/* Bidirectional */
	my_timeout[PFTM_FRAG] = 30;			/* Fragment expire */
	my_timeout[PFTM_INTERVAL] = 10;			/* Expire interval */
	my_timeout[PFTM_SRC_NODE] = 0;		/* Source Tracking */
	my_timeout[PFTM_TS_DIFF] = 30;		/* Allowed TS diff */
	my_timeout[PFTM_ADAPTIVE_START] = PFSTATE_ADAPT_START;
	my_timeout[PFTM_ADAPTIVE_END] = PFSTATE_ADAPT_END;
	
	pf_normalize_init();
	bzero(&pf_status, sizeof(pf_status));
	pf_status.debug = PF_DEBUG_URGENT;
	/* XXX do our best to avoid a conflict */
	pf_status.hostid = karc4random();

	if (kthread_create(pf_purge_thread, NULL, NULL, "pfpurge"))
		panic("pfpurge thread");
}

int
pfopen(struct dev_open_args *ap)
{
	lwkt_gettoken(&pf_token);
	cdev_t dev = ap->a_head.a_dev;
	if (minor(dev) >= 1) {
		lwkt_reltoken(&pf_token);
		return (ENXIO);
	}
	lwkt_reltoken(&pf_token);
	return (0);
}

int
pfclose(struct dev_close_args *ap)
{
	lwkt_gettoken(&pf_token);
	cdev_t dev = ap->a_head.a_dev;
	if (minor(dev) >= 1) {
		lwkt_reltoken(&pf_token);
		return (ENXIO);
	}
	lwkt_reltoken(&pf_token);
	return (0);
}

struct pf_pool *
pf_get_pool(char *anchor, u_int32_t ticket, u_int8_t rule_action,
    u_int32_t rule_number, u_int8_t r_last, u_int8_t active,
    u_int8_t check_ticket)
{
	struct pf_ruleset	*ruleset;
	struct pf_rule		*rule;
	int			 rs_num;

	ruleset = pf_find_ruleset(anchor);
	if (ruleset == NULL)
		return (NULL);
	rs_num = pf_get_ruleset_number(rule_action);
	if (rs_num >= PF_RULESET_MAX)
		return (NULL);
	if (active) {
		if (check_ticket && ticket !=
		    ruleset->rules[rs_num].active.ticket)
			return (NULL);
		if (r_last)
			rule = TAILQ_LAST(ruleset->rules[rs_num].active.ptr,
			    pf_rulequeue);
		else
			rule = TAILQ_FIRST(ruleset->rules[rs_num].active.ptr);
	} else {
		if (check_ticket && ticket !=
		    ruleset->rules[rs_num].inactive.ticket)
			return (NULL);
		if (r_last)
			rule = TAILQ_LAST(ruleset->rules[rs_num].inactive.ptr,
			    pf_rulequeue);
		else
			rule = TAILQ_FIRST(ruleset->rules[rs_num].inactive.ptr);
	}
	if (!r_last) {
		while ((rule != NULL) && (rule->nr != rule_number))
			rule = TAILQ_NEXT(rule, entries);
	}
	if (rule == NULL)
		return (NULL);

	return (&rule->rpool);
}

void
pf_mv_pool(struct pf_palist *poola, struct pf_palist *poolb)
{
	struct pf_pooladdr	*mv_pool_pa;

	while ((mv_pool_pa = TAILQ_FIRST(poola)) != NULL) {
		TAILQ_REMOVE(poola, mv_pool_pa, entries);
		TAILQ_INSERT_TAIL(poolb, mv_pool_pa, entries);
	}
}

void
pf_empty_pool(struct pf_palist *poola)
{
	struct pf_pooladdr	*empty_pool_pa;

	while ((empty_pool_pa = TAILQ_FIRST(poola)) != NULL) {
		pfi_dynaddr_remove(&empty_pool_pa->addr);
		pf_tbladdr_remove(&empty_pool_pa->addr);
		pfi_kif_unref(empty_pool_pa->kif, PFI_KIF_REF_RULE);
		TAILQ_REMOVE(poola, empty_pool_pa, entries);
		kfree(empty_pool_pa, M_PFPOOLADDRPL);
	}
}

void
pf_rm_rule(struct pf_rulequeue *rulequeue, struct pf_rule *rule)
{
	if (rulequeue != NULL) {
		if (rule->states_cur <= 0) {
			/*
			 * XXX - we need to remove the table *before* detaching
			 * the rule to make sure the table code does not delete
			 * the anchor under our feet.
			 */
			pf_tbladdr_remove(&rule->src.addr);
			pf_tbladdr_remove(&rule->dst.addr);
			if (rule->overload_tbl)
				pfr_detach_table(rule->overload_tbl);
		}
		TAILQ_REMOVE(rulequeue, rule, entries);
		rule->entries.tqe_prev = NULL;
		rule->nr = -1;
	}

	if (rule->states_cur > 0 || rule->src_nodes > 0 ||
	    rule->entries.tqe_prev != NULL)
		return;
	pf_tag_unref(rule->tag);
	pf_tag_unref(rule->match_tag);
#ifdef ALTQ
	if (rule->pqid != rule->qid)
		pf_qid_unref(rule->pqid);
	pf_qid_unref(rule->qid);
#endif
	pf_rtlabel_remove(&rule->src.addr);
	pf_rtlabel_remove(&rule->dst.addr);
	pfi_dynaddr_remove(&rule->src.addr);
	pfi_dynaddr_remove(&rule->dst.addr);
	if (rulequeue == NULL) {
		pf_tbladdr_remove(&rule->src.addr);
		pf_tbladdr_remove(&rule->dst.addr);
		if (rule->overload_tbl)
			pfr_detach_table(rule->overload_tbl);
	}
	pfi_kif_unref(rule->kif, PFI_KIF_REF_RULE);
	pf_anchor_remove(rule);
	pf_empty_pool(&rule->rpool.list);
	kfree(rule, M_PFRULEPL);
}

u_int16_t
tagname2tag(struct pf_tags *head, char *tagname)
{
	struct pf_tagname	*tag, *p = NULL;
	u_int16_t		 new_tagid = 1;

	TAILQ_FOREACH(tag, head, entries)
		if (strcmp(tagname, tag->name) == 0) {
			tag->ref++;
			return (tag->tag);
		}

	/*
	 * to avoid fragmentation, we do a linear search from the beginning
	 * and take the first free slot we find. if there is none or the list
	 * is empty, append a new entry at the end.
	 */

	/* new entry */
	if (!TAILQ_EMPTY(head))
		for (p = TAILQ_FIRST(head); p != NULL &&
		    p->tag == new_tagid; p = TAILQ_NEXT(p, entries))
			new_tagid = p->tag + 1;

	if (new_tagid > TAGID_MAX)
		return (0);

	/* allocate and fill new struct pf_tagname */
	tag = kmalloc(sizeof(*tag), M_TEMP, M_WAITOK);
	strlcpy(tag->name, tagname, sizeof(tag->name));
	tag->tag = new_tagid;
	tag->ref++;

	if (p != NULL)	/* insert new entry before p */
		TAILQ_INSERT_BEFORE(p, tag, entries);
	else	/* either list empty or no free slot in between */
		TAILQ_INSERT_TAIL(head, tag, entries);

	return (tag->tag);
}

void
tag2tagname(struct pf_tags *head, u_int16_t tagid, char *p)
{
	struct pf_tagname	*tag;

	TAILQ_FOREACH(tag, head, entries)
		if (tag->tag == tagid) {
			strlcpy(p, tag->name, PF_TAG_NAME_SIZE);
			return;
		}
}

void
tag_unref(struct pf_tags *head, u_int16_t tag)
{
	struct pf_tagname	*p, *next;

	if (tag == 0)
		return;

	for (p = TAILQ_FIRST(head); p != NULL; p = next) {
		next = TAILQ_NEXT(p, entries);
		if (tag == p->tag) {
			if (--p->ref == 0) {
				TAILQ_REMOVE(head, p, entries);
				kfree(p, M_TEMP);
			}
			break;
		}
	}
}

u_int16_t
pf_tagname2tag(char *tagname)
{
	return (tagname2tag(&pf_tags, tagname));
}

void
pf_tag2tagname(u_int16_t tagid, char *p)
{
	tag2tagname(&pf_tags, tagid, p);
}

void
pf_tag_ref(u_int16_t tag)
{
	struct pf_tagname *t;

	TAILQ_FOREACH(t, &pf_tags, entries)
		if (t->tag == tag)
			break;
	if (t != NULL)
		t->ref++;
}

void
pf_tag_unref(u_int16_t tag)
{
	tag_unref(&pf_tags, tag);
}

int
pf_rtlabel_add(struct pf_addr_wrap *a)
{
	return (0);
}

void
pf_rtlabel_remove(struct pf_addr_wrap *a)
{
}

void
pf_rtlabel_copyout(struct pf_addr_wrap *a)
{
	if (a->type == PF_ADDR_RTLABEL && a->v.rtlabel)
		strlcpy(a->v.rtlabelname, "?", sizeof(a->v.rtlabelname));
}

#ifdef ALTQ
u_int32_t
pf_qname2qid(char *qname)
{
	return ((u_int32_t)tagname2tag(&pf_qids, qname));
}

void
pf_qid2qname(u_int32_t qid, char *p)
{
	tag2tagname(&pf_qids, (u_int16_t)qid, p);
}

void
pf_qid_unref(u_int32_t qid)
{
	tag_unref(&pf_qids, (u_int16_t)qid);
}

int
pf_begin_altq(u_int32_t *ticket)
{
	struct pf_altq	*altq;
	int		 error = 0;

	/* Purge the old altq list */
	while ((altq = TAILQ_FIRST(pf_altqs_inactive)) != NULL) {
		TAILQ_REMOVE(pf_altqs_inactive, altq, entries);
		if (altq->qname[0] == 0) {
			/* detach and destroy the discipline */
			error = altq_remove(altq);
		} else
			pf_qid_unref(altq->qid);
		kfree(altq, M_PFALTQPL);
	}
	if (error)
		return (error);
	*ticket = ++ticket_altqs_inactive;
	altqs_inactive_open = 1;
	return (0);
}

int
pf_rollback_altq(u_int32_t ticket)
{
	struct pf_altq	*altq;
	int		 error = 0;

	if (!altqs_inactive_open || ticket != ticket_altqs_inactive)
		return (0);
	/* Purge the old altq list */
	while ((altq = TAILQ_FIRST(pf_altqs_inactive)) != NULL) {
		TAILQ_REMOVE(pf_altqs_inactive, altq, entries);
		if (altq->qname[0] == 0) {
			/* detach and destroy the discipline */
			error = altq_remove(altq);
		} else
			pf_qid_unref(altq->qid);
		kfree(altq, M_PFALTQPL);
	}
	altqs_inactive_open = 0;
	return (error);
}

int
pf_commit_altq(u_int32_t ticket)
{
	struct pf_altqqueue	*old_altqs;
	struct pf_altq		*altq;
	int			 err, error = 0;

	if (!altqs_inactive_open || ticket != ticket_altqs_inactive)
		return (EBUSY);

	/* swap altqs, keep the old. */
	crit_enter();
	old_altqs = pf_altqs_active;
	pf_altqs_active = pf_altqs_inactive;
	pf_altqs_inactive = old_altqs;
	ticket_altqs_active = ticket_altqs_inactive;

	/* Attach new disciplines */
	TAILQ_FOREACH(altq, pf_altqs_active, entries) {
		if (altq->qname[0] == 0) {
			/* attach the discipline */
			error = altq_pfattach(altq);
			if (error) {
				crit_exit();
				return (error);
			}
		}
	}

	/* Purge the old altq list */
	while ((altq = TAILQ_FIRST(pf_altqs_inactive)) != NULL) {
		TAILQ_REMOVE(pf_altqs_inactive, altq, entries);
		if (altq->qname[0] == 0) {
			/* detach and destroy the discipline */
			if (pf_altq_running)
				error = pf_disable_altq(altq);
			err = altq_pfdetach(altq);
			if (err != 0 && error == 0)
				error = err;
			err = altq_remove(altq);
			if (err != 0 && error == 0)
				error = err;
		} else
			pf_qid_unref(altq->qid);
		kfree(altq, M_PFALTQPL);
	}
	crit_exit();

	altqs_inactive_open = 0;
	return (error);
}

int
pf_enable_altq(struct pf_altq *altq)
{
	struct ifnet		*ifp;
	struct tb_profile	 tb;
	int			 error = 0;

	if ((ifp = ifunit(altq->ifname)) == NULL)
		return (EINVAL);

	if (ifp->if_snd.altq_type != ALTQT_NONE)
		error = altq_enable(&ifp->if_snd);

	/* set tokenbucket regulator */
	if (error == 0 && ifp != NULL && ALTQ_IS_ENABLED(&ifp->if_snd)) {
		tb.rate = altq->ifbandwidth;
		tb.depth = altq->tbrsize;
		crit_enter();
		error = tbr_set(&ifp->if_snd, &tb);
		crit_exit();
	}

	return (error);
}

int
pf_disable_altq(struct pf_altq *altq)
{
	struct ifnet		*ifp;
	struct tb_profile	 tb;
	int			 error;

	if ((ifp = ifunit(altq->ifname)) == NULL)
		return (EINVAL);

	/*
	 * when the discipline is no longer referenced, it was overridden
	 * by a new one.  if so, just return.
	 */
	if (altq->altq_disc != ifp->if_snd.altq_disc)
		return (0);

	error = altq_disable(&ifp->if_snd);

	if (error == 0) {
		/* clear tokenbucket regulator */
		tb.rate = 0;
		crit_enter();
		error = tbr_set(&ifp->if_snd, &tb);
		crit_exit();
	}

	return (error);
}
#endif /* ALTQ */

int
pf_begin_rules(u_int32_t *ticket, int rs_num, const char *anchor)
{
	struct pf_ruleset	*rs;
	struct pf_rule		*rule;

	if (rs_num < 0 || rs_num >= PF_RULESET_MAX)
		return (EINVAL);
	rs = pf_find_or_create_ruleset(anchor);
	if (rs == NULL)
		return (EINVAL);
	while ((rule = TAILQ_FIRST(rs->rules[rs_num].inactive.ptr)) != NULL) {
		pf_rm_rule(rs->rules[rs_num].inactive.ptr, rule);
		rs->rules[rs_num].inactive.rcount--;
	}
	*ticket = ++rs->rules[rs_num].inactive.ticket;
	rs->rules[rs_num].inactive.open = 1;
	return (0);
}

int
pf_rollback_rules(u_int32_t ticket, int rs_num, char *anchor)
{
	struct pf_ruleset	*rs;
	struct pf_rule		*rule;

	if (rs_num < 0 || rs_num >= PF_RULESET_MAX)
		return (EINVAL);
	rs = pf_find_ruleset(anchor);
	if (rs == NULL || !rs->rules[rs_num].inactive.open ||
	    rs->rules[rs_num].inactive.ticket != ticket)
		return (0);
	while ((rule = TAILQ_FIRST(rs->rules[rs_num].inactive.ptr)) != NULL) {
		pf_rm_rule(rs->rules[rs_num].inactive.ptr, rule);
		rs->rules[rs_num].inactive.rcount--;
	}
	rs->rules[rs_num].inactive.open = 0;
	return (0);
}

#define PF_MD5_UPD(st, elm)						\
		MD5Update(ctx, (u_int8_t *) &(st)->elm, sizeof((st)->elm))

#define PF_MD5_UPD_STR(st, elm)						\
		MD5Update(ctx, (u_int8_t *) (st)->elm, strlen((st)->elm))

#define PF_MD5_UPD_HTONL(st, elm, stor) do {				\
		(stor) = htonl((st)->elm);				\
		MD5Update(ctx, (u_int8_t *) &(stor), sizeof(u_int32_t));\
} while (0)

#define PF_MD5_UPD_HTONS(st, elm, stor) do {				\
		(stor) = htons((st)->elm);				\
		MD5Update(ctx, (u_int8_t *) &(stor), sizeof(u_int16_t));\
} while (0)

void
pf_hash_rule_addr(MD5_CTX *ctx, struct pf_rule_addr *pfr)
{
	PF_MD5_UPD(pfr, addr.type);
	switch (pfr->addr.type) {
		case PF_ADDR_DYNIFTL:
			PF_MD5_UPD(pfr, addr.v.ifname);
			PF_MD5_UPD(pfr, addr.iflags);
			break;
		case PF_ADDR_TABLE:
			PF_MD5_UPD(pfr, addr.v.tblname);
			break;
		case PF_ADDR_ADDRMASK:
			/* XXX ignore af? */
			PF_MD5_UPD(pfr, addr.v.a.addr.addr32);
			PF_MD5_UPD(pfr, addr.v.a.mask.addr32);
			break;
		case PF_ADDR_RTLABEL:
			PF_MD5_UPD(pfr, addr.v.rtlabelname);
			break;
	}

	PF_MD5_UPD(pfr, port[0]);
	PF_MD5_UPD(pfr, port[1]);
	PF_MD5_UPD(pfr, neg);
	PF_MD5_UPD(pfr, port_op);
}

void
pf_hash_rule(MD5_CTX *ctx, struct pf_rule *rule)
{
	u_int16_t x;
	u_int32_t y;

	pf_hash_rule_addr(ctx, &rule->src);
	pf_hash_rule_addr(ctx, &rule->dst);
	PF_MD5_UPD_STR(rule, label);
	PF_MD5_UPD_STR(rule, ifname);
	PF_MD5_UPD_STR(rule, match_tagname);
	PF_MD5_UPD_HTONS(rule, match_tag, x); /* dup? */
	PF_MD5_UPD_HTONL(rule, os_fingerprint, y);
	PF_MD5_UPD_HTONL(rule, prob, y);
	PF_MD5_UPD_HTONL(rule, uid.uid[0], y);
	PF_MD5_UPD_HTONL(rule, uid.uid[1], y);
	PF_MD5_UPD(rule, uid.op);
	PF_MD5_UPD_HTONL(rule, gid.gid[0], y);
	PF_MD5_UPD_HTONL(rule, gid.gid[1], y);
	PF_MD5_UPD(rule, gid.op);
	PF_MD5_UPD_HTONL(rule, rule_flag, y);
	PF_MD5_UPD(rule, action);
	PF_MD5_UPD(rule, direction);
	PF_MD5_UPD(rule, af);
	PF_MD5_UPD(rule, quick);
	PF_MD5_UPD(rule, ifnot);
	PF_MD5_UPD(rule, match_tag_not);
	PF_MD5_UPD(rule, natpass);
	PF_MD5_UPD(rule, keep_state);
	PF_MD5_UPD(rule, proto);
	PF_MD5_UPD(rule, type);
	PF_MD5_UPD(rule, code);
	PF_MD5_UPD(rule, flags);
	PF_MD5_UPD(rule, flagset);
	PF_MD5_UPD(rule, allow_opts);
	PF_MD5_UPD(rule, rt);
	PF_MD5_UPD(rule, tos);
}

int
pf_commit_rules(u_int32_t ticket, int rs_num, char *anchor)
{
	struct pf_ruleset	*rs;
	struct pf_rule		*rule, **old_array;
	struct pf_rulequeue	*old_rules;
	int			 error;
	u_int32_t		 old_rcount;

	if (rs_num < 0 || rs_num >= PF_RULESET_MAX)
		return (EINVAL);
	rs = pf_find_ruleset(anchor);
	if (rs == NULL || !rs->rules[rs_num].inactive.open ||
	    ticket != rs->rules[rs_num].inactive.ticket)
		return (EBUSY);

	/* Calculate checksum for the main ruleset */
	if (rs == &pf_main_ruleset) {
		error = pf_setup_pfsync_matching(rs);
		if (error != 0)
			return (error);
	}

	/* Swap rules, keep the old. */
	crit_enter();
	old_rules = rs->rules[rs_num].active.ptr;
	old_rcount = rs->rules[rs_num].active.rcount;
	old_array = rs->rules[rs_num].active.ptr_array;

	rs->rules[rs_num].active.ptr =
	    rs->rules[rs_num].inactive.ptr;
	rs->rules[rs_num].active.ptr_array =
	    rs->rules[rs_num].inactive.ptr_array;
	rs->rules[rs_num].active.rcount =
	    rs->rules[rs_num].inactive.rcount;
	rs->rules[rs_num].inactive.ptr = old_rules;
	rs->rules[rs_num].inactive.ptr_array = old_array;
	rs->rules[rs_num].inactive.rcount = old_rcount;

	rs->rules[rs_num].active.ticket =
	    rs->rules[rs_num].inactive.ticket;
	pf_calc_skip_steps(rs->rules[rs_num].active.ptr);


	/* Purge the old rule list. */
	while ((rule = TAILQ_FIRST(old_rules)) != NULL)
		pf_rm_rule(old_rules, rule);
	if (rs->rules[rs_num].inactive.ptr_array)
		kfree(rs->rules[rs_num].inactive.ptr_array, M_TEMP);
	rs->rules[rs_num].inactive.ptr_array = NULL;
	rs->rules[rs_num].inactive.rcount = 0;
	rs->rules[rs_num].inactive.open = 0;
	pf_remove_if_empty_ruleset(rs);
	crit_exit();
	return (0);
}

int
pf_setup_pfsync_matching(struct pf_ruleset *rs)
{
	MD5_CTX			 ctx;
	struct pf_rule		*rule;
	int			 rs_cnt;
	u_int8_t		 digest[PF_MD5_DIGEST_LENGTH];

	MD5Init(&ctx);
	for (rs_cnt = 0; rs_cnt < PF_RULESET_MAX; rs_cnt++) {
		/* XXX PF_RULESET_SCRUB as well? */
		if (rs_cnt == PF_RULESET_SCRUB)
			continue;

		if (rs->rules[rs_cnt].inactive.ptr_array)
			kfree(rs->rules[rs_cnt].inactive.ptr_array, M_TEMP);
		rs->rules[rs_cnt].inactive.ptr_array = NULL;

		if (rs->rules[rs_cnt].inactive.rcount) {
			rs->rules[rs_cnt].inactive.ptr_array =
			    kmalloc(sizeof(caddr_t) *
				    rs->rules[rs_cnt].inactive.rcount,
				    M_TEMP, M_WAITOK);
		}

		TAILQ_FOREACH(rule, rs->rules[rs_cnt].inactive.ptr,
		    entries) {
			pf_hash_rule(&ctx, rule);
			(rs->rules[rs_cnt].inactive.ptr_array)[rule->nr] = rule;
		}
	}

	MD5Final(digest, &ctx);
	memcpy(pf_status.pf_chksum, digest, sizeof(pf_status.pf_chksum));
	return (0);
}

int
pf_addr_setup(struct pf_ruleset *ruleset, struct pf_addr_wrap *addr,
    sa_family_t af)
{
	if (pfi_dynaddr_setup(addr, af) ||
	    pf_tbladdr_setup(ruleset, addr))
		return (EINVAL);

	return (0);
}

void
pf_addr_copyout(struct pf_addr_wrap *addr)
{
	pfi_dynaddr_copyout(addr);
	pf_tbladdr_copyout(addr);
	pf_rtlabel_copyout(addr);
}

int
pfioctl(struct dev_ioctl_args *ap)
{
	u_long cmd = ap->a_cmd;
	caddr_t addr = ap->a_data;
	struct pf_pooladdr	*pa = NULL;
	struct pf_pool		*pool = NULL;
	int			 error = 0;

	lwkt_gettoken(&pf_token);

	/* XXX keep in sync with switch() below */
	if (securelevel > 1)
		switch (cmd) {
		case DIOCGETRULES:
		case DIOCGETRULE:
		case DIOCGETADDRS:
		case DIOCGETADDR:
		case DIOCGETSTATE:
		case DIOCSETSTATUSIF:
		case DIOCGETSTATUS:
		case DIOCCLRSTATUS:
		case DIOCNATLOOK:
		case DIOCSETDEBUG:
		case DIOCGETSTATES:
		case DIOCGETTIMEOUT:
		case DIOCCLRRULECTRS:
		case DIOCGETLIMIT:
		case DIOCGETALTQS:
		case DIOCGETALTQ:
		case DIOCGETQSTATS:
		case DIOCGETRULESETS:
		case DIOCGETRULESET:
		case DIOCRGETTABLES:
		case DIOCRGETTSTATS:
		case DIOCRCLRTSTATS:
		case DIOCRCLRADDRS:
		case DIOCRADDADDRS:
		case DIOCRDELADDRS:
		case DIOCRSETADDRS:
		case DIOCRGETADDRS:
		case DIOCRGETASTATS:
		case DIOCRCLRASTATS:
		case DIOCRTSTADDRS:
		case DIOCOSFPGET:
		case DIOCGETSRCNODES:
		case DIOCCLRSRCNODES:
		case DIOCIGETIFACES:
		case DIOCSETIFFLAG:
		case DIOCCLRIFFLAG:
		case DIOCGIFSPEED:
			break;
		case DIOCRCLRTABLES:
		case DIOCRADDTABLES:
		case DIOCRDELTABLES:
		case DIOCRSETTFLAGS:
			if (((struct pfioc_table *)addr)->pfrio_flags &
			    PFR_FLAG_DUMMY)
				break; /* dummy operation ok */
			lwkt_reltoken(&pf_token);
			return (EPERM);
		default:
			lwkt_reltoken(&pf_token);
			return (EPERM);
		}

	if (!(ap->a_fflag & FWRITE))
		switch (cmd) {
		case DIOCGETRULES:
		case DIOCGETADDRS:
		case DIOCGETADDR:
		case DIOCGETSTATE:
		case DIOCGETSTATUS:
		case DIOCGETSTATES:
		case DIOCGETTIMEOUT:
		case DIOCGETLIMIT:
		case DIOCGETALTQS:
		case DIOCGETALTQ:
		case DIOCGETQSTATS:
		case DIOCGETRULESETS:
		case DIOCGETRULESET:
		case DIOCNATLOOK:
		case DIOCRGETTABLES:
		case DIOCRGETTSTATS:
		case DIOCRGETADDRS:
		case DIOCRGETASTATS:
		case DIOCRTSTADDRS:
		case DIOCOSFPGET:
		case DIOCGETSRCNODES:
		case DIOCIGETIFACES:
		case DIOCGIFSPEED:
			break;
		case DIOCRCLRTABLES:
		case DIOCRADDTABLES:
		case DIOCRDELTABLES:
		case DIOCRCLRTSTATS:
		case DIOCRCLRADDRS:
		case DIOCRADDADDRS:
		case DIOCRDELADDRS:
		case DIOCRSETADDRS:
		case DIOCRSETTFLAGS:
			if (((struct pfioc_table *)addr)->pfrio_flags &
			    PFR_FLAG_DUMMY)
				break; /* dummy operation ok */
			lwkt_reltoken(&pf_token);
			return (EACCES);
		case DIOCGETRULE:
			if (((struct pfioc_rule *)addr)->action ==
			    PF_GET_CLR_CNTR) {
				lwkt_reltoken(&pf_token);
				return (EACCES);
			}
			break;
		default:
			lwkt_reltoken(&pf_token);
			return (EACCES);
		}

	switch (cmd) {

	case DIOCSTART:
		if (pf_status.running)
			error = EEXIST;
		else {
			error = hook_pf();
			if (error) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: pfil registration fail\n"));
				break;
			}
			pf_status.running = 1;
			pf_status.since = time_second;
			if (pf_status.stateid == 0) {
				pf_status.stateid = time_second;
				pf_status.stateid = pf_status.stateid << 32;
			}
			DPFPRINTF(PF_DEBUG_MISC, ("pf: started\n"));
		}
		break;

	case DIOCSTOP:
		if (!pf_status.running)
			error = ENOENT;
		else {
			pf_status.running = 0;
			error = dehook_pf();
			if (error) {
				pf_status.running = 1;
				DPFPRINTF(PF_DEBUG_MISC,
					("pf: pfil unregistration failed\n"));
			}
			pf_status.since = time_second;
			DPFPRINTF(PF_DEBUG_MISC, ("pf: stopped\n"));
		}
		break;

	case DIOCADDRULE: {
		struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*rule, *tail;
		struct pf_pooladdr	*pa;
		int			 rs_num;

		pr->anchor[sizeof(pr->anchor) - 1] = 0;
		ruleset = pf_find_ruleset(pr->anchor);
		if (ruleset == NULL) {
			error = EINVAL;
			break;
		}
		rs_num = pf_get_ruleset_number(pr->rule.action);
		if (rs_num >= PF_RULESET_MAX) {
			error = EINVAL;
			break;
		}
		if (pr->rule.return_icmp >> 8 > ICMP_MAXTYPE) {
			error = EINVAL;
			break;
		}
		if (pr->ticket != ruleset->rules[rs_num].inactive.ticket) {
			error = EBUSY;
			break;
		}
		if (pr->pool_ticket != ticket_pabuf) {
			error = EBUSY;
			break;
		}
		rule = kmalloc(sizeof(struct pf_rule), M_PFRULEPL, M_WAITOK);
		bcopy(&pr->rule, rule, sizeof(struct pf_rule));
		rule->cuid = ap->a_cred->cr_ruid;
		rule->cpid = 0;
		rule->anchor = NULL;
		rule->kif = NULL;
		TAILQ_INIT(&rule->rpool.list);
		/* initialize refcounting */
		rule->states_cur = 0;
		rule->src_nodes = 0;
		rule->entries.tqe_prev = NULL;
#ifndef INET
		if (rule->af == AF_INET) {
			kfree(rule, M_PFRULEPL);
			error = EAFNOSUPPORT;
			break;
		}
#endif /* INET */
#ifndef INET6
		if (rule->af == AF_INET6) {
			kfree(rule, M_PFRULEPL);
			error = EAFNOSUPPORT;
			break;
		}
#endif /* INET6 */
		tail = TAILQ_LAST(ruleset->rules[rs_num].inactive.ptr,
		    pf_rulequeue);
		if (tail)
			rule->nr = tail->nr + 1;
		else
			rule->nr = 0;
		if (rule->ifname[0]) {
			rule->kif = pfi_kif_get(rule->ifname);
			if (rule->kif == NULL) {
				kfree(rule, M_PFRULEPL);
				error = EINVAL;
				break;
			}
			pfi_kif_ref(rule->kif, PFI_KIF_REF_RULE);
		}

		if (rule->rtableid > 0 && rule->rtableid > rt_numfibs)
			error = EBUSY;

#ifdef ALTQ
		/* set queue IDs */
		if (rule->qname[0] != 0) {
			if ((rule->qid = pf_qname2qid(rule->qname)) == 0)
				error = EBUSY;
			else if (rule->pqname[0] != 0) {
				if ((rule->pqid =
				    pf_qname2qid(rule->pqname)) == 0)
					error = EBUSY;
			} else
				rule->pqid = rule->qid;
		}
#endif
		if (rule->tagname[0])
			if ((rule->tag = pf_tagname2tag(rule->tagname)) == 0)
				error = EBUSY;
		if (rule->match_tagname[0])
			if ((rule->match_tag =
			    pf_tagname2tag(rule->match_tagname)) == 0)
				error = EBUSY;
		if (rule->rt && !rule->direction)
			error = EINVAL;
#if NPFLOG > 0
		if (!rule->log)
			rule->logif = 0;
		if (rule->logif >= PFLOGIFS_MAX)
			error = EINVAL;
#endif
		if (pf_rtlabel_add(&rule->src.addr) ||
		    pf_rtlabel_add(&rule->dst.addr))
			error = EBUSY;
		if (pf_addr_setup(ruleset, &rule->src.addr, rule->af))
			error = EINVAL;
		if (pf_addr_setup(ruleset, &rule->dst.addr, rule->af))
			error = EINVAL;
		if (pf_anchor_setup(rule, ruleset, pr->anchor_call))
			error = EINVAL;
		TAILQ_FOREACH(pa, &pf_pabuf, entries)
			if (pf_tbladdr_setup(ruleset, &pa->addr))
				error = EINVAL;

		if (rule->overload_tblname[0]) {
			if ((rule->overload_tbl = pfr_attach_table(ruleset,
			    rule->overload_tblname)) == NULL)
				error = EINVAL;
			else
				rule->overload_tbl->pfrkt_flags |=
				    PFR_TFLAG_ACTIVE;
		}

		pf_mv_pool(&pf_pabuf, &rule->rpool.list);
		if (((((rule->action == PF_NAT) || (rule->action == PF_RDR) ||
		    (rule->action == PF_BINAT)) && rule->anchor == NULL) ||
		    (rule->rt > PF_FASTROUTE)) &&
		    (TAILQ_FIRST(&rule->rpool.list) == NULL))
			error = EINVAL;

		if (error) {
			pf_rm_rule(NULL, rule);
			break;
		}
		rule->rpool.cur = TAILQ_FIRST(&rule->rpool.list);
		rule->evaluations = rule->packets[0] = rule->packets[1] =
		    rule->bytes[0] = rule->bytes[1] = 0;
		TAILQ_INSERT_TAIL(ruleset->rules[rs_num].inactive.ptr,
		    rule, entries);
		ruleset->rules[rs_num].inactive.rcount++;
		break;
	}

	case DIOCGETRULES: {
		struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*tail;
		int			 rs_num;

		pr->anchor[sizeof(pr->anchor) - 1] = 0;
		ruleset = pf_find_ruleset(pr->anchor);
		if (ruleset == NULL) {
			error = EINVAL;
			break;
		}
		rs_num = pf_get_ruleset_number(pr->rule.action);
		if (rs_num >= PF_RULESET_MAX) {
			error = EINVAL;
			break;
		}
		tail = TAILQ_LAST(ruleset->rules[rs_num].active.ptr,
		    pf_rulequeue);
		if (tail)
			pr->nr = tail->nr + 1;
		else
			pr->nr = 0;
		pr->ticket = ruleset->rules[rs_num].active.ticket;
		break;
	}

	case DIOCGETRULE: {
		struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*rule;
		int			 rs_num, i;

		pr->anchor[sizeof(pr->anchor) - 1] = 0;
		ruleset = pf_find_ruleset(pr->anchor);
		if (ruleset == NULL) {
			error = EINVAL;
			break;
		}
		rs_num = pf_get_ruleset_number(pr->rule.action);
		if (rs_num >= PF_RULESET_MAX) {
			error = EINVAL;
			break;
		}
		if (pr->ticket != ruleset->rules[rs_num].active.ticket) {
			error = EBUSY;
			break;
		}
		rule = TAILQ_FIRST(ruleset->rules[rs_num].active.ptr);
		while ((rule != NULL) && (rule->nr != pr->nr))
			rule = TAILQ_NEXT(rule, entries);
		if (rule == NULL) {
			error = EBUSY;
			break;
		}
		bcopy(rule, &pr->rule, sizeof(struct pf_rule));
		if (pf_anchor_copyout(ruleset, rule, pr)) {
			error = EBUSY;
			break;
		}
		pf_addr_copyout(&pr->rule.src.addr);
		pf_addr_copyout(&pr->rule.dst.addr);
		for (i = 0; i < PF_SKIP_COUNT; ++i)
			if (rule->skip[i].ptr == NULL)
				pr->rule.skip[i].nr = (uint32_t)(-1);
			else
				pr->rule.skip[i].nr =
				    rule->skip[i].ptr->nr;

		if (pr->action == PF_GET_CLR_CNTR) {
			rule->evaluations = 0;
			rule->packets[0] = rule->packets[1] = 0;
			rule->bytes[0] = rule->bytes[1] = 0;
			rule->states_tot = 0;
		}
		break;
	}

	case DIOCCHANGERULE: {
		struct pfioc_rule	*pcr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*oldrule = NULL, *newrule = NULL;
		u_int32_t		 nr = 0;
		int			 rs_num;

		if (!(pcr->action == PF_CHANGE_REMOVE ||
		    pcr->action == PF_CHANGE_GET_TICKET) &&
		    pcr->pool_ticket != ticket_pabuf) {
			error = EBUSY;
			break;
		}

		if (pcr->action < PF_CHANGE_ADD_HEAD ||
		    pcr->action > PF_CHANGE_GET_TICKET) {
			error = EINVAL;
			break;
		}
		ruleset = pf_find_ruleset(pcr->anchor);
		if (ruleset == NULL) {
			error = EINVAL;
			break;
		}
		rs_num = pf_get_ruleset_number(pcr->rule.action);
		if (rs_num >= PF_RULESET_MAX) {
			error = EINVAL;
			break;
		}

		if (pcr->action == PF_CHANGE_GET_TICKET) {
			pcr->ticket = ++ruleset->rules[rs_num].active.ticket;
			break;
		} else {
			if (pcr->ticket !=
			    ruleset->rules[rs_num].active.ticket) {
				error = EINVAL;
				break;
			}
			if (pcr->rule.return_icmp >> 8 > ICMP_MAXTYPE) {
				error = EINVAL;
				break;
			}
		}

		if (pcr->action != PF_CHANGE_REMOVE) {
			newrule = kmalloc(sizeof(struct pf_rule), M_PFRULEPL, M_WAITOK|M_NULLOK);
			if (newrule == NULL) {
				error = ENOMEM;
				break;
			}
			bcopy(&pcr->rule, newrule, sizeof(struct pf_rule));
			newrule->cuid = ap->a_cred->cr_ruid;
			newrule->cpid = 0;
			TAILQ_INIT(&newrule->rpool.list);
			/* initialize refcounting */
			newrule->states_cur = 0;
			newrule->entries.tqe_prev = NULL;
#ifndef INET
			if (newrule->af == AF_INET) {
				kfree(newrule, M_PFRULEPL);
				error = EAFNOSUPPORT;
				break;
			}
#endif /* INET */
#ifndef INET6
			if (newrule->af == AF_INET6) {
				kfree(newrule, M_PFRULEPL);
				error = EAFNOSUPPORT;
				break;
			}
#endif /* INET6 */
			if (newrule->ifname[0]) {
				newrule->kif = pfi_kif_get(newrule->ifname);
				if (newrule->kif == NULL) {
					kfree(newrule, M_PFRULEPL);
					error = EINVAL;
					break;
				}
				pfi_kif_ref(newrule->kif, PFI_KIF_REF_RULE);
			} else
				newrule->kif = NULL;

			if (newrule->rtableid > 0 &&
			    newrule->rtableid > rt_numfibs)
				error = EBUSY;

#ifdef ALTQ
			/* set queue IDs */
			if (newrule->qname[0] != 0) {
				if ((newrule->qid =
				    pf_qname2qid(newrule->qname)) == 0)
					error = EBUSY;
				else if (newrule->pqname[0] != 0) {
					if ((newrule->pqid =
					    pf_qname2qid(newrule->pqname)) == 0)
						error = EBUSY;
				} else
					newrule->pqid = newrule->qid;
			}
#endif /* ALTQ */
			if (newrule->tagname[0])
				if ((newrule->tag =
				    pf_tagname2tag(newrule->tagname)) == 0)
					error = EBUSY;
			if (newrule->match_tagname[0])
				if ((newrule->match_tag = pf_tagname2tag(
				    newrule->match_tagname)) == 0)
					error = EBUSY;
			if (newrule->rt && !newrule->direction)
				error = EINVAL;
#if NPFLOG > 0
			if (!newrule->log)
				newrule->logif = 0;
			if (newrule->logif >= PFLOGIFS_MAX)
				error = EINVAL;
#endif
			if (pf_rtlabel_add(&newrule->src.addr) ||
			    pf_rtlabel_add(&newrule->dst.addr))
				error = EBUSY;
			if (pf_addr_setup(ruleset, &newrule->src.addr, newrule->af))
				error = EINVAL;
			if (pf_addr_setup(ruleset, &newrule->dst.addr, newrule->af))
				error = EINVAL;
			if (pf_anchor_setup(newrule, ruleset, pcr->anchor_call))
				error = EINVAL;
			TAILQ_FOREACH(pa, &pf_pabuf, entries)
				if (pf_tbladdr_setup(ruleset, &pa->addr))
					error = EINVAL;

			if (newrule->overload_tblname[0]) {
				if ((newrule->overload_tbl = pfr_attach_table(
				    ruleset, newrule->overload_tblname)) ==
				    NULL)
					error = EINVAL;
				else
					newrule->overload_tbl->pfrkt_flags |=
					    PFR_TFLAG_ACTIVE;
			}

			pf_mv_pool(&pf_pabuf, &newrule->rpool.list);
			if (((((newrule->action == PF_NAT) ||
			    (newrule->action == PF_RDR) ||
			    (newrule->action == PF_BINAT) ||
			    (newrule->rt > PF_FASTROUTE)) &&
			    !newrule->anchor)) &&
			    (TAILQ_FIRST(&newrule->rpool.list) == NULL))
				error = EINVAL;

			if (error) {
				pf_rm_rule(NULL, newrule);
				break;
			}
			newrule->rpool.cur = TAILQ_FIRST(&newrule->rpool.list);
			newrule->evaluations = 0;
			newrule->packets[0] = newrule->packets[1] = 0;
			newrule->bytes[0] = newrule->bytes[1] = 0;
		}
		pf_empty_pool(&pf_pabuf);

		if (pcr->action == PF_CHANGE_ADD_HEAD)
			oldrule = TAILQ_FIRST(
			    ruleset->rules[rs_num].active.ptr);
		else if (pcr->action == PF_CHANGE_ADD_TAIL)
			oldrule = TAILQ_LAST(
			    ruleset->rules[rs_num].active.ptr, pf_rulequeue);
		else {
			oldrule = TAILQ_FIRST(
			    ruleset->rules[rs_num].active.ptr);
			while ((oldrule != NULL) && (oldrule->nr != pcr->nr))
				oldrule = TAILQ_NEXT(oldrule, entries);
			if (oldrule == NULL) {
				if (newrule != NULL)
					pf_rm_rule(NULL, newrule);
				error = EINVAL;
				break;
			}
		}

		if (pcr->action == PF_CHANGE_REMOVE) {
			pf_rm_rule(ruleset->rules[rs_num].active.ptr, oldrule);
			ruleset->rules[rs_num].active.rcount--;
		} else {
			if (oldrule == NULL)
				TAILQ_INSERT_TAIL(
				    ruleset->rules[rs_num].active.ptr,
				    newrule, entries);
			else if (pcr->action == PF_CHANGE_ADD_HEAD ||
			    pcr->action == PF_CHANGE_ADD_BEFORE)
				TAILQ_INSERT_BEFORE(oldrule, newrule, entries);
			else
				TAILQ_INSERT_AFTER(
				    ruleset->rules[rs_num].active.ptr,
				    oldrule, newrule, entries);
			ruleset->rules[rs_num].active.rcount++;
		}

		nr = 0;
		TAILQ_FOREACH(oldrule,
		    ruleset->rules[rs_num].active.ptr, entries)
			oldrule->nr = nr++;

		ruleset->rules[rs_num].active.ticket++;

		pf_calc_skip_steps(ruleset->rules[rs_num].active.ptr);
		pf_remove_if_empty_ruleset(ruleset);

		break;
	}

	case DIOCCLRSTATES: {
		struct pf_state		*s, *nexts;
		struct pfioc_state_kill *psk = (struct pfioc_state_kill *)addr;
		u_int			 killed = 0;

		for (s = RB_MIN(pf_state_tree_id, &tree_id); s; s = nexts) {
			nexts = RB_NEXT(pf_state_tree_id, &tree_id, s);

			if (!psk->psk_ifname[0] || !strcmp(psk->psk_ifname,
			    s->kif->pfik_name)) {
				/* don't send out individual delete messages */
				s->sync_flags = PFSTATE_NOSYNC;
				pf_unlink_state(s);
				killed++;
			}
		}
		psk->psk_killed = killed;
		pfsync_clear_states(pf_status.hostid, psk->psk_ifname);
		break;
	}

	case DIOCKILLSTATES: {
		struct pf_state		*s, *nexts;
		struct pf_state_key	*sk;
		struct pf_addr		*srcaddr, *dstaddr;
		u_int16_t		 srcport, dstport;
		struct pfioc_state_kill	*psk = (struct pfioc_state_kill *)addr;
		u_int			 killed = 0;

		if (psk->psk_pfcmp.id) {
			if (psk->psk_pfcmp.creatorid == 0)
				psk->psk_pfcmp.creatorid = pf_status.hostid;
			if ((s = pf_find_state_byid(&psk->psk_pfcmp))) {
				/* send immediate delete of state */
				pfsync_delete_state(s);
				s->sync_flags |= PFSTATE_NOSYNC;
				pf_unlink_state(s);
				psk->psk_killed = 1;
			}
			break;
		}

		for (s = RB_MIN(pf_state_tree_id, &tree_id); s;
		    s = nexts) {
			nexts = RB_NEXT(pf_state_tree_id, &tree_id, s);
			sk = s->key[PF_SK_WIRE];

			if (s->direction == PF_OUT) {
				srcaddr = &sk->addr[1];
				dstaddr = &sk->addr[0];
				srcport = sk->port[0];
				dstport = sk->port[0];
			} else {
				srcaddr = &sk->addr[0];
				dstaddr = &sk->addr[1];
				srcport = sk->port[0];
				dstport = sk->port[0];
			}
			if ((!psk->psk_af || sk->af == psk->psk_af)
			    && (!psk->psk_proto || psk->psk_proto ==
			    sk->proto) &&
			    PF_MATCHA(psk->psk_src.neg,
			    &psk->psk_src.addr.v.a.addr,
			    &psk->psk_src.addr.v.a.mask,
			    srcaddr, sk->af) &&
			    PF_MATCHA(psk->psk_dst.neg,
			    &psk->psk_dst.addr.v.a.addr,
			    &psk->psk_dst.addr.v.a.mask,
			    dstaddr, sk->af) &&
			    (psk->psk_src.port_op == 0 ||
			    pf_match_port(psk->psk_src.port_op,
			    psk->psk_src.port[0], psk->psk_src.port[1],
			    srcport)) &&
			    (psk->psk_dst.port_op == 0 ||
			    pf_match_port(psk->psk_dst.port_op,
			    psk->psk_dst.port[0], psk->psk_dst.port[1],
			    dstport)) &&
			    (!psk->psk_label[0] || (s->rule.ptr->label[0] &&
			    !strcmp(psk->psk_label, s->rule.ptr->label))) &&
			    (!psk->psk_ifname[0] || !strcmp(psk->psk_ifname,
			    s->kif->pfik_name))) {
				/* send immediate delete of state */
				pfsync_delete_state(s);
				s->sync_flags |= PFSTATE_NOSYNC;
				pf_unlink_state(s);
				killed++;
			}
		}
		psk->psk_killed = killed;
		break;
	}

	case DIOCADDSTATE: {
		struct pfioc_state	*ps = (struct pfioc_state *)addr;
		struct pfsync_state	*sp = &ps->state;

		if (sp->timeout >= PFTM_MAX &&
		    sp->timeout != PFTM_UNTIL_PACKET) {
			error = EINVAL;
			break;
		}
		error = pfsync_state_import(sp, PFSYNC_SI_IOCTL);
		break;
	}

	case DIOCGETSTATE: {
		struct pfioc_state	*ps = (struct pfioc_state *)addr;
		struct pf_state		*s;
		struct pf_state_cmp	 id_key;

		bcopy(ps->state.id, &id_key.id, sizeof(id_key.id));
		id_key.creatorid = ps->state.creatorid;

		s = pf_find_state_byid(&id_key);
		if (s == NULL) {
			error = ENOENT;
			break;
		}
		pfsync_state_export(&ps->state, s);
		break;
	}

	case DIOCGETSTATES: {
		struct pfioc_states	*ps = (struct pfioc_states *)addr;
		struct pf_state		*state;
		struct pfsync_state	*p, *pstore;
		u_int32_t		 nr = 0;

		if (ps->ps_len == 0) {
			nr = pf_status.states;
			ps->ps_len = sizeof(struct pfsync_state) * nr;
			break;
		}

		pstore = kmalloc(sizeof(*pstore), M_TEMP, M_WAITOK);

		p = ps->ps_states;

		state = TAILQ_FIRST(&state_list);
		while (state) {
			if (state->timeout != PFTM_UNLINKED) {
				if ((nr+1) * sizeof(*p) > (unsigned)ps->ps_len)
					break;
				pfsync_state_export(pstore, state);
				error = copyout(pstore, p, sizeof(*p));
				if (error) {
					kfree(pstore, M_TEMP);
					goto fail;
				}
				p++;
				nr++;
			}
			state = TAILQ_NEXT(state, entry_list);
		}

		ps->ps_len = sizeof(struct pfsync_state) * nr;

		kfree(pstore, M_TEMP);
		break;
	}

	case DIOCGETSTATUS: {
		struct pf_status *s = (struct pf_status *)addr;
		bcopy(&pf_status, s, sizeof(struct pf_status));
		pfi_update_status(s->ifname, s);
		break;
	}

	case DIOCSETSTATUSIF: {
		struct pfioc_if	*pi = (struct pfioc_if *)addr;

		if (pi->ifname[0] == 0) {
			bzero(pf_status.ifname, IFNAMSIZ);
			break;
		}
		strlcpy(pf_status.ifname, pi->ifname, IFNAMSIZ);
		break;
	}

	case DIOCCLRSTATUS: {
		bzero(pf_status.counters, sizeof(pf_status.counters));
		bzero(pf_status.fcounters, sizeof(pf_status.fcounters));
		bzero(pf_status.scounters, sizeof(pf_status.scounters));
		pf_status.since = time_second;
		if (*pf_status.ifname)
			pfi_update_status(pf_status.ifname, NULL);
		break;
	}

	case DIOCNATLOOK: {
		struct pfioc_natlook	*pnl = (struct pfioc_natlook *)addr;
		struct pf_state_key	*sk;
		struct pf_state		*state;
		struct pf_state_key_cmp	 key;
		int			 m = 0, direction = pnl->direction;
		int			 sidx, didx;

		/* NATLOOK src and dst are reversed, so reverse sidx/didx */
		sidx = (direction == PF_IN) ? 1 : 0;
		didx = (direction == PF_IN) ? 0 : 1;

		if (!pnl->proto ||
		    PF_AZERO(&pnl->saddr, pnl->af) ||
		    PF_AZERO(&pnl->daddr, pnl->af) ||
		    ((pnl->proto == IPPROTO_TCP ||
		    pnl->proto == IPPROTO_UDP) &&
		    (!pnl->dport || !pnl->sport)))
			error = EINVAL;
		else {
			key.af = pnl->af;
			key.proto = pnl->proto;
			PF_ACPY(&key.addr[sidx], &pnl->saddr, pnl->af);
			key.port[sidx] = pnl->sport;
			PF_ACPY(&key.addr[didx], &pnl->daddr, pnl->af);
			key.port[didx] = pnl->dport;

			state = pf_find_state_all(&key, direction, &m);

			if (m > 1)
				error = E2BIG;	/* more than one state */
			else if (state != NULL) {
				sk = state->key[sidx];
				PF_ACPY(&pnl->rsaddr, &sk->addr[sidx], sk->af);
				pnl->rsport = sk->port[sidx];
				PF_ACPY(&pnl->rdaddr, &sk->addr[didx], sk->af);
				pnl->rdport = sk->port[didx];
			} else
				error = ENOENT;
		}
		break;
	}

	case DIOCSETTIMEOUT: {
		struct pfioc_tm	*pt = (struct pfioc_tm *)addr;
		int		 old;

		if (pt->timeout < 0 || pt->timeout >= PFTM_MAX ||
		    pt->seconds < 0) {
			error = EINVAL;
			goto fail;
		}
		old = pf_default_rule.timeout[pt->timeout];
		if (pt->timeout == PFTM_INTERVAL && pt->seconds == 0)
			pt->seconds = 1;
		pf_default_rule.timeout[pt->timeout] = pt->seconds;
		if (pt->timeout == PFTM_INTERVAL && pt->seconds < old)
			wakeup(pf_purge_thread);
		pt->seconds = old;
		break;
	}

	case DIOCGETTIMEOUT: {
		struct pfioc_tm	*pt = (struct pfioc_tm *)addr;

		if (pt->timeout < 0 || pt->timeout >= PFTM_MAX) {
			error = EINVAL;
			goto fail;
		}
		pt->seconds = pf_default_rule.timeout[pt->timeout];
		break;
	}

	case DIOCGETLIMIT: {
		struct pfioc_limit	*pl = (struct pfioc_limit *)addr;

		if (pl->index < 0 || pl->index >= PF_LIMIT_MAX) {
			error = EINVAL;
			goto fail;
		}
		pl->limit = pf_pool_limits[pl->index].limit;
		break;
	}

	case DIOCSETLIMIT: {
		struct pfioc_limit	*pl = (struct pfioc_limit *)addr;
		int			 old_limit;

		if (pl->index < 0 || pl->index >= PF_LIMIT_MAX ||
		    pf_pool_limits[pl->index].pp == NULL) {
			error = EINVAL;
			goto fail;
		}

		/* XXX Get an API to set limits on the zone/pool */
		old_limit = pf_pool_limits[pl->index].limit;
		pf_pool_limits[pl->index].limit = pl->limit;
		pl->limit = old_limit;
		break;
	}

	case DIOCSETDEBUG: {
		u_int32_t	*level = (u_int32_t *)addr;

		pf_status.debug = *level;
		break;
	}

	case DIOCCLRRULECTRS: {
		/* obsoleted by DIOCGETRULE with action=PF_GET_CLR_CNTR */
		struct pf_ruleset	*ruleset = &pf_main_ruleset;
		struct pf_rule		*rule;

		TAILQ_FOREACH(rule,
		    ruleset->rules[PF_RULESET_FILTER].active.ptr, entries) {
			rule->evaluations = 0;
			rule->packets[0] = rule->packets[1] = 0;
			rule->bytes[0] = rule->bytes[1] = 0;
		}
		break;
	}

	case DIOCGIFSPEED: {
		struct pf_ifspeed	*psp = (struct pf_ifspeed *)addr;
		struct pf_ifspeed	ps;
		struct ifnet		*ifp;

		if (psp->ifname[0] != 0) {
			/* Can we completely trust user-land? */
			strlcpy(ps.ifname, psp->ifname, IFNAMSIZ);
			ifp = ifunit(ps.ifname);
			if (ifp )
				psp->baudrate = ifp->if_baudrate;
			else
				error = EINVAL;
		} else
			error = EINVAL;
		break;
	}
#ifdef ALTQ
	case DIOCSTARTALTQ: {
		struct pf_altq		*altq;

		/* enable all altq interfaces on active list */
		TAILQ_FOREACH(altq, pf_altqs_active, entries) {
			if (altq->qname[0] == 0) {
				error = pf_enable_altq(altq);
				if (error != 0)
					break;
			}
		}
		if (error == 0)
			pf_altq_running = 1;
		DPFPRINTF(PF_DEBUG_MISC, ("altq: started\n"));
		break;
	}

	case DIOCSTOPALTQ: {
		struct pf_altq		*altq;

		/* disable all altq interfaces on active list */
		TAILQ_FOREACH(altq, pf_altqs_active, entries) {
			if (altq->qname[0] == 0) {
				error = pf_disable_altq(altq);
				if (error != 0)
					break;
			}
		}
		if (error == 0)
			pf_altq_running = 0;
		DPFPRINTF(PF_DEBUG_MISC, ("altq: stopped\n"));
		break;
	}

	case DIOCADDALTQ: {
		struct pfioc_altq	*pa = (struct pfioc_altq *)addr;
		struct pf_altq		*altq, *a;

		if (pa->ticket != ticket_altqs_inactive) {
			error = EBUSY;
			break;
		}
		altq = kmalloc(sizeof(struct pf_altq), M_PFALTQPL, M_WAITOK|M_NULLOK);
		if (altq == NULL) {
			error = ENOMEM;
			break;
		}
		bcopy(&pa->altq, altq, sizeof(struct pf_altq));

		/*
		 * if this is for a queue, find the discipline and
		 * copy the necessary fields
		 */
		if (altq->qname[0] != 0) {
			if ((altq->qid = pf_qname2qid(altq->qname)) == 0) {
				error = EBUSY;
				kfree(altq, M_PFALTQPL);
				break;
			}
			altq->altq_disc = NULL;
			TAILQ_FOREACH(a, pf_altqs_inactive, entries) {
				if (strncmp(a->ifname, altq->ifname,
				    IFNAMSIZ) == 0 && a->qname[0] == 0) {
					altq->altq_disc = a->altq_disc;
					break;
				}
			}
		}

		error = altq_add(altq);
		if (error) {
			kfree(altq, M_PFALTQPL);
			break;
		}

		TAILQ_INSERT_TAIL(pf_altqs_inactive, altq, entries);
		bcopy(altq, &pa->altq, sizeof(struct pf_altq));
		break;
	}

	case DIOCGETALTQS: {
		struct pfioc_altq	*pa = (struct pfioc_altq *)addr;
		struct pf_altq		*altq;

		pa->nr = 0;
		TAILQ_FOREACH(altq, pf_altqs_active, entries)
			pa->nr++;
		pa->ticket = ticket_altqs_active;
		break;
	}

	case DIOCGETALTQ: {
		struct pfioc_altq	*pa = (struct pfioc_altq *)addr;
		struct pf_altq		*altq;
		u_int32_t		 nr;

		if (pa->ticket != ticket_altqs_active) {
			error = EBUSY;
			break;
		}
		nr = 0;
		altq = TAILQ_FIRST(pf_altqs_active);
		while ((altq != NULL) && (nr < pa->nr)) {
			altq = TAILQ_NEXT(altq, entries);
			nr++;
		}
		if (altq == NULL) {
			error = EBUSY;
			break;
		}
		bcopy(altq, &pa->altq, sizeof(struct pf_altq));
		break;
	}

	case DIOCCHANGEALTQ:
		/* CHANGEALTQ not supported yet! */
		error = ENODEV;
		break;

	case DIOCGETQSTATS: {
		struct pfioc_qstats	*pq = (struct pfioc_qstats *)addr;
		struct pf_altq		*altq;
		u_int32_t		 nr;
		int			 nbytes;

		if (pq->ticket != ticket_altqs_active) {
			error = EBUSY;
			break;
		}
		nbytes = pq->nbytes;
		nr = 0;
		altq = TAILQ_FIRST(pf_altqs_active);
		while ((altq != NULL) && (nr < pq->nr)) {
			altq = TAILQ_NEXT(altq, entries);
			nr++;
		}
		if (altq == NULL) {
			error = EBUSY;
			break;
		}
		error = altq_getqstats(altq, pq->buf, &nbytes);
		if (error == 0) {
			pq->scheduler = altq->scheduler;
			pq->nbytes = nbytes;
		}
		break;
	}
#endif /* ALTQ */

	case DIOCBEGINADDRS: {
		struct pfioc_pooladdr	*pp = (struct pfioc_pooladdr *)addr;

		pf_empty_pool(&pf_pabuf);
		pp->ticket = ++ticket_pabuf;
		break;
	}

	case DIOCADDADDR: {
		struct pfioc_pooladdr	*pp = (struct pfioc_pooladdr *)addr;

		if (pp->ticket != ticket_pabuf) {
			error = EBUSY;
			break;
		}
#ifndef INET
		if (pp->af == AF_INET) {
			error = EAFNOSUPPORT;
			break;
		}
#endif /* INET */
#ifndef INET6
		if (pp->af == AF_INET6) {
			error = EAFNOSUPPORT;
			break;
		}
#endif /* INET6 */
		if (pp->addr.addr.type != PF_ADDR_ADDRMASK &&
		    pp->addr.addr.type != PF_ADDR_DYNIFTL &&
		    pp->addr.addr.type != PF_ADDR_TABLE) {
			error = EINVAL;
			break;
		}
		pa = kmalloc(sizeof(struct pf_altq), M_PFPOOLADDRPL, M_WAITOK|M_NULLOK);
		if (pa == NULL) {
			error = ENOMEM;
			break;
		}
		bcopy(&pp->addr, pa, sizeof(struct pf_pooladdr));
		if (pa->ifname[0]) {
			pa->kif = pfi_kif_get(pa->ifname);
			if (pa->kif == NULL) {
				kfree(ap, M_PFPOOLADDRPL);
				error = EINVAL;
				break;
			}
			pfi_kif_ref(pa->kif, PFI_KIF_REF_RULE);
		}
		if (pfi_dynaddr_setup(&pa->addr, pp->af)) {
			pfi_dynaddr_remove(&pa->addr);
			pfi_kif_unref(pa->kif, PFI_KIF_REF_RULE);
			kfree(pa, M_PFPOOLADDRPL);
			error = EINVAL;
			break;
		}
		TAILQ_INSERT_TAIL(&pf_pabuf, pa, entries);
		break;
	}

	case DIOCGETADDRS: {
		struct pfioc_pooladdr	*pp = (struct pfioc_pooladdr *)addr;

		pp->nr = 0;
		pool = pf_get_pool(pp->anchor, pp->ticket, pp->r_action,
		    pp->r_num, 0, 1, 0);
		if (pool == NULL) {
			error = EBUSY;
			break;
		}
		TAILQ_FOREACH(pa, &pool->list, entries)
			pp->nr++;
		break;
	}

	case DIOCGETADDR: {
		struct pfioc_pooladdr	*pp = (struct pfioc_pooladdr *)addr;
		u_int32_t		 nr = 0;

		pool = pf_get_pool(pp->anchor, pp->ticket, pp->r_action,
		    pp->r_num, 0, 1, 1);
		if (pool == NULL) {
			error = EBUSY;
			break;
		}
		pa = TAILQ_FIRST(&pool->list);
		while ((pa != NULL) && (nr < pp->nr)) {
			pa = TAILQ_NEXT(pa, entries);
			nr++;
		}
		if (pa == NULL) {
			error = EBUSY;
			break;
		}
		bcopy(pa, &pp->addr, sizeof(struct pf_pooladdr));
		pf_addr_copyout(&pp->addr.addr);
		break;
	}

	case DIOCCHANGEADDR: {
		struct pfioc_pooladdr	*pca = (struct pfioc_pooladdr *)addr;
		struct pf_pooladdr	*oldpa = NULL, *newpa = NULL;
		struct pf_ruleset	*ruleset;

		if (pca->action < PF_CHANGE_ADD_HEAD ||
		    pca->action > PF_CHANGE_REMOVE) {
			error = EINVAL;
			break;
		}
		if (pca->addr.addr.type != PF_ADDR_ADDRMASK &&
		    pca->addr.addr.type != PF_ADDR_DYNIFTL &&
		    pca->addr.addr.type != PF_ADDR_TABLE) {
			error = EINVAL;
			break;
		}

		ruleset = pf_find_ruleset(pca->anchor);
		if (ruleset == NULL) {
			error = EBUSY;
			break;
		}
		pool = pf_get_pool(pca->anchor, pca->ticket, pca->r_action,
		    pca->r_num, pca->r_last, 1, 1);
		if (pool == NULL) {
			error = EBUSY;
			break;
		}
		if (pca->action != PF_CHANGE_REMOVE) {
			newpa = kmalloc(sizeof(struct pf_pooladdr),
				M_PFPOOLADDRPL, M_WAITOK|M_NULLOK);
			if (newpa == NULL) {
				error = ENOMEM;
				break;
			}
			bcopy(&pca->addr, newpa, sizeof(struct pf_pooladdr));
#ifndef INET
			if (pca->af == AF_INET) {
				kfree(newpa, M_PFPOOLADDRPL);
				error = EAFNOSUPPORT;
				break;
			}
#endif /* INET */
#ifndef INET6
			if (pca->af == AF_INET6) {
				kfree(newpa, M_PFPOOLADDRPL);
				error = EAFNOSUPPORT;
				break;
			}
#endif /* INET6 */
			if (newpa->ifname[0]) {
				newpa->kif = pfi_kif_get(newpa->ifname);
				if (newpa->kif == NULL) {
					kfree(newpa, M_PFPOOLADDRPL);
					error = EINVAL;
					break;
				}
				pfi_kif_ref(newpa->kif, PFI_KIF_REF_RULE);
			} else
				newpa->kif = NULL;
			if (pfi_dynaddr_setup(&newpa->addr, pca->af) ||
			    pf_tbladdr_setup(ruleset, &newpa->addr)) {
				pfi_dynaddr_remove(&newpa->addr);
				pfi_kif_unref(newpa->kif, PFI_KIF_REF_RULE);
				kfree(newpa, M_PFPOOLADDRPL);
				error = EINVAL;
				break;
			}
		}

		if (pca->action == PF_CHANGE_ADD_HEAD)
			oldpa = TAILQ_FIRST(&pool->list);
		else if (pca->action == PF_CHANGE_ADD_TAIL)
			oldpa = TAILQ_LAST(&pool->list, pf_palist);
		else {
			int	i = 0;

			oldpa = TAILQ_FIRST(&pool->list);
			while ((oldpa != NULL) && (i < pca->nr)) {
				oldpa = TAILQ_NEXT(oldpa, entries);
				i++;
			}
			if (oldpa == NULL) {
				error = EINVAL;
				break;
			}
		}

		if (pca->action == PF_CHANGE_REMOVE) {
			TAILQ_REMOVE(&pool->list, oldpa, entries);
			pfi_dynaddr_remove(&oldpa->addr);
			pf_tbladdr_remove(&oldpa->addr);
			pfi_kif_unref(oldpa->kif, PFI_KIF_REF_RULE);
			kfree(oldpa, M_PFPOOLADDRPL);
		} else {
			if (oldpa == NULL)
				TAILQ_INSERT_TAIL(&pool->list, newpa, entries);
			else if (pca->action == PF_CHANGE_ADD_HEAD ||
			    pca->action == PF_CHANGE_ADD_BEFORE)
				TAILQ_INSERT_BEFORE(oldpa, newpa, entries);
			else
				TAILQ_INSERT_AFTER(&pool->list, oldpa,
				    newpa, entries);
		}

		pool->cur = TAILQ_FIRST(&pool->list);
		PF_ACPY(&pool->counter, &pool->cur->addr.v.a.addr,
		    pca->af);
		break;
	}

	case DIOCGETRULESETS: {
		struct pfioc_ruleset	*pr = (struct pfioc_ruleset *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_anchor	*anchor;

		pr->path[sizeof(pr->path) - 1] = 0;
		if ((ruleset = pf_find_ruleset(pr->path)) == NULL) {
			error = EINVAL;
			break;
		}
		pr->nr = 0;
		if (ruleset->anchor == NULL) {
			/* XXX kludge for pf_main_ruleset */
			RB_FOREACH(anchor, pf_anchor_global, &pf_anchors)
				if (anchor->parent == NULL)
					pr->nr++;
		} else {
			RB_FOREACH(anchor, pf_anchor_node,
			    &ruleset->anchor->children)
				pr->nr++;
		}
		break;
	}

	case DIOCGETRULESET: {
		struct pfioc_ruleset	*pr = (struct pfioc_ruleset *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_anchor	*anchor;
		u_int32_t		 nr = 0;

		pr->path[sizeof(pr->path) - 1] = 0;
		if ((ruleset = pf_find_ruleset(pr->path)) == NULL) {
			error = EINVAL;
			break;
		}
		pr->name[0] = 0;
		if (ruleset->anchor == NULL) {
			/* XXX kludge for pf_main_ruleset */
			RB_FOREACH(anchor, pf_anchor_global, &pf_anchors)
				if (anchor->parent == NULL && nr++ == pr->nr) {
					strlcpy(pr->name, anchor->name,
					    sizeof(pr->name));
					break;
				}
		} else {
			RB_FOREACH(anchor, pf_anchor_node,
			    &ruleset->anchor->children)
				if (nr++ == pr->nr) {
					strlcpy(pr->name, anchor->name,
					    sizeof(pr->name));
					break;
				}
		}
		if (!pr->name[0])
			error = EBUSY;
		break;
	}

	case DIOCRCLRTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != 0) {
			error = ENODEV;
			break;
		}
		error = pfr_clr_tables(&io->pfrio_table, &io->pfrio_ndel,
		    io->pfrio_flags | PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRADDTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}
		error = pfr_add_tables(io->pfrio_buffer, io->pfrio_size,
		    &io->pfrio_nadd, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRDELTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}
		error = pfr_del_tables(io->pfrio_buffer, io->pfrio_size,
		    &io->pfrio_ndel, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRGETTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}
		error = pfr_get_tables(&io->pfrio_table, io->pfrio_buffer,
		    &io->pfrio_size, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRGETTSTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_tstats)) {
			error = ENODEV;
			break;
		}
		error = pfr_get_tstats(&io->pfrio_table, io->pfrio_buffer,
		    &io->pfrio_size, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRCLRTSTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}
		error = pfr_clr_tstats(io->pfrio_buffer, io->pfrio_size,
		    &io->pfrio_nzero, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRSETTFLAGS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}
		error = pfr_set_tflags(io->pfrio_buffer, io->pfrio_size,
		    io->pfrio_setflag, io->pfrio_clrflag, &io->pfrio_nchange,
		    &io->pfrio_ndel, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRCLRADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != 0) {
			error = ENODEV;
			break;
		}
		error = pfr_clr_addrs(&io->pfrio_table, &io->pfrio_ndel,
		    io->pfrio_flags | PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRADDADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		error = pfr_add_addrs(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_nadd, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRDELADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		error = pfr_del_addrs(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_ndel, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRSETADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		error = pfr_set_addrs(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_size2, &io->pfrio_nadd,
		    &io->pfrio_ndel, &io->pfrio_nchange, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL, 0);
		break;
	}

	case DIOCRGETADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		error = pfr_get_addrs(&io->pfrio_table, io->pfrio_buffer,
		    &io->pfrio_size, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRGETASTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_astats)) {
			error = ENODEV;
			break;
		}
		error = pfr_get_astats(&io->pfrio_table, io->pfrio_buffer,
		    &io->pfrio_size, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRCLRASTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		error = pfr_clr_astats(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_nzero, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRTSTADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		error = pfr_tst_addrs(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_nmatch, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRINADEFINE: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		error = pfr_ina_define(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_nadd, &io->pfrio_naddr,
		    io->pfrio_ticket, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCOSFPADD: {
		struct pf_osfp_ioctl *io = (struct pf_osfp_ioctl *)addr;
		error = pf_osfp_add(io);
		break;
	}

	case DIOCOSFPGET: {
		struct pf_osfp_ioctl *io = (struct pf_osfp_ioctl *)addr;
		error = pf_osfp_get(io);
		break;
	}

	case DIOCXBEGIN: {
		struct pfioc_trans	*io = (struct pfioc_trans *)addr;
		struct pfioc_trans_e	*ioe;
		struct pfr_table	*table;
		int			 i;

		if (io->esize != sizeof(*ioe)) {
			error = ENODEV;
			goto fail;
		}
		ioe = kmalloc(sizeof(*ioe), M_TEMP, M_WAITOK);
		table = kmalloc(sizeof(*table), M_TEMP, M_WAITOK);
		for (i = 0; i < io->size; i++) {
			if (copyin(io->array+i, ioe, sizeof(*ioe))) {
				kfree(table, M_TEMP);
				kfree(ioe, M_TEMP);
				error = EFAULT;
				goto fail;
			}
			switch (ioe->rs_num) {
#ifdef ALTQ
			case PF_RULESET_ALTQ:
				if (ioe->anchor[0]) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					error = EINVAL;
					goto fail;
				}
				if ((error = pf_begin_altq(&ioe->ticket))) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					goto fail;
				}
				break;
#endif /* ALTQ */
			case PF_RULESET_TABLE:
				bzero(table, sizeof(*table));
				strlcpy(table->pfrt_anchor, ioe->anchor,
				    sizeof(table->pfrt_anchor));
				if ((error = pfr_ina_begin(table,
				    &ioe->ticket, NULL, 0))) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					goto fail;
				}
				break;
			default:
				if ((error = pf_begin_rules(&ioe->ticket,
				    ioe->rs_num, ioe->anchor))) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					goto fail;
				}
				break;
			}
			if (copyout(ioe, io->array+i, sizeof(io->array[i]))) {
				kfree(table, M_TEMP);
				kfree(ioe, M_TEMP);
				error = EFAULT;
				goto fail;
			}
		}
		kfree(table, M_TEMP);
		kfree(ioe, M_TEMP);
		break;
	}

	case DIOCXROLLBACK: {
		struct pfioc_trans	*io = (struct pfioc_trans *)addr;
		struct pfioc_trans_e	*ioe;
		struct pfr_table	*table;
		int			 i;

		if (io->esize != sizeof(*ioe)) {
			error = ENODEV;
			goto fail;
		}
		ioe = kmalloc(sizeof(*ioe), M_TEMP, M_WAITOK);
		table = kmalloc(sizeof(*table), M_TEMP, M_WAITOK);
		for (i = 0; i < io->size; i++) {
			if (copyin(io->array+i, ioe, sizeof(*ioe))) {
				kfree(table, M_TEMP);
				kfree(ioe, M_TEMP);
				error = EFAULT;
				goto fail;
			}
			switch (ioe->rs_num) {
#ifdef ALTQ
			case PF_RULESET_ALTQ:
				if (ioe->anchor[0]) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					error = EINVAL;
					goto fail;
				}
				if ((error = pf_rollback_altq(ioe->ticket))) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					goto fail; /* really bad */
				}
				break;
#endif /* ALTQ */
			case PF_RULESET_TABLE:
				bzero(table, sizeof(*table));
				strlcpy(table->pfrt_anchor, ioe->anchor,
				    sizeof(table->pfrt_anchor));
				if ((error = pfr_ina_rollback(table,
				    ioe->ticket, NULL, 0))) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					goto fail; /* really bad */
				}
				break;
			default:
				if ((error = pf_rollback_rules(ioe->ticket,
				    ioe->rs_num, ioe->anchor))) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					goto fail; /* really bad */
				}
				break;
			}
		}
		kfree(table, M_TEMP);
		kfree(ioe, M_TEMP);
		break;
	}

	case DIOCXCOMMIT: {
		struct pfioc_trans	*io = (struct pfioc_trans *)addr;
		struct pfioc_trans_e	*ioe;
		struct pfr_table	*table;
		struct pf_ruleset	*rs;
		int			 i;

		if (io->esize != sizeof(*ioe)) {
			error = ENODEV;
			goto fail;
		}
		ioe = kmalloc(sizeof(*ioe), M_TEMP, M_WAITOK);
		table = kmalloc(sizeof(*table), M_TEMP, M_WAITOK);
		/* first makes sure everything will succeed */
		for (i = 0; i < io->size; i++) {
			if (copyin(io->array+i, ioe, sizeof(*ioe))) {
				kfree(table, M_TEMP);
				kfree(ioe, M_TEMP);
				error = EFAULT;
				goto fail;
			}
			switch (ioe->rs_num) {
#ifdef ALTQ
			case PF_RULESET_ALTQ:
				if (ioe->anchor[0]) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					error = EINVAL;
					goto fail;
				}
				if (!altqs_inactive_open || ioe->ticket !=
				    ticket_altqs_inactive) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					error = EBUSY;
					goto fail;
				}
				break;
#endif /* ALTQ */
			case PF_RULESET_TABLE:
				rs = pf_find_ruleset(ioe->anchor);
				if (rs == NULL || !rs->topen || ioe->ticket !=
				     rs->tticket) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					error = EBUSY;
					goto fail;
				}
				break;
			default:
				if (ioe->rs_num < 0 || ioe->rs_num >=
				    PF_RULESET_MAX) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					error = EINVAL;
					goto fail;
				}
				rs = pf_find_ruleset(ioe->anchor);
				if (rs == NULL ||
				    !rs->rules[ioe->rs_num].inactive.open ||
				    rs->rules[ioe->rs_num].inactive.ticket !=
				    ioe->ticket) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					error = EBUSY;
					goto fail;
				}
				break;
			}
		}
		/* now do the commit - no errors should happen here */
		for (i = 0; i < io->size; i++) {
			if (copyin(io->array+i, ioe, sizeof(*ioe))) {
				kfree(table, M_TEMP);
				kfree(ioe, M_TEMP);
				error = EFAULT;
				goto fail;
			}
			switch (ioe->rs_num) {
#ifdef ALTQ
			case PF_RULESET_ALTQ:
				if ((error = pf_commit_altq(ioe->ticket))) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					goto fail; /* really bad */
				}
				break;
#endif /* ALTQ */
			case PF_RULESET_TABLE:
				bzero(table, sizeof(*table));
				strlcpy(table->pfrt_anchor, ioe->anchor,
				    sizeof(table->pfrt_anchor));
				if ((error = pfr_ina_commit(table, ioe->ticket,
				    NULL, NULL, 0))) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					goto fail; /* really bad */
				}
				break;
			default:
				if ((error = pf_commit_rules(ioe->ticket,
				    ioe->rs_num, ioe->anchor))) {
					kfree(table, M_TEMP);
					kfree(ioe, M_TEMP);
					goto fail; /* really bad */
				}
				break;
			}
		}
		kfree(table, M_TEMP);
		kfree(ioe, M_TEMP);
		break;
	}

	case DIOCGETSRCNODES: {
		struct pfioc_src_nodes	*psn = (struct pfioc_src_nodes *)addr;
		struct pf_src_node	*n, *p, *pstore;
		u_int32_t		 nr = 0;
		int			 space = psn->psn_len;

		if (space == 0) {
			RB_FOREACH(n, pf_src_tree, &tree_src_tracking)
				nr++;
			psn->psn_len = sizeof(struct pf_src_node) * nr;
			break;
		}

		pstore = kmalloc(sizeof(*pstore), M_TEMP, M_WAITOK);

		p = psn->psn_src_nodes;
		RB_FOREACH(n, pf_src_tree, &tree_src_tracking) {
			int	secs = time_second, diff;

			if ((nr + 1) * sizeof(*p) > (unsigned)psn->psn_len)
				break;

			bcopy(n, pstore, sizeof(*pstore));
			if (n->rule.ptr != NULL)
				pstore->rule.nr = n->rule.ptr->nr;
			pstore->creation = secs - pstore->creation;
			if (pstore->expire > secs)
				pstore->expire -= secs;
			else
				pstore->expire = 0;

			/* adjust the connection rate estimate */
			diff = secs - n->conn_rate.last;
			if (diff >= n->conn_rate.seconds)
				pstore->conn_rate.count = 0;
			else
				pstore->conn_rate.count -=
				    n->conn_rate.count * diff /
				    n->conn_rate.seconds;

			error = copyout(pstore, p, sizeof(*p));
			if (error) {
				kfree(pstore, M_TEMP);
				goto fail;
			}
			p++;
			nr++;
		}
		psn->psn_len = sizeof(struct pf_src_node) * nr;

		kfree(pstore, M_TEMP);
		break;
	}

	case DIOCCLRSRCNODES: {
		struct pf_src_node	*n;
		struct pf_state		*state;

		RB_FOREACH(state, pf_state_tree_id, &tree_id) {
			state->src_node = NULL;
			state->nat_src_node = NULL;
		}
		RB_FOREACH(n, pf_src_tree, &tree_src_tracking) {
			n->expire = 1;
			n->states = 0;
		}
		pf_purge_expired_src_nodes(1);
		pf_status.src_nodes = 0;
		break;
	}

	case DIOCKILLSRCNODES: {
		struct pf_src_node	*sn;
		struct pf_state		*s;
		struct pfioc_src_node_kill *psnk =
		    (struct pfioc_src_node_kill *)addr;
		u_int			killed = 0;

		RB_FOREACH(sn, pf_src_tree, &tree_src_tracking) {
			if (PF_MATCHA(psnk->psnk_src.neg,
				&psnk->psnk_src.addr.v.a.addr,
				&psnk->psnk_src.addr.v.a.mask,
				&sn->addr, sn->af) &&
			    PF_MATCHA(psnk->psnk_dst.neg,
				&psnk->psnk_dst.addr.v.a.addr,
				&psnk->psnk_dst.addr.v.a.mask,
				&sn->raddr, sn->af)) {
				/* Handle state to src_node linkage */
				if (sn->states != 0) {
					RB_FOREACH(s, pf_state_tree_id,
					    &tree_id) {
						if (s->src_node == sn)
							s->src_node = NULL;
						if (s->nat_src_node == sn)
							s->nat_src_node = NULL;
					}
					sn->states = 0;
				}
				sn->expire = 1;
				killed++;
			}
		}

		if (killed > 0)
			pf_purge_expired_src_nodes(1);

		psnk->psnk_killed = killed;
		break;
	}

	case DIOCSETHOSTID: {
		u_int32_t	*hostid = (u_int32_t *)addr;

		if (*hostid == 0)
			pf_status.hostid = karc4random();
		else
			pf_status.hostid = *hostid;
		break;
	}

	case DIOCOSFPFLUSH:
		crit_enter();
		pf_osfp_flush();
		crit_exit();
		break;

	case DIOCIGETIFACES: {
		struct pfioc_iface *io = (struct pfioc_iface *)addr;

		if (io->pfiio_esize != sizeof(struct pfi_kif)) {
			error = ENODEV;
			break;
		}
		error = pfi_get_ifaces(io->pfiio_name, io->pfiio_buffer,
		    &io->pfiio_size);
		break;
	}

	case DIOCSETIFFLAG: {
		struct pfioc_iface *io = (struct pfioc_iface *)addr;

		error = pfi_set_flags(io->pfiio_name, io->pfiio_flags);
		break;
	}

	case DIOCCLRIFFLAG: {
		struct pfioc_iface *io = (struct pfioc_iface *)addr;

		error = pfi_clear_flags(io->pfiio_name, io->pfiio_flags);
		break;
	}

	default:
		error = ENODEV;
		break;
	}
fail:
	lwkt_reltoken(&pf_token);
	return (error);
}

/*
 * XXX - Check for version missmatch!!!
 */
static void
pf_clear_states(void)
{
	struct pf_state		*s, *nexts;
	u_int			killed = 0;

	for (s = RB_MIN(pf_state_tree_id, &tree_id); s; s = nexts) {
		nexts = RB_NEXT(pf_state_tree_id, &tree_id, s);

		/* don't send out individual delete messages */
		s->sync_flags = PFSTATE_NOSYNC;
		pf_unlink_state(s);
		killed++;
                        
	}

#if 0 /* PFSYNC */
/*
 * XXX This is called on module unload, we do not want to sync that over? */
 */
	pfsync_clear_states(pf_status.hostid, psk->psk_ifname);
#endif
}

static int
pf_clear_tables(void)
{
	struct pfioc_table io;
	int error;

	bzero(&io, sizeof(io));

	error = pfr_clr_tables(&io.pfrio_table, &io.pfrio_ndel,
	    io.pfrio_flags);

	return (error);
}

static void
pf_clear_srcnodes(void)
{
	struct pf_src_node	*n;
	struct pf_state		*state;

	RB_FOREACH(state, pf_state_tree_id, &tree_id) {
		state->src_node = NULL;
		state->nat_src_node = NULL;
	}
	RB_FOREACH(n, pf_src_tree, &tree_src_tracking) {
		n->expire = 1;
		n->states = 0;
	}
	pf_purge_expired_src_nodes(0);
	pf_status.src_nodes = 0;
}

/*
 * XXX - Check for version missmatch!!!
 */

/*
 * Duplicate pfctl -Fa operation to get rid of as much as we can.
 */
static int
shutdown_pf(void)
{
	int error = 0;
	u_int32_t t[5];
	char nn = '\0';


	pf_status.running = 0;
	error = dehook_pf();
	if (error) {
		pf_status.running = 1;
		DPFPRINTF(PF_DEBUG_MISC,
		    ("pf: pfil unregistration failed\n"));
		return(error);
	}
	do {
		if ((error = pf_begin_rules(&t[0], PF_RULESET_SCRUB, &nn)) != 0) {
			DPFPRINTF(PF_DEBUG_MISC, ("shutdown_pf: SCRUB\n"));
			break;
		}
		if ((error = pf_begin_rules(&t[1], PF_RULESET_FILTER, &nn)) != 0) {
			DPFPRINTF(PF_DEBUG_MISC, ("shutdown_pf: FILTER\n"));
			break;		/* XXX: rollback? */
		}
		if ((error = pf_begin_rules(&t[2], PF_RULESET_NAT, &nn))    != 0) {
			DPFPRINTF(PF_DEBUG_MISC, ("shutdown_pf: NAT\n"));
			break;		/* XXX: rollback? */
		}
		if ((error = pf_begin_rules(&t[3], PF_RULESET_BINAT, &nn))
		    != 0) {
			DPFPRINTF(PF_DEBUG_MISC, ("shutdown_pf: BINAT\n"));
			break;		/* XXX: rollback? */
		}
		if ((error = pf_begin_rules(&t[4], PF_RULESET_RDR, &nn))
		    != 0) {
			DPFPRINTF(PF_DEBUG_MISC, ("shutdown_pf: RDR\n"));
			break;		/* XXX: rollback? */
		}

		/* XXX: these should always succeed here */
		pf_commit_rules(t[0], PF_RULESET_SCRUB, &nn);
		pf_commit_rules(t[1], PF_RULESET_FILTER, &nn);
		pf_commit_rules(t[2], PF_RULESET_NAT, &nn);
		pf_commit_rules(t[3], PF_RULESET_BINAT, &nn);
		pf_commit_rules(t[4], PF_RULESET_RDR, &nn);

		if ((error = pf_clear_tables()) != 0)
			break;
#ifdef ALTQ
		if ((error = pf_begin_altq(&t[0])) != 0) {
			DPFPRINTF(PF_DEBUG_MISC, ("shutdown_pf: ALTQ\n"));
			break;
		}
		pf_commit_altq(t[0]);
#endif
		pf_clear_states();
		pf_clear_srcnodes();

		/* status does not use malloced mem so no need to cleanup */
		/* fingerprints and interfaces have their own cleanup code */
	} while(0);
        return (error);
}

static int
pf_check_in(void *arg, struct mbuf **m, struct ifnet *ifp, int dir)
{
	/*
	 * DragonFly's version of pf uses FreeBSD's native host byte ordering
	 * for ip_len/ip_off. This is why we don't have to change byte order
	 * like the FreeBSD-5 version does.
	 */
	int chk;

	lwkt_gettoken(&pf_token);

	chk = pf_test(PF_IN, ifp, m, NULL, NULL);
	if (chk && *m) {
		m_freem(*m);
		*m = NULL;
	}
	lwkt_reltoken(&pf_token);
	return chk;
}

static int
pf_check_out(void *arg, struct mbuf **m, struct ifnet *ifp, int dir)
{
	/*
	 * DragonFly's version of pf uses FreeBSD's native host byte ordering
	 * for ip_len/ip_off. This is why we don't have to change byte order
	 * like the FreeBSD-5 version does.
	 */
	int chk;

	lwkt_gettoken(&pf_token);

	/* We need a proper CSUM befor we start (s. OpenBSD ip_output) */
	if ((*m)->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
		in_delayed_cksum(*m);
		(*m)->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
	}
	chk = pf_test(PF_OUT, ifp, m, NULL, NULL);
	if (chk && *m) {
		m_freem(*m);
		*m = NULL;
	}
	lwkt_reltoken(&pf_token);
	return chk;
}

#ifdef INET6
static int
pf_check6_in(void *arg, struct mbuf **m, struct ifnet *ifp, int dir)
{
	/*
	 * IPv6 is not affected by ip_len/ip_off byte order changes.
	 */
	int chk;

	lwkt_gettoken(&pf_token);

	chk = pf_test6(PF_IN, ifp, m, NULL, NULL);
	if (chk && *m) {
		m_freem(*m);
		*m = NULL;
	}
	lwkt_reltoken(&pf_token);
	return chk;
}

static int
pf_check6_out(void *arg, struct mbuf **m, struct ifnet *ifp, int dir)
{
	/*
	 * IPv6 is not affected by ip_len/ip_off byte order changes.
	 */
	int chk;

	lwkt_gettoken(&pf_token);

	/* We need a proper CSUM befor we start (s. OpenBSD ip_output) */
	if ((*m)->m_pkthdr.csum_flags & CSUM_DELAY_DATA) {
		in_delayed_cksum(*m);
		(*m)->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
	}
	chk = pf_test6(PF_OUT, ifp, m, NULL, NULL);
	if (chk && *m) {
		m_freem(*m);
		*m = NULL;
	}
	lwkt_reltoken(&pf_token);
	return chk;
}
#endif /* INET6 */

static int
hook_pf(void)
{
	struct pfil_head *pfh_inet;
#ifdef INET6
	struct pfil_head *pfh_inet6;
#endif

	lwkt_gettoken(&pf_token);

	if (pf_pfil_hooked) {
		lwkt_reltoken(&pf_token);
		return (0);
	}
	
	pfh_inet = pfil_head_get(PFIL_TYPE_AF, AF_INET);
	if (pfh_inet == NULL) {
		lwkt_reltoken(&pf_token);
		return (ENODEV);
	}
	pfil_add_hook(pf_check_in, NULL, PFIL_IN | PFIL_MPSAFE, pfh_inet);
	pfil_add_hook(pf_check_out, NULL, PFIL_OUT | PFIL_MPSAFE, pfh_inet);
#ifdef INET6
	pfh_inet6 = pfil_head_get(PFIL_TYPE_AF, AF_INET6);
	if (pfh_inet6 == NULL) {
		pfil_remove_hook(pf_check_in, NULL, PFIL_IN, pfh_inet);
		pfil_remove_hook(pf_check_out, NULL, PFIL_OUT, pfh_inet);
		lwkt_reltoken(&pf_token);
		return (ENODEV);
	}
	pfil_add_hook(pf_check6_in, NULL, PFIL_IN | PFIL_MPSAFE, pfh_inet6);
	pfil_add_hook(pf_check6_out, NULL, PFIL_OUT | PFIL_MPSAFE, pfh_inet6);
#endif

	pf_pfil_hooked = 1;
	lwkt_reltoken(&pf_token);
	return (0);
}

static int
dehook_pf(void)
{
	struct pfil_head *pfh_inet;
#ifdef INET6
	struct pfil_head *pfh_inet6;
#endif

	lwkt_gettoken(&pf_token);

	if (pf_pfil_hooked == 0) {
		lwkt_reltoken(&pf_token);
		return (0);
	}

	pfh_inet = pfil_head_get(PFIL_TYPE_AF, AF_INET);
	if (pfh_inet == NULL) {
		lwkt_reltoken(&pf_token);
		return (ENODEV);
	}
	pfil_remove_hook(pf_check_in, NULL, PFIL_IN, pfh_inet);
	pfil_remove_hook(pf_check_out, NULL, PFIL_OUT, pfh_inet);
#ifdef INET6
	pfh_inet6 = pfil_head_get(PFIL_TYPE_AF, AF_INET6);
	if (pfh_inet6 == NULL) {
		lwkt_reltoken(&pf_token);
		return (ENODEV);
	}
	pfil_remove_hook(pf_check6_in, NULL, PFIL_IN, pfh_inet6);
	pfil_remove_hook(pf_check6_out, NULL, PFIL_OUT, pfh_inet6);
#endif

	pf_pfil_hooked = 0;
	lwkt_reltoken(&pf_token);
	return (0);
}

static int
pf_load(void)
{
	lwkt_gettoken(&pf_token);

	pf_dev = make_dev(&pf_ops, 0, 0, 0, 0600, PF_NAME);
	pfattach();
	lockinit(&pf_consistency_lock, "pfconslck", 0, LK_CANRECURSE);
	lwkt_reltoken(&pf_token);
	return (0);
}

static int
pf_mask_del(struct radix_node *rn, void *arg)
{
	struct radix_node_head *rnh = arg;

	rnh->rnh_deladdr(rn->rn_key, rn->rn_mask, rnh);
	Free(rn);
	return 0;
}

static int
pf_unload(void)
{
	int error;
	pf_status.running = 0;

	lwkt_gettoken(&pf_token);

	error = dehook_pf();
	if (error) {
		/*
		 * Should not happen!
		 * XXX Due to error code ESRCH, kldunload will show
		 * a message like 'No such process'.
		 */
		kprintf("pfil unregistration fail\n");
		lwkt_reltoken(&pf_token);
		return error;
	}
	shutdown_pf();
	pf_end_threads = 1;
	while (pf_end_threads < 2) {
		wakeup_one(pf_purge_thread);
		tsleep(pf_purge_thread, 0, "pftmo", hz);
	}
	pfi_cleanup();
	pf_osfp_flush();
	dev_ops_remove_all(&pf_ops);
	lockuninit(&pf_consistency_lock);
	lwkt_reltoken(&pf_token);

	if (pf_maskhead != NULL) {
		pf_maskhead->rnh_walktree(pf_maskhead,
			pf_mask_del, pf_maskhead);
		Free(pf_maskhead);
		pf_maskhead = NULL;
	}
	kmalloc_destroy(&pf_state_pl);
	kmalloc_destroy(&pf_frent_pl);
	kmalloc_destroy(&pf_cent_pl);
	return 0;
}

static int
pf_modevent(module_t mod, int type, void *data)
{
	int error = 0;

	lwkt_gettoken(&pf_token);

	switch(type) {
	case MOD_LOAD:
		error = pf_load();
		break;

	case MOD_UNLOAD:
		error = pf_unload();
		break;
	default:
		error = EINVAL;
		break;
	}
	lwkt_reltoken(&pf_token);
	return error;
}

static moduledata_t pf_mod = {
	"pf",
	pf_modevent,
	0
};
DECLARE_MODULE(pf, pf_mod, SI_SUB_PSEUDO, SI_ORDER_FIRST);
MODULE_VERSION(pf, PF_MODVER);
