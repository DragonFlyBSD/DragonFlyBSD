/*
 * Copyright (c) 2003, 2004 Jeffrey M. Hsu.  All rights reserved.
 * Copyright (c) 2003, 2004 The DragonFly Project.  All rights reserved.
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
 * Copyright (c) 1982, 1986, 1988, 1990, 1993, 1995
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
 *	@(#)tcp_timer.c	8.2 (Berkeley) 5/24/95
 * $FreeBSD: src/sys/netinet/tcp_timer.c,v 1.34.2.14 2003/02/03 02:33:41 hsu Exp $
 * $DragonFly: src/sys/netinet/tcp_timer.c,v 1.17 2008/03/30 20:39:01 dillon Exp $
 */

#include "opt_compat.h"
#include "opt_inet6.h"
#include "opt_tcpdebug.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/thread.h>
#include <sys/globaldata.h>
#include <sys/thread2.h>
#include <sys/msgport2.h>

#include <machine/cpu.h>	/* before tcp_seq.h, for tcp_random18() */

#include <net/route.h>
#include <net/netmsg2.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_pcb.h>
#ifdef INET6
#include <netinet6/in6_pcb.h>
#endif
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_timer2.h>
#include <netinet/tcp_var.h>
#include <netinet/tcpip.h>
#ifdef TCPDEBUG
#include <netinet/tcp_debug.h>
#endif

#define TCP_TIMER_REXMT		0x01
#define TCP_TIMER_PERSIST	0x02
#define TCP_TIMER_KEEP		0x04
#define TCP_TIMER_2MSL		0x08
#define TCP_TIMER_DELACK	0x10

static struct tcpcb	*tcp_timer_rexmt_handler(struct tcpcb *);
static struct tcpcb	*tcp_timer_persist_handler(struct tcpcb *);
static struct tcpcb	*tcp_timer_keep_handler(struct tcpcb *);
static struct tcpcb	*tcp_timer_2msl_handler(struct tcpcb *);
static struct tcpcb	*tcp_timer_delack_handler(struct tcpcb *);

static const struct tcp_timer {
	uint32_t	tt_task;
	struct tcpcb	*(*tt_handler)(struct tcpcb *);
} tcp_timer_handlers[] = {
	{ TCP_TIMER_DELACK,	tcp_timer_delack_handler },
	{ TCP_TIMER_REXMT,	tcp_timer_rexmt_handler },
	{ TCP_TIMER_PERSIST,	tcp_timer_persist_handler },
	{ TCP_TIMER_KEEP,	tcp_timer_keep_handler },
	{ TCP_TIMER_2MSL,	tcp_timer_2msl_handler },
	{ 0, NULL }
};

static int
sysctl_msec_to_ticks(SYSCTL_HANDLER_ARGS)
{
	int error, s, tt;

	tt = *(int *)oidp->oid_arg1;
	s = (int)((int64_t)tt * 1000 / hz);

	error = sysctl_handle_int(oidp, &s, 0, req);
	if (error || !req->newptr)
		return (error);

	tt = (int)((int64_t)s * hz / 1000);
	if (tt < 1)
		return (EINVAL);

	*(int *)oidp->oid_arg1 = tt;
	return (0);
}

int	tcp_keepinit;
SYSCTL_PROC(_net_inet_tcp, TCPCTL_KEEPINIT, keepinit, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_keepinit, 0, sysctl_msec_to_ticks, "I", "Time to establish TCP connection");

int	tcp_keepidle;
SYSCTL_PROC(_net_inet_tcp, TCPCTL_KEEPIDLE, keepidle, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_keepidle, 0, sysctl_msec_to_ticks, "I", "Time before TCP keepalive probes begin");

int	tcp_keepintvl;
SYSCTL_PROC(_net_inet_tcp, TCPCTL_KEEPINTVL, keepintvl, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_keepintvl, 0, sysctl_msec_to_ticks, "I", "Time between TCP keepalive probes");

int	tcp_delacktime;
SYSCTL_PROC(_net_inet_tcp, TCPCTL_DELACKTIME, delacktime,
    CTLTYPE_INT|CTLFLAG_RW, &tcp_delacktime, 0, sysctl_msec_to_ticks, "I",
    "Time before a delayed ACK is sent");

int	tcp_msl;
SYSCTL_PROC(_net_inet_tcp, OID_AUTO, msl, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_msl, 0, sysctl_msec_to_ticks, "I", "Maximum segment lifetime");

