/*
 * Copyright (c) 2003-2004 Jeffrey Hsu.  All rights reserved.
 * Copyright (c) 1982, 1986, 1993, 1994, 1995
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
 *	@(#)tcp_var.h	8.4 (Berkeley) 5/24/95
 * $FreeBSD: src/sys/netinet/tcp_var.h,v 1.56.2.13 2003/02/03 02:34:07 hsu Exp $
 * $DragonFly: src/sys/netinet/tcp_var.h,v 1.16 2004/04/07 17:01:25 dillon Exp $
 */

#ifndef _NETINET_TCP_VAR_H_
#define _NETINET_TCP_VAR_H_

#include <netinet/in_pcb.h>		/* needed for in_conninfo, inp_gen_t */

#ifndef _NETINET_TCP_STATS_H_
#include <netinet/tcp_stats.h>
#endif

/*
 * Kernel variables for tcp.
 */
extern int	tcp_do_rfc1323;
extern int	tcp_do_rfc1644;

/* TCP segment queue entry */
struct tseg_qent {
	LIST_ENTRY(tseg_qent) tqe_q;
	int	tqe_len;		/* TCP segment data length */
	struct	tcphdr *tqe_th;		/* a pointer to tcp header */
	struct	mbuf	*tqe_m;		/* mbuf contains packet */
};
LIST_HEAD(tsegqe_head, tseg_qent);
extern int	tcp_reass_maxseg;
extern int	tcp_reass_qsize;
#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_TSEGQ);
#endif

struct tcptemp {
	u_char	tt_ipgen[40]; /* the size must be of max ip header, now IPv6 */
	struct	tcphdr tt_t;
};

#define tcp6cb		tcpcb  /* for KAME src sync over BSD*'s */

/*
 * Tcp control block, one per tcp; fields:
 * Organized for 16 byte cacheline efficiency.
 */
struct tcpcb {
	struct	tsegqe_head t_segq;
	int	t_dupacks;		/* consecutive dup acks recd */
	struct	tcptemp	*unused;	/* unused */

	struct	callout *tt_rexmt;	/* retransmit timer */
	struct	callout *tt_persist;	/* retransmit persistence */
	struct	callout *tt_keep;	/* keepalive */
	struct	callout *tt_2msl;	/* 2*msl TIME_WAIT timer */
	struct	callout *tt_delack;	/* delayed ACK timer */

	struct	inpcb *t_inpcb;		/* back pointer to internet pcb */
	int	t_state;		/* state of this connection */
	u_int	t_flags;
#define	TF_ACKNOW	0x00000001	/* ack peer immediately */
#define	TF_DELACK	0x00000002	/* ack, but try to delay it */
#define	TF_NODELAY	0x00000004	/* don't delay packets to coalesce */
#define	TF_NOOPT	0x00000008	/* don't use tcp options */
#define	TF_SENTFIN	0x00000010	/* have sent FIN */
#define	TF_REQ_SCALE	0x00000020	/* have/will request window scaling */
#define	TF_RCVD_SCALE	0x00000040	/* other side has requested scaling */
#define	TF_REQ_TSTMP	0x00000080	/* have/will request timestamps */
#define	TF_RCVD_TSTMP	0x00000100	/* a timestamp was received in SYN */
#define	TF_SACK_PERMIT	0x00000200	/* other side said I could SACK */
#define	TF_NEEDSYN	0x00000400	/* send SYN (implicit state) */
#define	TF_NEEDFIN	0x00000800	/* send FIN (implicit state) */
#define	TF_NOPUSH	0x00001000	/* don't push */
#define	TF_REQ_CC	0x00002000	/* have/will request CC */
#define	TF_RCVD_CC	0x00004000	/* a CC was received in SYN */
#define	TF_SENDCCNEW	0x00008000	/* send CCnew instead of CC in SYN */
#define	TF_MORETOCOME	0x00010000	/* More data to be appended to sock */
#define	TF_LQ_OVERFLOW	0x00020000	/* listen queue overflow */
#define	TF_LASTIDLE	0x00040000	/* connection was previously idle */
#define	TF_RXWIN0SENT	0x00080000	/* sent a receiver win 0 in response */
#define	TF_FASTRECOVERY	0x00100000	/* in NewReno Fast Recovery */
#define	TF_WASFRECOVERY	0x00200000	/* was in NewReno Fast Recovery */
#define	TF_FIRSTACCACK	0x00400000	/* Look for 1st acceptable ACK. */
#define	TF_FASTREXMT	0x00800000	/* Did Fast Retransmit. */
#define	TF_EARLYREXMT	0x01000000	/* Did Early (Fast) Retransmit. */
	int	t_force;		/* 1 if forcing out a byte */

