/*	$KAME: altq_subr.c,v 1.23 2004/04/20 16:10:06 itojun Exp $	*/
/*	$DragonFly: src/sys/net/altq/altq_subr.c,v 1.2 2005/04/04 17:08:16 joerg Exp $ */

/*
 * Copyright (C) 1997-2003
 *	Sony Computer Science Laboratories Inc.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY SONY CSL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL SONY CSL OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt_altq.h"
#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/kernel.h>
#include <sys/callout.h>
#include <sys/errno.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/ifq_var.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <net/pf/pfvar.h>
#include <net/altq/altq.h>

/* machine dependent clock related includes */
#if defined(__i386__)
#include <machine/clock.h>		/* for tsc_freq */
#include <machine/md_var.h>		/* for cpu_feature */
#include <machine/specialreg.h>		/* for CPUID_TSC */
#endif /* __i386__ */

/*
 * internal function prototypes
 */
static void	tbr_timeout(void *);
int (*altq_input)(struct mbuf *, int) = NULL;
static int tbr_timer = 0;	/* token bucket regulator timer */
static struct callout tbr_callout;

int pfaltq_running;	/* keep track of running state */

MALLOC_DEFINE(M_ALTQ, "altq", "ALTQ structures");

/*
 * alternate queueing support routines
 */

/* look up the queue state by the interface name and the queueing type. */
void *
altq_lookup(const char *name, int type)
{
	struct ifnet *ifp;

	if ((ifp = ifunit(name)) != NULL) {
		if (type != ALTQT_NONE && ifp->if_snd.altq_type == type)
			return (ifp->if_snd.altq_disc);
	}

	return (NULL);
}

int
altq_attach(struct ifaltq *ifq, int type, void *discipline,
	    int (*enqueue)(struct ifaltq *, struct mbuf *, struct altq_pktattr *),
	    struct mbuf *(*dequeue)(struct ifaltq *, int),
	    int (*request)(struct ifaltq *, int, void *),
	    void *clfier,
	    void *(*classify)(struct ifaltq *, struct mbuf *,
			      struct altq_pktattr *))
{
	if (!ifq_is_ready(ifq))
		return ENXIO;

	ifq->altq_type     = type;
	ifq->altq_disc     = discipline;
	ifq->altq_enqueue  = enqueue;
	ifq->altq_dequeue  = dequeue;
	ifq->altq_request  = request;
	ifq->altq_clfier   = clfier;
	ifq->altq_classify = classify;
	ifq->altq_flags &= (ALTQF_CANTCHANGE|ALTQF_ENABLED);
	return 0;
}

int
altq_detach(struct ifaltq *ifq)
{
	if (!ifq_is_ready(ifq))
		return ENXIO;
	if (ifq_is_enabled(ifq))
		return EBUSY;
	if (!ifq_is_attached(ifq))
		return (0);

	ifq_set_classic(ifq);
	ifq->altq_type     = ALTQT_NONE;
	ifq->altq_disc     = NULL;
	ifq->altq_clfier   = NULL;
	ifq->altq_classify = NULL;
	ifq->altq_flags &= ALTQF_CANTCHANGE;
	return 0;
}

int
altq_enable(struct ifaltq *ifq)
{
	int s;

	if (!ifq_is_ready(ifq))
		return ENXIO;
	if (ifq_is_enabled(ifq))
		return 0;

	s = splimp();
	ifq_purge(ifq);
	KKASSERT(ifq->ifq_len == 0);
	ifq->altq_flags |= ALTQF_ENABLED;
	if (ifq->altq_clfier != NULL)
		ifq->altq_flags |= ALTQF_CLASSIFY;
	splx(s);

	return 0;
}

int
altq_disable(struct ifaltq *ifq)
{
	int s;

	if (!ifq_is_enabled(ifq))
		return 0;

	s = splimp();
	ifq_purge(ifq);
	KKASSERT(ifq->ifq_len == 0);
	ifq->altq_flags &= ~(ALTQF_ENABLED|ALTQF_CLASSIFY);
	splx(s);
	return 0;
}

/*
 * internal representation of token bucket parameters
 *	rate:	byte_per_unittime << 32
 *		(((bits_per_sec) / 8) << 32) / machclk_freq
 *	depth:	byte << 32
 *
 */
