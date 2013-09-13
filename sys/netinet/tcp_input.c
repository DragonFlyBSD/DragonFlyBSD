/*
 * Copyright (c) 2002, 2003, 2004 Jeffrey M. Hsu.  All rights reserved.
 * Copyright (c) 2002, 2003, 2004 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Jeffrey M. Hsu.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993, 1994, 1995
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
 *	@(#)tcp_input.c	8.12 (Berkeley) 5/24/95
 * $FreeBSD: src/sys/netinet/tcp_input.c,v 1.107.2.38 2003/05/21 04:46:41 cjc Exp $
 */

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"
#include "opt_tcpdebug.h"
#include "opt_tcp_input.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>		/* for proc0 declaration */
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syslog.h>
#include <sys/in_cksum.h>

#include <sys/socketvar2.h>

#include <machine/cpu.h>	/* before tcp_seq.h, for tcp_random18() */
#include <machine/stdarg.h>

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>	/* for ICMP_BANDLIM */
#include <netinet/in_var.h>
#include <netinet/icmp_var.h>	/* for ICMP_BANDLIM */
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/nd6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_pcb.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_timer2.h>
#include <netinet/tcp_var.h>
#include <netinet6/tcp6_var.h>
#include <netinet/tcpip.h>

#ifdef TCPDEBUG
#include <netinet/tcp_debug.h>

u_char tcp_saveipgen[40];    /* the size must be of max ip header, now IPv6 */
struct tcphdr tcp_savetcp;
#endif

#ifdef FAST_IPSEC
#include <netproto/ipsec/ipsec.h>
#include <netproto/ipsec/ipsec6.h>
#endif

#ifdef IPSEC
#include <netinet6/ipsec.h>
#include <netinet6/ipsec6.h>
#include <netproto/key/key.h>
#endif

/*
 * Limit burst of new packets during SACK based fast recovery
 * or extended limited transmit.
 */
#define TCP_SACK_MAXBURST	4

MALLOC_DEFINE(M_TSEGQ, "tseg_qent", "TCP segment queue entry");

static int log_in_vain = 0;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, log_in_vain, CTLFLAG_RW,
    &log_in_vain, 0, "Log all incoming TCP connections");

static int blackhole = 0;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, blackhole, CTLFLAG_RW,
    &blackhole, 0, "Do not send RST when dropping refused connections");

int tcp_delack_enabled = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, delayed_ack, CTLFLAG_RW,
    &tcp_delack_enabled, 0,
    "Delay ACK to try and piggyback it onto a data packet");

#ifdef TCP_DROP_SYNFIN
static int drop_synfin = 0;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, drop_synfin, CTLFLAG_RW,
    &drop_synfin, 0, "Drop TCP packets with SYN+FIN set");
#endif

static int tcp_do_limitedtransmit = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, limitedtransmit, CTLFLAG_RW,
    &tcp_do_limitedtransmit, 0, "Enable RFC 3042 (Limited Transmit)");

static int tcp_do_early_retransmit = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, earlyretransmit, CTLFLAG_RW,
    &tcp_do_early_retransmit, 0, "Early retransmit");

int tcp_aggregate_acks = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, aggregate_acks, CTLFLAG_RW,
    &tcp_aggregate_acks, 0, "Aggregate built-up acks into one ack");

static int tcp_do_eifel_detect = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, eifel, CTLFLAG_RW,
    &tcp_do_eifel_detect, 0, "Eifel detection algorithm (RFC 3522)");

static int tcp_do_abc = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, abc, CTLFLAG_RW,
    &tcp_do_abc, 0,
    "TCP Appropriate Byte Counting (RFC 3465)");

/*
 * The following value actually takes range [25ms, 250ms],
 * given that most modern systems use 1ms ~ 10ms as the unit
 * of timestamp option.
 */
static u_int tcp_paws_tolerance = 25;
SYSCTL_UINT(_net_inet_tcp, OID_AUTO, paws_tolerance, CTLFLAG_RW,
    &tcp_paws_tolerance, 0, "RFC1323 PAWS tolerance");

/*
 * Define as tunable for easy testing with SACK on and off.
 * Warning:  do not change setting in the middle of an existing active TCP flow,
 *   else strange things might happen to that flow.
 */
int tcp_do_sack = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, sack, CTLFLAG_RW,
    &tcp_do_sack, 0, "Enable SACK Algorithms");

int tcp_do_smartsack = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, smartsack, CTLFLAG_RW,
    &tcp_do_smartsack, 0, "Enable Smart SACK Algorithms");

int tcp_do_rescuesack = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, rescuesack, CTLFLAG_RW,
    &tcp_do_rescuesack, 0, "Rescue retransmission for SACK");

int tcp_aggressive_rescuesack = 0;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, rescuesack_agg, CTLFLAG_RW,
    &tcp_aggressive_rescuesack, 0, "Aggressive rescue retransmission for SACK");

static int tcp_force_sackrxt = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, force_sackrxt, CTLFLAG_RW,
    &tcp_force_sackrxt, 0, "Allowed forced SACK retransmit burst");

int tcp_do_rfc6675 = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, rfc6675, CTLFLAG_RW,
    &tcp_do_rfc6675, 0, "Enable RFC6675");

int tcp_rfc6675_rxt = 0;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, rfc6675_rxt, CTLFLAG_RW,
    &tcp_rfc6675_rxt, 0, "Enable RFC6675 retransmit");

SYSCTL_NODE(_net_inet_tcp, OID_AUTO, reass, CTLFLAG_RW, 0,
    "TCP Segment Reassembly Queue");

int tcp_reass_maxseg = 0;
SYSCTL_INT(_net_inet_tcp_reass, OID_AUTO, maxsegments, CTLFLAG_RD,
    &tcp_reass_maxseg, 0,
    "Global maximum number of TCP Segments in Reassembly Queue");

int tcp_reass_qsize = 0;
SYSCTL_INT(_net_inet_tcp_reass, OID_AUTO, cursegments, CTLFLAG_RD,
    &tcp_reass_qsize, 0,
    "Global number of TCP Segments currently in Reassembly Queue");

static int tcp_reass_overflows = 0;
SYSCTL_INT(_net_inet_tcp_reass, OID_AUTO, overflows, CTLFLAG_RD,
    &tcp_reass_overflows, 0,
    "Global number of TCP Segment Reassembly Queue Overflows");

int tcp_do_autorcvbuf = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, recvbuf_auto, CTLFLAG_RW,
    &tcp_do_autorcvbuf, 0, "Enable automatic receive buffer sizing");

int tcp_autorcvbuf_inc = 16*1024;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, recvbuf_inc, CTLFLAG_RW,
    &tcp_autorcvbuf_inc, 0,
    "Incrementor step size of automatic receive buffer");

int tcp_autorcvbuf_max = 2*1024*1024;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, recvbuf_max, CTLFLAG_RW,
    &tcp_autorcvbuf_max, 0, "Max size of automatic receive buffer");

int tcp_sosend_agglim = 2;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, sosend_agglim, CTLFLAG_RW,
    &tcp_sosend_agglim, 0, "TCP sosend mbuf aggregation limit");

int tcp_sosend_async = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, sosend_async, CTLFLAG_RW,
    &tcp_sosend_async, 0, "TCP asynchronized pru_send");

int tcp_sosend_jcluster = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, sosend_jcluster, CTLFLAG_RW,
    &tcp_sosend_jcluster, 0, "TCP output uses jcluster");

static int tcp_ignore_redun_dsack = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, ignore_redun_dsack, CTLFLAG_RW,
    &tcp_ignore_redun_dsack, 0, "Ignore redundant DSACK");

static int tcp_reuseport_ext = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, reuseport_ext, CTLFLAG_RW,
    &tcp_reuseport_ext, 0, "SO_REUSEPORT extension");

static void	 tcp_dooptions(struct tcpopt *, u_char *, int, boolean_t,
		    tcp_seq);
static void	 tcp_pulloutofband(struct socket *,
		     struct tcphdr *, struct mbuf *, int);
static int	 tcp_reass(struct tcpcb *, struct tcphdr *, int *,
		     struct mbuf *);
static void	 tcp_xmit_timer(struct tcpcb *, int, tcp_seq);
static void	 tcp_newreno_partial_ack(struct tcpcb *, struct tcphdr *, int);
static void	 tcp_sack_rexmt(struct tcpcb *, boolean_t);
static boolean_t tcp_sack_limitedxmit(struct tcpcb *);
static int	 tcp_rmx_msl(const struct tcpcb *);
static void	 tcp_established(struct tcpcb *);
static boolean_t tcp_recv_dupack(struct tcpcb *, tcp_seq, u_int);

/* Neighbor Discovery, Neighbor Unreachability Detection Upper layer hint. */
#ifdef INET6
#define ND6_HINT(tp) \
do { \
	if ((tp) && (tp)->t_inpcb && \
	    ((tp)->t_inpcb->inp_vflag & INP_IPV6) && \
	    (tp)->t_inpcb->in6p_route.ro_rt) \
		nd6_nud_hint((tp)->t_inpcb->in6p_route.ro_rt, NULL, 0); \
} while (0)
#else
#define ND6_HINT(tp)
#endif

/*
 * Indicate whether this ack should be delayed.  We can delay the ack if
 *	- delayed acks are enabled and
 *	- there is no delayed ack timer in progress and
 *	- our last ack wasn't a 0-sized window.  We never want to delay
 *	  the ack that opens up a 0-sized window.
 */
#define DELAY_ACK(tp) \
	(tcp_delack_enabled && !tcp_callout_pending(tp, tp->tt_delack) && \
	!(tp->t_flags & TF_RXWIN0SENT))

#define acceptable_window_update(tp, th, tiwin)				\
    (SEQ_LT(tp->snd_wl1, th->th_seq) ||					\
     (tp->snd_wl1 == th->th_seq &&					\
      (SEQ_LT(tp->snd_wl2, th->th_ack) ||				\
       (tp->snd_wl2 == th->th_ack && tiwin > tp->snd_wnd))))

#define	iceildiv(n, d)		(((n)+(d)-1) / (d))
#define need_early_retransmit(tp, ownd) \
    (tcp_do_early_retransmit && \
     (tcp_do_eifel_detect && (tp->t_flags & TF_RCVD_TSTMP)) && \
     ownd < ((tp->t_rxtthresh + 1) * tp->t_maxseg) && \
     tp->t_dupacks + 1 >= iceildiv(ownd, tp->t_maxseg) && \
     (!TCP_DO_SACK(tp) || ownd <= tp->t_maxseg || \
      tcp_sack_has_sacked(&tp->scb, ownd - tp->t_maxseg)))

/*
 * Returns TRUE, if this segment can be merged with the last
 * pending segment in the reassemble queue and this segment
 * does not overlap with the pending segment immediately
 * preceeding the last pending segment.
 */
static __inline boolean_t
tcp_paws_canreasslast(const struct tcpcb *tp, const struct tcphdr *th, int tlen)
{
	const struct tseg_qent *last, *prev;

	last = TAILQ_LAST(&tp->t_segq, tsegqe_head);
	if (last == NULL)
		return FALSE;

	/* This segment comes immediately after the last pending segment */
	if (last->tqe_th->th_seq + last->tqe_len == th->th_seq) {
		if (last->tqe_th->th_flags & TH_FIN) {
			/* No segments should follow segment w/ FIN */
			return FALSE;
		}
		return TRUE;
	}

	if (th->th_seq + tlen != last->tqe_th->th_seq)
		return FALSE;
	/* This segment comes immediately before the last pending segment */

	prev = TAILQ_PREV(last, tsegqe_head, tqe_q);
	if (prev == NULL) {
		/*
		 * No pending preceeding segment, we assume this segment
		 * could be reassembled.
		 */
		return TRUE;
	}

	/* This segment does not overlap with the preceeding segment */
	if (SEQ_GEQ(th->th_seq, prev->tqe_th->th_seq + prev->tqe_len))
		return TRUE;

	return FALSE;
}

static __inline void
tcp_ncr_update_rxtthresh(struct tcpcb *tp)
{
	int old_rxtthresh = tp->t_rxtthresh;
	uint32_t ownd = tp->snd_max - tp->snd_una;

	tp->t_rxtthresh = max(tcprexmtthresh, ((ownd / tp->t_maxseg) >> 1));
	if (tp->t_rxtthresh != old_rxtthresh) {
		tcp_sack_update_lostseq(&tp->scb, tp->snd_una,
		    tp->t_maxseg, tp->t_rxtthresh);
	}
}

static int
tcp_reass(struct tcpcb *tp, struct tcphdr *th, int *tlenp, struct mbuf *m)
{
	struct tseg_qent *q;
	struct tseg_qent *p = NULL;
	struct tseg_qent *te;
	struct socket *so = tp->t_inpcb->inp_socket;
	int flags;

	/*
	 * Call with th == NULL after become established to
	 * force pre-ESTABLISHED data up to user socket.
	 */
	if (th == NULL)
		goto present;

	/*
	 * Limit the number of segments in the reassembly queue to prevent
	 * holding on to too many segments (and thus running out of mbufs).
	 * Make sure to let the missing segment through which caused this
	 * queue.  Always keep one global queue entry spare to be able to
	 * process the missing segment.
	 */
	if (th->th_seq != tp->rcv_nxt &&
	    tcp_reass_qsize + 1 >= tcp_reass_maxseg) {
		tcp_reass_overflows++;
		tcpstat.tcps_rcvmemdrop++;
		m_freem(m);
		/* no SACK block to report */
		tp->reportblk.rblk_start = tp->reportblk.rblk_end;
		return (0);
	}

	/* Allocate a new queue entry. */
	te = kmalloc(sizeof(struct tseg_qent), M_TSEGQ, M_INTWAIT | M_NULLOK);
	if (te == NULL) {
		tcpstat.tcps_rcvmemdrop++;
		m_freem(m);
		/* no SACK block to report */
		tp->reportblk.rblk_start = tp->reportblk.rblk_end;
		return (0);
	}
	atomic_add_int(&tcp_reass_qsize, 1);

	if (th->th_flags & TH_FIN)
		tp->t_flags |= TF_QUEDFIN;

	/*
	 * Find a segment which begins after this one does.
	 */
	TAILQ_FOREACH(q, &tp->t_segq, tqe_q) {
		if (SEQ_GT(q->tqe_th->th_seq, th->th_seq))
			break;
		p = q;
	}

	/*
	 * If there is a preceding segment, it may provide some of
	 * our data already.  If so, drop the data from the incoming
	 * segment.  If it provides all of our data, drop us.
	 */
	if (p != NULL) {
		tcp_seq_diff_t i;

		/* conversion to int (in i) handles seq wraparound */
		i = p->tqe_th->th_seq + p->tqe_len - th->th_seq;
		if (i > 0) {		/* overlaps preceding segment */
			tp->sack_flags |=
			    (TSACK_F_DUPSEG | TSACK_F_ENCLOSESEG);
			/* enclosing block starts w/ preceding segment */
			tp->encloseblk.rblk_start = p->tqe_th->th_seq;
			if (i >= *tlenp) {
				if (th->th_flags & TH_FIN)
					p->tqe_th->th_flags |= TH_FIN;

				/* preceding encloses incoming segment */
				tp->encloseblk.rblk_end = TCP_SACK_BLKEND(
				    p->tqe_th->th_seq + p->tqe_len,
				    p->tqe_th->th_flags);
				tcpstat.tcps_rcvduppack++;
				tcpstat.tcps_rcvdupbyte += *tlenp;
				m_freem(m);
				kfree(te, M_TSEGQ);
				atomic_add_int(&tcp_reass_qsize, -1);
				/*
				 * Try to present any queued data
				 * at the left window edge to the user.
				 * This is needed after the 3-WHS
				 * completes.
				 */
				goto present;	/* ??? */
			}
			m_adj(m, i);
			*tlenp -= i;
			th->th_seq += i;
			/* incoming segment end is enclosing block end */
			tp->encloseblk.rblk_end = TCP_SACK_BLKEND(
			    th->th_seq + *tlenp, th->th_flags);
			/* trim end of reported D-SACK block */
			tp->reportblk.rblk_end = th->th_seq;
		}
	}
	tcpstat.tcps_rcvoopack++;
	tcpstat.tcps_rcvoobyte += *tlenp;

	/*
	 * While we overlap succeeding segments trim them or,
	 * if they are completely covered, dequeue them.
	 */
	while (q) {
		tcp_seq_diff_t i = (th->th_seq + *tlenp) - q->tqe_th->th_seq;
		tcp_seq qend = q->tqe_th->th_seq + q->tqe_len;
		tcp_seq qend_sack = TCP_SACK_BLKEND(qend, q->tqe_th->th_flags);
		struct tseg_qent *nq;

		if (i <= 0)
			break;
		if (!(tp->sack_flags & TSACK_F_DUPSEG)) {
			/* first time through */
			tp->sack_flags |= (TSACK_F_DUPSEG | TSACK_F_ENCLOSESEG);
			tp->encloseblk = tp->reportblk;
			/* report trailing duplicate D-SACK segment */
			tp->reportblk.rblk_start = q->tqe_th->th_seq;
		}
		if ((tp->sack_flags & TSACK_F_ENCLOSESEG) &&
		    SEQ_GT(qend_sack, tp->encloseblk.rblk_end)) {
			/* extend enclosing block if one exists */
			tp->encloseblk.rblk_end = qend_sack;
		}
		if (i < q->tqe_len) {
			q->tqe_th->th_seq += i;
			q->tqe_len -= i;
			m_adj(q->tqe_m, i);
			break;
		}

		if (q->tqe_th->th_flags & TH_FIN)
			th->th_flags |= TH_FIN;

		nq = TAILQ_NEXT(q, tqe_q);
		TAILQ_REMOVE(&tp->t_segq, q, tqe_q);
		m_freem(q->tqe_m);
		kfree(q, M_TSEGQ);
		atomic_add_int(&tcp_reass_qsize, -1);
		q = nq;
	}

	/* Insert the new segment queue entry into place. */
	te->tqe_m = m;
	te->tqe_th = th;
	te->tqe_len = *tlenp;

	/* check if can coalesce with following segment */
	if (q != NULL && (th->th_seq + *tlenp == q->tqe_th->th_seq)) {
		tcp_seq tend_sack;

		te->tqe_len += q->tqe_len;
		if (q->tqe_th->th_flags & TH_FIN)
			te->tqe_th->th_flags |= TH_FIN;
		tend_sack = TCP_SACK_BLKEND(te->tqe_th->th_seq + te->tqe_len,
		    te->tqe_th->th_flags);

		m_cat(te->tqe_m, q->tqe_m);
		tp->encloseblk.rblk_end = tend_sack;
		/*
		 * When not reporting a duplicate segment, use
		 * the larger enclosing block as the SACK block.
		 */
		if (!(tp->sack_flags & TSACK_F_DUPSEG))
			tp->reportblk.rblk_end = tend_sack;
		TAILQ_REMOVE(&tp->t_segq, q, tqe_q);
		kfree(q, M_TSEGQ);
		atomic_add_int(&tcp_reass_qsize, -1);
	}

	if (p == NULL) {
		TAILQ_INSERT_HEAD(&tp->t_segq, te, tqe_q);
	} else {
		/* check if can coalesce with preceding segment */
		if (p->tqe_th->th_seq + p->tqe_len == th->th_seq) {
			if (te->tqe_th->th_flags & TH_FIN)
				p->tqe_th->th_flags |= TH_FIN;
			p->tqe_len += te->tqe_len;
			m_cat(p->tqe_m, te->tqe_m);
			tp->encloseblk.rblk_start = p->tqe_th->th_seq;
			/*
			 * When not reporting a duplicate segment, use
			 * the larger enclosing block as the SACK block.
			 */
			if (!(tp->sack_flags & TSACK_F_DUPSEG))
				tp->reportblk.rblk_start = p->tqe_th->th_seq;
			kfree(te, M_TSEGQ);
			atomic_add_int(&tcp_reass_qsize, -1);
		} else {
			TAILQ_INSERT_AFTER(&tp->t_segq, p, te, tqe_q);
		}
	}

present:
	/*
	 * Present data to user, advancing rcv_nxt through
	 * completed sequence space.
	 */
	if (!TCPS_HAVEESTABLISHED(tp->t_state))
		return (0);
	q = TAILQ_FIRST(&tp->t_segq);
	if (q == NULL || q->tqe_th->th_seq != tp->rcv_nxt)
		return (0);
	tp->rcv_nxt += q->tqe_len;
	if (!(tp->sack_flags & TSACK_F_DUPSEG))	{
		/* no SACK block to report since ACK advanced */
		tp->reportblk.rblk_start = tp->reportblk.rblk_end;
	}
	/* no enclosing block to report since ACK advanced */
	tp->sack_flags &= ~TSACK_F_ENCLOSESEG;
	flags = q->tqe_th->th_flags & TH_FIN;
	TAILQ_REMOVE(&tp->t_segq, q, tqe_q);
	KASSERT(TAILQ_EMPTY(&tp->t_segq) ||
		TAILQ_FIRST(&tp->t_segq)->tqe_th->th_seq != tp->rcv_nxt,
		("segment not coalesced"));
	if (so->so_state & SS_CANTRCVMORE) {
		m_freem(q->tqe_m);
	} else {
		lwkt_gettoken(&so->so_rcv.ssb_token);
		ssb_appendstream(&so->so_rcv, q->tqe_m);
		lwkt_reltoken(&so->so_rcv.ssb_token);
	}
	kfree(q, M_TSEGQ);
	atomic_add_int(&tcp_reass_qsize, -1);
	ND6_HINT(tp);
	sorwakeup(so);
	return (flags);
}