	tcp_seq	snd_una;		/* send unacknowledged */
	tcp_seq	snd_max;		/* highest sequence number sent;
					 * used to recognize retransmits
					 */
	tcp_seq	snd_nxt;		/* send next */
	tcp_seq	snd_up;			/* send urgent pointer */

	tcp_seq	snd_wl1;		/* window update seg seq number */
	tcp_seq	snd_wl2;		/* window update seg ack number */
	tcp_seq	iss;			/* initial send sequence number */
	tcp_seq	irs;			/* initial receive sequence number */

	tcp_seq	rcv_nxt;		/* receive next */
	tcp_seq	rcv_adv;		/* advertised window */
	u_long	rcv_wnd;		/* receive window */
	tcp_seq	rcv_up;			/* receive urgent pointer */

	u_long	snd_wnd;		/* send window */
	u_long	snd_cwnd;		/* congestion-controlled window */
	u_long	snd_bwnd;		/* bandwidth-controlled window */
	u_long	snd_ssthresh;		/* snd_cwnd size threshold for
					 * for slow start exponential to
					 * linear switch
					 */
	u_long	snd_bandwidth;		/* calculated bandwidth or 0 */
	tcp_seq	snd_recover;		/* for use in NewReno fast recovery */

	u_int	t_maxopd;		/* mss plus options */

	u_long	t_rcvtime;		/* inactivity time */
	u_long	t_starttime;		/* time connection was established */
	int	t_rtttime;		/* round trip time */
	tcp_seq	t_rtseq;		/* sequence number being timed */

	int	t_bw_rtttime;		/* used for bandwidth calculation */
	tcp_seq	t_bw_rtseq;		/* used for bandwidth calculation */

	int	t_rxtcur;		/* current retransmit value (ticks) */
	u_int	t_maxseg;		/* maximum segment size */
	int	t_srtt;			/* smoothed round-trip time */
	int	t_rttvar;		/* variance in round-trip time */

	int	t_rxtshift;		/* log(2) of rexmt exp. backoff */
	u_int	t_rttmin;		/* minimum rtt allowed */
	u_int	t_rttbest;		/* best rtt we've seen */
	u_long	t_rttupdated;		/* number of times rtt sampled */
	u_long	max_sndwnd;		/* largest window peer has offered */

	int	t_softerror;		/* possible error not yet reported */
/* out-of-band data */
	char	t_oobflags;		/* have some */
	char	t_iobc;			/* input character */
#define	TCPOOB_HAVEDATA	0x01
#define	TCPOOB_HADDATA	0x02
/* RFC 1323 variables */
	u_char	snd_scale;		/* window scaling for send window */
	u_char	rcv_scale;		/* window scaling for recv window */
	u_char	request_r_scale;	/* pending window scaling */
	u_char	requested_s_scale;
	u_long	ts_recent;		/* timestamp echo data */

	u_long	ts_recent_age;		/* when last updated */
	tcp_seq	last_ack_sent;
/* RFC 1644 variables */
	tcp_cc	cc_send;		/* send connection count */
	tcp_cc	cc_recv;		/* receive connection count */
/* experimental */
	u_long	snd_cwnd_prev;		/* cwnd prior to retransmit */
	u_long	snd_ssthresh_prev;	/* ssthresh prior to retransmit */
	tcp_seq snd_recover_prev;	/* snd_recover prior to retransmit */
	u_long	t_badrxtwin;		/* window for retransmit recovery */
	u_long	t_rexmtTS;		/* timestamp of last retransmit */
	u_char	snd_limited;		/* segments limited transmitted */
};

