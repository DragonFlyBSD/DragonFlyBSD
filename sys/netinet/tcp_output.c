/*
 * Copyright (c) 2004 Jeffrey M. Hsu.  All rights reserved.
 * Copyright (c) 2004 The DragonFly Project.  All rights reserved.
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
 *	@(#)tcp_output.c	8.4 (Berkeley) 5/24/95
 * $FreeBSD: src/sys/netinet/tcp_output.c,v 1.39.2.20 2003/01/29 22:45:36 hsu Exp $
 * $DragonFly: src/sys/netinet/tcp_output.c,v 1.34 2007/04/22 01:13:14 dillon Exp $
 */

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"
#include "opt_tcpdebug.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/in_cksum.h>
#include <sys/thread.h>
#include <sys/globaldata.h>

#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet6/in6_pcb.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/tcp.h>
#define	TCPOUTFLAGS
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_timer2.h>
#include <netinet/tcp_var.h>
#include <netinet/tcpip.h>
#ifdef TCPDEBUG
#include <netinet/tcp_debug.h>
#endif

#ifdef IPSEC
#include <netinet6/ipsec.h>
#endif /*IPSEC*/

#ifdef FAST_IPSEC
#include <netproto/ipsec/ipsec.h>
#define	IPSEC
#endif /*FAST_IPSEC*/

#ifdef notyet
extern struct mbuf *m_copypack();
#endif

int path_mtu_discovery = 0;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, path_mtu_discovery, CTLFLAG_RW,
	&path_mtu_discovery, 1, "Enable Path MTU Discovery");

static int avoid_pure_win_update = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, avoid_pure_win_update, CTLFLAG_RW,
	&avoid_pure_win_update, 1, "Avoid pure window updates when possible");

int tcp_do_autosndbuf = 1;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, sendbuf_auto, CTLFLAG_RW,
    &tcp_do_autosndbuf, 0, "Enable automatic send buffer sizing");

int tcp_autosndbuf_inc = 8*1024;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, sendbuf_inc, CTLFLAG_RW,
    &tcp_autosndbuf_inc, 0, "Incrementor step size of automatic send buffer");

int tcp_autosndbuf_max = 2*1024*1024;
SYSCTL_INT(_net_inet_tcp, OID_AUTO, sendbuf_max, CTLFLAG_RW,
    &tcp_autosndbuf_max, 0, "Max size of automatic send buffer");

/*
 * Tcp output routine: figure out what should be sent and send it.
 */