/*
 * TCP input routine, follows pages 65-76 of the
 * protocol specification dated September, 1981 very closely.
 */
#ifdef INET6
int
tcp6_input(struct mbuf **mp, int *offp, int proto)
{
	struct mbuf *m = *mp;
	struct in6_ifaddr *ia6;

	IP6_EXTHDR_CHECK(m, *offp, sizeof(struct tcphdr), IPPROTO_DONE);

	/*
	 * draft-itojun-ipv6-tcp-to-anycast
	 * better place to put this in?
	 */
	ia6 = ip6_getdstifaddr(m);
	if (ia6 && (ia6->ia6_flags & IN6_IFF_ANYCAST)) {
		icmp6_error(m, ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_ADDR,
			    offsetof(struct ip6_hdr, ip6_dst));
		return (IPPROTO_DONE);
	}

	tcp_input(mp, offp, proto);
	return (IPPROTO_DONE);
}
#endif

int
tcp_input(struct mbuf **mp, int *offp, int proto)
{
	int off0;
	struct tcphdr *th;
	struct ip *ip = NULL;
	struct ipovly *ipov;
	struct inpcb *inp = NULL;
	u_char *optp = NULL;
	int optlen = 0;
	int tlen, off;
	int len = 0;
	int drop_hdrlen;
	struct tcpcb *tp = NULL;
	int thflags;
	struct socket *so = NULL;
	int todrop, acked;
	boolean_t ourfinisacked, needoutput = FALSE, delayed_dupack = FALSE;
	tcp_seq th_dupack = 0; /* XXX gcc warning */
	u_int to_flags = 0; /* XXX gcc warning */
	u_long tiwin;
	int recvwin;
	struct tcpopt to;		/* options in this segment */
	struct sockaddr_in *next_hop = NULL;
	int rstreason; /* For badport_bandlim accounting purposes */
	int cpu;
	struct ip6_hdr *ip6 = NULL;
	struct mbuf *m;
#ifdef INET6
	boolean_t isipv6;
#else
	const boolean_t isipv6 = FALSE;
#endif
#ifdef TCPDEBUG
	short ostate = 0;
#endif

	off0 = *offp;
	m = *mp;
	*mp = NULL;

	tcpstat.tcps_rcvtotal++;

	if (m->m_pkthdr.fw_flags & IPFORWARD_MBUF_TAGGED) {
		struct m_tag *mtag;

		mtag = m_tag_find(m, PACKET_TAG_IPFORWARD, NULL);
		KKASSERT(mtag != NULL);
		next_hop = m_tag_data(mtag);
	}

#ifdef INET6
	isipv6 = (mtod(m, struct ip *)->ip_v == 6) ? TRUE : FALSE;
#endif

	if (isipv6) {
		/* IP6_EXTHDR_CHECK() is already done at tcp6_input() */
		ip6 = mtod(m, struct ip6_hdr *);
		tlen = (sizeof *ip6) + ntohs(ip6->ip6_plen) - off0;
		if (in6_cksum(m, IPPROTO_TCP, off0, tlen)) {
			tcpstat.tcps_rcvbadsum++;
			goto drop;
		}
		th = (struct tcphdr *)((caddr_t)ip6 + off0);

		/*
		 * Be proactive about unspecified IPv6 address in source.
		 * As we use all-zero to indicate unbounded/unconnected pcb,
		 * unspecified IPv6 address can be used to confuse us.
		 *
		 * Note that packets with unspecified IPv6 destination is
		 * already dropped in ip6_input.
		 */
		if (IN6_IS_ADDR_UNSPECIFIED(&ip6->ip6_src)) {
			/* XXX stat */
			goto drop;
		}
	} else {
		/*
		 * Get IP and TCP header together in first mbuf.
		 * Note: IP leaves IP header in first mbuf.
		 */
		if (off0 > sizeof(struct ip)) {
			ip_stripoptions(m);
			off0 = sizeof(struct ip);
		}
		/* already checked and pulled up in ip_demux() */
		KASSERT(m->m_len >= sizeof(struct tcpiphdr),
		    ("TCP header not in one mbuf: m->m_len %d", m->m_len));
		ip = mtod(m, struct ip *);
		ipov = (struct ipovly *)ip;
		th = (struct tcphdr *)((caddr_t)ip + off0);
		tlen = ip->ip_len;

		if (m->m_pkthdr.csum_flags & CSUM_DATA_VALID) {
			if (m->m_pkthdr.csum_flags & CSUM_PSEUDO_HDR)
				th->th_sum = m->m_pkthdr.csum_data;
			else
				th->th_sum = in_pseudo(ip->ip_src.s_addr,
						ip->ip_dst.s_addr,
						htonl(m->m_pkthdr.csum_data +
							ip->ip_len +
							IPPROTO_TCP));
			th->th_sum ^= 0xffff;
		} else {
			/*
			 * Checksum extended TCP header and data.
			 */
			len = sizeof(struct ip) + tlen;
			bzero(ipov->ih_x1, sizeof ipov->ih_x1);
			ipov->ih_len = (u_short)tlen;
			ipov->ih_len = htons(ipov->ih_len);
			th->th_sum = in_cksum(m, len);
		}
		if (th->th_sum) {
			tcpstat.tcps_rcvbadsum++;
			goto drop;
		}
#ifdef INET6
		/* Re-initialization for later version check */
		ip->ip_v = IPVERSION;
#endif
	}

	/*
	 * Check that TCP offset makes sense,
	 * pull out TCP options and adjust length.		XXX
	 */
	off = th->th_off << 2;
	/* already checked and pulled up in ip_demux() */
	KASSERT(off >= sizeof(struct tcphdr) && off <= tlen,
	    ("bad TCP data offset %d (tlen %d)", off, tlen));
	tlen -= off;	/* tlen is used instead of ti->ti_len */
	if (off > sizeof(struct tcphdr)) {
		if (isipv6) {
			IP6_EXTHDR_CHECK(m, off0, off, IPPROTO_DONE);
			ip6 = mtod(m, struct ip6_hdr *);
			th = (struct tcphdr *)((caddr_t)ip6 + off0);
		} else {
			/* already pulled up in ip_demux() */
			KASSERT(m->m_len >= sizeof(struct ip) + off,
			    ("TCP header and options not in one mbuf: "
			     "m_len %d, off %d", m->m_len, off));
		}
		optlen = off - sizeof(struct tcphdr);
		optp = (u_char *)(th + 1);
	}
	thflags = th->th_flags;

#ifdef TCP_DROP_SYNFIN
	/*
	 * If the drop_synfin option is enabled, drop all packets with
	 * both the SYN and FIN bits set. This prevents e.g. nmap from
	 * identifying the TCP/IP stack.
	 *
	 * This is a violation of the TCP specification.
	 */
	if (drop_synfin && (thflags & (TH_SYN | TH_FIN)) == (TH_SYN | TH_FIN))
		goto drop;
#endif

	/*
	 * Convert TCP protocol specific fields to host format.
	 */
	th->th_seq = ntohl(th->th_seq);
	th->th_ack = ntohl(th->th_ack);
	th->th_win = ntohs(th->th_win);
	th->th_urp = ntohs(th->th_urp);

	/*
	 * Delay dropping TCP, IP headers, IPv6 ext headers, and TCP options,
	 * until after ip6_savecontrol() is called and before other functions
	 * which don't want those proto headers.
	 * Because ip6_savecontrol() is going to parse the mbuf to
	 * search for data to be passed up to user-land, it wants mbuf
	 * parameters to be unchanged.
	 * XXX: the call of ip6_savecontrol() has been obsoleted based on
	 * latest version of the advanced API (20020110).
	 */
	drop_hdrlen = off0 + off;

	/*
	 * Locate pcb for segment.
	 */
findpcb:
	/* IPFIREWALL_FORWARD section */
	if (next_hop != NULL && !isipv6) {  /* IPv6 support is not there yet */
		/*
		 * Transparently forwarded. Pretend to be the destination.
		 * already got one like this?
		 */
		cpu = mycpu->gd_cpuid;
		inp = in_pcblookup_hash(&tcbinfo[cpu],
					ip->ip_src, th->th_sport,
					ip->ip_dst, th->th_dport,
					0, m->m_pkthdr.rcvif);
		if (!inp) {
			/*
			 * It's new.  Try to find the ambushing socket.
			 */

			/*
			 * The rest of the ipfw code stores the port in
			 * host order.  XXX
			 * (The IP address is still in network order.)
			 */
			in_port_t dport = next_hop->sin_port ?
						htons(next_hop->sin_port) :
						th->th_dport;

			cpu = tcp_addrcpu(ip->ip_src.s_addr, th->th_sport,
					  next_hop->sin_addr.s_addr, dport);
			inp = in_pcblookup_hash(&tcbinfo[cpu],
						ip->ip_src, th->th_sport,
						next_hop->sin_addr, dport,
						1, m->m_pkthdr.rcvif);
		}
	} else {
		if (isipv6) {
			inp = in6_pcblookup_hash(&tcbinfo[0],
						 &ip6->ip6_src, th->th_sport,
						 &ip6->ip6_dst, th->th_dport,
						 1, m->m_pkthdr.rcvif);
		} else {
			cpu = mycpu->gd_cpuid;
			inp = in_pcblookup_pkthash(&tcbinfo[cpu],
			    ip->ip_src, th->th_sport,
			    ip->ip_dst, th->th_dport,
			    1, m->m_pkthdr.rcvif,
			    tcp_reuseport_ext ? m : NULL);
		}
	}

	/*
	 * If the state is CLOSED (i.e., TCB does not exist) then
	 * all data in the incoming segment is discarded.
	 * If the TCB exists but is in CLOSED state, it is embryonic,
	 * but should either do a listen or a connect soon.
	 */
	if (inp == NULL) {
		if (log_in_vain) {
#ifdef INET6
			char dbuf[INET6_ADDRSTRLEN+2], sbuf[INET6_ADDRSTRLEN+2];
#else
			char dbuf[sizeof "aaa.bbb.ccc.ddd"];
			char sbuf[sizeof "aaa.bbb.ccc.ddd"];
#endif
			if (isipv6) {
				strcpy(dbuf, "[");
				strcat(dbuf, ip6_sprintf(&ip6->ip6_dst));
				strcat(dbuf, "]");
				strcpy(sbuf, "[");
				strcat(sbuf, ip6_sprintf(&ip6->ip6_src));
				strcat(sbuf, "]");
			} else {
				strcpy(dbuf, inet_ntoa(ip->ip_dst));
				strcpy(sbuf, inet_ntoa(ip->ip_src));
			}
			switch (log_in_vain) {
			case 1:
				if (!(thflags & TH_SYN))
					break;
			case 2:
				log(LOG_INFO,
				    "Connection attempt to TCP %s:%d "
				    "from %s:%d flags:0x%02x\n",
				    dbuf, ntohs(th->th_dport), sbuf,
				    ntohs(th->th_sport), thflags);
				break;
			default:
				break;
			}
		}
		if (blackhole) {
			switch (blackhole) {
			case 1:
				if (thflags & TH_SYN)
					goto drop;
				break;
			case 2:
				goto drop;
			default:
				goto drop;
			}
		}
		rstreason = BANDLIM_RST_CLOSEDPORT;
		goto dropwithreset;
	}

#ifdef IPSEC
	if (isipv6) {
		if (ipsec6_in_reject_so(m, inp->inp_socket)) {
			ipsec6stat.in_polvio++;
			goto drop;
		}
	} else {
		if (ipsec4_in_reject_so(m, inp->inp_socket)) {
			ipsecstat.in_polvio++;
			goto drop;
		}
	}
#endif
#ifdef FAST_IPSEC
	if (isipv6) {
		if (ipsec6_in_reject(m, inp))
			goto drop;
	} else {
		if (ipsec4_in_reject(m, inp))
			goto drop;
	}
#endif
	/* Check the minimum TTL for socket. */
#ifdef INET6
	if ((isipv6 ? ip6->ip6_hlim : ip->ip_ttl) < inp->inp_ip_minttl)
		goto drop;
#endif

	tp = intotcpcb(inp);
	if (tp == NULL) {
		rstreason = BANDLIM_RST_CLOSEDPORT;
		goto dropwithreset;
	}
	if (tp->t_state <= TCPS_CLOSED)
		goto drop;

	so = inp->inp_socket;

#ifdef TCPDEBUG
	if (so->so_options & SO_DEBUG) {
		ostate = tp->t_state;
		if (isipv6)
			bcopy(ip6, tcp_saveipgen, sizeof(*ip6));
		else
			bcopy(ip, tcp_saveipgen, sizeof(*ip));
		tcp_savetcp = *th;
	}
#endif

	bzero(&to, sizeof to);

	if (so->so_options & SO_ACCEPTCONN) {
		struct in_conninfo inc;

#ifdef INET6
		inc.inc_isipv6 = (isipv6 == TRUE);
#endif
		if (isipv6) {
			inc.inc6_faddr = ip6->ip6_src;
			inc.inc6_laddr = ip6->ip6_dst;
			inc.inc6_route.ro_rt = NULL;		/* XXX */
		} else {
			inc.inc_faddr = ip->ip_src;
			inc.inc_laddr = ip->ip_dst;
			inc.inc_route.ro_rt = NULL;		/* XXX */
		}
		inc.inc_fport = th->th_sport;
		inc.inc_lport = th->th_dport;

		/*
		 * If the state is LISTEN then ignore segment if it contains
		 * a RST.  If the segment contains an ACK then it is bad and
		 * send a RST.  If it does not contain a SYN then it is not
		 * interesting; drop it.
		 *
		 * If the state is SYN_RECEIVED (syncache) and seg contains
		 * an ACK, but not for our SYN/ACK, send a RST.  If the seg
		 * contains a RST, check the sequence number to see if it
		 * is a valid reset segment.
		 */
		if ((thflags & (TH_RST | TH_ACK | TH_SYN)) != TH_SYN) {
			if ((thflags & (TH_RST | TH_ACK | TH_SYN)) == TH_ACK) {
				if (!syncache_expand(&inc, th, &so, m)) {
					/*
					 * No syncache entry, or ACK was not
					 * for our SYN/ACK.  Send a RST.
					 */
					tcpstat.tcps_badsyn++;
					rstreason = BANDLIM_RST_OPENPORT;
					goto dropwithreset;
				}

				/*
				 * Could not complete 3-way handshake,
				 * connection is being closed down, and
				 * syncache will free mbuf.
				 */
				if (so == NULL)
					return(IPPROTO_DONE);

				/*
				 * We must be in the correct protocol thread
				 * for this connection.
				 */
				KKASSERT(so->so_port == &curthread->td_msgport);

				/*
				 * Socket is created in state SYN_RECEIVED.
				 * Continue processing segment.
				 */
				inp = so->so_pcb;
				tp = intotcpcb(inp);
				/*
				 * This is what would have happened in
				 * tcp_output() when the SYN,ACK was sent.
				 */
				tp->snd_up = tp->snd_una;
				tp->snd_max = tp->snd_nxt = tp->iss + 1;
				tp->last_ack_sent = tp->rcv_nxt;

				goto after_listen;
			}
			if (thflags & TH_RST) {
				syncache_chkrst(&inc, th);
				goto drop;
			}
			if (thflags & TH_ACK) {
				syncache_badack(&inc);
				tcpstat.tcps_badsyn++;
				rstreason = BANDLIM_RST_OPENPORT;
				goto dropwithreset;
			}
			goto drop;
		}

		/*
		 * Segment's flags are (SYN) or (SYN | FIN).
		 */
#ifdef INET6
		/*
		 * If deprecated address is forbidden,
		 * we do not accept SYN to deprecated interface
		 * address to prevent any new inbound connection from
		 * getting established.
		 * When we do not accept SYN, we send a TCP RST,
		 * with deprecated source address (instead of dropping
		 * it).  We compromise it as it is much better for peer
		 * to send a RST, and RST will be the final packet
		 * for the exchange.
		 *
		 * If we do not forbid deprecated addresses, we accept
		 * the SYN packet.  RFC2462 does not suggest dropping
		 * SYN in this case.
		 * If we decipher RFC2462 5.5.4, it says like this:
		 * 1. use of deprecated addr with existing
		 *    communication is okay - "SHOULD continue to be
		 *    used"
		 * 2. use of it with new communication:
		 *   (2a) "SHOULD NOT be used if alternate address
		 *	  with sufficient scope is available"
		 *   (2b) nothing mentioned otherwise.
		 * Here we fall into (2b) case as we have no choice in
		 * our source address selection - we must obey the peer.
		 *
		 * The wording in RFC2462 is confusing, and there are
		 * multiple description text for deprecated address
		 * handling - worse, they are not exactly the same.
		 * I believe 5.5.4 is the best one, so we follow 5.5.4.
		 */
		if (isipv6 && !ip6_use_deprecated) {
			struct in6_ifaddr *ia6;

			if ((ia6 = ip6_getdstifaddr(m)) &&
			    (ia6->ia6_flags & IN6_IFF_DEPRECATED)) {
				tp = NULL;
				rstreason = BANDLIM_RST_OPENPORT;
				goto dropwithreset;
			}
		}
#endif
		/*
		 * If it is from this socket, drop it, it must be forged.
		 * Don't bother responding if the destination was a broadcast.
		 */
		if (th->th_dport == th->th_sport) {
			if (isipv6) {
				if (IN6_ARE_ADDR_EQUAL(&ip6->ip6_dst,
						       &ip6->ip6_src))
					goto drop;
			} else {
				if (ip->ip_dst.s_addr == ip->ip_src.s_addr)
					goto drop;
			}
		}
		/*
		 * RFC1122 4.2.3.10, p. 104: discard bcast/mcast SYN
		 *
		 * Note that it is quite possible to receive unicast
		 * link-layer packets with a broadcast IP address. Use
		 * in_broadcast() to find them.
		 */
		if (m->m_flags & (M_BCAST | M_MCAST))
			goto drop;
		if (isipv6) {
			if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst) ||
			    IN6_IS_ADDR_MULTICAST(&ip6->ip6_src))
				goto drop;
		} else {
			if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr)) ||
			    IN_MULTICAST(ntohl(ip->ip_src.s_addr)) ||
			    ip->ip_src.s_addr == htonl(INADDR_BROADCAST) ||
			    in_broadcast(ip->ip_dst, m->m_pkthdr.rcvif))
				goto drop;
		}
		/*
		 * SYN appears to be valid; create compressed TCP state
		 * for syncache, or perform t/tcp connection.
		 */
		if (so->so_qlen <= so->so_qlimit) {
			tcp_dooptions(&to, optp, optlen, TRUE, th->th_ack);
			if (!syncache_add(&inc, &to, th, so, m))
				goto drop;

			/*
			 * Entry added to syncache, mbuf used to
			 * send SYN,ACK packet.
			 */
			return(IPPROTO_DONE);
		}
		goto drop;
	}