#define	IN_FASTRECOVERY(tp)	(tp->t_flags & TF_FASTRECOVERY)
#define	ENTER_FASTRECOVERY(tp)	tp->t_flags |= TF_FASTRECOVERY
#define	EXIT_FASTRECOVERY(tp)	tp->t_flags &= ~TF_FASTRECOVERY

/*
 * Structure to hold TCP options that are only used during segment
 * processing (in tcp_input), but not held in the tcpcb.
 * It's basically used to reduce the number of parameters
 * to tcp_dooptions.
 */
struct tcpopt {
	u_long		to_flags;	/* which options are present */
#define TOF_TS		0x0001		/* timestamp */
#define TOF_CC		0x0002		/* CC and CCnew are exclusive */
#define TOF_CCNEW	0x0004
#define	TOF_CCECHO	0x0008
#define	TOF_MSS		0x0010
#define	TOF_SCALE	0x0020
	u_int32_t	to_tsval;
	u_int32_t	to_tsecr;
	tcp_cc		to_cc;		/* holds CC or CCnew */
	tcp_cc		to_ccecho;
	u_int16_t	to_mss;
	u_int8_t 	to_requested_s_scale;
	u_int8_t 	to_pad;
};

struct syncache {
	inp_gen_t	sc_inp_gencnt;		/* pointer check */
	struct 		tcpcb *sc_tp;		/* tcb for listening socket */
	struct		mbuf *sc_ipopts;	/* source route */
	struct 		in_conninfo sc_inc;	/* addresses */
#define sc_route	sc_inc.inc_route
#define sc_route6	sc_inc.inc6_route
	u_int32_t	sc_tsrecent;
	tcp_cc		sc_cc_send;		/* holds CC or CCnew */
	tcp_cc		sc_cc_recv;
	tcp_seq 	sc_irs;			/* seq from peer */
	tcp_seq 	sc_iss;			/* our ISS */
	u_long		sc_rxttime;		/* retransmit time */
	u_int16_t	sc_rxtslot; 		/* retransmit counter */
	u_int16_t	sc_peer_mss;		/* peer's MSS */
	u_int16_t	sc_wnd;			/* advertised window */
	u_int8_t 	sc_requested_s_scale:4,
			sc_request_r_scale:4;
	u_int8_t	sc_flags;
#define SCF_NOOPT	0x01			/* no TCP options */
#define SCF_WINSCALE	0x02			/* negotiated window scaling */
#define SCF_TIMESTAMP	0x04			/* negotiated timestamps */
#define SCF_CC		0x08			/* negotiated CC */
#define SCF_UNREACH	0x10			/* icmp unreachable received */
#define SCF_KEEPROUTE	0x20			/* keep cloned route */
	TAILQ_ENTRY(syncache)	sc_hash;
	TAILQ_ENTRY(syncache)	sc_timerq;
};

struct syncache_head {
	TAILQ_HEAD(, syncache)	sch_bucket;
	u_int		sch_length;
};
 
/*
 * The TAO cache entry which is stored in the protocol family specific
 * portion of the route metrics.
 */
struct rmxp_tao {
	tcp_cc	tao_cc;			/* latest CC in valid SYN */
	tcp_cc	tao_ccsent;		/* latest CC sent to peer */
	u_short	tao_mssopt;		/* peer's cached MSS */
#ifdef notyet
	u_short	tao_flags;		/* cache status flags */
#define	TAOF_DONT	0x0001		/* peer doesn't understand rfc1644 */
#define	TAOF_OK		0x0002		/* peer does understand rfc1644 */
#define	TAOF_UNDEF	0		/* we don't know yet */
#endif /* notyet */
};
#define rmx_taop(r)	((struct rmxp_tao *)(r).rmx_filler)

#define	intotcpcb(ip)	((struct tcpcb *)(ip)->inp_ppcb)
#define	sototcpcb(so)	(intotcpcb(sotoinpcb(so)))