int	tcp_rexmit_min;
SYSCTL_PROC(_net_inet_tcp, OID_AUTO, rexmit_min, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_rexmit_min, 0, sysctl_msec_to_ticks, "I", "Minimum Retransmission Timeout");

int	tcp_rexmit_slop;
SYSCTL_PROC(_net_inet_tcp, OID_AUTO, rexmit_slop, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_rexmit_slop, 0, sysctl_msec_to_ticks, "I",
    "Retransmission Timer Slop");

static int	always_keepalive = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, always_keepalive, CTLFLAG_RW,
    &always_keepalive , 0, "Assume SO_KEEPALIVE on all TCP connections");

/* max idle probes */
int	tcp_keepcnt = TCPTV_KEEPCNT;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, keepcnt, CTLFLAG_RW,
    &tcp_keepcnt, 0, "Maximum number of keepalive probes to be sent");

static int tcp_do_eifel_response = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, eifel_response, CTLFLAG_RW,
    &tcp_do_eifel_response, 0, "Eifel response algorithm (RFC 4015)");

int tcp_eifel_rtoinc = 2;
SYSCTL_PROC(_net_inet_tcp, OID_AUTO, eifel_rtoinc, CTLTYPE_INT|CTLFLAG_RW,
    &tcp_eifel_rtoinc, 0, sysctl_msec_to_ticks, "I",
    "Eifel response RTO increment");

/* max idle time in persist */
int	tcp_maxpersistidle;

/*
 * Cancel all timers for TCP tp.
 */
void
tcp_canceltimers(struct tcpcb *tp)
{
	tcp_callout_stop(tp, tp->tt_2msl);
	tcp_callout_stop(tp, tp->tt_persist);
	tcp_callout_stop(tp, tp->tt_keep);
	tcp_callout_stop(tp, tp->tt_rexmt);
}

/*
 * Caller should be in critical section
 */
static void
tcp_send_timermsg(struct tcpcb *tp, uint32_t task)
{
	struct netmsg_tcp_timer *tmsg = tp->tt_msg;

	KKASSERT(tmsg != NULL && tmsg->tt_cpuid == mycpuid &&
		 tmsg->tt_tcb != NULL);

	tmsg->tt_tasks |= task;
	if (tmsg->tt_msg.lmsg.ms_flags & MSGF_DONE)
		lwkt_sendmsg_oncpu(tmsg->tt_msgport, &tmsg->tt_msg.lmsg);
}

int	tcp_syn_backoff[TCP_MAXRXTSHIFT + 1] =
    { 1, 1, 1, 1, 1, 2, 4, 8, 16, 32, 64, 64, 64 };

int	tcp_syn_backoff_low[TCP_MAXRXTSHIFT + 1] =
    { 1, 1, 2, 4, 8, 8, 16, 16, 32, 64, 64, 64, 64 };

int	tcp_backoff[TCP_MAXRXTSHIFT + 1] =
    { 1, 2, 4, 8, 16, 32, 64, 64, 64, 64, 64, 64, 64 };

static int tcp_totbackoff = 511;	/* sum of tcp_backoff[] */

/* Caller should be in critical section */
static struct tcpcb *
tcp_timer_delack_handler(struct tcpcb *tp)
{
	tp->t_flags |= TF_ACKNOW;
	tcpstat.tcps_delack++;
	tcp_output(tp);
	return tp;
}

/*
 * TCP timer processing.
 */
void
tcp_timer_delack(void *xtp)
{
	struct tcpcb *tp = xtp;
	struct callout *co = &tp->tt_delack->tc_callout;

	crit_enter();
	if (callout_pending(co) || !callout_active(co)) {
		crit_exit();
		return;
	}
	callout_deactivate(co);
	tcp_send_timermsg(tp, TCP_TIMER_DELACK);
	crit_exit();
}

/* Caller should be in critical section */
static struct tcpcb *
tcp_timer_2msl_handler(struct tcpcb *tp)
{
#ifdef TCPDEBUG
	int ostate;
#endif

#ifdef TCPDEBUG
	ostate = tp->t_state;
#endif
	/*
	 * 2 MSL timeout in shutdown went off.  If we're closed but
	 * still waiting for peer to close and connection has been idle
	 * too long, or if 2MSL time is up from TIME_WAIT, delete connection
	 * control block.  Otherwise, check again in a bit.
	 */
	if (tp->t_state != TCPS_TIME_WAIT &&
	    (ticks - tp->t_rcvtime) <= tp->t_maxidle) {
		tcp_callout_reset(tp, tp->tt_2msl, tp->t_keepintvl,
				  tcp_timer_2msl);
	} else {
		tp = tcp_close(tp);
	}

#ifdef TCPDEBUG
	if (tp && (tp->t_inpcb->inp_socket->so_options & SO_DEBUG))
		tcp_trace(TA_USER, ostate, tp, NULL, NULL, PRU_SLOWTIMO);
#endif
	return tp;
}