#define	TBR_SHIFT	32
#define	TBR_SCALE(x)	((int64_t)(x) << TBR_SHIFT)
#define	TBR_UNSCALE(x)	((x) >> TBR_SHIFT)

struct mbuf *
tbr_dequeue(struct ifaltq *ifq, int op)
{
	struct tb_regulator *tbr;
	struct mbuf *m;
	int64_t interval;
	uint64_t now;

	tbr = ifq->altq_tbr;
	if (op == ALTDQ_REMOVE && tbr->tbr_lastop == ALTDQ_POLL) {
		/* if this is a remove after poll, bypass tbr check */
	} else {
		/* update token only when it is negative */
		if (tbr->tbr_token <= 0) {
			now = read_machclk();
			interval = now - tbr->tbr_last;
			if (interval >= tbr->tbr_filluptime)
				tbr->tbr_token = tbr->tbr_depth;
			else {
				tbr->tbr_token += interval * tbr->tbr_rate;
				if (tbr->tbr_token > tbr->tbr_depth)
					tbr->tbr_token = tbr->tbr_depth;
			}
			tbr->tbr_last = now;
		}
		/* if token is still negative, don't allow dequeue */
		if (tbr->tbr_token <= 0)
			return (NULL);
	}

	if (ifq_is_enabled(ifq))
		m = (*ifq->altq_dequeue)(ifq, op);
	else if (op == ALTDQ_POLL)
		IF_POLL(ifq, m);
	else
		IF_DEQUEUE(ifq, m);

	if (m != NULL && op == ALTDQ_REMOVE)
		tbr->tbr_token -= TBR_SCALE(m_pktlen(m));
	tbr->tbr_lastop = op;
	return (m);
}

/*
 * set a token bucket regulator.
 * if the specified rate is zero, the token bucket regulator is deleted.
 */
int
tbr_set(struct ifaltq *ifq, struct tb_profile *profile)
{
	struct tb_regulator *tbr, *otbr;

	if (machclk_freq == 0)
		init_machclk();
	if (machclk_freq == 0) {
		printf("tbr_set: no cpu clock available!\n");
		return (ENXIO);
	}

	if (profile->rate == 0) {
		/* delete this tbr */
		if ((tbr = ifq->altq_tbr) == NULL)
			return (ENOENT);
		ifq->altq_tbr = NULL;
		free(tbr, M_ALTQ);
		return (0);
	}

	tbr = malloc(sizeof(*tbr), M_ALTQ, M_WAITOK | M_ZERO);
	tbr->tbr_rate = TBR_SCALE(profile->rate / 8) / machclk_freq;
	tbr->tbr_depth = TBR_SCALE(profile->depth);
	if (tbr->tbr_rate > 0)
		tbr->tbr_filluptime = tbr->tbr_depth / tbr->tbr_rate;
	else
		tbr->tbr_filluptime = 0xffffffffffffffffLL;
	tbr->tbr_token = tbr->tbr_depth;
	tbr->tbr_last = read_machclk();
	tbr->tbr_lastop = ALTDQ_REMOVE;

	otbr = ifq->altq_tbr;
	ifq->altq_tbr = tbr;	/* set the new tbr */

	if (otbr != NULL)
		free(otbr, M_ALTQ);
	else if (tbr_timer == 0) {
		callout_reset(&tbr_callout, 1, tbr_timeout, NULL);
		tbr_timer = 1;
	}
	return (0);
}

/*
 * tbr_timeout goes through the interface list, and kicks the drivers
 * if necessary.
 */
static void
tbr_timeout(void *arg)
{
	struct ifnet *ifp;
	int active, s;

	active = 0;
	s = splimp();
	for (ifp = TAILQ_FIRST(&ifnet); ifp; ifp = TAILQ_NEXT(ifp, if_list)) {
		if (ifp->if_snd.altq_tbr == NULL)
			continue;
		active++;
		if (!ifq_is_empty(&ifp->if_snd) && ifp->if_start != NULL)
			(*ifp->if_start)(ifp);
	}
	splx(s);
	if (active > 0)
		callout_reset(&tbr_callout, 1, tbr_timeout, NULL);
	else
		tbr_timer = 0;	/* don't need tbr_timer anymore */
}

/*
 * get token bucket regulator profile
 */