after_listen:
	/*
	 * Should not happen - syncache should pick up these connections.
	 *
	 * Once we are past handling listen sockets we must be in the
	 * correct protocol processing thread.
	 */
	KASSERT(tp->t_state != TCPS_LISTEN, ("tcp_input: TCPS_LISTEN state"));
	KKASSERT(so->so_port == &curthread->td_msgport);

	/* Unscale the window into a 32-bit value. */
	if (!(thflags & TH_SYN))
		tiwin = th->th_win << tp->snd_scale;
	else
		tiwin = th->th_win;

	/*
	 * This is the second part of the MSS DoS prevention code (after
	 * minmss on the sending side) and it deals with too many too small
	 * tcp packets in a too short timeframe (1 second).
	 *
	 * XXX Removed.  This code was crap.  It does not scale to network
	 *     speed, and default values break NFS.  Gone.
	 */
	/* REMOVED */

	/*
	 * Segment received on connection.
	 *
	 * Reset idle time and keep-alive timer.  Don't waste time if less
	 * then a second has elapsed.
	 */
	if ((int)(ticks - tp->t_rcvtime) > hz)
		tcp_timer_keep_activity(tp, thflags);

	/*
	 * Process options.
	 * XXX this is tradtitional behavior, may need to be cleaned up.
	 */
	tcp_dooptions(&to, optp, optlen, (thflags & TH_SYN) != 0, th->th_ack);
	if (tp->t_state == TCPS_SYN_SENT && (thflags & TH_SYN)) {
		if ((to.to_flags & TOF_SCALE) && (tp->t_flags & TF_REQ_SCALE)) {
			tp->t_flags |= TF_RCVD_SCALE;
			tp->snd_scale = to.to_requested_s_scale;
		}

		/*
		 * Initial send window; will be updated upon next ACK
		 */
		tp->snd_wnd = th->th_win;

		if (to.to_flags & TOF_TS) {
			tp->t_flags |= TF_RCVD_TSTMP;
			tp->ts_recent = to.to_tsval;
			tp->ts_recent_age = ticks;
		}
		if (!(to.to_flags & TOF_MSS))
			to.to_mss = 0;
		tcp_mss(tp, to.to_mss);
		/*
		 * Only set the TF_SACK_PERMITTED per-connection flag
		 * if we got a SACK_PERMITTED option from the other side
		 * and the global tcp_do_sack variable is true.
		 */
		if (tcp_do_sack && (to.to_flags & TOF_SACK_PERMITTED))
			tp->t_flags |= TF_SACK_PERMITTED;
	}

	/*
	 * Header prediction: check for the two common cases
	 * of a uni-directional data xfer.  If the packet has
	 * no control flags, is in-sequence, the window didn't
	 * change and we're not retransmitting, it's a
	 * candidate.  If the length is zero and the ack moved
	 * forward, we're the sender side of the xfer.  Just
	 * free the data acked & wake any higher level process
	 * that was blocked waiting for space.  If the length
	 * is non-zero and the ack didn't move, we're the
	 * receiver side.  If we're getting packets in-order
	 * (the reassembly queue is empty), add the data to
	 * the socket buffer and note that we need a delayed ack.
	 * Make sure that the hidden state-flags are also off.
	 * Since we check for TCPS_ESTABLISHED above, it can only
	 * be TH_NEEDSYN.
	 */
	if (tp->t_state == TCPS_ESTABLISHED &&
	    (thflags & (TH_SYN|TH_FIN|TH_RST|TH_URG|TH_ACK)) == TH_ACK &&
	    !(tp->t_flags & (TF_NEEDSYN | TF_NEEDFIN)) &&
	    (!(to.to_flags & TOF_TS) ||
	     TSTMP_GEQ(to.to_tsval, tp->ts_recent)) &&
	    th->th_seq == tp->rcv_nxt &&
	    tp->snd_nxt == tp->snd_max) {

		/*
		 * If last ACK falls within this segment's sequence numbers,
		 * record the timestamp.
		 * NOTE that the test is modified according to the latest
		 * proposal of the tcplw@cray.com list (Braden 1993/04/26).
		 */
		if ((to.to_flags & TOF_TS) &&
		    SEQ_LEQ(th->th_seq, tp->last_ack_sent)) {
			tp->ts_recent_age = ticks;
			tp->ts_recent = to.to_tsval;
		}

		if (tlen == 0) {
			if (SEQ_GT(th->th_ack, tp->snd_una) &&
			    SEQ_LEQ(th->th_ack, tp->snd_max) &&
			    tp->snd_cwnd >= tp->snd_wnd &&
			    !IN_FASTRECOVERY(tp)) {
				/*
				 * This is a pure ack for outstanding data.
				 */
				++tcpstat.tcps_predack;
				/*
				 * "bad retransmit" recovery
				 *
				 * If Eifel detection applies, then
				 * it is deterministic, so use it
				 * unconditionally over the old heuristic.
				 * Otherwise, fall back to the old heuristic.
				 */
				if (tcp_do_eifel_detect &&
				    (to.to_flags & TOF_TS) && to.to_tsecr &&
				    (tp->rxt_flags & TRXT_F_FIRSTACCACK)) {
					/* Eifel detection applicable. */
					if (to.to_tsecr < tp->t_rexmtTS) {
						tcp_revert_congestion_state(tp);
						++tcpstat.tcps_eifeldetected;
						if (tp->t_rxtshift != 1 ||
						    ticks >= tp->t_badrxtwin)
							++tcpstat.tcps_rttcantdetect;
					}
				} else if (tp->t_rxtshift == 1 &&
					   ticks < tp->t_badrxtwin) {
					tcp_revert_congestion_state(tp);
					++tcpstat.tcps_rttdetected;
				}
				tp->rxt_flags &= ~(TRXT_F_FIRSTACCACK |
				    TRXT_F_FASTREXMT | TRXT_F_EARLYREXMT);
				/*
				 * Recalculate the retransmit timer / rtt.
				 *
				 * Some machines (certain windows boxes)
				 * send broken timestamp replies during the
				 * SYN+ACK phase, ignore timestamps of 0.
				 */
				if ((to.to_flags & TOF_TS) && to.to_tsecr) {
					tcp_xmit_timer(tp,
					    ticks - to.to_tsecr + 1,
					    th->th_ack);
				} else if (tp->t_rtttime &&
					   SEQ_GT(th->th_ack, tp->t_rtseq)) {
					tcp_xmit_timer(tp,
					    ticks - tp->t_rtttime,
					    th->th_ack);
				}
				tcp_xmit_bandwidth_limit(tp, th->th_ack);
				acked = th->th_ack - tp->snd_una;
				tcpstat.tcps_rcvackpack++;
				tcpstat.tcps_rcvackbyte += acked;
				sbdrop(&so->so_snd.sb, acked);
				tp->snd_recover = th->th_ack - 1;
				tp->snd_una = th->th_ack;
				tp->t_dupacks = 0;
				/*
				 * Update window information.
				 */
				if (tiwin != tp->snd_wnd &&
				    acceptable_window_update(tp, th, tiwin)) {
					/* keep track of pure window updates */
					if (tp->snd_wl2 == th->th_ack &&
					    tiwin > tp->snd_wnd)
						tcpstat.tcps_rcvwinupd++;
					tp->snd_wnd = tiwin;
					tp->snd_wl1 = th->th_seq;
					tp->snd_wl2 = th->th_ack;
					if (tp->snd_wnd > tp->max_sndwnd)
						tp->max_sndwnd = tp->snd_wnd;
				}
				m_freem(m);
				ND6_HINT(tp); /* some progress has been done */
				/*
				 * If all outstanding data are acked, stop
				 * retransmit timer, otherwise restart timer
				 * using current (possibly backed-off) value.
				 * If process is waiting for space,
				 * wakeup/selwakeup/signal.  If data
				 * are ready to send, let tcp_output
				 * decide between more output or persist.
				 */
				if (tp->snd_una == tp->snd_max) {
					tcp_callout_stop(tp, tp->tt_rexmt);
				} else if (!tcp_callout_active(tp,
					    tp->tt_persist)) {
					tcp_callout_reset(tp, tp->tt_rexmt,
					    tp->t_rxtcur, tcp_timer_rexmt);
				}
				sowwakeup(so);
				if (so->so_snd.ssb_cc > 0 &&
				    !tcp_output_pending(tp))
					tcp_output_fair(tp);
				return(IPPROTO_DONE);
			}
		} else if (tiwin == tp->snd_wnd &&
		    th->th_ack == tp->snd_una &&
		    TAILQ_EMPTY(&tp->t_segq) &&
		    tlen <= ssb_space(&so->so_rcv)) {
			u_long newsize = 0;	/* automatic sockbuf scaling */
			/*
			 * This is a pure, in-sequence data packet
			 * with nothing on the reassembly queue and
			 * we have enough buffer space to take it.
			 */
			++tcpstat.tcps_preddat;
			tp->rcv_nxt += tlen;
			tcpstat.tcps_rcvpack++;
			tcpstat.tcps_rcvbyte += tlen;
			ND6_HINT(tp);	/* some progress has been done */
		/*
		 * Automatic sizing of receive socket buffer.  Often the send
		 * buffer size is not optimally adjusted to the actual network
		 * conditions at hand (delay bandwidth product).  Setting the
		 * buffer size too small limits throughput on links with high
		 * bandwidth and high delay (eg. trans-continental/oceanic links).
		 *
		 * On the receive side the socket buffer memory is only rarely
		 * used to any significant extent.  This allows us to be much
		 * more aggressive in scaling the receive socket buffer.  For
		 * the case that the buffer space is actually used to a large
		 * extent and we run out of kernel memory we can simply drop
		 * the new segments; TCP on the sender will just retransmit it
		 * later.  Setting the buffer size too big may only consume too
		 * much kernel memory if the application doesn't read() from
		 * the socket or packet loss or reordering makes use of the
		 * reassembly queue.
		 *
		 * The criteria to step up the receive buffer one notch are:
		 *  1. the number of bytes received during the time it takes
		 *     one timestamp to be reflected back to us (the RTT);
		 *  2. received bytes per RTT is within seven eighth of the
		 *     current socket buffer size;
		 *  3. receive buffer size has not hit maximal automatic size;
		 *
		 * This algorithm does one step per RTT at most and only if
		 * we receive a bulk stream w/o packet losses or reorderings.
		 * Shrinking the buffer during idle times is not necessary as
		 * it doesn't consume any memory when idle.
		 *
		 * TODO: Only step up if the application is actually serving
		 * the buffer to better manage the socket buffer resources.
		 */
			if (tcp_do_autorcvbuf &&
			    to.to_tsecr &&
			    (so->so_rcv.ssb_flags & SSB_AUTOSIZE)) {
				if (to.to_tsecr > tp->rfbuf_ts &&
				    to.to_tsecr - tp->rfbuf_ts < hz) {
					if (tp->rfbuf_cnt >
					    (so->so_rcv.ssb_hiwat / 8 * 7) &&
					    so->so_rcv.ssb_hiwat <
					    tcp_autorcvbuf_max) {
						newsize =
						    ulmin(so->so_rcv.ssb_hiwat +
							  tcp_autorcvbuf_inc,
							  tcp_autorcvbuf_max);
					}
					/* Start over with next RTT. */
					tp->rfbuf_ts = 0;
					tp->rfbuf_cnt = 0;
				} else
					tp->rfbuf_cnt += tlen;	/* add up */
			}
			/*
			 * Add data to socket buffer.
			 */
			if (so->so_state & SS_CANTRCVMORE) {
				m_freem(m);
			} else {
				/*
				 * Set new socket buffer size, give up when
				 * limit is reached.
				 *
				 * Adjusting the size can mess up ACK
				 * sequencing when pure window updates are
				 * being avoided (which is the default),
				 * so force an ack.
				 */
				lwkt_gettoken(&so->so_rcv.ssb_token);
				if (newsize) {
					tp->t_flags |= TF_RXRESIZED;
					if (!ssb_reserve(&so->so_rcv, newsize,
							 so, NULL)) {
						atomic_clear_int(&so->so_rcv.ssb_flags, SSB_AUTOSIZE);
					}
					if (newsize >=
					    (TCP_MAXWIN << tp->rcv_scale)) {
						atomic_clear_int(&so->so_rcv.ssb_flags, SSB_AUTOSIZE);
					}
				}
				m_adj(m, drop_hdrlen); /* delayed header drop */
				ssb_appendstream(&so->so_rcv, m);
				lwkt_reltoken(&so->so_rcv.ssb_token);
			}
			sorwakeup(so);
			/*
			 * This code is responsible for most of the ACKs
			 * the TCP stack sends back after receiving a data
			 * packet.  Note that the DELAY_ACK check fails if
			 * the delack timer is already running, which results
			 * in an ack being sent every other packet (which is
			 * what we want).
			 *
			 * We then further aggregate acks by not actually
			 * sending one until the protocol thread has completed
			 * processing the current backlog of packets.  This
			 * does not delay the ack any further, but allows us
			 * to take advantage of the packet aggregation that
			 * high speed NICs do (usually blocks of 8-10 packets)
			 * to send a single ack rather then four or five acks,
			 * greatly reducing the ack rate, the return channel
			 * bandwidth, and the protocol overhead on both ends.
			 *
			 * Since this also has the effect of slowing down
			 * the exponential slow-start ramp-up, systems with 
			 * very large bandwidth-delay products might want
			 * to turn the feature off.
			 */
			if (DELAY_ACK(tp)) {
				tcp_callout_reset(tp, tp->tt_delack,
				    tcp_delacktime, tcp_timer_delack);
			} else if (tcp_aggregate_acks) {
				tp->t_flags |= TF_ACKNOW;
				if (!(tp->t_flags & TF_ONOUTPUTQ)) {
					tp->t_flags |= TF_ONOUTPUTQ;
					tp->tt_cpu = mycpu->gd_cpuid;
					TAILQ_INSERT_TAIL(
					    &tcpcbackq[tp->tt_cpu],
					    tp, t_outputq);
				}
			} else {
				tp->t_flags |= TF_ACKNOW;
				tcp_output(tp);
			}
			return(IPPROTO_DONE);
		}
	}

	/*
	 * Calculate amount of space in receive window,
	 * and then do TCP input processing.
	 * Receive window is amount of space in rcv queue,
	 * but not less than advertised window.
	 */
	recvwin = ssb_space(&so->so_rcv);
	if (recvwin < 0)
		recvwin = 0;
	tp->rcv_wnd = imax(recvwin, (int)(tp->rcv_adv - tp->rcv_nxt));

	/* Reset receive buffer auto scaling when not in bulk receive mode. */
	tp->rfbuf_ts = 0;
	tp->rfbuf_cnt = 0;

	switch (tp->t_state) {
	/*
	 * If the state is SYN_RECEIVED:
	 *	if seg contains an ACK, but not for our SYN/ACK, send a RST.
	 */
	case TCPS_SYN_RECEIVED:
		if ((thflags & TH_ACK) &&
		    (SEQ_LEQ(th->th_ack, tp->snd_una) ||
		     SEQ_GT(th->th_ack, tp->snd_max))) {
				rstreason = BANDLIM_RST_OPENPORT;
				goto dropwithreset;
		}
		break;

	/*
	 * If the state is SYN_SENT:
	 *	if seg contains an ACK, but not for our SYN, drop the input.
	 *	if seg contains a RST, then drop the connection.
	 *	if seg does not contain SYN, then drop it.
	 * Otherwise this is an acceptable SYN segment
	 *	initialize tp->rcv_nxt and tp->irs
	 *	if seg contains ack then advance tp->snd_una
	 *	if SYN has been acked change to ESTABLISHED else SYN_RCVD state
	 *	arrange for segment to be acked (eventually)
	 *	continue processing rest of data/controls, beginning with URG
	 */
	case TCPS_SYN_SENT:
		if ((thflags & TH_ACK) &&
		    (SEQ_LEQ(th->th_ack, tp->iss) ||
		     SEQ_GT(th->th_ack, tp->snd_max))) {
			rstreason = BANDLIM_UNLIMITED;
			goto dropwithreset;
		}
		if (thflags & TH_RST) {
			if (thflags & TH_ACK)
				tp = tcp_drop(tp, ECONNREFUSED);
			goto drop;
		}
		if (!(thflags & TH_SYN))
			goto drop;

		tp->irs = th->th_seq;
		tcp_rcvseqinit(tp);
		if (thflags & TH_ACK) {
			/* Our SYN was acked. */
			tcpstat.tcps_connects++;
			soisconnected(so);
			/* Do window scaling on this connection? */
			if ((tp->t_flags & (TF_RCVD_SCALE | TF_REQ_SCALE)) ==
			    (TF_RCVD_SCALE | TF_REQ_SCALE))
				tp->rcv_scale = tp->request_r_scale;
			tp->rcv_adv += tp->rcv_wnd;
			tp->snd_una++;		/* SYN is acked */
			tcp_callout_stop(tp, tp->tt_rexmt);
			/*
			 * If there's data, delay ACK; if there's also a FIN
			 * ACKNOW will be turned on later.
			 */
			if (DELAY_ACK(tp) && tlen != 0) {
				tcp_callout_reset(tp, tp->tt_delack,
				    tcp_delacktime, tcp_timer_delack);
			} else {
				tp->t_flags |= TF_ACKNOW;
			}
			/*
			 * Received <SYN,ACK> in SYN_SENT[*] state.
			 * Transitions:
			 *	SYN_SENT  --> ESTABLISHED
			 *	SYN_SENT* --> FIN_WAIT_1
			 */
			tp->t_starttime = ticks;
			if (tp->t_flags & TF_NEEDFIN) {
				tp->t_state = TCPS_FIN_WAIT_1;
				tp->t_flags &= ~TF_NEEDFIN;
				thflags &= ~TH_SYN;
			} else {
				tcp_established(tp);
			}
		} else {
			/*
			 * Received initial SYN in SYN-SENT[*] state =>
			 * simultaneous open.
			 * Do 3-way handshake:
			 *	  SYN-SENT -> SYN-RECEIVED
			 *	  SYN-SENT* -> SYN-RECEIVED*
			 */
			tp->t_flags |= TF_ACKNOW;
			tcp_callout_stop(tp, tp->tt_rexmt);
			tp->t_state = TCPS_SYN_RECEIVED;
		}

		/*
		 * Advance th->th_seq to correspond to first data byte.
		 * If data, trim to stay within window,
		 * dropping FIN if necessary.
		 */
		th->th_seq++;
		if (tlen > tp->rcv_wnd) {
			todrop = tlen - tp->rcv_wnd;
			m_adj(m, -todrop);
			tlen = tp->rcv_wnd;
			thflags &= ~TH_FIN;
			tcpstat.tcps_rcvpackafterwin++;
			tcpstat.tcps_rcvbyteafterwin += todrop;
		}
		tp->snd_wl1 = th->th_seq - 1;
		tp->rcv_up = th->th_seq;
		/*
		 * Client side of transaction: already sent SYN and data.
		 * If the remote host used T/TCP to validate the SYN,
		 * our data will be ACK'd; if so, enter normal data segment
		 * processing in the middle of step 5, ack processing.
		 * Otherwise, goto step 6.
		 */
		if (thflags & TH_ACK)
			goto process_ACK;

		goto step6;

	/*
	 * If the state is LAST_ACK or CLOSING or TIME_WAIT:
	 *	do normal processing (we no longer bother with T/TCP).
	 */
	case TCPS_LAST_ACK:
	case TCPS_CLOSING:
	case TCPS_TIME_WAIT:
		break;  /* continue normal processing */
	}

	/*
	 * States other than LISTEN or SYN_SENT.
	 * First check the RST flag and sequence number since reset segments
	 * are exempt from the timestamp and connection count tests.  This
	 * fixes a bug introduced by the Stevens, vol. 2, p. 960 bugfix
	 * below which allowed reset segments in half the sequence space
	 * to fall though and be processed (which gives forged reset
	 * segments with a random sequence number a 50 percent chance of
	 * killing a connection).
	 * Then check timestamp, if present.
	 * Then check the connection count, if present.
	 * Then check that at least some bytes of segment are within
	 * receive window.  If segment begins before rcv_nxt,
	 * drop leading data (and SYN); if nothing left, just ack.
	 *
	 *
	 * If the RST bit is set, check the sequence number to see
	 * if this is a valid reset segment.
	 * RFC 793 page 37:
	 *   In all states except SYN-SENT, all reset (RST) segments
	 *   are validated by checking their SEQ-fields.  A reset is
	 *   valid if its sequence number is in the window.
	 * Note: this does not take into account delayed ACKs, so
	 *   we should test against last_ack_sent instead of rcv_nxt.
	 *   The sequence number in the reset segment is normally an
	 *   echo of our outgoing acknowledgement numbers, but some hosts
	 *   send a reset with the sequence number at the rightmost edge
	 *   of our receive window, and we have to handle this case.
	 * If we have multiple segments in flight, the intial reset
	 * segment sequence numbers will be to the left of last_ack_sent,
	 * but they will eventually catch up.
	 * In any case, it never made sense to trim reset segments to
	 * fit the receive window since RFC 1122 says:
	 *   4.2.2.12  RST Segment: RFC-793 Section 3.4
	 *
	 *    A TCP SHOULD allow a received RST segment to include data.
	 *
	 *    DISCUSSION
	 *	   It has been suggested that a RST segment could contain
	 *	   ASCII text that encoded and explained the cause of the
	 *	   RST.  No standard has yet been established for such
	 *	   data.
	 *
	 * If the reset segment passes the sequence number test examine
	 * the state:
	 *    SYN_RECEIVED STATE:
	 *	If passive open, return to LISTEN state.
	 *	If active open, inform user that connection was refused.
	 *    ESTABLISHED, FIN_WAIT_1, FIN_WAIT_2, CLOSE_WAIT STATES:
	 *	Inform user that connection was reset, and close tcb.
	 *    CLOSING, LAST_ACK STATES:
	 *	Close the tcb.
	 *    TIME_WAIT STATE:
	 *	Drop the segment - see Stevens, vol. 2, p. 964 and
	 *	RFC 1337.
	 */
	if (thflags & TH_RST) {
		if (SEQ_GEQ(th->th_seq, tp->last_ack_sent) &&
		    SEQ_LEQ(th->th_seq, tp->last_ack_sent + tp->rcv_wnd)) {
			switch (tp->t_state) {

			case TCPS_SYN_RECEIVED:
				so->so_error = ECONNREFUSED;
				goto close;

			case TCPS_ESTABLISHED:
			case TCPS_FIN_WAIT_1:
			case TCPS_FIN_WAIT_2:
			case TCPS_CLOSE_WAIT:
				so->so_error = ECONNRESET;
			close:
				tp->t_state = TCPS_CLOSED;
				tcpstat.tcps_drops++;
				tp = tcp_close(tp);
				break;

			case TCPS_CLOSING:
			case TCPS_LAST_ACK:
				tp = tcp_close(tp);
				break;

			case TCPS_TIME_WAIT:
				break;
			}
		}
		goto drop;
	}

	/*
	 * RFC 1323 PAWS: If we have a timestamp reply on this segment
	 * and it's less than ts_recent, drop it.
	 */
	if ((to.to_flags & TOF_TS) && tp->ts_recent != 0 &&
	    TSTMP_LT(to.to_tsval, tp->ts_recent)) {
		/* Check to see if ts_recent is over 24 days old.  */
		if ((int)(ticks - tp->ts_recent_age) > TCP_PAWS_IDLE) {
			/*
			 * Invalidate ts_recent.  If this segment updates
			 * ts_recent, the age will be reset later and ts_recent
			 * will get a valid value.  If it does not, setting
			 * ts_recent to zero will at least satisfy the
			 * requirement that zero be placed in the timestamp
			 * echo reply when ts_recent isn't valid.  The
			 * age isn't reset until we get a valid ts_recent
			 * because we don't want out-of-order segments to be
			 * dropped when ts_recent is old.
			 */
			tp->ts_recent = 0;
		} else if (tcp_paws_tolerance && tlen != 0 &&
		    tp->t_state == TCPS_ESTABLISHED &&
		    (thflags & (TH_SYN|TH_FIN|TH_RST|TH_URG|TH_ACK)) == TH_ACK&&
		    !(tp->t_flags & (TF_NEEDSYN | TF_NEEDFIN)) &&
		    th->th_ack == tp->snd_una &&
		    tiwin == tp->snd_wnd &&
		    TSTMP_GEQ(to.to_tsval + tcp_paws_tolerance, tp->ts_recent)&&
		    (th->th_seq == tp->rcv_nxt ||
		     (SEQ_GT(th->th_seq, tp->rcv_nxt) && 
		      tcp_paws_canreasslast(tp, th, tlen)))) {
			/*
			 * This tends to prevent valid new segments from being
			 * dropped by the reordered segments sent by the fast
			 * retransmission algorithm on the sending side, i.e.
			 * the fast retransmitted segment w/ larger timestamp
			 * arrives earlier than the previously sent new segments
			 * w/ smaller timestamp.
			 *
			 * If following conditions are met, the segment is
			 * accepted:
			 * - The segment contains data
			 * - The connection is established
			 * - The header does not contain important flags
			 * - SYN or FIN is not needed
			 * - It does not acknowledge new data
			 * - Receive window is not changed
			 * - The timestamp is within "acceptable" range
			 * - The new segment is what we are expecting or
			 *   the new segment could be merged w/ the last
			 *   pending segment on the reassemble queue
			 */
			tcpstat.tcps_pawsaccept++;
			tcpstat.tcps_pawsdrop++;
		} else {
			tcpstat.tcps_rcvduppack++;
			tcpstat.tcps_rcvdupbyte += tlen;
			tcpstat.tcps_pawsdrop++;
			if (tlen)
				goto dropafterack;
			goto drop;
		}
	}

	/*
	 * In the SYN-RECEIVED state, validate that the packet belongs to
	 * this connection before trimming the data to fit the receive
	 * window.  Check the sequence number versus IRS since we know
	 * the sequence numbers haven't wrapped.  This is a partial fix
	 * for the "LAND" DoS attack.
	 */
	if (tp->t_state == TCPS_SYN_RECEIVED && SEQ_LT(th->th_seq, tp->irs)) {
		rstreason = BANDLIM_RST_OPENPORT;
		goto dropwithreset;
	}

	todrop = tp->rcv_nxt - th->th_seq;
	if (todrop > 0) {
		if (TCP_DO_SACK(tp)) {
			/* Report duplicate segment at head of packet. */
			tp->reportblk.rblk_start = th->th_seq;
			tp->reportblk.rblk_end = TCP_SACK_BLKEND(
			    th->th_seq + tlen, thflags);
			if (SEQ_GT(tp->reportblk.rblk_end, tp->rcv_nxt))
				tp->reportblk.rblk_end = tp->rcv_nxt;
			tp->sack_flags |= (TSACK_F_DUPSEG | TSACK_F_SACKLEFT);
			tp->t_flags |= TF_ACKNOW;
		}
		if (thflags & TH_SYN) {
			thflags &= ~TH_SYN;
			th->th_seq++;
			if (th->th_urp > 1)
				th->th_urp--;
			else
				thflags &= ~TH_URG;
			todrop--;
		}
		/*
		 * Following if statement from Stevens, vol. 2, p. 960.
		 */
		if (todrop > tlen ||
		    (todrop == tlen && !(thflags & TH_FIN))) {
			/*
			 * Any valid FIN must be to the left of the window.
			 * At this point the FIN must be a duplicate or out
			 * of sequence; drop it.
			 */
			thflags &= ~TH_FIN;

			/*
			 * Send an ACK to resynchronize and drop any data.
			 * But keep on processing for RST or ACK.
			 */
			tp->t_flags |= TF_ACKNOW;
			todrop = tlen;
			tcpstat.tcps_rcvduppack++;
			tcpstat.tcps_rcvdupbyte += todrop;
		} else {
			tcpstat.tcps_rcvpartduppack++;
			tcpstat.tcps_rcvpartdupbyte += todrop;
		}
		drop_hdrlen += todrop;	/* drop from the top afterwards */
		th->th_seq += todrop;
		tlen -= todrop;
		if (th->th_urp > todrop)
			th->th_urp -= todrop;
		else {
			thflags &= ~TH_URG;
			th->th_urp = 0;
		}
	}

	/*
	 * If new data are received on a connection after the
	 * user processes are gone, then RST the other end.
	 */
	if ((so->so_state & SS_NOFDREF) &&
	    tp->t_state > TCPS_CLOSE_WAIT && tlen) {
		tp = tcp_close(tp);
		tcpstat.tcps_rcvafterclose++;
		rstreason = BANDLIM_UNLIMITED;
		goto dropwithreset;
	}

	/*
	 * If segment ends after window, drop trailing data
	 * (and PUSH and FIN); if nothing left, just ACK.
	 */
	todrop = (th->th_seq + tlen) - (tp->rcv_nxt + tp->rcv_wnd);
	if (todrop > 0) {
		tcpstat.tcps_rcvpackafterwin++;
		if (todrop >= tlen) {
			tcpstat.tcps_rcvbyteafterwin += tlen;
			/*
			 * If a new connection request is received
			 * while in TIME_WAIT, drop the old connection
			 * and start over if the sequence numbers
			 * are above the previous ones.
			 */
			if (thflags & TH_SYN &&
			    tp->t_state == TCPS_TIME_WAIT &&
			    SEQ_GT(th->th_seq, tp->rcv_nxt)) {
				tp = tcp_close(tp);
				goto findpcb;
			}
			/*
			 * If window is closed can only take segments at
			 * window edge, and have to drop data and PUSH from
			 * incoming segments.  Continue processing, but
			 * remember to ack.  Otherwise, drop segment
			 * and ack.
			 */
			if (tp->rcv_wnd == 0 && th->th_seq == tp->rcv_nxt) {
				tp->t_flags |= TF_ACKNOW;
				tcpstat.tcps_rcvwinprobe++;
			} else
				goto dropafterack;
		} else
			tcpstat.tcps_rcvbyteafterwin += todrop;
		m_adj(m, -todrop);
		tlen -= todrop;
		thflags &= ~(TH_PUSH | TH_FIN);
	}

	/*
	 * If last ACK falls within this segment's sequence numbers,
	 * record its timestamp.
	 * NOTE:
	 * 1) That the test incorporates suggestions from the latest
	 *    proposal of the tcplw@cray.com list (Braden 1993/04/26).
	 * 2) That updating only on newer timestamps interferes with
	 *    our earlier PAWS tests, so this check should be solely
	 *    predicated on the sequence space of this segment.
	 * 3) That we modify the segment boundary check to be
	 *        Last.ACK.Sent <= SEG.SEQ + SEG.LEN
	 *    instead of RFC1323's
	 *        Last.ACK.Sent < SEG.SEQ + SEG.LEN,
	 *    This modified check allows us to overcome RFC1323's
	 *    limitations as described in Stevens TCP/IP Illustrated
	 *    Vol. 2 p.869. In such cases, we can still calculate the
	 *    RTT correctly when RCV.NXT == Last.ACK.Sent.
	 */
	if ((to.to_flags & TOF_TS) && SEQ_LEQ(th->th_seq, tp->last_ack_sent) &&
	    SEQ_LEQ(tp->last_ack_sent, (th->th_seq + tlen
					+ ((thflags & TH_SYN) != 0)
					+ ((thflags & TH_FIN) != 0)))) {
		tp->ts_recent_age = ticks;
		tp->ts_recent = to.to_tsval;
	}

	/*
	 * If a SYN is in the window, then this is an
	 * error and we send an RST and drop the connection.
	 */
	if (thflags & TH_SYN) {
		tp = tcp_drop(tp, ECONNRESET);
		rstreason = BANDLIM_UNLIMITED;
		goto dropwithreset;
	}

	/*
	 * If the ACK bit is off:  if in SYN-RECEIVED state or SENDSYN
	 * flag is on (half-synchronized state), then queue data for
	 * later processing; else drop segment and return.
	 */
	if (!(thflags & TH_ACK)) {
		if (tp->t_state == TCPS_SYN_RECEIVED ||
		    (tp->t_flags & TF_NEEDSYN))
			goto step6;
		else
			goto drop;
	}

	/*
	 * Ack processing.
	 */
	switch (tp->t_state) {
	/*
	 * In SYN_RECEIVED state, the ACK acknowledges our SYN, so enter
	 * ESTABLISHED state and continue processing.
	 * The ACK was checked above.
	 */
	case TCPS_SYN_RECEIVED:

		tcpstat.tcps_connects++;
		soisconnected(so);
		/* Do window scaling? */
		if ((tp->t_flags & (TF_RCVD_SCALE | TF_REQ_SCALE)) ==
		    (TF_RCVD_SCALE | TF_REQ_SCALE))
			tp->rcv_scale = tp->request_r_scale;
		/*
		 * Make transitions:
		 *      SYN-RECEIVED  -> ESTABLISHED
		 *      SYN-RECEIVED* -> FIN-WAIT-1
		 */
		tp->t_starttime = ticks;
		if (tp->t_flags & TF_NEEDFIN) {
			tp->t_state = TCPS_FIN_WAIT_1;
			tp->t_flags &= ~TF_NEEDFIN;
		} else {
			tcp_established(tp);
		}
		/*
		 * If segment contains data or ACK, will call tcp_reass()
		 * later; if not, do so now to pass queued data to user.
		 */
		if (tlen == 0 && !(thflags & TH_FIN))
			tcp_reass(tp, NULL, NULL, NULL);
		/* fall into ... */

	/*
	 * In ESTABLISHED state: drop duplicate ACKs; ACK out of range
	 * ACKs.  If the ack is in the range
	 *	tp->snd_una < th->th_ack <= tp->snd_max
	 * then advance tp->snd_una to th->th_ack and drop
	 * data from the retransmission queue.  If this ACK reflects
	 * more up to date window information we update our window information.
	 */
	case TCPS_ESTABLISHED:
	case TCPS_FIN_WAIT_1:
	case TCPS_FIN_WAIT_2:
	case TCPS_CLOSE_WAIT:
	case TCPS_CLOSING:
	case TCPS_LAST_ACK:
	case TCPS_TIME_WAIT:

		if (SEQ_LEQ(th->th_ack, tp->snd_una)) {
			boolean_t maynotdup = FALSE;

			if (TCP_DO_SACK(tp))
				tcp_sack_update_scoreboard(tp, &to);

			if (tlen != 0 || tiwin != tp->snd_wnd ||
			    ((thflags & TH_FIN) && !(tp->t_flags & TF_SAWFIN)))
				maynotdup = TRUE;

			if (!tcp_callout_active(tp, tp->tt_rexmt) ||
			    th->th_ack != tp->snd_una) {
				if (!maynotdup)
					tcpstat.tcps_rcvdupack++;
				tp->t_dupacks = 0;
				break;
			}

#define DELAY_DUPACK \
do { \
	delayed_dupack = TRUE; \
	th_dupack = th->th_ack; \
	to_flags = to.to_flags; \
} while (0)
			if (maynotdup) {
				if (!tcp_do_rfc6675 ||
				    !TCP_DO_SACK(tp) ||
				    (to.to_flags &
				     (TOF_SACK | TOF_SACK_REDUNDANT))
				     != TOF_SACK) {
					tp->t_dupacks = 0;
				} else {
					DELAY_DUPACK;
				}
				break;
			}
			if ((thflags & TH_FIN) && !(tp->t_flags & TF_QUEDFIN)) {
				/*
				 * This could happen, if the reassemable
				 * queue overflew or was drained.  Don't
				 * drop this FIN here; defer the duplicated
				 * ACK processing until this FIN gets queued.
				 */
				DELAY_DUPACK;
				break;
			}
#undef DELAY_DUPACK

			if (tcp_recv_dupack(tp, th->th_ack, to.to_flags))
				goto drop;
			else
				break;
		}

		KASSERT(SEQ_GT(th->th_ack, tp->snd_una), ("th_ack <= snd_una"));
		tp->t_dupacks = 0;
		if (SEQ_GT(th->th_ack, tp->snd_max)) {
			/*
			 * Detected optimistic ACK attack.
			 * Force slow-start to de-synchronize attack.
			 */
			tp->snd_cwnd = tp->t_maxseg;
			tp->snd_wacked = 0;

			tcpstat.tcps_rcvacktoomuch++;
			goto dropafterack;
		}
		/*
		 * If we reach this point, ACK is not a duplicate,
		 *     i.e., it ACKs something we sent.
		 */
		if (tp->t_flags & TF_NEEDSYN) {
			/*
			 * T/TCP: Connection was half-synchronized, and our
			 * SYN has been ACK'd (so connection is now fully
			 * synchronized).  Go to non-starred state,
			 * increment snd_una for ACK of SYN, and check if
			 * we can do window scaling.
			 */
			tp->t_flags &= ~TF_NEEDSYN;
			tp->snd_una++;
			/* Do window scaling? */
			if ((tp->t_flags & (TF_RCVD_SCALE | TF_REQ_SCALE)) ==
			    (TF_RCVD_SCALE | TF_REQ_SCALE))
				tp->rcv_scale = tp->request_r_scale;
		}

process_ACK:
		acked = th->th_ack - tp->snd_una;
		tcpstat.tcps_rcvackpack++;
		tcpstat.tcps_rcvackbyte += acked;

		if (tcp_do_eifel_detect && acked > 0 &&
		    (to.to_flags & TOF_TS) && (to.to_tsecr != 0) &&
		    (tp->rxt_flags & TRXT_F_FIRSTACCACK)) {
			/* Eifel detection applicable. */
			if (to.to_tsecr < tp->t_rexmtTS) {
				++tcpstat.tcps_eifeldetected;
				tcp_revert_congestion_state(tp);
				if (tp->t_rxtshift != 1 ||
				    ticks >= tp->t_badrxtwin)
					++tcpstat.tcps_rttcantdetect;
			}
		} else if (tp->t_rxtshift == 1 && ticks < tp->t_badrxtwin) {
			/*
			 * If we just performed our first retransmit,
			 * and the ACK arrives within our recovery window,
			 * then it was a mistake to do the retransmit
			 * in the first place.  Recover our original cwnd
			 * and ssthresh, and proceed to transmit where we
			 * left off.
			 */
			tcp_revert_congestion_state(tp);
			++tcpstat.tcps_rttdetected;
		}

		/*
		 * If we have a timestamp reply, update smoothed
		 * round trip time.  If no timestamp is present but
		 * transmit timer is running and timed sequence
		 * number was acked, update smoothed round trip time.
		 * Since we now have an rtt measurement, cancel the
		 * timer backoff (cf., Phil Karn's retransmit alg.).
		 * Recompute the initial retransmit timer.
		 *
		 * Some machines (certain windows boxes) send broken
		 * timestamp replies during the SYN+ACK phase, ignore
		 * timestamps of 0.
		 */
		if ((to.to_flags & TOF_TS) && (to.to_tsecr != 0))
			tcp_xmit_timer(tp, ticks - to.to_tsecr + 1, th->th_ack);
		else if (tp->t_rtttime && SEQ_GT(th->th_ack, tp->t_rtseq))
			tcp_xmit_timer(tp, ticks - tp->t_rtttime, th->th_ack);
		tcp_xmit_bandwidth_limit(tp, th->th_ack);

		/*
		 * If no data (only SYN) was ACK'd,
		 *    skip rest of ACK processing.
		 */
		if (acked == 0)
			goto step6;

		/* Stop looking for an acceptable ACK since one was received. */
		tp->rxt_flags &= ~(TRXT_F_FIRSTACCACK |
		    TRXT_F_FASTREXMT | TRXT_F_EARLYREXMT);

		if (acked > so->so_snd.ssb_cc) {
			tp->snd_wnd -= so->so_snd.ssb_cc;
			sbdrop(&so->so_snd.sb, (int)so->so_snd.ssb_cc);
			ourfinisacked = TRUE;
		} else {
			sbdrop(&so->so_snd.sb, acked);
			tp->snd_wnd -= acked;
			ourfinisacked = FALSE;
		}
		sowwakeup(so);

		/*
		 * Update window information.
		 */
		if (acceptable_window_update(tp, th, tiwin)) {
			/* keep track of pure window updates */
			if (tlen == 0 && tp->snd_wl2 == th->th_ack &&
			    tiwin > tp->snd_wnd)
				tcpstat.tcps_rcvwinupd++;
			tp->snd_wnd = tiwin;
			tp->snd_wl1 = th->th_seq;
			tp->snd_wl2 = th->th_ack;
			if (tp->snd_wnd > tp->max_sndwnd)
				tp->max_sndwnd = tp->snd_wnd;
			needoutput = TRUE;
		}

		tp->snd_una = th->th_ack;
		if (TCP_DO_SACK(tp))
			tcp_sack_update_scoreboard(tp, &to);
		if (IN_FASTRECOVERY(tp)) {
			if (SEQ_GEQ(th->th_ack, tp->snd_recover)) {
				EXIT_FASTRECOVERY(tp);
				needoutput = TRUE;
				/*
				 * If the congestion window was inflated
				 * to account for the other side's
				 * cached packets, retract it.
				 */
				if (!TCP_DO_SACK(tp))
					tp->snd_cwnd = tp->snd_ssthresh;

				/*
				 * Window inflation should have left us
				 * with approximately snd_ssthresh outstanding
				 * data.  But, in case we would be inclined
				 * to send a burst, better do it using
				 * slow start.
				 */
				if (SEQ_GT(th->th_ack + tp->snd_cwnd,
					   tp->snd_max + 2 * tp->t_maxseg))
					tp->snd_cwnd =
					    (tp->snd_max - tp->snd_una) +
					    2 * tp->t_maxseg;

				tp->snd_wacked = 0;
			} else {
				if (TCP_DO_SACK(tp)) {
					tp->snd_max_rexmt = tp->snd_max;
					tcp_sack_rexmt(tp,
					    tp->snd_una == tp->rexmt_high);
				} else {
					tcp_newreno_partial_ack(tp, th, acked);
				}
				needoutput = FALSE;
			}
		} else {
			/*
			 * Open the congestion window.  When in slow-start,
			 * open exponentially: maxseg per packet.  Otherwise,
			 * open linearly: maxseg per window.
			 */
			if (tp->snd_cwnd <= tp->snd_ssthresh) {
				u_int abc_sslimit =
				    (SEQ_LT(tp->snd_nxt, tp->snd_max) ?
				     tp->t_maxseg : 2 * tp->t_maxseg);

				/* slow-start */
				tp->snd_cwnd += tcp_do_abc ?
				    min(acked, abc_sslimit) : tp->t_maxseg;
			} else {
				/* linear increase */
				tp->snd_wacked += tcp_do_abc ? acked :
				    tp->t_maxseg;
				if (tp->snd_wacked >= tp->snd_cwnd) {
					tp->snd_wacked -= tp->snd_cwnd;
					tp->snd_cwnd += tp->t_maxseg;
				}
			}
			tp->snd_cwnd = min(tp->snd_cwnd,
					   TCP_MAXWIN << tp->snd_scale);
			tp->snd_recover = th->th_ack - 1;
		}
		if (SEQ_LT(tp->snd_nxt, tp->snd_una))
			tp->snd_nxt = tp->snd_una;

		/*
		 * If all outstanding data is acked, stop retransmit
		 * timer and remember to restart (more output or persist).
		 * If there is more data to be acked, restart retransmit
		 * timer, using current (possibly backed-off) value.
		 */
		if (th->th_ack == tp->snd_max) {
			tcp_callout_stop(tp, tp->tt_rexmt);
			needoutput = TRUE;
		} else if (!tcp_callout_active(tp, tp->tt_persist)) {
			tcp_callout_reset(tp, tp->tt_rexmt, tp->t_rxtcur,
			    tcp_timer_rexmt);
		}

		switch (tp->t_state) {
		/*
		 * In FIN_WAIT_1 STATE in addition to the processing
		 * for the ESTABLISHED state if our FIN is now acknowledged
		 * then enter FIN_WAIT_2.
		 */
		case TCPS_FIN_WAIT_1:
			if (ourfinisacked) {
				/*
				 * If we can't receive any more
				 * data, then closing user can proceed.
				 * Starting the timer is contrary to the
				 * specification, but if we don't get a FIN
				 * we'll hang forever.
				 */
				if (so->so_state & SS_CANTRCVMORE) {
					soisdisconnected(so);
					tcp_callout_reset(tp, tp->tt_2msl,
					    tp->t_maxidle, tcp_timer_2msl);
				}
				tp->t_state = TCPS_FIN_WAIT_2;
			}
			break;

		/*
		 * In CLOSING STATE in addition to the processing for
		 * the ESTABLISHED state if the ACK acknowledges our FIN
		 * then enter the TIME-WAIT state, otherwise ignore
		 * the segment.
		 */
		case TCPS_CLOSING:
			if (ourfinisacked) {
				tp->t_state = TCPS_TIME_WAIT;
				tcp_canceltimers(tp);
				tcp_callout_reset(tp, tp->tt_2msl,
					    2 * tcp_rmx_msl(tp),
					    tcp_timer_2msl);
				soisdisconnected(so);
			}
			break;

		/*
		 * In LAST_ACK, we may still be waiting for data to drain
		 * and/or to be acked, as well as for the ack of our FIN.
		 * If our FIN is now acknowledged, delete the TCB,
		 * enter the closed state and return.
		 */
		case TCPS_LAST_ACK:
			if (ourfinisacked) {
				tp = tcp_close(tp);
				goto drop;
			}
			break;

		/*
		 * In TIME_WAIT state the only thing that should arrive
		 * is a retransmission of the remote FIN.  Acknowledge
		 * it and restart the finack timer.
		 */
		case TCPS_TIME_WAIT:
			tcp_callout_reset(tp, tp->tt_2msl, 2 * tcp_rmx_msl(tp),
			    tcp_timer_2msl);
			goto dropafterack;
		}
	}