void
tcp_timer_2msl(void *xtp)
{
	struct tcpcb *tp = xtp;
	struct callout *co = &tp->tt_2msl->tc_callout;

	crit_enter();
	if (callout_pending(co) || !callout_active(co)) {
		crit_exit();
		return;
	}
	callout_deactivate(co);
	tcp_send_timermsg(tp, TCP_TIMER_2MSL);
	crit_exit();
}

/* Caller should be in critical section */
static struct tcpcb *
tcp_timer_keep_handler(struct tcpcb *tp)
{
	struct tcptemp *t_template;
#ifdef TCPDEBUG
	int ostate = tp->t_state;
#endif

	/*
	 * Keep-alive timer went off; send something
	 * or drop connection if idle for too long.
	 */
	tcpstat.tcps_keeptimeo++;
	if (tp->t_state < TCPS_ESTABLISHED)
		goto dropit;
	if ((always_keepalive || (tp->t_flags & TF_KEEPALIVE) ||
	     (tp->t_inpcb->inp_socket->so_options & SO_KEEPALIVE)) &&
	    tp->t_state <= TCPS_CLOSING) {
		if ((ticks - tp->t_rcvtime) >= tp->t_keepidle + tp->t_maxidle)
			goto dropit;
		/*
		 * Send a packet designed to force a response
		 * if the peer is up and reachable:
		 * either an ACK if the connection is still alive,
		 * or an RST if the peer has closed the connection
		 * due to timeout or reboot.
		 * Using sequence number tp->snd_una-1
		 * causes the transmitted zero-length segment
		 * to lie outside the receive window;
		 * by the protocol spec, this requires the
		 * correspondent TCP to respond.
		 */
		tcpstat.tcps_keepprobe++;
		t_template = tcp_maketemplate(tp);
		if (t_template) {
			tcp_respond(tp, t_template->tt_ipgen,
				    &t_template->tt_t, NULL,
				    tp->rcv_nxt, tp->snd_una - 1, 0);
			tcp_freetemplate(t_template);
		}
		tcp_callout_reset(tp, tp->tt_keep, tp->t_keepintvl,
				  tcp_timer_keep);
	} else {
		tcp_callout_reset(tp, tp->tt_keep, tp->t_keepidle,
				  tcp_timer_keep);
	}

#ifdef TCPDEBUG
	if (tp->t_inpcb->inp_socket->so_options & SO_DEBUG)
		tcp_trace(TA_USER, ostate, tp, NULL, NULL, PRU_SLOWTIMO);
#endif
	return tp;

dropit:
	tcpstat.tcps_keepdrops++;
	tp = tcp_drop(tp, ETIMEDOUT);

#ifdef TCPDEBUG
	if (tp && (tp->t_inpcb->inp_socket->so_options & SO_DEBUG))
		tcp_trace(TA_USER, ostate, tp, NULL, NULL, PRU_SLOWTIMO);
#endif
	return tp;
}

void
tcp_timer_keep(void *xtp)
{
	struct tcpcb *tp = xtp;
	struct callout *co = &tp->tt_keep->tc_callout;

	crit_enter();
	if (callout_pending(co) || !callout_active(co)) {
		crit_exit();
		return;
	}
	callout_deactivate(co);
	tcp_send_timermsg(tp, TCP_TIMER_KEEP);
	crit_exit();
}