int
tbr_get(struct ifaltq *ifq, struct tb_profile *profile)
{
	struct tb_regulator *tbr;

	if ((tbr = ifq->altq_tbr) == NULL) {
		profile->rate = 0;
		profile->depth = 0;
	} else {
		profile->rate =
		    (u_int)TBR_UNSCALE(tbr->tbr_rate * 8 * machclk_freq);
		profile->depth = (u_int)TBR_UNSCALE(tbr->tbr_depth);
	}
	return (0);
}

/*
 * attach a discipline to the interface.  if one already exists, it is
 * overridden.
 */
int
altq_pfattach(struct pf_altq *a)
{
	struct ifnet *ifp;
	struct tb_profile tb;
	int s, error = 0;

	switch (a->scheduler) {
	case ALTQT_NONE:
		break;
#ifdef ALTQ_CBQ
	case ALTQT_CBQ:
		error = cbq_pfattach(a);
		break;
#endif
#ifdef ALTQ_PRIQ
	case ALTQT_PRIQ:
		error = priq_pfattach(a);
		break;
#endif
#ifdef ALTQ_HFSC
	case ALTQT_HFSC:
		error = hfsc_pfattach(a);
		break;
#endif
	default:
		error = ENXIO;
	}

	ifp = ifunit(a->ifname);

	/* if the state is running, enable altq */
	if (error == 0 && pfaltq_running &&
	    ifp != NULL && ifp->if_snd.altq_type != ALTQT_NONE &&
	    !ifq_is_enabled(&ifp->if_snd))
			error = altq_enable(&ifp->if_snd);

	/* if altq is already enabled, reset set tokenbucket regulator */
	if (error == 0 && ifp != NULL && ifq_is_enabled(&ifp->if_snd)) {
		tb.rate = a->ifbandwidth;
		tb.depth = a->tbrsize;
		s = splimp();
		error = tbr_set(&ifp->if_snd, &tb);
		splx(s);
	}

	return (error);
}

/*
 * detach a discipline from the interface.
 * it is possible that the discipline was already overridden by another
 * discipline.
 */
int
altq_pfdetach(struct pf_altq *a)
{
	struct ifnet *ifp;
	int s, error = 0;

	if ((ifp = ifunit(a->ifname)) == NULL)
		return (EINVAL);

	/* if this discipline is no longer referenced, just return */
	if (a->altq_disc == NULL || a->altq_disc != ifp->if_snd.altq_disc)
		return (0);

	s = splimp();
	if (ifq_is_enabled(&ifp->if_snd))
		error = altq_disable(&ifp->if_snd);
	if (error == 0)
		error = altq_detach(&ifp->if_snd);
	splx(s);

	return (error);
}

/*
 * add a discipline or a queue
 */
int
altq_add(struct pf_altq *a)
{
	int error = 0;

	if (a->qname[0] != 0)
		return (altq_add_queue(a));

	if (machclk_freq == 0)
		init_machclk();
	if (machclk_freq == 0)
		panic("altq_add: no cpu clock");

	switch (a->scheduler) {
#ifdef ALTQ_CBQ
	case ALTQT_CBQ:
		error = cbq_add_altq(a);
		break;
#endif
#ifdef ALTQ_PRIQ
	case ALTQT_PRIQ:
		error = priq_add_altq(a);
		break;
#endif
#ifdef ALTQ_HFSC
	case ALTQT_HFSC:
		error = hfsc_add_altq(a);
		break;
#endif
	default:
		error = ENXIO;
	}

	return (error);
}

/*
 * remove a discipline or a queue
 */
int
altq_remove(struct pf_altq *a)
{
	int error = 0;

	if (a->qname[0] != 0)
		return (altq_remove_queue(a));

	switch (a->scheduler) {
#ifdef ALTQ_CBQ
	case ALTQT_CBQ:
		error = cbq_remove_altq(a);
		break;
#endif
#ifdef ALTQ_PRIQ
	case ALTQT_PRIQ:
		error = priq_remove_altq(a);
		break;
#endif
#ifdef ALTQ_HFSC
	case ALTQT_HFSC:
		error = hfsc_remove_altq(a);
		break;
#endif
	default:
		error = ENXIO;
	}

	return (error);
}

/*
 * add a queue to the discipline
 */