/*
 * The smoothed round-trip time and estimated variance
 * are stored as fixed point numbers scaled by the values below.
 * For convenience, these scales are also used in smoothing the average
 * (smoothed = (1/scale)sample + ((scale-1)/scale)smoothed).
 * With these scales, srtt has 3 bits to the right of the binary point,
 * and thus an "ALPHA" of 0.875.  rttvar has 2 bits to the right of the
 * binary point, and is smoothed with an ALPHA of 0.75.
 */
#define	TCP_RTT_SCALE		32	/* multiplier for srtt; 3 bits frac. */
#define	TCP_RTT_SHIFT		5	/* shift for srtt; 3 bits frac. */
#define	TCP_RTTVAR_SCALE	16	/* multiplier for rttvar; 2 bits */
#define	TCP_RTTVAR_SHIFT	4	/* shift for rttvar; 2 bits */
#define	TCP_DELTA_SHIFT		2	/* see tcp_input.c */

/*
 * The initial retransmission should happen at rtt + 4 * rttvar.
 * Because of the way we do the smoothing, srtt and rttvar
 * will each average +1/2 tick of bias.  When we compute
 * the retransmit timer, we want 1/2 tick of rounding and
 * 1 extra tick because of +-1/2 tick uncertainty in the
 * firing of the timer.  The bias will give us exactly the
 * 1.5 tick we need.  But, because the bias is
 * statistical, we have to test that we don't drop below
 * the minimum feasible timer (which is 2 ticks).
 * This version of the macro adapted from a paper by Lawrence
 * Brakmo and Larry Peterson which outlines a problem caused
 * by insufficient precision in the original implementation,
 * which results in inappropriately large RTO values for very
 * fast networks.
 */
#define	TCP_REXMTVAL(tp) \
	max((tp)->t_rttmin, (((tp)->t_srtt >> (TCP_RTT_SHIFT - TCP_DELTA_SHIFT))  \
	  + (tp)->t_rttvar) >> TCP_DELTA_SHIFT)

/*
 * TCB structure exported to user-land via sysctl(3).
 * Evil hack: declare only if in_pcb.h and sys/socketvar.h have been
 * included.  Not all of our clients do.
 */
#if defined(_NETINET_IN_PCB_H_) && defined(_SYS_SOCKETVAR_H_)
struct	xtcpcb {
	size_t	xt_len;
	struct	inpcb	xt_inp;
	struct	tcpcb	xt_tp;
	struct	xsocket	xt_socket;
	u_quad_t	xt_alignment_hack;
};
#endif

/*
 * Names for TCP sysctl objects
 */
#define	TCPCTL_DO_RFC1323	1	/* use RFC-1323 extensions */
#define	TCPCTL_DO_RFC1644	2	/* use RFC-1644 extensions */
#define	TCPCTL_MSSDFLT		3	/* MSS default */
#define TCPCTL_STATS		4	/* statistics (read-only) */
#define	TCPCTL_RTTDFLT		5	/* default RTT estimate */
#define	TCPCTL_KEEPIDLE		6	/* keepalive idle timer */
#define	TCPCTL_KEEPINTVL	7	/* interval to send keepalives */
#define	TCPCTL_SENDSPACE	8	/* send buffer space */
#define	TCPCTL_RECVSPACE	9	/* receive buffer space */
#define	TCPCTL_KEEPINIT		10	/* timeout for establishing syn */
#define	TCPCTL_PCBLIST		11	/* list of all outstanding PCBs */
#define	TCPCTL_DELACKTIME	12	/* time before sending delayed ACK */
#define	TCPCTL_V6MSSDFLT	13	/* MSS default for IPv6 */
#define	TCPCTL_MAXID		14