int
tcp_output(struct tcpcb *tp)
{
	struct inpcb * const inp = tp->t_inpcb;
	struct socket *so = inp->inp_socket;
	long len, recvwin, sendwin;
	int nsacked = 0;
	int off, flags, error = 0;
#ifdef TCP_SIGNATURE
	int sigoff = 0;
#endif
	struct mbuf *m;
	struct ip *ip;
	struct ipovly *ipov;
	struct tcphdr *th;
	u_char opt[TCP_MAXOLEN];
	unsigned int ipoptlen, optlen, hdrlen;
	int idle;
	boolean_t sendalot;
	struct ip6_hdr *ip6;
#ifdef INET6
	const boolean_t isipv6 = (inp->inp_vflag & INP_IPV6) != 0;
#else
	const boolean_t isipv6 = FALSE;
#endif

	KKASSERT(so->so_port == &curthread->td_msgport);

	/*
	 * Determine length of data that should be transmitted,
	 * and flags that will be used.
	 * If there is some data or critical controls (SYN, RST)
	 * to send, then transmit; otherwise, investigate further.
	 */

	/*
	 * If we have been idle for a while, the send congestion window
	 * could be no longer representative of the current state of the link.
	 * So unless we are expecting more acks to come in, slow-start from
	 * scratch to re-determine the send congestion window.
	 */
	if (tp->snd_max == tp->snd_una &&
	    (ticks - tp->t_rcvtime) >= tp->t_rxtcur) {
		if (tcp_do_rfc3390 && !(tp->t_flags & TF_SYN_WASLOST)) {
			int initial_cwnd =
			    min(4 * tp->t_maxseg, max(2 * tp->t_maxseg, 4380));

			tp->snd_cwnd = min(tp->snd_cwnd, initial_cwnd);
		} else {
			tp->snd_cwnd = tp->t_maxseg;
		}
		tp->snd_wacked = 0;
	}

	/*
	 * Calculate whether the transmit stream was previously idle 
	 * and adjust TF_LASTIDLE for the next time.
	 */
	idle = (tp->t_flags & TF_LASTIDLE) || (tp->snd_max == tp->snd_una);
	if (idle && (tp->t_flags & TF_MORETOCOME))
		tp->t_flags |= TF_LASTIDLE;
	else
		tp->t_flags &= ~TF_LASTIDLE;

	if (TCP_DO_SACK(tp) && tp->snd_nxt != tp->snd_max &&
	    !IN_FASTRECOVERY(tp))
		nsacked = tcp_sack_bytes_below(&tp->scb, tp->snd_nxt);

again:
	m = NULL;
	ip = NULL;
	ipov = NULL;
	th = NULL;
	ip6 = NULL;

	/* Make use of SACK information when slow-starting after a RTO. */
	if (TCP_DO_SACK(tp) && tp->snd_nxt != tp->snd_max &&
	    !IN_FASTRECOVERY(tp)) {
		tcp_seq old_snd_nxt = tp->snd_nxt;

		tcp_sack_skip_sacked(&tp->scb, &tp->snd_nxt);
		nsacked += tp->snd_nxt - old_snd_nxt;
	}

	sendalot = FALSE;
	off = tp->snd_nxt - tp->snd_una;
	sendwin = min(tp->snd_wnd, tp->snd_cwnd + nsacked);
	sendwin = min(sendwin, tp->snd_bwnd);

	flags = tcp_outflags[tp->t_state];
	/*
	 * Get standard flags, and add SYN or FIN if requested by 'hidden'
	 * state flags.
	 */
	if (tp->t_flags & TF_NEEDFIN)
		flags |= TH_FIN;
	if (tp->t_flags & TF_NEEDSYN)
		flags |= TH_SYN;

	/*
	 * If in persist timeout with window of 0, send 1 byte.
	 * Otherwise, if window is small but nonzero
	 * and timer expired, we will send what we can
	 * and go to transmit state.
	 */
	if (tp->t_flags & TF_FORCE) {
		if (sendwin == 0) {
			/*
			 * If we still have some data to send, then
			 * clear the FIN bit.  Usually this would
			 * happen below when it realizes that we
			 * aren't sending all the data.  However,
			 * if we have exactly 1 byte of unsent data,
			 * then it won't clear the FIN bit below,
			 * and if we are in persist state, we wind
			 * up sending the packet without recording
			 * that we sent the FIN bit.
			 *
			 * We can't just blindly clear the FIN bit,
			 * because if we don't have any more data
			 * to send then the probe will be the FIN
			 * itself.
			 */
			if (off < so->so_snd.ssb_cc)
				flags &= ~TH_FIN;
			sendwin = 1;
		} else {
			tcp_callout_stop(tp, tp->tt_persist);
			tp->t_rxtshift = 0;
		}
	}

	/*
	 * If snd_nxt == snd_max and we have transmitted a FIN, the
	 * offset will be > 0 even if so_snd.ssb_cc is 0, resulting in
	 * a negative length.  This can also occur when TCP opens up
	 * its congestion window while receiving additional duplicate
	 * acks after fast-retransmit because TCP will reset snd_nxt
	 * to snd_max after the fast-retransmit.
	 *
	 * A negative length can also occur when we are in the
	 * TCPS_SYN_RECEIVED state due to a simultanious connect where
	 * our SYN has not been acked yet.
	 *
	 * In the normal retransmit-FIN-only case, however, snd_nxt will
	 * be set to snd_una, the offset will be 0, and the length may
	 * wind up 0.
	 */
	len = (long)ulmin(so->so_snd.ssb_cc, sendwin) - off;

	/*
	 * Lop off SYN bit if it has already been sent.  However, if this
	 * is SYN-SENT state and if segment contains data, suppress sending
	 * segment (sending the segment would be an option if we still
	 * did TAO and the remote host supported it).
	 */
	if ((flags & TH_SYN) && SEQ_GT(tp->snd_nxt, tp->snd_una)) {
		flags &= ~TH_SYN;
		off--, len++;
		if (len > 0 && tp->t_state == TCPS_SYN_SENT)
			return 0;
	}

	/*
	 * Be careful not to send data and/or FIN on SYN segments.
	 * This measure is needed to prevent interoperability problems
	 * with not fully conformant TCP implementations.
	 */
	if (flags & TH_SYN) {
		len = 0;
		flags &= ~TH_FIN;
	}

	if (len < 0) {
		/*
		 * A negative len can occur if our FIN has been sent but not
		 * acked, or if we are in a simultanious connect in the
		 * TCPS_SYN_RECEIVED state with our SYN sent but not yet
		 * acked.
		 *
		 * If our window has contracted to 0 in the FIN case
		 * (which can only occur if we have NOT been called to
		 * retransmit as per code a few paragraphs up) then we
		 * want to shift the retransmit timer over to the
		 * persist timer.
		 *
		 * However, if we are in the TCPS_SYN_RECEIVED state
		 * (the SYN case) we will be in a simultanious connect and
		 * the window may be zero degeneratively.  In this case we
		 * do not want to shift to the persist timer after the SYN
		 * or the SYN+ACK transmission.
		 */
		len = 0;
		if (sendwin == 0 && tp->t_state != TCPS_SYN_RECEIVED) {
			tcp_callout_stop(tp, tp->tt_rexmt);
			tp->t_rxtshift = 0;
			tp->snd_nxt = tp->snd_una;
			if (!tcp_callout_active(tp, tp->tt_persist))
				tcp_setpersist(tp);
		}
	}

	KASSERT(len >= 0, ("%s: len < 0", __func__));
	/*
	 * Automatic sizing of send socket buffer.  Often the send buffer
	 * size is not optimally adjusted to the actual network conditions
	 * at hand (delay bandwidth product).  Setting the buffer size too
	 * small limits throughput on links with high bandwidth and high
	 * delay (eg. trans-continental/oceanic links).  Setting the
	 * buffer size too big consumes too much real kernel memory,
	 * especially with many connections on busy servers.
	 *
	 * The criteria to step up the send buffer one notch are:
	 *  1. receive window of remote host is larger than send buffer
	 *     (with a fudge factor of 5/4th);
	 *  2. send buffer is filled to 7/8th with data (so we actually
	 *     have data to make use of it);
	 *  3. send buffer fill has not hit maximal automatic size;
	 *  4. our send window (slow start and cogestion controlled) is
	 *     larger than sent but unacknowledged data in send buffer.
	 *
	 * The remote host receive window scaling factor may limit the
	 * growing of the send buffer before it reaches its allowed
	 * maximum.
	 *
	 * It scales directly with slow start or congestion window
	 * and does at most one step per received ACK.  This fast
	 * scaling has the drawback of growing the send buffer beyond
	 * what is strictly necessary to make full use of a given
	 * delay*bandwith product.  However testing has shown this not
	 * to be much of an problem.  At worst we are trading wasting
	 * of available bandwith (the non-use of it) for wasting some
	 * socket buffer memory.
	 *
	 * TODO: Shrink send buffer during idle periods together
	 * with congestion window.  Requires another timer.  Has to
	 * wait for upcoming tcp timer rewrite.
	 */
	if (tcp_do_autosndbuf && so->so_snd.ssb_flags & SSB_AUTOSIZE) {
		if ((tp->snd_wnd / 4 * 5) >= so->so_snd.ssb_hiwat &&
		    so->so_snd.ssb_cc >= (so->so_snd.ssb_hiwat / 8 * 7) &&
		    so->so_snd.ssb_cc < tcp_autosndbuf_max &&
		    sendwin >= (so->so_snd.ssb_cc - (tp->snd_nxt - tp->snd_una))) {
			u_long newsize;

			newsize = ulmin(so->so_snd.ssb_hiwat +
					 tcp_autosndbuf_inc,
					tcp_autosndbuf_max);
			if (!ssb_reserve(&so->so_snd, newsize, so, NULL))
				atomic_clear_int(&so->so_snd.ssb_flags, SSB_AUTOSIZE);
			if (newsize >= (TCP_MAXWIN << tp->snd_scale))
				atomic_clear_int(&so->so_snd.ssb_flags, SSB_AUTOSIZE);
		}
	}

	/*
	 * Truncate to the maximum segment length and ensure that FIN is
	 * removed if the length no longer contains the last data byte.
	 */
	if (len > tp->t_maxseg) {
		len = tp->t_maxseg;
		sendalot = TRUE;
	}
	if (SEQ_LT(tp->snd_nxt + len, tp->snd_una + so->so_snd.ssb_cc))
		flags &= ~TH_FIN;

	recvwin = ssb_space(&so->so_rcv);

	/*
	 * Sender silly window avoidance.   We transmit under the following
	 * conditions when len is non-zero:
	 *
	 *	- We have a full segment
	 *	- This is the last buffer in a write()/send() and we are
	 *	  either idle or running NODELAY
	 *	- we've timed out (e.g. persist timer)
	 *	- we have more then 1/2 the maximum send window's worth of
	 *	  data (receiver may be limiting the window size)
	 *	- we need to retransmit
	 */
	if (len) {
		if (len == tp->t_maxseg)
			goto send;
		/*
		 * NOTE! on localhost connections an 'ack' from the remote
		 * end may occur synchronously with the output and cause
		 * us to flush a buffer queued with moretocome.  XXX
		 *
		 * note: the len + off check is almost certainly unnecessary.
		 */
		if (!(tp->t_flags & TF_MORETOCOME) &&	/* normal case */
		    (idle || (tp->t_flags & TF_NODELAY)) &&
		    len + off >= so->so_snd.ssb_cc &&
		    !(tp->t_flags & TF_NOPUSH)) {
			goto send;
		}
		if (tp->t_flags & TF_FORCE)		/* typ. timeout case */
			goto send;
		if (len >= tp->max_sndwnd / 2 && tp->max_sndwnd > 0)
			goto send;
		if (SEQ_LT(tp->snd_nxt, tp->snd_max))	/* retransmit case */
			goto send;
	}

	/*
	 * Compare available window to amount of window
	 * known to peer (as advertised window less
	 * next expected input).  If the difference is at least two
	 * max size segments, or at least 50% of the maximum possible
	 * window, then want to send a window update to peer.
	 */
	if (recvwin > 0) {
		/*
		 * "adv" is the amount we can increase the window,
		 * taking into account that we are limited by
		 * TCP_MAXWIN << tp->rcv_scale.
		 */
		long adv = min(recvwin, (long)TCP_MAXWIN << tp->rcv_scale) -
			(tp->rcv_adv - tp->rcv_nxt);
		long hiwat;

		/*
		 * This ack case typically occurs when the user has drained
		 * the TCP socket buffer sufficiently to warrent an ack
		 * containing a 'pure window update'... that is, an ack that
		 * ONLY updates the tcp window.
		 *
		 * It is unclear why we would need to do a pure window update
		 * past 2 segments if we are going to do one at 1/2 the high
		 * water mark anyway, especially since under normal conditions
		 * the user program will drain the socket buffer quickly.
		 * The 2-segment pure window update will often add a large
		 * number of extra, unnecessary acks to the stream.
		 *
		 * avoid_pure_win_update now defaults to 1.
		 */
		if (avoid_pure_win_update == 0 ||
		    (tp->t_flags & TF_RXRESIZED)) {
			if (adv >= (long) (2 * tp->t_maxseg)) {
				goto send;
			}
		}
		hiwat = (long)(TCP_MAXWIN << tp->rcv_scale);
		if (hiwat > (long)so->so_rcv.ssb_hiwat)
			hiwat = (long)so->so_rcv.ssb_hiwat;
		if (adv >= hiwat / 2)
			goto send;
	}

	/*
	 * Send if we owe the peer an ACK, RST, SYN, or urgent data.  ACKNOW
	 * is also a catch-all for the retransmit timer timeout case.
	 */
	if (tp->t_flags & TF_ACKNOW)
		goto send;
	if ((flags & TH_RST) ||
	    ((flags & TH_SYN) && !(tp->t_flags & TF_NEEDSYN)))
		goto send;
	if (SEQ_GT(tp->snd_up, tp->snd_una))
		goto send;
	/*
	 * If our state indicates that FIN should be sent
	 * and we have not yet done so, then we need to send.
	 */
	if ((flags & TH_FIN) &&
	    (!(tp->t_flags & TF_SENTFIN) || tp->snd_nxt == tp->snd_una))
		goto send;

	/*
	 * TCP window updates are not reliable, rather a polling protocol
	 * using ``persist'' packets is used to insure receipt of window
	 * updates.  The three ``states'' for the output side are:
	 *	idle			not doing retransmits or persists
	 *	persisting		to move a small or zero window
	 *	(re)transmitting	and thereby not persisting
	 *
	 * tcp_callout_active(tp, tp->tt_persist)
	 *	is true when we are in persist state.
	 * The TF_FORCE flag in tp->t_flags
	 *	is set when we are called to send a persist packet.
	 * tcp_callout_active(tp, tp->tt_rexmt)
	 *	is set when we are retransmitting
	 * The output side is idle when both timers are zero.
	 *
	 * If send window is too small, there is data to transmit, and no
	 * retransmit or persist is pending, then go to persist state.
	 *
	 * If nothing happens soon, send when timer expires:
	 * if window is nonzero, transmit what we can, otherwise force out
	 * a byte.
	 *
	 * Don't try to set the persist state if we are in TCPS_SYN_RECEIVED
	 * with data pending.  This situation can occur during a
	 * simultanious connect.
	 */
	if (so->so_snd.ssb_cc > 0 &&
	    tp->t_state != TCPS_SYN_RECEIVED &&
	    !tcp_callout_active(tp, tp->tt_rexmt) &&
	    !tcp_callout_active(tp, tp->tt_persist)) {
		tp->t_rxtshift = 0;
		tcp_setpersist(tp);
	}

	/*
	 * No reason to send a segment, just return.
	 */
	return (0);

send:
	/*
	 * Before ESTABLISHED, force sending of initial options
	 * unless TCP set not to do any options.
	 * NOTE: we assume that the IP/TCP header plus TCP options
	 * always fit in a single mbuf, leaving room for a maximum
	 * link header, i.e.
	 *	max_linkhdr + sizeof(struct tcpiphdr) + optlen <= MCLBYTES
	 */
	optlen = 0;
	if (isipv6)
		hdrlen = sizeof(struct ip6_hdr) + sizeof(struct tcphdr);
	else
		hdrlen = sizeof(struct tcpiphdr);
	if (flags & TH_SYN) {
		tp->snd_nxt = tp->iss;
		if (!(tp->t_flags & TF_NOOPT)) {
			u_short mss;

			opt[0] = TCPOPT_MAXSEG;
			opt[1] = TCPOLEN_MAXSEG;
			mss = htons((u_short) tcp_mssopt(tp));
			memcpy(opt + 2, &mss, sizeof mss);
			optlen = TCPOLEN_MAXSEG;

			if ((tp->t_flags & TF_REQ_SCALE) &&
			    (!(flags & TH_ACK) ||
			     (tp->t_flags & TF_RCVD_SCALE))) {
				*((u_int32_t *)(opt + optlen)) = htonl(
					TCPOPT_NOP << 24 |
					TCPOPT_WINDOW << 16 |
					TCPOLEN_WINDOW << 8 |
					tp->request_r_scale);
				optlen += 4;
			}

			if ((tcp_do_sack && !(flags & TH_ACK)) ||
			    tp->t_flags & TF_SACK_PERMITTED) {
				uint32_t *lp = (uint32_t *)(opt + optlen);

				*lp = htonl(TCPOPT_SACK_PERMITTED_ALIGNED);
				optlen += TCPOLEN_SACK_PERMITTED_ALIGNED;
			}
		}
	}

	/*
	 * Send a timestamp and echo-reply if this is a SYN and our side
	 * wants to use timestamps (TF_REQ_TSTMP is set) or both our side
	 * and our peer have sent timestamps in our SYN's.
	 */
	if ((tp->t_flags & (TF_REQ_TSTMP | TF_NOOPT)) == TF_REQ_TSTMP &&
	    !(flags & TH_RST) &&
	    (!(flags & TH_ACK) || (tp->t_flags & TF_RCVD_TSTMP))) {
		u_int32_t *lp = (u_int32_t *)(opt + optlen);

		/* Form timestamp option as shown in appendix A of RFC 1323. */
		*lp++ = htonl(TCPOPT_TSTAMP_HDR);
		*lp++ = htonl(ticks);
		*lp   = htonl(tp->ts_recent);
		optlen += TCPOLEN_TSTAMP_APPA;
	}

	/* Set receive buffer autosizing timestamp. */
	if (tp->rfbuf_ts == 0 && (so->so_rcv.ssb_flags & SSB_AUTOSIZE))
		tp->rfbuf_ts = ticks;

	/*
	 * If this is a SACK connection and we have a block to report,
	 * fill in the SACK blocks in the TCP options.
	 */
	if ((tp->t_flags & (TF_SACK_PERMITTED | TF_NOOPT)) ==
		TF_SACK_PERMITTED &&
	    (!LIST_EMPTY(&tp->t_segq) ||
	     tp->reportblk.rblk_start != tp->reportblk.rblk_end))
		tcp_sack_fill_report(tp, opt, &optlen);

#ifdef TCP_SIGNATURE
	if (tp->t_flags & TF_SIGNATURE) {
		int i;
		u_char *bp;
		/*
		 * Initialize TCP-MD5 option (RFC2385)
		 */
		bp = (u_char *)opt + optlen;
		*bp++ = TCPOPT_SIGNATURE;
		*bp++ = TCPOLEN_SIGNATURE;
		sigoff = optlen + 2;
		for (i = 0; i < TCP_SIGLEN; i++)
			*bp++ = 0;
		optlen += TCPOLEN_SIGNATURE;
		/*
		 * Terminate options list and maintain 32-bit alignment.
		 */
		*bp++ = TCPOPT_NOP;
		*bp++ = TCPOPT_EOL;
		optlen += 2;
	}
#endif /* TCP_SIGNATURE */
	KASSERT(optlen <= TCP_MAXOLEN, ("too many TCP options"));
	hdrlen += optlen;

	if (isipv6) {
		ipoptlen = ip6_optlen(inp);
	} else {
		if (inp->inp_options) {
			ipoptlen = inp->inp_options->m_len -
			    offsetof(struct ipoption, ipopt_list);
		} else {
			ipoptlen = 0;
		}
	}
#ifdef IPSEC
	ipoptlen += ipsec_hdrsiz_tcp(tp);
#endif

	/*
	 * Adjust data length if insertion of options will bump the packet
	 * length beyond the t_maxopd length.  Clear FIN to prevent premature
	 * closure since there is still more data to send after this (now
	 * truncated) packet.
	 *
	 * If just the options do not fit we are in a no-win situation and
	 * we treat it as an unreachable host.
	 */
	if (len + optlen + ipoptlen > tp->t_maxopd) {
		if (tp->t_maxopd <= optlen + ipoptlen) {
			static time_t last_optlen_report;

			if (last_optlen_report != time_second) {
				last_optlen_report = time_second;
				kprintf("tcpcb %p: MSS (%d) too small to hold options!\n", tp, tp->t_maxopd);
			}
			error = EHOSTUNREACH;
			goto out;
		} else {
			flags &= ~TH_FIN;
			len = tp->t_maxopd - optlen - ipoptlen;
			sendalot = TRUE;
		}
	}

#ifdef INET6
	KASSERT(max_linkhdr + hdrlen <= MCLBYTES, ("tcphdr too big"));
#else
	KASSERT(max_linkhdr + hdrlen <= MHLEN, ("tcphdr too big"));
#endif

	/*
	 * Grab a header mbuf, attaching a copy of data to
	 * be transmitted, and initialize the header from
	 * the template for sends on this connection.
	 */
	if (len) {
		if ((tp->t_flags & TF_FORCE) && len == 1)
			tcpstat.tcps_sndprobe++;
		else if (SEQ_LT(tp->snd_nxt, tp->snd_max)) {
			if (tp->snd_nxt == tp->snd_una)
				tp->snd_max_rexmt = tp->snd_max;
			if (nsacked) {
				tcpstat.tcps_sndsackrtopack++;
				tcpstat.tcps_sndsackrtobyte += len;
			}
			tcpstat.tcps_sndrexmitpack++;
			tcpstat.tcps_sndrexmitbyte += len;
		} else {
			tcpstat.tcps_sndpack++;
			tcpstat.tcps_sndbyte += len;
		}
#ifdef notyet
		if ((m = m_copypack(so->so_snd.ssb_mb, off, (int)len,
		    max_linkhdr + hdrlen)) == NULL) {
			error = ENOBUFS;
			goto after_th;
		}
		/*
		 * m_copypack left space for our hdr; use it.
		 */
		m->m_len += hdrlen;
		m->m_data -= hdrlen;
#else
#ifndef INET6
		m = m_gethdr(MB_DONTWAIT, MT_HEADER);
#else
		m = m_getl(hdrlen + max_linkhdr, MB_DONTWAIT, MT_HEADER,
			   M_PKTHDR, NULL);
#endif
		if (m == NULL) {
			error = ENOBUFS;
			goto after_th;
		}
		m->m_data += max_linkhdr;
		m->m_len = hdrlen;
		if (len <= MHLEN - hdrlen - max_linkhdr) {
			m_copydata(so->so_snd.ssb_mb, off, (int) len,
			    mtod(m, caddr_t) + hdrlen);
			m->m_len += len;
		} else {
			m->m_next = m_copy(so->so_snd.ssb_mb, off, (int) len);
			if (m->m_next == NULL) {
				m_free(m);
				m = NULL;
				error = ENOBUFS;
				goto after_th;
			}
		}
#endif
		/*
		 * If we're sending everything we've got, set PUSH.
		 * (This will keep happy those implementations which only
		 * give data to the user when a buffer fills or
		 * a PUSH comes in.)
		 */
		if (off + len == so->so_snd.ssb_cc)
			flags |= TH_PUSH;
	} else {
		if (tp->t_flags & TF_ACKNOW)
			tcpstat.tcps_sndacks++;
		else if (flags & (TH_SYN | TH_FIN | TH_RST))
			tcpstat.tcps_sndctrl++;
		else if (SEQ_GT(tp->snd_up, tp->snd_una))
			tcpstat.tcps_sndurg++;
		else
			tcpstat.tcps_sndwinup++;

		MGETHDR(m, MB_DONTWAIT, MT_HEADER);
		if (m == NULL) {
			error = ENOBUFS;
			goto after_th;
		}
		if (isipv6 &&
		    (hdrlen + max_linkhdr > MHLEN) && hdrlen <= MHLEN)
			MH_ALIGN(m, hdrlen);
		else
			m->m_data += max_linkhdr;
		m->m_len = hdrlen;
	}
	m->m_pkthdr.rcvif = NULL;
	if (isipv6) {
		ip6 = mtod(m, struct ip6_hdr *);
		th = (struct tcphdr *)(ip6 + 1);
		tcp_fillheaders(tp, ip6, th);
	} else {
		ip = mtod(m, struct ip *);
		ipov = (struct ipovly *)ip;
		th = (struct tcphdr *)(ip + 1);
		/* this picks up the pseudo header (w/o the length) */
		tcp_fillheaders(tp, ip, th);
	}
after_th:
	/*
	 * Fill in fields, remembering maximum advertised
	 * window for use in delaying messages about window sizes.
	 * If resending a FIN, be sure not to use a new sequence number.
	 */
	if (flags & TH_FIN && tp->t_flags & TF_SENTFIN &&
	    tp->snd_nxt == tp->snd_max)
		tp->snd_nxt--;

	if (th != NULL) {
		/*
		 * If we are doing retransmissions, then snd_nxt will
		 * not reflect the first unsent octet.  For ACK only
		 * packets, we do not want the sequence number of the
		 * retransmitted packet, we want the sequence number
		 * of the next unsent octet.  So, if there is no data
		 * (and no SYN or FIN), use snd_max instead of snd_nxt
		 * when filling in ti_seq.  But if we are in persist
		 * state, snd_max might reflect one byte beyond the
		 * right edge of the window, so use snd_nxt in that
		 * case, since we know we aren't doing a retransmission.
		 * (retransmit and persist are mutually exclusive...)
		 */
		if (len || (flags & (TH_SYN|TH_FIN)) ||
		    tcp_callout_active(tp, tp->tt_persist))
			th->th_seq = htonl(tp->snd_nxt);
		else
			th->th_seq = htonl(tp->snd_max);
		th->th_ack = htonl(tp->rcv_nxt);
		if (optlen) {
			bcopy(opt, th + 1, optlen);
			th->th_off = (sizeof(struct tcphdr) + optlen) >> 2;
		}
		th->th_flags = flags;
	}

	/*
	 * Calculate receive window.  Don't shrink window, but avoid
	 * silly window syndrome by sending a 0 window if the actual
	 * window is less then one segment.
	 */
	if (recvwin < (long)(so->so_rcv.ssb_hiwat / 4) &&
	    recvwin < (long)tp->t_maxseg)
		recvwin = 0;
	if (recvwin < (tcp_seq_diff_t)(tp->rcv_adv - tp->rcv_nxt))
		recvwin = (tcp_seq_diff_t)(tp->rcv_adv - tp->rcv_nxt);
	if (recvwin > (long)TCP_MAXWIN << tp->rcv_scale)
		recvwin = (long)TCP_MAXWIN << tp->rcv_scale;

	/*
	 * Adjust the RXWIN0SENT flag - indicate that we have advertised
	 * a 0 window.  This may cause the remote transmitter to stall.  This
	 * flag tells soreceive() to disable delayed acknowledgements when
	 * draining the buffer.  This can occur if the receiver is attempting
	 * to read more data then can be buffered prior to transmitting on
	 * the connection.
	 */
	if (recvwin == 0)
		tp->t_flags |= TF_RXWIN0SENT;
	else
		tp->t_flags &= ~TF_RXWIN0SENT;

	if (th != NULL)
		th->th_win = htons((u_short) (recvwin>>tp->rcv_scale));

	if (SEQ_GT(tp->snd_up, tp->snd_nxt)) {
		if (th != NULL) {
			th->th_urp = htons((u_short)(tp->snd_up - tp->snd_nxt));
			th->th_flags |= TH_URG;
		}
	} else {
		/*
		 * If no urgent pointer to send, then we pull
		 * the urgent pointer to the left edge of the send window
		 * so that it doesn't drift into the send window on sequence
		 * number wraparound.
		 */
		tp->snd_up = tp->snd_una;		/* drag it along */
	}

	if (th != NULL) {
#ifdef TCP_SIGNATURE
		if (tp->t_flags & TF_SIGNATURE) {
			tcpsignature_compute(m, len, optlen,
			    (u_char *)(th + 1) + sigoff, IPSEC_DIR_OUTBOUND);
		}
#endif /* TCP_SIGNATURE */

		/*
		 * Put TCP length in extended header, and then
		 * checksum extended header and data.
		 */
		m->m_pkthdr.len = hdrlen + len; /* in6_cksum() need this */
		if (isipv6) {
			/*
			 * ip6_plen is not need to be filled now, and will be
			 * filled in ip6_output().
			 */
			th->th_sum = in6_cksum(m, IPPROTO_TCP,
			    sizeof(struct ip6_hdr),
			    sizeof(struct tcphdr) + optlen + len);
		} else {
			m->m_pkthdr.csum_flags = CSUM_TCP;
			m->m_pkthdr.csum_data = offsetof(struct tcphdr, th_sum);
			if (len + optlen) {
				th->th_sum = in_addword(th->th_sum,
				    htons((u_short)(optlen + len)));
			}

			/*
			 * IP version must be set here for ipv4/ipv6 checking
			 * later
			 */
			KASSERT(ip->ip_v == IPVERSION,
			    ("%s: IP version incorrect: %d",
			     __func__, ip->ip_v));
		}
	}

	/*
	 * In transmit state, time the transmission and arrange for
	 * the retransmit.  In persist state, just set snd_max.
	 */
	if (!(tp->t_flags & TF_FORCE) ||
	    !tcp_callout_active(tp, tp->tt_persist)) {
		tcp_seq startseq = tp->snd_nxt;

		/*
		 * Advance snd_nxt over sequence space of this segment.
		 */
		if (flags & (TH_SYN | TH_FIN)) {
			if (flags & TH_SYN)
				tp->snd_nxt++;
			if (flags & TH_FIN) {
				tp->snd_nxt++;
				tp->t_flags |= TF_SENTFIN;
			}
		}
		tp->snd_nxt += len;
		if (SEQ_GT(tp->snd_nxt, tp->snd_max)) {
			tp->snd_max = tp->snd_nxt;
			/*
			 * Time this transmission if not a retransmission and
			 * not currently timing anything.
			 */
			if (tp->t_rtttime == 0) {
				tp->t_rtttime = ticks;
				tp->t_rtseq = startseq;
				tcpstat.tcps_segstimed++;
			}
		}

		/*
		 * Set retransmit timer if not currently set,
		 * and not doing a pure ack or a keep-alive probe.
		 * Initial value for retransmit timer is smoothed
		 * round-trip time + 2 * round-trip time variance.
		 * Initialize shift counter which is used for backoff
		 * of retransmit time.
		 */
		if (!tcp_callout_active(tp, tp->tt_rexmt) &&
		    tp->snd_nxt != tp->snd_una) {
			if (tcp_callout_active(tp, tp->tt_persist)) {
				tcp_callout_stop(tp, tp->tt_persist);
				tp->t_rxtshift = 0;
			}
			tcp_callout_reset(tp, tp->tt_rexmt, tp->t_rxtcur,
			    tcp_timer_rexmt);
		}
	} else {
		/*
		 * Persist case, update snd_max but since we are in
		 * persist mode (no window) we do not update snd_nxt.
		 */
		int xlen = len;
		if (flags & TH_SYN)
			panic("tcp_output: persist timer to send SYN\n");
		if (flags & TH_FIN) {
			++xlen;
			tp->t_flags |= TF_SENTFIN;
		}
		if (SEQ_GT(tp->snd_nxt + xlen, tp->snd_max))
			tp->snd_max = tp->snd_nxt + xlen;
	}

	if (th != NULL) {
#ifdef TCPDEBUG
		/* Trace. */
		if (so->so_options & SO_DEBUG) {
			tcp_trace(TA_OUTPUT, tp->t_state, tp,
			    mtod(m, void *), th, 0);
		}
#endif

		/*
		 * Fill in IP length and desired time to live and
		 * send to IP level.  There should be a better way
		 * to handle ttl and tos; we could keep them in
		 * the template, but need a way to checksum without them.
		 */
		/*
		 * m->m_pkthdr.len should have been set before cksum
		 * calcuration, because in6_cksum() need it.
		 */
		if (isipv6) {
			/*
			 * we separately set hoplimit for every segment,
			 * since the user might want to change the value
			 * via setsockopt.  Also, desired default hop
			 * limit might be changed via Neighbor Discovery.
			 */
			ip6->ip6_hlim = in6_selecthlim(inp,
			    (inp->in6p_route.ro_rt ?
			     inp->in6p_route.ro_rt->rt_ifp : NULL));

			/* TODO: IPv6 IP6TOS_ECT bit on */
			error = ip6_output(m, inp->in6p_outputopts,
			    &inp->in6p_route, (so->so_options & SO_DONTROUTE),
			    NULL, NULL, inp);
		} else {
			struct rtentry *rt;
			ip->ip_len = m->m_pkthdr.len;
#ifdef INET6
			if (INP_CHECK_SOCKAF(so, AF_INET6))
				ip->ip_ttl = in6_selecthlim(inp,
				    (inp->in6p_route.ro_rt ?
				     inp->in6p_route.ro_rt->rt_ifp : NULL));
			else
#endif
				ip->ip_ttl = inp->inp_ip_ttl;	/* XXX */

			ip->ip_tos = inp->inp_ip_tos;	/* XXX */
			/*
			 * See if we should do MTU discovery.
			 * We do it only if the following are true:
			 *	1) we have a valid route to the destination
			 *	2) the MTU is not locked (if it is,
			 *	   then discovery has been disabled)
			 */
			if (path_mtu_discovery &&
			    (rt = inp->inp_route.ro_rt) &&
			    (rt->rt_flags & RTF_UP) &&
			    !(rt->rt_rmx.rmx_locks & RTV_MTU))
				ip->ip_off |= IP_DF;

			error = ip_output(m, inp->inp_options, &inp->inp_route,
					  (so->so_options & SO_DONTROUTE) |
					  IP_DEBUGROUTE, NULL, inp);
		}
	} else {
		KASSERT(error != 0, ("no error, but th not set\n"));
	}
	if (error) {

		/*
		 * We know that the packet was lost, so back out the
		 * sequence number advance, if any.
		 */
		if (!(tp->t_flags & TF_FORCE) ||
		    !tcp_callout_active(tp, tp->tt_persist)) {
			/*
			 * No need to check for TH_FIN here because
			 * the TF_SENTFIN flag handles that case.
			 */
			if (!(flags & TH_SYN))
				tp->snd_nxt -= len;
		}

out:
		if (error == ENOBUFS) {
			/*
			 * If we can't send, make sure there is something
			 * to get us going again later.
			 *
			 * The persist timer isn't necessarily allowed in all
			 * states, use the rexmt timer.
			 */
			if (!tcp_callout_active(tp, tp->tt_rexmt) &&
			    !tcp_callout_active(tp, tp->tt_persist)) {
				tcp_callout_reset(tp, tp->tt_rexmt,
						  tp->t_rxtcur,
						  tcp_timer_rexmt);
#if 0
				tp->t_rxtshift = 0;
				tcp_setpersist(tp);
#endif
			}
			tcp_quench(inp, 0);
			return (0);
		}
		if (error == EMSGSIZE) {
			/*
			 * ip_output() will have already fixed the route
			 * for us.  tcp_mtudisc() will, as its last action,
			 * initiate retransmission, so it is important to
			 * not do so here.
			 */
			tcp_mtudisc(inp, 0);
			return 0;
		}
		if ((error == EHOSTUNREACH || error == ENETDOWN) &&
		    TCPS_HAVERCVDSYN(tp->t_state)) {
			tp->t_softerror = error;
			return (0);
		}
		return (error);
	}
	tcpstat.tcps_sndtotal++;

	/*
	 * Data sent (as far as we can tell).
	 *
	 * If this advertises a larger window than any other segment,
	 * then remember the size of the advertised window.
	 *
	 * Any pending ACK has now been sent.
	 */
	if (recvwin > 0 && SEQ_GT(tp->rcv_nxt + recvwin, tp->rcv_adv)) {
		tp->rcv_adv = tp->rcv_nxt + recvwin;
		tp->t_flags &= ~TF_RXRESIZED;
	}
	tp->last_ack_sent = tp->rcv_nxt;
	tp->t_flags &= ~TF_ACKNOW;
	if (tcp_delack_enabled)
		tcp_callout_stop(tp, tp->tt_delack);
	if (sendalot)
		goto again;
	return (0);
}

void
tcp_setpersist(struct tcpcb *tp)
{
	int t = ((tp->t_srtt >> 2) + tp->t_rttvar) >> 1;
	int tt;

	if (tp->t_state == TCPS_SYN_SENT ||
	    tp->t_state == TCPS_SYN_RECEIVED) {
		panic("tcp_setpersist: not established yet, current %s\n",
		      tp->t_state == TCPS_SYN_SENT ?
		      "SYN_SENT" : "SYN_RECEIVED");
	}

	if (tcp_callout_active(tp, tp->tt_rexmt))
		panic("tcp_setpersist: retransmit pending");
	/*
	 * Start/restart persistance timer.
	 */
	TCPT_RANGESET(tt, t * tcp_backoff[tp->t_rxtshift], TCPTV_PERSMIN,
		      TCPTV_PERSMAX);
	tcp_callout_reset(tp, tp->tt_persist, tt, tcp_timer_persist);
	if (tp->t_rxtshift < TCP_MAXRXTSHIFT)
		tp->t_rxtshift++;
}