step6:
	/*
	 * Update window information.
	 * Don't look at window if no ACK: TAC's send garbage on first SYN.
	 */
	if ((thflags & TH_ACK) &&
	    acceptable_window_update(tp, th, tiwin)) {
		/* keep track of pure window updates */
		if (tlen == 0 && tp->snd_wl2 == th->th_ack &&
		    tiwin > tp->snd_wnd)
			tcpstat.tcps_rcvwinupd++;
		tp->snd_wnd = tiwin;
		tp->snd_wl1 = th->th_seq;
		tp->snd_wl2 = th->th_ack;
		if (tp->snd_wnd > tp->max_sndwnd)
			tp->max_sndwnd = tp->snd_wnd;
		needoutput = TRUE;
	}

	/*
	 * Process segments with URG.
	 */
	if ((thflags & TH_URG) && th->th_urp &&
	    !TCPS_HAVERCVDFIN(tp->t_state)) {
		/*
		 * This is a kludge, but if we receive and accept
		 * random urgent pointers, we'll crash in
		 * soreceive.  It's hard to imagine someone
		 * actually wanting to send this much urgent data.
		 */
		if (th->th_urp + so->so_rcv.ssb_cc > sb_max) {
			th->th_urp = 0;			/* XXX */
			thflags &= ~TH_URG;		/* XXX */
			goto dodata;			/* XXX */
		}
		/*
		 * If this segment advances the known urgent pointer,
		 * then mark the data stream.  This should not happen
		 * in CLOSE_WAIT, CLOSING, LAST_ACK or TIME_WAIT STATES since
		 * a FIN has been received from the remote side.
		 * In these states we ignore the URG.
		 *
		 * According to RFC961 (Assigned Protocols),
		 * the urgent pointer points to the last octet
		 * of urgent data.  We continue, however,
		 * to consider it to indicate the first octet
		 * of data past the urgent section as the original
		 * spec states (in one of two places).
		 */
		if (SEQ_GT(th->th_seq + th->th_urp, tp->rcv_up)) {
			tp->rcv_up = th->th_seq + th->th_urp;
			so->so_oobmark = so->so_rcv.ssb_cc +
			    (tp->rcv_up - tp->rcv_nxt) - 1;
			if (so->so_oobmark == 0)
				sosetstate(so, SS_RCVATMARK);
			sohasoutofband(so);
			tp->t_oobflags &= ~(TCPOOB_HAVEDATA | TCPOOB_HADDATA);
		}
		/*
		 * Remove out of band data so doesn't get presented to user.
		 * This can happen independent of advancing the URG pointer,
		 * but if two URG's are pending at once, some out-of-band
		 * data may creep in... ick.
		 */
		if (th->th_urp <= (u_long)tlen &&
		    !(so->so_options & SO_OOBINLINE)) {
			/* hdr drop is delayed */
			tcp_pulloutofband(so, th, m, drop_hdrlen);
		}
	} else {
		/*
		 * If no out of band data is expected,
		 * pull receive urgent pointer along
		 * with the receive window.
		 */
		if (SEQ_GT(tp->rcv_nxt, tp->rcv_up))
			tp->rcv_up = tp->rcv_nxt;
	}