/* Caller should be in critical section */
static struct tcpcb *
tcp_timer_persist_handler(struct tcpcb *tp)
{
#ifdef TCPDEBUG
	int ostate;
#endif

#ifdef TCPDEBUG
	ostate = tp->t_state;
#endif
	/*
	 * Persistance timer into zero window.
	 * Force a byte to be output, if possible.
	 */
	tcpstat.tcps_persisttimeo++;
	/*
	 * Hack: if the peer is dead/unreachable, we do not
	 * time out if the window is closed.  After a full
	 * backoff, drop the connection if the idle time
	 * (no responses to probes) reaches the maximum
	 * backoff that we would use if retransmitting.
	 */
	if (tp->t_rxtshift == TCP_MAXRXTSHIFT &&
	    ((ticks - tp->t_rcvtime) >= tcp_maxpersistidle ||
	     (ticks - tp->t_rcvtime) >= TCP_REXMTVAL(tp) * tcp_totbackoff)) {
		tcpstat.tcps_persistdrop++;
		tp = tcp_drop(tp, ETIMEDOUT);
		goto out;
	}
	tcp_setpersist(tp);
	tp->t_flags |= TF_FORCE;
	tcp_output(tp);
	tp->t_flags &= ~TF_FORCE;

out:
#ifdef TCPDEBUG
	if (tp && tp->t_inpcb->inp_socket->so_options & SO_DEBUG)
		tcp_trace(TA_USER, ostate, tp, NULL, NULL, PRU_SLOWTIMO);
#endif
	return tp;
}

void
tcp_timer_persist(void *xtp)
{
	struct tcpcb *tp = xtp;
	struct callout *co = &tp->tt_persist->tc_callout;

	crit_enter();
	if (callout_pending(co) || !callout_active(co)){
		crit_exit();
		return;
	}
	callout_deactivate(co);
	tcp_send_timermsg(tp, TCP_TIMER_PERSIST);
	crit_exit();
}

void
tcp_save_congestion_state(struct tcpcb *tp)
{
	/*
	 * Record connection's current states so that they could be
	 * recovered, if this turns out to be a spurious retransmit.
	 */
	tp->snd_cwnd_prev = tp->snd_cwnd;
	tp->snd_wacked_prev = tp->snd_wacked;
	tp->snd_ssthresh_prev = tp->snd_ssthresh;
	tp->snd_recover_prev = tp->snd_recover;

	/*
	 * State for Eifel response after spurious timeout retransmit
	 * is detected.  We save the current value of snd_max even if
	 * we are called from fast retransmit code, so if RTO needs
	 * rebase, it will be rebased using the RTT of segment that
	 * is not sent during possible congestion.
	 */
	tp->snd_max_prev = tp->snd_max;

	if (IN_FASTRECOVERY(tp))
		tp->rxt_flags |= TRXT_F_WASFRECOVERY;
	else
		tp->rxt_flags &= ~TRXT_F_WASFRECOVERY;
	if (tp->t_flags & TF_RCVD_TSTMP) {
		/* States for Eifel detection */
		tp->t_rexmtTS = ticks;
		tp->rxt_flags |= TRXT_F_FIRSTACCACK;
	}
#ifdef later
	tcp_sack_save_scoreboard(&tp->scb);
#endif
}

void
tcp_revert_congestion_state(struct tcpcb *tp)
{
	tp->snd_cwnd = tp->snd_cwnd_prev;
	tp->snd_wacked = tp->snd_wacked_prev;
	tp->snd_ssthresh = tp->snd_ssthresh_prev;
	tp->snd_recover = tp->snd_recover_prev;
	if (tp->rxt_flags & TRXT_F_WASFRECOVERY)
		ENTER_FASTRECOVERY(tp);
	if (tp->rxt_flags & TRXT_F_FASTREXMT) {
		++tcpstat.tcps_sndfastrexmitbad;
		if (tp->rxt_flags & TRXT_F_EARLYREXMT)
			++tcpstat.tcps_sndearlyrexmitbad;
	} else {
		++tcpstat.tcps_sndrtobad;
		tp->snd_last = ticks;
		if (tcp_do_eifel_response)
			tp->rxt_flags |= TRXT_F_REBASERTO;
	}
	tp->t_badrxtwin = 0;
	tp->t_rxtshift = 0;
	tp->snd_nxt = tp->snd_max;
#ifdef later
	tcp_sack_revert_scoreboard(&tp->scb, tp->snd_una);
#endif
}