#define TCPCTL_NAMES { \
	{ 0, 0 }, \
	{ "rfc1323", CTLTYPE_INT }, \
	{ "rfc1644", CTLTYPE_INT }, \
	{ "mssdflt", CTLTYPE_INT }, \
	{ "stats", CTLTYPE_STRUCT }, \
	{ "rttdflt", CTLTYPE_INT }, \
	{ "keepidle", CTLTYPE_INT }, \
	{ "keepintvl", CTLTYPE_INT }, \
	{ "sendspace", CTLTYPE_INT }, \
	{ "recvspace", CTLTYPE_INT }, \
	{ "keepinit", CTLTYPE_INT }, \
	{ "pcblist", CTLTYPE_STRUCT }, \
	{ "delacktime", CTLTYPE_INT }, \
	{ "v6mssdflt", CTLTYPE_INT }, \
}

#ifdef _KERNEL
#ifdef SYSCTL_DECL
SYSCTL_DECL(_net_inet_tcp);
#endif

extern	struct inpcbinfo tcbinfo[];
extern	int tcp_mssdflt;	/* XXX */
extern	int tcp_delack_enabled;
extern	int tcp_do_newreno;
extern	int path_mtu_discovery;
extern	int ss_fltsz;
extern	int ss_fltsz_local;

int	 tcp_addrcpu(in_addr_t faddr, in_port_t fport,
	    in_addr_t laddr, in_port_t lport);
void	 tcp_canceltimers (struct tcpcb *);
struct tcpcb *
	 tcp_close (struct tcpcb *);
void	 tcp_ctlinput (int, struct sockaddr *, void *);
int	 tcp_ctloutput (struct socket *, struct sockopt *);
struct lwkt_port *
	 tcp_cport(int cpu);
struct tcpcb *
	 tcp_drop (struct tcpcb *, int);
void	 tcp_drain (void);
void	 tcp_fasttimo (void);
struct rmxp_tao *
	 tcp_gettaocache (struct in_conninfo *);
void	 tcp_init (void);
void	 tcp_thread_init (void);
void	 tcp_input (struct mbuf *, int, int);
void	 tcp_mss (struct tcpcb *, int);
int	 tcp_mssopt (struct tcpcb *);
void	 tcp_drop_syn_sent (struct inpcb *, int);
void	 tcp_mtudisc (struct inpcb *, int);
struct tcpcb *
	 tcp_newtcpcb (struct inpcb *);
int	 tcp_output (struct tcpcb *);
void	 tcp_quench (struct inpcb *, int);
void	 tcp_respond (struct tcpcb *, void *,
	    struct tcphdr *, struct mbuf *, tcp_seq, tcp_seq, int);
struct rtentry *
	 tcp_rtlookup (struct in_conninfo *);
void	 tcp_save_congestion_state(struct tcpcb *tp);
void	 tcp_revert_congestion_state(struct tcpcb *tp);
void	 tcp_setpersist (struct tcpcb *);
void	 tcp_slowtimo (void);
struct tcptemp *
	 tcp_maketemplate (struct tcpcb *);
void	 tcp_fillheaders (struct tcpcb *, void *, void *);
struct lwkt_port *
	 tcp_soport(struct socket *, struct sockaddr *nam);
struct tcpcb *
	 tcp_timers (struct tcpcb *, int);
void	 tcp_trace (int, int, struct tcpcb *, void *, struct tcphdr *,
			int);
void	 tcp_xmit_bandwidth_limit(struct tcpcb *tp, tcp_seq ack_seq);
void	 syncache_init(void);
void	 syncache_unreach(struct in_conninfo *, struct tcphdr *);
int	 syncache_expand(struct in_conninfo *, struct tcphdr *,
	     struct socket **, struct mbuf *);
int	 syncache_add(struct in_conninfo *, struct tcpopt *,
	     struct tcphdr *, struct socket **, struct mbuf *);
void	 syncache_chkrst(struct in_conninfo *, struct tcphdr *);
void	 syncache_badack(struct in_conninfo *);

extern	struct pr_usrreqs tcp_usrreqs;
extern	u_long tcp_sendspace;
extern	u_long tcp_recvspace;
tcp_seq tcp_new_isn (struct tcpcb *);

#endif /* _KERNEL */

#endif /* _NETINET_TCP_VAR_H_ */