dodata:							/* XXX */
	/*
	 * Process the segment text, merging it into the TCP sequencing queue,
	 * and arranging for acknowledgment of receipt if necessary.
	 * This process logically involves adjusting tp->rcv_wnd as data
	 * is presented to the user (this happens in tcp_usrreq.c,
	 * case PRU_RCVD).  If a FIN has already been received on this
	 * connection then we just ignore the text.
	 */
	if ((tlen || (thflags & TH_FIN)) && !TCPS_HAVERCVDFIN(tp->t_state)) {
		if (thflags & TH_FIN)
			tp->t_flags |= TF_SAWFIN;
		m_adj(m, drop_hdrlen);	/* delayed header drop */
		/*
		 * Insert segment which includes th into TCP reassembly queue
		 * with control block tp.  Set thflags to whether reassembly now
		 * includes a segment with FIN.  This handles the common case
		 * inline (segment is the next to be received on an established
		 * connection, and the queue is empty), avoiding linkage into
		 * and removal from the queue and repetition of various
		 * conversions.
		 * Set DELACK for segments received in order, but ack
		 * immediately when segments are out of order (so
		 * fast retransmit can work).
		 */
		if (th->th_seq == tp->rcv_nxt &&
		    TAILQ_EMPTY(&tp->t_segq) &&
		    TCPS_HAVEESTABLISHED(tp->t_state)) {
			if (thflags & TH_FIN)
				tp->t_flags |= TF_QUEDFIN;
			if (DELAY_ACK(tp)) {
				tcp_callout_reset(tp, tp->tt_delack,
				    tcp_delacktime, tcp_timer_delack);
			} else {
				tp->t_flags |= TF_ACKNOW;
			}
			tp->rcv_nxt += tlen;
			thflags = th->th_flags & TH_FIN;
			tcpstat.tcps_rcvpack++;
			tcpstat.tcps_rcvbyte += tlen;
			ND6_HINT(tp);
			if (so->so_state & SS_CANTRCVMORE) {
				m_freem(m);
			} else {
				lwkt_gettoken(&so->so_rcv.ssb_token);
				ssb_appendstream(&so->so_rcv, m);
				lwkt_reltoken(&so->so_rcv.ssb_token);
			}
			sorwakeup(so);
		} else {
			if (!(tp->sack_flags & TSACK_F_DUPSEG)) {
				/* Initialize SACK report block. */
				tp->reportblk.rblk_start = th->th_seq;
				tp->reportblk.rblk_end = TCP_SACK_BLKEND(
				    th->th_seq + tlen, thflags);
			}
			thflags = tcp_reass(tp, th, &tlen, m);
			tp->t_flags |= TF_ACKNOW;
		}

		/*
		 * Note the amount of data that peer has sent into
		 * our window, in order to estimate the sender's
		 * buffer size.
		 */
		len = so->so_rcv.ssb_hiwat - (tp->rcv_adv - tp->rcv_nxt);
	} else {
		m_freem(m);
		thflags &= ~TH_FIN;
	}

	/*
	 * If FIN is received ACK the FIN and let the user know
	 * that the connection is closing.
	 */
	if (thflags & TH_FIN) {
		if (!TCPS_HAVERCVDFIN(tp->t_state)) {
			socantrcvmore(so);
			/*
			 * If connection is half-synchronized
			 * (ie NEEDSYN flag on) then delay ACK,
			 * so it may be piggybacked when SYN is sent.
			 * Otherwise, since we received a FIN then no
			 * more input can be expected, send ACK now.
			 */
			if (DELAY_ACK(tp) && (tp->t_flags & TF_NEEDSYN)) {
				tcp_callout_reset(tp, tp->tt_delack,
				    tcp_delacktime, tcp_timer_delack);
			} else {
				tp->t_flags |= TF_ACKNOW;
			}
			tp->rcv_nxt++;
		}

		switch (tp->t_state) {
		/*
		 * In SYN_RECEIVED and ESTABLISHED STATES
		 * enter the CLOSE_WAIT state.
		 */
		case TCPS_SYN_RECEIVED:
			tp->t_starttime = ticks;
			/*FALLTHROUGH*/
		case TCPS_ESTABLISHED:
			tp->t_state = TCPS_CLOSE_WAIT;
			break;

		/*
		 * If still in FIN_WAIT_1 STATE FIN has not been acked so
		 * enter the CLOSING state.
		 */
		case TCPS_FIN_WAIT_1:
			tp->t_state = TCPS_CLOSING;
			break;

		/*
		 * In FIN_WAIT_2 state enter the TIME_WAIT state,
		 * starting the time-wait timer, turning off the other
		 * standard timers.
		 */
		case TCPS_FIN_WAIT_2:
			tp->t_state = TCPS_TIME_WAIT;
			tcp_canceltimers(tp);
			tcp_callout_reset(tp, tp->tt_2msl, 2 * tcp_rmx_msl(tp),
				    tcp_timer_2msl);
			soisdisconnected(so);
			break;

		/*
		 * In TIME_WAIT state restart the 2 MSL time_wait timer.
		 */
		case TCPS_TIME_WAIT:
			tcp_callout_reset(tp, tp->tt_2msl, 2 * tcp_rmx_msl(tp),
			    tcp_timer_2msl);
			break;
		}
	}