int
altq_add_queue(struct pf_altq *a)
{
	int error = 0;

	switch (a->scheduler) {
#ifdef ALTQ_CBQ
	case ALTQT_CBQ:
		error = cbq_add_queue(a);
		break;
#endif
#ifdef ALTQ_PRIQ
	case ALTQT_PRIQ:
		error = priq_add_queue(a);
		break;
#endif
#ifdef ALTQ_HFSC
	case ALTQT_HFSC:
		error = hfsc_add_queue(a);
		break;
#endif
	default:
		error = ENXIO;
	}

	return (error);
}

/*
 * remove a queue from the discipline
 */
int
altq_remove_queue(struct pf_altq *a)
{
	int error = 0;

	switch (a->scheduler) {
#ifdef ALTQ_CBQ
	case ALTQT_CBQ:
		error = cbq_remove_queue(a);
		break;
#endif
#ifdef ALTQ_PRIQ
	case ALTQT_PRIQ:
		error = priq_remove_queue(a);
		break;
#endif
#ifdef ALTQ_HFSC
	case ALTQT_HFSC:
		error = hfsc_remove_queue(a);
		break;
#endif
	default:
		error = ENXIO;
	}

	return (error);
}

/*
 * get queue statistics
 */
int
altq_getqstats(struct pf_altq *a, void *ubuf, int *nbytes)
{
	int error = 0;

	switch (a->scheduler) {
#ifdef ALTQ_CBQ
	case ALTQT_CBQ:
		error = cbq_getqstats(a, ubuf, nbytes);
		break;
#endif
#ifdef ALTQ_PRIQ
	case ALTQT_PRIQ:
		error = priq_getqstats(a, ubuf, nbytes);
		break;
#endif
#ifdef ALTQ_HFSC
	case ALTQT_HFSC:
		error = hfsc_getqstats(a, ubuf, nbytes);
		break;
#endif
	default:
		error = ENXIO;
	}

	return (error);
}

/*
 * read and write diffserv field in IPv4 or IPv6 header
 */
uint8_t
read_dsfield(struct mbuf *m, struct altq_pktattr *pktattr)
{
	struct mbuf *m0;
	uint8_t ds_field = 0;

	if (pktattr == NULL ||
	    (pktattr->pattr_af != AF_INET && pktattr->pattr_af != AF_INET6))
		return ((uint8_t)0);

	/* verify that pattr_hdr is within the mbuf data */
	for (m0 = m; m0 != NULL; m0 = m0->m_next) {
		if ((pktattr->pattr_hdr >= m0->m_data) &&
		    (pktattr->pattr_hdr < m0->m_data + m0->m_len))
			break;
	}
	if (m0 == NULL) {
		/* ick, pattr_hdr is stale */
		pktattr->pattr_af = AF_UNSPEC;
#ifdef ALTQ_DEBUG
		printf("read_dsfield: can't locate header!\n");
#endif
		return ((uint8_t)0);
	}

	if (pktattr->pattr_af == AF_INET) {
		struct ip *ip = (struct ip *)pktattr->pattr_hdr;

		if (ip->ip_v != 4)
			return ((uint8_t)0);	/* version mismatch! */
		ds_field = ip->ip_tos;
	}
#ifdef INET6
	else if (pktattr->pattr_af == AF_INET6) {
		struct ip6_hdr *ip6 = (struct ip6_hdr *)pktattr->pattr_hdr;
		uint32_t flowlabel;

		flowlabel = ntohl(ip6->ip6_flow);
		if ((flowlabel >> 28) != 6)
			return ((uint8_t)0);	/* version mismatch! */
		ds_field = (flowlabel >> 20) & 0xff;
	}
#endif
	return (ds_field);
}