/* Caller should be in critical section */
static struct tcpcb *
tcp_timer_rexmt_handler(struct tcpcb *tp)
{
	int rexmt;
#ifdef TCPDEBUG
	int ostate;
#endif

#ifdef TCPDEBUG
	ostate = tp->t_state;
#endif
	/*
	 * Retransmission timer went off.  Message has not
	 * been acked within retransmit interval.  Back off
	 * to a longer retransmit interval and retransmit one segment.
	 */
	if (++tp->t_rxtshift > TCP_MAXRXTSHIFT) {
		tp->t_rxtshift = TCP_MAXRXTSHIFT;
		tcpstat.tcps_timeoutdrop++;
		tp = tcp_drop(tp, tp->t_softerror ?
			      tp->t_softerror : ETIMEDOUT);
		goto out;
	}
	if (tp->t_rxtshift == 1) {
		/*
		 * First retransmit.
		 */

		/*
		 * State for "RTT based spurious timeout retransmit detection"
		 *
		 * RTT based spurious timeout retransmit detection:
		 * A retransmit is considered spurious if an ACK for this
		 * segment is received within RTT/2 interval; the assumption
		 * here is that the ACK was already in flight.  See
		 * "On Estimating End-to-End Network Path Properties" by
		 * Allman and Paxson for more details.
		 */
		tp->t_badrxtwin = ticks + (tp->t_srtt >> (TCP_RTT_SHIFT + 1));

		/*
		 * States for Eifel response after spurious timeout retransmit
		 * is detected.
		 */
		tp->t_rxtcur_prev = tp->t_rxtcur;
		tp->t_srtt_prev = tp->t_srtt +
		    (tcp_eifel_rtoinc << TCP_RTT_SHIFT);
		tp->t_rttvar_prev = tp->t_rttvar;

		tcp_save_congestion_state(tp);
		tp->rxt_flags &= ~(TRXT_F_FASTREXMT | TRXT_F_EARLYREXMT |
		    TRXT_F_REBASERTO);
	}
	if (tp->t_state == TCPS_SYN_SENT || tp->t_state == TCPS_SYN_RECEIVED) {
		/*
		 * Record the time that we spent in SYN or SYN|ACK
		 * retransmition.
		 *
		 * Needed by RFC3390 and RFC6298.
		 */
		tp->t_rxtsyn += tp->t_rxtcur;
	}
	/* Throw away SACK blocks on a RTO, as specified by RFC2018. */
	tcp_sack_discard(tp);
	tcpstat.tcps_rexmttimeo++;
	if (tp->t_state == TCPS_SYN_SENT) {
		if (tcp_low_rtobase) {
			rexmt = TCP_REXMTVAL(tp) *
				tcp_syn_backoff_low[tp->t_rxtshift];
		} else {
			rexmt = TCP_REXMTVAL(tp) *
				tcp_syn_backoff[tp->t_rxtshift];
		}
	} else {
		rexmt = TCP_REXMTVAL(tp) * tcp_backoff[tp->t_rxtshift];
	}
	TCPT_RANGESET(tp->t_rxtcur, rexmt,
		      tp->t_rttmin, TCPTV_REXMTMAX);
	/*
	 * If losing, let the lower level know and try for
	 * a better route.  Also, if we backed off this far,
	 * our srtt estimate is probably bogus.  Clobber it
	 * so we'll take the next rtt measurement as our srtt;
	 * move the current srtt into rttvar to keep the current
	 * retransmit times until then.
	 */
	if (tp->t_rxtshift > TCP_MAXRXTSHIFT / 4) {
#ifdef INET6
		if (INP_ISIPV6(tp->t_inpcb))
			in6_losing(tp->t_inpcb);
		else
#endif
		in_losing(tp->t_inpcb);
		tp->t_rttvar += (tp->t_srtt >> TCP_RTT_SHIFT);
		tp->t_srtt = 0;
	}
	tp->snd_nxt = tp->snd_una;
	tp->snd_recover = tp->snd_max;
	/*
	 * Force a segment to be sent.
	 */
	tp->t_flags |= TF_ACKNOW;
	/*
	 * If timing a segment in this window, stop the timer.
	 */
	tp->t_rtttime = 0;
	/*
	 * Close the congestion window down to one segment
	 * (we'll open it by one segment for each ack we get).
	 * Since we probably have a window's worth of unacked
	 * data accumulated, this "slow start" keeps us from
	 * dumping all that data as back-to-back packets (which
	 * might overwhelm an intermediate gateway).
	 *
	 * There are two phases to the opening: Initially we
	 * open by one mss on each ack.  This makes the window
	 * size increase exponentially with time.  If the
	 * window is larger than the path can handle, this
	 * exponential growth results in dropped packet(s)
	 * almost immediately.  To get more time between
	 * drops but still "push" the network to take advantage
	 * of improving conditions, we switch from exponential
	 * to linear window opening at some threshhold size.
	 * For a threshhold, we use half the current window
	 * size, truncated to a multiple of the mss.
	 *
	 * (the minimum cwnd that will give us exponential
	 * growth is 2 mss.  We don't allow the threshhold
	 * to go below this.)
	 */
	{
		u_int win = min(tp->snd_wnd, tp->snd_cwnd) / 2 / tp->t_maxseg;

		if (win < 2)
			win = 2;
		tp->snd_cwnd = tp->t_maxseg;
		tp->snd_wacked = 0;
		tp->snd_ssthresh = win * tp->t_maxseg;
		tp->t_dupacks = 0;
	}
	EXIT_FASTRECOVERY(tp);
	tcp_output(tp);

out:
#ifdef TCPDEBUG
	if (tp && (tp->t_inpcb->inp_socket->so_options & SO_DEBUG))
		tcp_trace(TA_USER, ostate, tp, NULL, NULL, PRU_SLOWTIMO);
#endif
	return tp;
}