#ifdef TCPDEBUG
	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_INPUT, ostate, tp, tcp_saveipgen, &tcp_savetcp, 0);
#endif

	/*
	 * Delayed duplicated ACK processing
	 */
	if (delayed_dupack && tcp_recv_dupack(tp, th_dupack, to_flags))
		needoutput = FALSE;

	/*
	 * Return any desired output.
	 */
	if ((tp->t_flags & TF_ACKNOW) ||
	    (needoutput && tcp_sack_report_needed(tp))) {
		tcp_output_cancel(tp);
		tcp_output_fair(tp);
	} else if (needoutput && !tcp_output_pending(tp)) {
		tcp_output_fair(tp);
	}
	tcp_sack_report_cleanup(tp);
	return(IPPROTO_DONE);

dropafterack:
	/*
	 * Generate an ACK dropping incoming segment if it occupies
	 * sequence space, where the ACK reflects our state.
	 *
	 * We can now skip the test for the RST flag since all
	 * paths to this code happen after packets containing
	 * RST have been dropped.
	 *
	 * In the SYN-RECEIVED state, don't send an ACK unless the
	 * segment we received passes the SYN-RECEIVED ACK test.
	 * If it fails send a RST.  This breaks the loop in the
	 * "LAND" DoS attack, and also prevents an ACK storm
	 * between two listening ports that have been sent forged
	 * SYN segments, each with the source address of the other.
	 */
	if (tp->t_state == TCPS_SYN_RECEIVED && (thflags & TH_ACK) &&
	    (SEQ_GT(tp->snd_una, th->th_ack) ||
	     SEQ_GT(th->th_ack, tp->snd_max)) ) {
		rstreason = BANDLIM_RST_OPENPORT;
		goto dropwithreset;
	}
#ifdef TCPDEBUG
	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_DROP, ostate, tp, tcp_saveipgen, &tcp_savetcp, 0);
#endif
	m_freem(m);
	tp->t_flags |= TF_ACKNOW;
	tcp_output(tp);
	tcp_sack_report_cleanup(tp);
	return(IPPROTO_DONE);

dropwithreset:
	/*
	 * Generate a RST, dropping incoming segment.
	 * Make ACK acceptable to originator of segment.
	 * Don't bother to respond if destination was broadcast/multicast.
	 */
	if ((thflags & TH_RST) || m->m_flags & (M_BCAST | M_MCAST))
		goto drop;
	if (isipv6) {
		if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst) ||
		    IN6_IS_ADDR_MULTICAST(&ip6->ip6_src))
			goto drop;
	} else {
		if (IN_MULTICAST(ntohl(ip->ip_dst.s_addr)) ||
		    IN_MULTICAST(ntohl(ip->ip_src.s_addr)) ||
		    ip->ip_src.s_addr == htonl(INADDR_BROADCAST) ||
		    in_broadcast(ip->ip_dst, m->m_pkthdr.rcvif))
			goto drop;
	}
	/* IPv6 anycast check is done at tcp6_input() */

	/*
	 * Perform bandwidth limiting.
	 */
#ifdef ICMP_BANDLIM
	if (badport_bandlim(rstreason) < 0)
		goto drop;
#endif

#ifdef TCPDEBUG
	if (tp == NULL || (tp->t_inpcb->inp_socket->so_options & SO_DEBUG))
		tcp_trace(TA_DROP, ostate, tp, tcp_saveipgen, &tcp_savetcp, 0);
#endif
	if (thflags & TH_ACK)
		/* mtod() below is safe as long as hdr dropping is delayed */
		tcp_respond(tp, mtod(m, void *), th, m, (tcp_seq)0, th->th_ack,
			    TH_RST);
	else {
		if (thflags & TH_SYN)
			tlen++;
		/* mtod() below is safe as long as hdr dropping is delayed */
		tcp_respond(tp, mtod(m, void *), th, m, th->th_seq + tlen,
			    (tcp_seq)0, TH_RST | TH_ACK);
	}
	if (tp != NULL)
		tcp_sack_report_cleanup(tp);
	return(IPPROTO_DONE);

drop:
	/*
	 * Drop space held by incoming segment and return.
	 */
#ifdef TCPDEBUG
	if (tp == NULL || (tp->t_inpcb->inp_socket->so_options & SO_DEBUG))
		tcp_trace(TA_DROP, ostate, tp, tcp_saveipgen, &tcp_savetcp, 0);
#endif
	m_freem(m);
	if (tp != NULL)
		tcp_sack_report_cleanup(tp);
	return(IPPROTO_DONE);
}

/*
 * Parse TCP options and place in tcpopt.
 */
static void
tcp_dooptions(struct tcpopt *to, u_char *cp, int cnt, boolean_t is_syn,
    tcp_seq ack)
{
	int opt, optlen, i;

	to->to_flags = 0;
	for (; cnt > 0; cnt -= optlen, cp += optlen) {
		opt = cp[0];
		if (opt == TCPOPT_EOL)
			break;
		if (opt == TCPOPT_NOP)
			optlen = 1;
		else {
			if (cnt < 2)
				break;
			optlen = cp[1];
			if (optlen < 2 || optlen > cnt)
				break;
		}
		switch (opt) {
		case TCPOPT_MAXSEG:
			if (optlen != TCPOLEN_MAXSEG)
				continue;
			if (!is_syn)
				continue;
			to->to_flags |= TOF_MSS;
			bcopy(cp + 2, &to->to_mss, sizeof to->to_mss);
			to->to_mss = ntohs(to->to_mss);
			break;
		case TCPOPT_WINDOW:
			if (optlen != TCPOLEN_WINDOW)
				continue;
			if (!is_syn)
				continue;
			to->to_flags |= TOF_SCALE;
			to->to_requested_s_scale = min(cp[2], TCP_MAX_WINSHIFT);
			break;
		case TCPOPT_TIMESTAMP:
			if (optlen != TCPOLEN_TIMESTAMP)
				continue;
			to->to_flags |= TOF_TS;
			bcopy(cp + 2, &to->to_tsval, sizeof to->to_tsval);
			to->to_tsval = ntohl(to->to_tsval);
			bcopy(cp + 6, &to->to_tsecr, sizeof to->to_tsecr);
			to->to_tsecr = ntohl(to->to_tsecr);
			/*
			 * If echoed timestamp is later than the current time,
			 * fall back to non RFC1323 RTT calculation.
			 */
			if (to->to_tsecr != 0 && TSTMP_GT(to->to_tsecr, ticks))
				to->to_tsecr = 0;
			break;
		case TCPOPT_SACK_PERMITTED:
			if (optlen != TCPOLEN_SACK_PERMITTED)
				continue;
			if (!is_syn)
				continue;
			to->to_flags |= TOF_SACK_PERMITTED;
			break;
		case TCPOPT_SACK:
			if ((optlen - 2) & 0x07)	/* not multiple of 8 */
				continue;
			to->to_nsackblocks = (optlen - 2) / 8;
			to->to_sackblocks = (struct raw_sackblock *) (cp + 2);
			to->to_flags |= TOF_SACK;
			for (i = 0; i < to->to_nsackblocks; i++) {
				struct raw_sackblock *r = &to->to_sackblocks[i];

				r->rblk_start = ntohl(r->rblk_start);
				r->rblk_end = ntohl(r->rblk_end);

				if (SEQ_LEQ(r->rblk_end, r->rblk_start)) {
					/*
					 * Invalid SACK block; discard all
					 * SACK blocks
					 */
					tcpstat.tcps_rcvbadsackopt++;
					to->to_nsackblocks = 0;
					to->to_sackblocks = NULL;
					to->to_flags &= ~TOF_SACK;
					break;
				}
			}
			if ((to->to_flags & TOF_SACK) &&
			    tcp_sack_ndsack_blocks(to->to_sackblocks,
			    to->to_nsackblocks, ack))
				to->to_flags |= TOF_DSACK;
			break;
#ifdef TCP_SIGNATURE
		/*
		 * XXX In order to reply to a host which has set the
		 * TCP_SIGNATURE option in its initial SYN, we have to
		 * record the fact that the option was observed here
		 * for the syncache code to perform the correct response.
		 */
		case TCPOPT_SIGNATURE:
			if (optlen != TCPOLEN_SIGNATURE)
				continue;
			to->to_flags |= (TOF_SIGNATURE | TOF_SIGLEN);
			break;
#endif /* TCP_SIGNATURE */
		default:
			continue;
		}
	}
}