void
write_dsfield(struct mbuf *m, struct altq_pktattr *pktattr, uint8_t dsfield)
{
	struct mbuf *m0;

	if (pktattr == NULL ||
	    (pktattr->pattr_af != AF_INET && pktattr->pattr_af != AF_INET6))
		return;

	/* verify that pattr_hdr is within the mbuf data */
	for (m0 = m; m0 != NULL; m0 = m0->m_next) {
		if ((pktattr->pattr_hdr >= m0->m_data) &&
		    (pktattr->pattr_hdr < m0->m_data + m0->m_len))
			break;
	}
	if (m0 == NULL) {
		/* ick, pattr_hdr is stale */
		pktattr->pattr_af = AF_UNSPEC;
#ifdef ALTQ_DEBUG
		printf("write_dsfield: can't locate header!\n");
#endif
		return;
	}

	if (pktattr->pattr_af == AF_INET) {
		struct ip *ip = (struct ip *)pktattr->pattr_hdr;
		uint8_t old;
		int32_t sum;

		if (ip->ip_v != 4)
			return;		/* version mismatch! */
		old = ip->ip_tos;
		dsfield |= old & 3;	/* leave CU bits */
		if (old == dsfield)
			return;
		ip->ip_tos = dsfield;
		/*
		 * update checksum (from RFC1624)
		 *	   HC' = ~(~HC + ~m + m')
		 */
		sum = ~ntohs(ip->ip_sum) & 0xffff;
		sum += 0xff00 + (~old & 0xff) + dsfield;
		sum = (sum >> 16) + (sum & 0xffff);
		sum += (sum >> 16);  /* add carry */

		ip->ip_sum = htons(~sum & 0xffff);
	}
#ifdef INET6
	else if (pktattr->pattr_af == AF_INET6) {
		struct ip6_hdr *ip6 = (struct ip6_hdr *)pktattr->pattr_hdr;
		uint32_t flowlabel;

		flowlabel = ntohl(ip6->ip6_flow);
		if ((flowlabel >> 28) != 6)
			return;		/* version mismatch! */
		flowlabel = (flowlabel & 0xf03fffff) | (dsfield << 20);
		ip6->ip6_flow = htonl(flowlabel);
	}
#endif
}

/*
 * high resolution clock support taking advantage of a machine dependent
 * high resolution time counter (e.g., timestamp counter of intel pentium).
 * we assume
 *  - 64-bit-long monotonically-increasing counter
 *  - frequency range is 100M-4GHz (CPU speed)
 */
/* if pcc is not available or disabled, emulate 256MHz using microtime() */
#define	MACHCLK_SHIFT	8

int machclk_usepcc;
uint32_t machclk_freq = 0;
uint32_t machclk_per_tick = 0;

void
init_machclk(void)
{
	callout_init(&tbr_callout);

	machclk_usepcc = 1;

#if !defined(__i386__) || defined(ALTQ_NOPCC)
	machclk_usepcc = 0;
#elif defined(__DragonFly__) && defined(SMP)
	machclk_usepcc = 0;
#elif defined(__i386__)
	/* check if TSC is available */
	if (machclk_usepcc == 1 && (cpu_feature & CPUID_TSC) == 0)
		machclk_usepcc = 0;
#endif

	if (machclk_usepcc == 0) {
		/* emulate 256MHz using microtime() */
		machclk_freq = 1000000 << MACHCLK_SHIFT;
		machclk_per_tick = machclk_freq / hz;
#ifdef ALTQ_DEBUG
		printf("altq: emulate %uHz cpu clock\n", machclk_freq);
#endif
		return;
	}

	/*
	 * if the clock frequency (of Pentium TSC or Alpha PCC) is
	 * accessible, just use it.
	 */
#ifdef __i386__
	machclk_freq = tsc_freq;
#else
#error "machclk_freq interface not implemented"
#endif

	/*
	 * if we don't know the clock frequency, measure it.
	 */
	if (machclk_freq == 0) {
		static int	wait;
		struct timeval	tv_start, tv_end;
		uint64_t	start, end, diff;
		int		timo;

		microtime(&tv_start);
		start = read_machclk();
		timo = hz;	/* 1 sec */
		tsleep(&wait, PCATCH, "init_machclk", timo);
		microtime(&tv_end);
		end = read_machclk();
		diff = (uint64_t)(tv_end.tv_sec - tv_start.tv_sec) * 1000000
		    + tv_end.tv_usec - tv_start.tv_usec;
		if (diff != 0)
			machclk_freq = (u_int)((end - start) * 1000000 / diff);
	}

	machclk_per_tick = machclk_freq / hz;

#ifdef ALTQ_DEBUG
	printf("altq: CPU clock: %uHz\n", machclk_freq);
#endif
}

uint64_t
read_machclk(void)
{
	uint64_t val;

	if (machclk_usepcc) {
#if defined(__i386__)
		val = rdtsc();
#else
		panic("read_machclk");
#endif
	} else {
		struct timeval tv;

		microtime(&tv);
		val = (((uint64_t)(tv.tv_sec - boottime.tv_sec) * 1000000
		    + tv.tv_usec) << MACHCLK_SHIFT);
	}
	return (val);
}