void
tcp_timer_rexmt(void *xtp)
{
	struct tcpcb *tp = xtp;
	struct callout *co = &tp->tt_rexmt->tc_callout;

	crit_enter();
	if (callout_pending(co) || !callout_active(co)) {
		crit_exit();
		return;
	}
	callout_deactivate(co);
	tcp_send_timermsg(tp, TCP_TIMER_REXMT);
	crit_exit();
}

static void
tcp_timer_handler(netmsg_t msg)
{
	struct netmsg_tcp_timer *tmsg = (struct netmsg_tcp_timer *)msg;
	const struct tcp_timer *tt;
	struct tcpcb *tp;

	crit_enter();

	KKASSERT(tmsg->tt_cpuid == mycpuid && tmsg->tt_tcb != NULL);
	tp = tmsg->tt_tcb;

	/* Save pending tasks and reset the tasks in message */
	tmsg->tt_running_tasks = tmsg->tt_tasks;
	tmsg->tt_prev_tasks = tmsg->tt_tasks;
	tmsg->tt_tasks = 0;

	/* Reply ASAP */
	lwkt_replymsg(&tmsg->tt_msg.lmsg, 0);

	if (tmsg->tt_running_tasks == 0) {
		/*
		 * All of the timers are cancelled when the message
		 * is pending; bail out.
		 */
		crit_exit();
		return;
	}

	for (tt = tcp_timer_handlers; tt->tt_handler != NULL; ++tt) {
		if ((tmsg->tt_running_tasks & tt->tt_task) == 0)
			continue;

		tmsg->tt_running_tasks &= ~tt->tt_task;
		tp = tt->tt_handler(tp);
		if (tp == NULL)
			break;

		if (tmsg->tt_running_tasks == 0) /* nothing left to do */
			break;
	}

	crit_exit();
}

void
tcp_create_timermsg(struct tcpcb *tp, struct lwkt_port *msgport)
{
	struct netmsg_tcp_timer *tmsg = tp->tt_msg;

	netmsg_init(&tmsg->tt_msg, NULL, &netisr_adone_rport,
		    MSGF_DROPABLE | MSGF_PRIORITY, tcp_timer_handler);
	tmsg->tt_cpuid = mycpuid;
	tmsg->tt_msgport = msgport;
	tmsg->tt_tcb = tp;
	tmsg->tt_tasks = 0;
}

void
tcp_destroy_timermsg(struct tcpcb *tp)
{
	struct netmsg_tcp_timer *tmsg = tp->tt_msg;

	if (tmsg == NULL ||		/* listen socket */
	    tmsg->tt_tcb == NULL)	/* only tcp_attach() is called */
		return;

	KKASSERT(tmsg->tt_cpuid == mycpuid);

	/*
	 * This message is still pending to be processed;
	 * drop it.  Optimized.
	 */
	crit_enter();
	if ((tmsg->tt_msg.lmsg.ms_flags & MSGF_DONE) == 0) {
		lwkt_dropmsg(&tmsg->tt_msg.lmsg);
	}
	crit_exit();
}

static __inline void
tcp_callout_init(struct tcp_callout *tc, uint32_t task)
{
	callout_init_mp(&tc->tc_callout);
	tc->tc_task = task;
}

void
tcp_inittimers(struct tcpcb *tp)
{
	tcp_callout_init(tp->tt_rexmt, TCP_TIMER_REXMT);
	tcp_callout_init(tp->tt_persist, TCP_TIMER_PERSIST);
	tcp_callout_init(tp->tt_keep, TCP_TIMER_KEEP);
	tcp_callout_init(tp->tt_2msl, TCP_TIMER_2MSL);
	tcp_callout_init(tp->tt_delack, TCP_TIMER_DELACK);
}