/*
 * Pull out of band byte out of a segment so
 * it doesn't appear in the user's data queue.
 * It is still reflected in the segment length for
 * sequencing purposes.
 * "off" is the delayed to be dropped hdrlen.
 */
static void
tcp_pulloutofband(struct socket *so, struct tcphdr *th, struct mbuf *m, int off)
{
	int cnt = off + th->th_urp - 1;

	while (cnt >= 0) {
		if (m->m_len > cnt) {
			char *cp = mtod(m, caddr_t) + cnt;
			struct tcpcb *tp = sototcpcb(so);

			tp->t_iobc = *cp;
			tp->t_oobflags |= TCPOOB_HAVEDATA;
			bcopy(cp + 1, cp, m->m_len - cnt - 1);
			m->m_len--;
			if (m->m_flags & M_PKTHDR)
				m->m_pkthdr.len--;
			return;
		}
		cnt -= m->m_len;
		m = m->m_next;
		if (m == NULL)
			break;
	}
	panic("tcp_pulloutofband");
}

/*
 * Collect new round-trip time estimate
 * and update averages and current timeout.
 */
static void
tcp_xmit_timer(struct tcpcb *tp, int rtt, tcp_seq ack)
{
	int rebaserto = 0;

	tcpstat.tcps_rttupdated++;
	tp->t_rttupdated++;
	if ((tp->rxt_flags & TRXT_F_REBASERTO) &&
	    SEQ_GT(ack, tp->snd_max_prev)) {
#ifdef DEBUG_EIFEL_RESPONSE
		kprintf("srtt/rttvar, prev %d/%d, cur %d/%d, ",
		    tp->t_srtt_prev, tp->t_rttvar_prev,
		    tp->t_srtt, tp->t_rttvar);
#endif

		tcpstat.tcps_eifelresponse++;
		rebaserto = 1;
		tp->rxt_flags &= ~TRXT_F_REBASERTO;
		tp->t_srtt = max(tp->t_srtt_prev, (rtt << TCP_RTT_SHIFT));
		tp->t_rttvar = max(tp->t_rttvar_prev,
		    (rtt << (TCP_RTTVAR_SHIFT - 1)));
		if (tp->t_rttbest > tp->t_srtt + tp->t_rttvar)
			tp->t_rttbest = tp->t_srtt + tp->t_rttvar;

#ifdef DEBUG_EIFEL_RESPONSE
		kprintf("new %d/%d ", tp->t_srtt, tp->t_rttvar);
#endif
	} else if (tp->t_srtt != 0) {
		int delta;

		/*
		 * srtt is stored as fixed point with 5 bits after the
		 * binary point (i.e., scaled by 8).  The following magic
		 * is equivalent to the smoothing algorithm in rfc793 with
		 * an alpha of .875 (srtt = rtt/8 + srtt*7/8 in fixed
		 * point).  Adjust rtt to origin 0.
		 */
		delta = ((rtt - 1) << TCP_DELTA_SHIFT)
			- (tp->t_srtt >> (TCP_RTT_SHIFT - TCP_DELTA_SHIFT));

		if ((tp->t_srtt += delta) <= 0)
			tp->t_srtt = 1;

		/*
		 * We accumulate a smoothed rtt variance (actually, a
		 * smoothed mean difference), then set the retransmit
		 * timer to smoothed rtt + 4 times the smoothed variance.
		 * rttvar is stored as fixed point with 4 bits after the
		 * binary point (scaled by 16).  The following is
		 * equivalent to rfc793 smoothing with an alpha of .75
		 * (rttvar = rttvar*3/4 + |delta| / 4).  This replaces
		 * rfc793's wired-in beta.
		 */
		if (delta < 0)
			delta = -delta;
		delta -= tp->t_rttvar >> (TCP_RTTVAR_SHIFT - TCP_DELTA_SHIFT);
		if ((tp->t_rttvar += delta) <= 0)
			tp->t_rttvar = 1;
		if (tp->t_rttbest > tp->t_srtt + tp->t_rttvar)
			tp->t_rttbest = tp->t_srtt + tp->t_rttvar;
	} else {
		/*
		 * No rtt measurement yet - use the unsmoothed rtt.
		 * Set the variance to half the rtt (so our first
		 * retransmit happens at 3*rtt).
		 */
		tp->t_srtt = rtt << TCP_RTT_SHIFT;
		tp->t_rttvar = rtt << (TCP_RTTVAR_SHIFT - 1);
		tp->t_rttbest = tp->t_srtt + tp->t_rttvar;
	}
	tp->t_rtttime = 0;
	tp->t_rxtshift = 0;

#ifdef DEBUG_EIFEL_RESPONSE
	if (rebaserto) {
		kprintf("| rxtcur prev %d, old %d, ",
		    tp->t_rxtcur_prev, tp->t_rxtcur);
	}
#endif

	/*
	 * the retransmit should happen at rtt + 4 * rttvar.
	 * Because of the way we do the smoothing, srtt and rttvar
	 * will each average +1/2 tick of bias.  When we compute
	 * the retransmit timer, we want 1/2 tick of rounding and
	 * 1 extra tick because of +-1/2 tick uncertainty in the
	 * firing of the timer.  The bias will give us exactly the
	 * 1.5 tick we need.  But, because the bias is
	 * statistical, we have to test that we don't drop below
	 * the minimum feasible timer (which is 2 ticks).
	 */
	TCPT_RANGESET(tp->t_rxtcur, TCP_REXMTVAL(tp),
		      max(tp->t_rttmin, rtt + 2), TCPTV_REXMTMAX);

	if (rebaserto) {
		if (tp->t_rxtcur < tp->t_rxtcur_prev + tcp_eifel_rtoinc) {
			/*
			 * RFC4015 requires that the new RTO is at least
			 * 2*G (tcp_eifel_rtoinc) greater then the RTO
			 * (t_rxtcur_prev) when the spurious retransmit
			 * timeout happens.
			 *
			 * The above condition could be true, if the SRTT
			 * and RTTVAR used to calculate t_rxtcur_prev
			 * resulted in a value less than t_rttmin.  So
			 * simply increasing SRTT by tcp_eifel_rtoinc when
			 * preparing for the Eifel response could not ensure
			 * that the new RTO will be tcp_eifel_rtoinc greater
			 * t_rxtcur_prev.
			 */
			tp->t_rxtcur = tp->t_rxtcur_prev + tcp_eifel_rtoinc;
		}
#ifdef DEBUG_EIFEL_RESPONSE
		kprintf("new %d\n", tp->t_rxtcur);
#endif
	}

	/*
	 * We received an ack for a packet that wasn't retransmitted;
	 * it is probably safe to discard any error indications we've
	 * received recently.  This isn't quite right, but close enough
	 * for now (a route might have failed after we sent a segment,
	 * and the return path might not be symmetrical).
	 */
	tp->t_softerror = 0;
}

/*
 * Determine a reasonable value for maxseg size.
 * If the route is known, check route for mtu.
 * If none, use an mss that can be handled on the outgoing
 * interface without forcing IP to fragment; if bigger than
 * an mbuf cluster (MCLBYTES), round down to nearest multiple of MCLBYTES
 * to utilize large mbufs.  If no route is found, route has no mtu,
 * or the destination isn't local, use a default, hopefully conservative
 * size (usually 512 or the default IP max size, but no more than the mtu
 * of the interface), as we can't discover anything about intervening
 * gateways or networks.  We also initialize the congestion/slow start
 * window to be a single segment if the destination isn't local.
 * While looking at the routing entry, we also initialize other path-dependent
 * parameters from pre-set or cached values in the routing entry.
 *
 * Also take into account the space needed for options that we
 * send regularly.  Make maxseg shorter by that amount to assure
 * that we can send maxseg amount of data even when the options
 * are present.  Store the upper limit of the length of options plus
 * data in maxopd.
 *
 * NOTE that this routine is only called when we process an incoming
 * segment, for outgoing segments only tcp_mssopt is called.
 */
void
tcp_mss(struct tcpcb *tp, int offer)
{
	struct rtentry *rt;
	struct ifnet *ifp;
	int rtt, mss;
	u_long bufsize;
	struct inpcb *inp = tp->t_inpcb;
	struct socket *so;
#ifdef INET6
	boolean_t isipv6 = ((inp->inp_vflag & INP_IPV6) ? TRUE : FALSE);
	size_t min_protoh = isipv6 ?
			    sizeof(struct ip6_hdr) + sizeof(struct tcphdr) :
			    sizeof(struct tcpiphdr);
#else
	const boolean_t isipv6 = FALSE;
	const size_t min_protoh = sizeof(struct tcpiphdr);
#endif

	if (isipv6)
		rt = tcp_rtlookup6(&inp->inp_inc);
	else
		rt = tcp_rtlookup(&inp->inp_inc);
	if (rt == NULL) {
		tp->t_maxopd = tp->t_maxseg =
		    (isipv6 ? tcp_v6mssdflt : tcp_mssdflt);
		return;
	}
	ifp = rt->rt_ifp;
	so = inp->inp_socket;

	/*
	 * Offer == 0 means that there was no MSS on the SYN segment,
	 * in this case we use either the interface mtu or tcp_mssdflt.
	 *
	 * An offer which is too large will be cut down later.
	 */
	if (offer == 0) {
		if (isipv6) {
			if (in6_localaddr(&inp->in6p_faddr)) {
				offer = ND_IFINFO(rt->rt_ifp)->linkmtu -
					min_protoh;
			} else {
				offer = tcp_v6mssdflt;
			}
		} else {
			if (in_localaddr(inp->inp_faddr))
				offer = ifp->if_mtu - min_protoh;
			else
				offer = tcp_mssdflt;
		}
	}

	/*
	 * Prevent DoS attack with too small MSS. Round up
	 * to at least minmss.
	 *
	 * Sanity check: make sure that maxopd will be large
	 * enough to allow some data on segments even is the
	 * all the option space is used (40bytes).  Otherwise
	 * funny things may happen in tcp_output.
	 */
	offer = max(offer, tcp_minmss);
	offer = max(offer, 64);

	rt->rt_rmx.rmx_mssopt = offer;

	/*
	 * While we're here, check if there's an initial rtt
	 * or rttvar.  Convert from the route-table units
	 * to scaled multiples of the slow timeout timer.
	 */
	if (tp->t_srtt == 0 && (rtt = rt->rt_rmx.rmx_rtt)) {
		/*
		 * XXX the lock bit for RTT indicates that the value
		 * is also a minimum value; this is subject to time.
		 */
		if (rt->rt_rmx.rmx_locks & RTV_RTT)
			tp->t_rttmin = rtt / (RTM_RTTUNIT / hz);
		tp->t_srtt = rtt / (RTM_RTTUNIT / (hz * TCP_RTT_SCALE));
		tp->t_rttbest = tp->t_srtt + TCP_RTT_SCALE;
		tcpstat.tcps_usedrtt++;
		if (rt->rt_rmx.rmx_rttvar) {
			tp->t_rttvar = rt->rt_rmx.rmx_rttvar /
			    (RTM_RTTUNIT / (hz * TCP_RTTVAR_SCALE));
			tcpstat.tcps_usedrttvar++;
		} else {
			/* default variation is +- 1 rtt */
			tp->t_rttvar =
			    tp->t_srtt * TCP_RTTVAR_SCALE / TCP_RTT_SCALE;
		}
		TCPT_RANGESET(tp->t_rxtcur,
			      ((tp->t_srtt >> 2) + tp->t_rttvar) >> 1,
			      tp->t_rttmin, TCPTV_REXMTMAX);
	}

	/*
	 * if there's an mtu associated with the route, use it
	 * else, use the link mtu.  Take the smaller of mss or offer
	 * as our final mss.
	 */
	if (rt->rt_rmx.rmx_mtu) {
		mss = rt->rt_rmx.rmx_mtu - min_protoh;
	} else {
		if (isipv6)
			mss = ND_IFINFO(rt->rt_ifp)->linkmtu - min_protoh;
		else
			mss = ifp->if_mtu - min_protoh;
	}
	mss = min(mss, offer);

	/*
	 * maxopd stores the maximum length of data AND options
	 * in a segment; maxseg is the amount of data in a normal
	 * segment.  We need to store this value (maxopd) apart
	 * from maxseg, because now every segment carries options
	 * and thus we normally have somewhat less data in segments.
	 */
	tp->t_maxopd = mss;

	if ((tp->t_flags & (TF_REQ_TSTMP | TF_NOOPT)) == TF_REQ_TSTMP &&
	    ((tp->t_flags & TF_RCVD_TSTMP) == TF_RCVD_TSTMP))
		mss -= TCPOLEN_TSTAMP_APPA;

#if	(MCLBYTES & (MCLBYTES - 1)) == 0
	if (mss > MCLBYTES)
		mss &= ~(MCLBYTES-1);
#else
	if (mss > MCLBYTES)
		mss = mss / MCLBYTES * MCLBYTES;
#endif
	/*
	 * If there's a pipesize, change the socket buffer
	 * to that size.  Make the socket buffers an integral
	 * number of mss units; if the mss is larger than
	 * the socket buffer, decrease the mss.
	 */
#ifdef RTV_SPIPE
	if ((bufsize = rt->rt_rmx.rmx_sendpipe) == 0)
#endif
		bufsize = so->so_snd.ssb_hiwat;
	if (bufsize < mss)
		mss = bufsize;
	else {
		bufsize = roundup(bufsize, mss);
		if (bufsize > sb_max)
			bufsize = sb_max;
		if (bufsize > so->so_snd.ssb_hiwat)
			ssb_reserve(&so->so_snd, bufsize, so, NULL);
	}
	tp->t_maxseg = mss;

#ifdef RTV_RPIPE
	if ((bufsize = rt->rt_rmx.rmx_recvpipe) == 0)
#endif
		bufsize = so->so_rcv.ssb_hiwat;
	if (bufsize > mss) {
		bufsize = roundup(bufsize, mss);
		if (bufsize > sb_max)
			bufsize = sb_max;
		if (bufsize > so->so_rcv.ssb_hiwat) {
			lwkt_gettoken(&so->so_rcv.ssb_token);
			ssb_reserve(&so->so_rcv, bufsize, so, NULL);
			lwkt_reltoken(&so->so_rcv.ssb_token);
		}
	}

	/*
	 * Set the slow-start flight size
	 *
	 * NOTE: t_maxseg must have been configured!
	 */
	tp->snd_cwnd = tcp_initial_window(tp);

	if (rt->rt_rmx.rmx_ssthresh) {
		/*
		 * There's some sort of gateway or interface
		 * buffer limit on the path.  Use this to set
		 * the slow start threshhold, but set the
		 * threshold to no less than 2*mss.
		 */
		tp->snd_ssthresh = max(2 * mss, rt->rt_rmx.rmx_ssthresh);
		tcpstat.tcps_usedssthresh++;
	}
}

/*
 * Determine the MSS option to send on an outgoing SYN.
 */
int
tcp_mssopt(struct tcpcb *tp)
{
	struct rtentry *rt;
#ifdef INET6
	boolean_t isipv6 =
	    ((tp->t_inpcb->inp_vflag & INP_IPV6) ? TRUE : FALSE);
	int min_protoh = isipv6 ?
			     sizeof(struct ip6_hdr) + sizeof(struct tcphdr) :
			     sizeof(struct tcpiphdr);
#else
	const boolean_t isipv6 = FALSE;
	const size_t min_protoh = sizeof(struct tcpiphdr);
#endif

	if (isipv6)
		rt = tcp_rtlookup6(&tp->t_inpcb->inp_inc);
	else
		rt = tcp_rtlookup(&tp->t_inpcb->inp_inc);
	if (rt == NULL)
		return (isipv6 ? tcp_v6mssdflt : tcp_mssdflt);

	return (rt->rt_ifp->if_mtu - min_protoh);
}

/*
 * When a partial ack arrives, force the retransmission of the
 * next unacknowledged segment.  Do not exit Fast Recovery.
 *
 * Implement the Slow-but-Steady variant of NewReno by restarting the
 * the retransmission timer.  Turn it off here so it can be restarted
 * later in tcp_output().
 */
static void
tcp_newreno_partial_ack(struct tcpcb *tp, struct tcphdr *th, int acked)
{
	tcp_seq old_snd_nxt = tp->snd_nxt;
	u_long ocwnd = tp->snd_cwnd;

	tcp_callout_stop(tp, tp->tt_rexmt);
	tp->t_rtttime = 0;
	tp->snd_nxt = th->th_ack;
	/* Set snd_cwnd to one segment beyond acknowledged offset. */
	tp->snd_cwnd = tp->t_maxseg;
	tp->t_flags |= TF_ACKNOW;
	tcp_output(tp);
	if (SEQ_GT(old_snd_nxt, tp->snd_nxt))
		tp->snd_nxt = old_snd_nxt;
	/* partial window deflation */
	if (ocwnd > acked)
		tp->snd_cwnd = ocwnd - acked + tp->t_maxseg;
	else
		tp->snd_cwnd = tp->t_maxseg;
}

/*
 * In contrast to the Slow-but-Steady NewReno variant,
 * we do not reset the retransmission timer for SACK retransmissions,
 * except when retransmitting snd_una.
 */
static void
tcp_sack_rexmt(struct tcpcb *tp, boolean_t force)
{
	tcp_seq old_snd_nxt = tp->snd_nxt;
	u_long ocwnd = tp->snd_cwnd;
	uint32_t pipe;
	int nseg = 0;		/* consecutive new segments */
	int nseg_rexmt = 0;	/* retransmitted segments */
	int maxrexmt = 0;

	if (force) {
		uint32_t unsacked = tcp_sack_first_unsacked_len(tp);

		/*
		 * Try to fill the first hole in the receiver's
		 * reassemble queue.
		 */
		maxrexmt = howmany(unsacked, tp->t_maxseg);
		if (maxrexmt > tcp_force_sackrxt)
			maxrexmt = tcp_force_sackrxt;
	}

	tp->t_rtttime = 0;
	pipe = tcp_sack_compute_pipe(tp);
	while (((tcp_seq_diff_t)(ocwnd - pipe) >= (tcp_seq_diff_t)tp->t_maxseg
	        || (force && nseg_rexmt < maxrexmt && nseg == 0)) &&
	    (!tcp_do_smartsack || nseg < TCP_SACK_MAXBURST)) {
		tcp_seq old_snd_max, old_rexmt_high, nextrexmt;
		uint32_t sent, seglen;
		boolean_t rescue;
		int error;

		old_rexmt_high = tp->rexmt_high;
		if (!tcp_sack_nextseg(tp, &nextrexmt, &seglen, &rescue)) {
			tp->rexmt_high = old_rexmt_high;
			break;
		}

		/*
		 * If the next tranmission is a rescue retranmission,
		 * we check whether we have already sent some data
		 * (either new segments or retransmitted segments)
		 * into the the network or not.  Since the idea of rescue
		 * retransmission is to sustain ACK clock, as long as
		 * some segments are in the network, ACK clock will be
		 * kept ticking.
		 */
		if (rescue && (nseg_rexmt > 0 || nseg > 0)) {
			tp->rexmt_high = old_rexmt_high;
			break;
		}

		if (nextrexmt == tp->snd_max)
			++nseg;
		else
			++nseg_rexmt;
		tp->snd_nxt = nextrexmt;
		tp->snd_cwnd = nextrexmt - tp->snd_una + seglen;
		old_snd_max = tp->snd_max;
		if (nextrexmt == tp->snd_una)
			tcp_callout_stop(tp, tp->tt_rexmt);
		tp->t_flags |= TF_XMITNOW;
		error = tcp_output(tp);
		if (error != 0) {
			tp->rexmt_high = old_rexmt_high;
			break;
		}
		sent = tp->snd_nxt - nextrexmt;
		if (sent <= 0) {
			tp->rexmt_high = old_rexmt_high;
			break;
		}
		pipe += sent;
		tcpstat.tcps_sndsackpack++;
		tcpstat.tcps_sndsackbyte += sent;

		if (rescue) {
			tcpstat.tcps_sackrescue++;
			tp->rexmt_rescue = tp->snd_nxt;
			tp->sack_flags |= TSACK_F_SACKRESCUED;
			break;
		}
		if (SEQ_LT(nextrexmt, old_snd_max) &&
		    SEQ_LT(tp->rexmt_high, tp->snd_nxt)) {
			tp->rexmt_high = seq_min(tp->snd_nxt, old_snd_max);
			if (tcp_aggressive_rescuesack &&
			    (tp->sack_flags & TSACK_F_SACKRESCUED) &&
			    SEQ_LT(tp->rexmt_rescue, tp->rexmt_high)) {
				/* Drag RescueRxt along with HighRxt */
				tp->rexmt_rescue = tp->rexmt_high;
			}
		}
	}
	if (SEQ_GT(old_snd_nxt, tp->snd_nxt))
		tp->snd_nxt = old_snd_nxt;
	tp->snd_cwnd = ocwnd;
}

/*
 * Return TRUE, if some new segments are sent
 */
static boolean_t
tcp_sack_limitedxmit(struct tcpcb *tp)
{
	tcp_seq oldsndnxt = tp->snd_nxt;
	tcp_seq oldsndmax = tp->snd_max;
	u_long ocwnd = tp->snd_cwnd;
	uint32_t pipe, sent;
	boolean_t ret = FALSE;
	tcp_seq_diff_t cwnd_left;
	tcp_seq next;

	tp->rexmt_high = tp->snd_una - 1;
	pipe = tcp_sack_compute_pipe(tp);
	cwnd_left = (tcp_seq_diff_t)(ocwnd - pipe);
	if (cwnd_left < (tcp_seq_diff_t)tp->t_maxseg)
		return FALSE;

	if (tcp_do_smartsack)
		cwnd_left = ulmin(cwnd_left, tp->t_maxseg * TCP_SACK_MAXBURST);

	next = tp->snd_nxt = tp->snd_max;
	tp->snd_cwnd = tp->snd_nxt - tp->snd_una +
	    rounddown(cwnd_left, tp->t_maxseg);

	tp->t_flags |= TF_XMITNOW;
	tcp_output(tp);

	sent = tp->snd_nxt - next;
	if (sent > 0) {
		tcpstat.tcps_sndlimited += howmany(sent, tp->t_maxseg);
		ret = TRUE;
	}

	if (SEQ_LT(oldsndnxt, oldsndmax)) {
		KASSERT(SEQ_GEQ(oldsndnxt, tp->snd_una),
		    ("snd_una moved in other threads"));
		tp->snd_nxt = oldsndnxt;
	}
	tp->snd_cwnd = ocwnd;

	if (ret && TCP_DO_NCR(tp))
		tcp_ncr_update_rxtthresh(tp);

	return ret;
}

/*
 * Reset idle time and keep-alive timer, typically called when a valid
 * tcp packet is received but may also be called when FASTKEEP is set
 * to prevent the previous long-timeout from calculating to a drop.
 *
 * Only update t_rcvtime for non-SYN packets.
 *
 * Handle the case where one side thinks the connection is established
 * but the other side has, say, rebooted without cleaning out the
 * connection.   The SYNs could be construed as an attack and wind
 * up ignored, but in case it isn't an attack we can validate the
 * connection by forcing a keepalive.
 */
void
tcp_timer_keep_activity(struct tcpcb *tp, int thflags)
{
	if (TCPS_HAVEESTABLISHED(tp->t_state)) {
		if ((thflags & (TH_SYN | TH_ACK)) == TH_SYN) {
			tp->t_flags |= TF_KEEPALIVE;
			tcp_callout_reset(tp, tp->tt_keep, hz / 2,
					  tcp_timer_keep);
		} else {
			tp->t_rcvtime = ticks;
			tp->t_flags &= ~TF_KEEPALIVE;
			tcp_callout_reset(tp, tp->tt_keep,
					  tp->t_keepidle,
					  tcp_timer_keep);
		}
	}
}

static int
tcp_rmx_msl(const struct tcpcb *tp)
{
	struct rtentry *rt;
	struct inpcb *inp = tp->t_inpcb;
	int msl;
#ifdef INET6
	boolean_t isipv6 = ((inp->inp_vflag & INP_IPV6) ? TRUE : FALSE);
#else
	const boolean_t isipv6 = FALSE;
#endif

	if (isipv6)
		rt = tcp_rtlookup6(&inp->inp_inc);
	else
		rt = tcp_rtlookup(&inp->inp_inc);
	if (rt == NULL || rt->rt_rmx.rmx_msl == 0)
		return tcp_msl;

	msl = (rt->rt_rmx.rmx_msl * hz) / 1000;
	if (msl == 0)
		msl = 1;

	return msl;
}

static void
tcp_established(struct tcpcb *tp)
{
	tp->t_state = TCPS_ESTABLISHED;
	tcp_callout_reset(tp, tp->tt_keep, tp->t_keepidle, tcp_timer_keep);

	if (tp->t_rxtsyn > 0) {
		/*
		 * RFC6298:
		 * "If the timer expires awaiting the ACK of a SYN segment
		 *  and the TCP implementation is using an RTO less than 3
		 *  seconds, the RTO MUST be re-initialized to 3 seconds
		 *  when data transmission begins"
		 */
		if (tp->t_rxtcur < TCPTV_RTOBASE3)
			tp->t_rxtcur = TCPTV_RTOBASE3;
	}
}

/*
 * Returns TRUE, if the ACK should be dropped
 */
static boolean_t
tcp_recv_dupack(struct tcpcb *tp, tcp_seq th_ack, u_int to_flags)
{
	boolean_t fast_sack_rexmt = TRUE;

	tcpstat.tcps_rcvdupack++;

	/*
	 * We have outstanding data (other than a window probe),
	 * this is a completely duplicate ack (ie, window info
	 * didn't change), the ack is the biggest we've seen and
	 * we've seen exactly our rexmt threshhold of them, so
	 * assume a packet has been dropped and retransmit it.
	 * Kludge snd_nxt & the congestion window so we send only
	 * this one packet.
	 */
	if (IN_FASTRECOVERY(tp)) {
		if (TCP_DO_SACK(tp)) {
			boolean_t force = FALSE;

			if (tp->snd_una == tp->rexmt_high &&
			    (to_flags & (TOF_SACK | TOF_SACK_REDUNDANT)) ==
			    TOF_SACK) {
				/*
				 * New segments got SACKed and
				 * no retransmit yet.
				 */
				force = TRUE;
			}

			/* No artifical cwnd inflation. */
			tcp_sack_rexmt(tp, force);
		} else {
			/*
			 * Dup acks mean that packets have left
			 * the network (they're now cached at the
			 * receiver) so bump cwnd by the amount in
			 * the receiver to keep a constant cwnd
			 * packets in the network.
			 */
			tp->snd_cwnd += tp->t_maxseg;
			tcp_output(tp);
		}
		return TRUE;
	} else if (SEQ_LT(th_ack, tp->snd_recover)) {
		tp->t_dupacks = 0;
		return FALSE;
	} else if (tcp_ignore_redun_dsack && TCP_DO_SACK(tp) &&
	    (to_flags & (TOF_DSACK | TOF_SACK_REDUNDANT)) ==
	    (TOF_DSACK | TOF_SACK_REDUNDANT)) {
		/*
		 * If the ACK carries DSACK and other SACK blocks
		 * carry information that we have already known,
		 * don't count this ACK as duplicate ACK.  This
		 * prevents spurious early retransmit and fast
		 * retransmit.  This also meets the requirement of
		 * RFC3042 that new segments should not be sent if
		 * the SACK blocks do not contain new information
		 * (XXX we actually loosen the requirment that only
		 * DSACK is checked here).
		 *
		 * This kind of ACKs are usually sent after spurious
		 * retransmit.
		 */
		/* Do nothing; don't change t_dupacks */
		return TRUE;
	} else if (tp->t_dupacks == 0 && TCP_DO_NCR(tp)) {
		tcp_ncr_update_rxtthresh(tp);
	}

	if (++tp->t_dupacks == tp->t_rxtthresh) {
		tcp_seq old_snd_nxt;
		u_int win;

fastretransmit:
		if (tcp_do_eifel_detect && (tp->t_flags & TF_RCVD_TSTMP)) {
			tcp_save_congestion_state(tp);
			tp->rxt_flags |= TRXT_F_FASTREXMT;
		}
		/*
		 * We know we're losing at the current window size,
		 * so do congestion avoidance: set ssthresh to half
		 * the current window and pull our congestion window
		 * back to the new ssthresh.
		 */
		win = min(tp->snd_wnd, tp->snd_cwnd) / 2 / tp->t_maxseg;
		if (win < 2)
			win = 2;
		tp->snd_ssthresh = win * tp->t_maxseg;
		ENTER_FASTRECOVERY(tp);
		tp->snd_recover = tp->snd_max;
		tcp_callout_stop(tp, tp->tt_rexmt);
		tp->t_rtttime = 0;
		old_snd_nxt = tp->snd_nxt;
		tp->snd_nxt = th_ack;
		if (TCP_DO_SACK(tp)) {
			uint32_t rxtlen;

			rxtlen = tcp_sack_first_unsacked_len(tp);
			if (rxtlen > tp->t_maxseg)
				rxtlen = tp->t_maxseg;
			tp->snd_cwnd = rxtlen;
		} else {
			tp->snd_cwnd = tp->t_maxseg;
		}
		tcp_output(tp);
		++tcpstat.tcps_sndfastrexmit;
		tp->snd_cwnd = tp->snd_ssthresh;
		tp->rexmt_high = tp->snd_nxt;
		tp->sack_flags &= ~TSACK_F_SACKRESCUED;
		if (SEQ_GT(old_snd_nxt, tp->snd_nxt))
			tp->snd_nxt = old_snd_nxt;
		KASSERT(tp->snd_limited <= 2, ("tp->snd_limited too big"));
		if (TCP_DO_SACK(tp)) {
			if (fast_sack_rexmt)
				tcp_sack_rexmt(tp, FALSE);
		} else {
			tp->snd_cwnd += tp->t_maxseg *
			    (tp->t_dupacks - tp->snd_limited);
		}
	} else if ((tcp_do_rfc6675 && TCP_DO_SACK(tp)) || TCP_DO_NCR(tp)) {
		/*
		 * The RFC6675 recommends to reduce the byte threshold,
		 * and enter fast retransmit if IsLost(snd_una).  However,
		 * if we use IsLost(snd_una) based fast retransmit here,
		 * segments reordering will cause spurious retransmit.  So
		 * we defer the IsLost(snd_una) based fast retransmit until
		 * the extended limited transmit can't send any segments and
		 * early retransmit can't be done.
		 */
		if (tcp_rfc6675_rxt && tcp_do_rfc6675 &&
		    tcp_sack_islost(&tp->scb, tp->snd_una))
			goto fastretransmit;

		if (tcp_do_limitedtransmit || TCP_DO_NCR(tp)) {
			if (!tcp_sack_limitedxmit(tp)) {
				/* outstanding data */
				uint32_t ownd = tp->snd_max - tp->snd_una;

				if (need_early_retransmit(tp, ownd)) {
					++tcpstat.tcps_sndearlyrexmit;
					tp->rxt_flags |= TRXT_F_EARLYREXMT;
					goto fastretransmit;
				} else if (tcp_do_rfc6675 &&
				    tcp_sack_islost(&tp->scb, tp->snd_una)) {
					fast_sack_rexmt = FALSE;
					goto fastretransmit;
				}
			}
		}
	} else if (tcp_do_limitedtransmit) {
		u_long oldcwnd = tp->snd_cwnd;
		tcp_seq oldsndmax = tp->snd_max;
		tcp_seq oldsndnxt = tp->snd_nxt;
		/* outstanding data */
		uint32_t ownd = tp->snd_max - tp->snd_una;
		u_int sent;

		KASSERT(tp->t_dupacks == 1 || tp->t_dupacks == 2,
		    ("dupacks not 1 or 2"));
		if (tp->t_dupacks == 1)
			tp->snd_limited = 0;
		tp->snd_nxt = tp->snd_max;
		tp->snd_cwnd = ownd +
		    (tp->t_dupacks - tp->snd_limited) * tp->t_maxseg;
		tp->t_flags |= TF_XMITNOW;
		tcp_output(tp);

		if (SEQ_LT(oldsndnxt, oldsndmax)) {
			KASSERT(SEQ_GEQ(oldsndnxt, tp->snd_una),
			    ("snd_una moved in other threads"));
			tp->snd_nxt = oldsndnxt;
		}
		tp->snd_cwnd = oldcwnd;
		sent = tp->snd_max - oldsndmax;
		if (sent > tp->t_maxseg) {
			KASSERT((tp->t_dupacks == 2 && tp->snd_limited == 0) ||
			    (sent == tp->t_maxseg + 1 &&
			     (tp->t_flags & TF_SENTFIN)),
			    ("sent too much"));
			KASSERT(sent <= tp->t_maxseg * 2,
			    ("sent too many segments"));
			tp->snd_limited = 2;
			tcpstat.tcps_sndlimited += 2;
		} else if (sent > 0) {
			++tp->snd_limited;
			++tcpstat.tcps_sndlimited;
		} else if (need_early_retransmit(tp, ownd)) {
			++tcpstat.tcps_sndearlyrexmit;
			tp->rxt_flags |= TRXT_F_EARLYREXMT;
			goto fastretransmit;
		}
	}
	return TRUE;
}
