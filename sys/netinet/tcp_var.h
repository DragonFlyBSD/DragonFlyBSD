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
 */

#ifndef _NETINET_TCP_VAR_H_
#define _NETINET_TCP_VAR_H_

#ifndef _NETINET_IN_PCB_H_
#include <netinet/in_pcb.h>		/* needed for in_conninfo, inp_gen_t */
#endif
#ifndef _NETINET_TCP_H_
#include <netinet/tcp.h>
#endif

/*
 * Kernel variables for tcp.
 */
extern int tcp_do_rfc1323;
extern int tcp_low_rtobase;
extern int tcp_ncr_rxtthresh_max;
extern int tcp_do_sack;
extern int tcp_do_smartsack;
extern int tcp_do_rescuesack;
extern int tcp_aggressive_rescuesack;
extern int tcp_do_rfc6675;
extern int tcp_rfc6675_rxt;
extern int tcp_aggregate_acks;
extern int tcp_eifel_rtoinc;
extern int tcp_prio_synack;

/* TCP segment queue entry */
struct tseg_qent {
	TAILQ_ENTRY(tseg_qent) tqe_q;
	int	tqe_len;		/* TCP segment data length */
	struct	tcphdr *tqe_th;		/* a pointer to tcp header */
	struct	mbuf	*tqe_m;		/* mbuf contains packet */
};
TAILQ_HEAD(tsegqe_head, tseg_qent);
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

struct raw_sackblock {				/* covers [start, end) */
	tcp_seq rblk_start;
	tcp_seq rblk_end;
};

/* maximum number of SACK blocks that will fit in the TCP option space */
#define	MAX_SACK_REPORT_BLOCKS	4

TAILQ_HEAD(sackblock_list, sackblock);

struct scoreboard {
	int nblocks;
	struct sackblock_list sackblocks;
	tcp_seq lostseq;			/* passed SACK lost test */
	struct sackblock *lastfound;		/* search hint */
	struct sackblock *freecache;		/* one slot free block cache */
};

struct netmsg_tcp_timer;
struct netmsg_base;

/*
 * Tcp control block, one per tcp; fields:
 * Organized for 16 byte cacheline efficiency.
 */
struct tcpcb {
	struct	tsegqe_head t_segq;
	int	t_dupacks;		/* consecutive dup acks recd */
	int	t_rxtthresh;		/* # dup acks to start fast rxt */
	int	tt_cpu;			/* sanity check the cpu */

	struct	tcp_callout *tt_rexmt;	/* retransmit timer */
	struct	tcp_callout *tt_persist;/* retransmit persistence */
	struct	tcp_callout *tt_keep;	/* keepalive */
	struct	tcp_callout *tt_2msl;	/* 2*msl TIME_WAIT timer */
	struct	tcp_callout *tt_delack;	/* delayed ACK timer */
	struct	netmsg_tcp_timer *tt_msg; /* timer message */

	struct	netmsg_base *tt_sndmore;/* send more segments (fairsend) */

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
#define	TF_SACK_PERMITTED 0x00000200	/* other side said I could SACK */
#define	TF_NEEDSYN	0x00000400	/* send SYN (implicit state) */
#define	TF_NEEDFIN	0x00000800	/* send FIN (implicit state) */
#define	TF_NOPUSH	0x00001000	/* don't push */
#define TF_LISTEN	0x00002000	/* listen(2) has been called */
#define TF_SIGNATURE	0x00004000	/* require MD5 digests (RFC2385) */
#define TF_NCR		0x00008000	/* Non-Congestion Robustness RFC4653 */
#define	TF_MORETOCOME	0x00010000	/* More data to be appended to sock */
#define	TF_SAWFIN	0x00020000	/* FIN has been seen */
#define	TF_LASTIDLE	0x00040000	/* connection was previously idle */
#define	TF_RXWIN0SENT	0x00080000	/* sent a receiver win 0 in response */
#define	TF_FASTRECOVERY	0x00100000	/* in Fast Recovery */
#define	TF_QUEDFIN	0x00200000	/* FIN has been received */
#define	TF_XMITNOW	0x00400000	/* Temporarily override Nagle */
#define	TF_UNUSED008	0x00800000
#define	TF_UNUSED009	0x01000000
#define	TF_FORCE	0x02000000	/* Set if forcing out a byte */
#define TF_ONOUTPUTQ	0x04000000	/* on t_outputq list */
#define TF_FAIRSEND	0x08000000	/* fairsend is requested */
#define TF_UNUSED003	0x10000000
#define TF_UNUSED004	0x20000000
#define TF_KEEPALIVE	0x40000000	/* temporary keepalive */
#define TF_RXRESIZED	0x80000000	/* rcvbuf was resized */
	tcp_seq	snd_up;			/* send urgent pointer */
	u_long	snd_last;		/* time last data were sent */

	tcp_seq	snd_una;		/* send unacknowledged */
	tcp_seq	snd_recover;		/* for use with Fast Recovery */
	tcp_seq	snd_max;		/* highest sequence number sent;
					 * used to recognize retransmits */
	tcp_seq	snd_nxt;		/* send next */

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
	u_long	snd_wacked;		/* bytes acked in one send window */
	u_long	snd_ssthresh;		/* snd_cwnd size threshold for
					 * for slow start exponential to
					 * linear switch */

	int	t_rxtcur;		/* current retransmit value (ticks) */
	u_int	t_maxseg;		/* maximum segment size */
	int	t_srtt;			/* smoothed round-trip time */
	int	t_rttvar;		/* variance in round-trip time */

	u_int	t_maxopd;		/* mss plus options */

	u_long	t_rcvtime;		/* reception inactivity time */
	u_long	t_starttime;		/* time connection was established */
	int	t_rtttime;		/* round trip time */
	tcp_seq	t_rtseq;		/* sequence number being timed */

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
	u_long	ts_recent;		/* timestamp echo data */

	u_long	ts_recent_age;		/* when last updated */
	tcp_seq	last_ack_sent;

/* experimental */
	u_int	rxt_flags;
#define	TRXT_F_REBASERTO	0x0001	/* Recalculate RTO based on new RTT */
#define	TRXT_F_WASFRECOVERY	0x0002	/* was in Fast Recovery */
#define	TRXT_F_FIRSTACCACK	0x0004	/* Look for 1st acceptable ACK. */
#define	TRXT_F_FASTREXMT	0x0008	/* Did Fast Retransmit. */
#define	TRXT_F_EARLYREXMT	0x0010	/* Did Early (Fast) Retransmit. */
	int	t_srtt_prev;		/* adjusted SRTT prior to retransmit */
	int	t_rttvar_prev;		/* RTTVAR prior to retransmit */
	int	t_rxtcur_prev;		/* rexmt timeout prior to retransmit */
	tcp_seq	snd_max_prev;		/* SND_MAX prior to retransmit */
	u_long	snd_cwnd_prev;		/* cwnd prior to retransmit */
	u_long	snd_wacked_prev;	/* prior bytes acked in send window */
	u_long	snd_ssthresh_prev;	/* ssthresh prior to retransmit */
	tcp_seq snd_recover_prev;	/* snd_recover prior to retransmit */
	u_long	t_badrxtwin;		/* window for retransmit recovery */
	u_long	t_rexmtTS;		/* timestamp of last retransmit */
	u_char	snd_limited;		/* segments limited transmitted */

	u_int	sack_flags;
#define TSACK_F_SACKRESCUED	0x0001	/* sent rescue SACK recovery data */
#define TSACK_F_DUPSEG		0x0002	/* last seg a duplicate */
#define TSACK_F_ENCLOSESEG	0x0004	/* enclosing SACK block */
#define TSACK_F_SACKLEFT	0x0008	/* send SACK blocks from left side */
	tcp_seq	rexmt_high;		/* highest seq # retransmitted + 1 */
	tcp_seq	rexmt_rescue;		/* rescue SACKED sequence number */
	tcp_seq	snd_max_rexmt;		/* snd_max when rexmting snd_una */
	struct scoreboard scb;		/* sack scoreboard */
	struct raw_sackblock reportblk; /* incoming segment or D-SACK block */
	struct raw_sackblock encloseblk;
	int	nsackhistory;
	struct raw_sackblock sackhistory[MAX_SACK_REPORT_BLOCKS]; /* reported */

	TAILQ_ENTRY(tcpcb) t_outputq;	/* tcp_output needed list */

	/* bandwith limitation */
	u_long	snd_bandwidth;		/* calculated bandwidth or 0 */
	u_long	snd_bwnd;		/* bandwidth-controlled window */
	int	t_bw_rtttime;		/* used for bandwidth calculation */
	tcp_seq	t_bw_rtseq;		/* used for bandwidth calculation */

/* anti DoS counters */
	u_long	rcv_second;		/* start of interval second */
	u_long	rcv_pps;		/* received packets per second */
	u_long	rcv_byps;		/* received bytes per second */

	u_int32_t	rfbuf_ts;	/* recv buffer autoscaling timestamp */
	int	rfbuf_cnt;		/* recv buffer autoscaling byte count */

	int	t_keepinit;		/* time to establish connection */

	int	t_keepidle;		/* time before keepalive probes begin */
	int	t_keepintvl;		/* time between keepalive probes */
	int	t_keepcnt;		/* maximum number of keepalive probes */
	int	t_maxidle;		/* time to drop after starting probes */

	int	t_rxtsyn;		/* time spent in SYN or SYN|ACK rexmt */
};

#define	IN_FASTRECOVERY(tp)	(tp->t_flags & TF_FASTRECOVERY)
#define	ENTER_FASTRECOVERY(tp)	tp->t_flags |= TF_FASTRECOVERY
#define	EXIT_FASTRECOVERY(tp)	tp->t_flags &= ~TF_FASTRECOVERY

#ifdef TCP_SIGNATURE
/*
 * Defines which are needed by the xform_tcp module and tcp_[in|out]put
 * for SADB verification and lookup.
 */
#define TCP_SIGLEN      16      /* length of computed digest in bytes */
#define TCP_KEYLEN_MIN  1       /* minimum length of TCP-MD5 key */
#define TCP_KEYLEN_MAX  80      /* maximum length of TCP-MD5 key */
/*
 * Only a single SA per host may be specified at this time. An SPI is
 * needed in order for the KEY_ALLOCSA() lookup to work.
 */
#define TCP_SIG_SPI     0x1000
#endif /* TCP_SIGNATURE */

/*
 * TCP statistics.
 *
 * NOTE: Make sure this struct's size is multiple cache line size.
 */
struct tcp_stats {
	u_long	tcps_connattempt;	/* connections initiated */
	u_long	tcps_accepts;		/* connections accepted */
	u_long	tcps_connects;		/* connections established */
	u_long	tcps_drops;		/* connections dropped */
	u_long	tcps_conndrops;		/* embryonic connections dropped */
	u_long	tcps_minmssdrops;	/* average minmss too low drops */
	u_long	tcps_closed;		/* conn. closed (includes drops) */
	u_long	tcps_segstimed;		/* segs where we tried to get rtt */
	u_long	tcps_rttupdated;	/* times we succeeded */
	u_long	tcps_delack;		/* delayed acks sent */
	u_long	tcps_timeoutdrop;	/* conn. dropped in rxmt timeout */
	u_long	tcps_rexmttimeo;	/* retransmit timeouts */
	u_long	tcps_persisttimeo;	/* persist timeouts */
	u_long	tcps_keeptimeo;		/* keepalive timeouts */
	u_long	tcps_keepprobe;		/* keepalive probes sent */
	u_long	tcps_keepdrops;		/* connections dropped in keepalive */

	u_long	tcps_sndtotal;		/* total packets sent */
	u_long	tcps_sndpack;		/* data packets sent */
	u_long	tcps_sndbyte;		/* data bytes sent */
	u_long	tcps_sndrexmitpack;	/* data packets retransmitted */
	u_long	tcps_sndrexmitbyte;	/* data bytes retransmitted */
	u_long	tcps_sndsackrtopack;	/* packets sent by SACK after RTO */
	u_long	tcps_sndsackrtobyte;	/* bytes sent by SACK after RTO */
	u_long	tcps_sndfastrexmit;	/* Fast Retransmissions */
	u_long	tcps_sndearlyrexmit;	/* early Fast Retransmissions */
	u_long	tcps_sndlimited;	/* Limited Transmit packets */
	u_long	tcps_sndrtobad;		/* spurious RTO retransmissions */
	u_long	tcps_sndfastrexmitbad;	/* spurious Fast Retransmissions */
	u_long	tcps_sndearlyrexmitbad;	/* spurious early Fast Retransmissions,
					   a subset of tcps_sndfastrexmitbad */
	u_long	tcps_eifeldetected;	/* Eifel-detected spurious rexmits */
	u_long	tcps_rttcantdetect;	/* Eifel but not 1/2 RTT-detectable */
	u_long	tcps_rttdetected;	/* RTT-detected spurious RTO rexmits */
	u_long	tcps_sndacks;		/* ack-only packets sent */
	u_long	tcps_sndprobe;		/* window probes sent */
	u_long	tcps_sndurg;		/* packets sent with URG only */
	u_long	tcps_sndwinup;		/* window update-only packets sent */
	u_long	tcps_sndctrl;		/* control (SYN|FIN|RST) packets sent */
	u_long	tcps_sndsackpack;	/* packets sent by SACK recovery alg */
	u_long	tcps_sndsackbyte;	/* bytes sent by SACK recovery */
	u_long	tcps_snduna3;		/* re-retransmit snd_una on 3 new seg */
	u_long	tcps_snduna1;		/* re-retransmit snd_una on 1 new seg */
	u_long	tcps_sndsackopt;	/* SACK options sent */
	u_long	tcps_snddsackopt;	/* D-SACK options sent */
	u_long	tcps_sndidle;		/* sending idle detected */
	u_long	tcps_sackrescue;	/* SACK rescue data packets sent */
	u_long	tcps_sackrescue_try;	/* SACK rescues attempted */
	u_long	tcps_eifelresponse;	/* Eifel responses */

	u_long	tcps_rcvtotal;		/* total packets received */
	u_long	tcps_rcvpack;		/* packets received in sequence */
	u_long	tcps_rcvbyte;		/* bytes received in sequence */
	u_long	tcps_rcvbadsum;		/* packets received with ccksum errs */
	u_long	tcps_rcvbadoff;		/* packets received with bad offset */
	u_long	tcps_rcvmemdrop;	/* packets dropped for lack of memory */
	u_long	tcps_rcvshort;		/* packets received too short */
	u_long	tcps_rcvduppack;	/* duplicate-only packets received */
	u_long	tcps_rcvdupbyte;	/* duplicate-only bytes received */
	u_long	tcps_rcvpartduppack;	/* packets with some duplicate data */
	u_long	tcps_rcvpartdupbyte;	/* dup. bytes in part-dup. packets */
	u_long	tcps_rcvoopack;		/* out-of-order packets received */
	u_long	tcps_rcvoobyte;		/* out-of-order bytes received */
	u_long	tcps_rcvpackafterwin;	/* packets with data after window */
	u_long	tcps_rcvbyteafterwin;	/* bytes rcvd after window */
	u_long	tcps_rcvafterclose;	/* packets rcvd after "close" */
	u_long	tcps_rcvwinprobe;	/* rcvd window probe packets */
	u_long	tcps_rcvdupack;		/* rcvd duplicate acks */
	u_long	tcps_rcvacktoomuch;	/* rcvd acks for unsent data */
	u_long	tcps_rcvackpack;	/* rcvd ack packets */
	u_long	tcps_rcvackbyte;	/* bytes acked by rcvd acks */
	u_long	tcps_rcvwinupd;		/* rcvd window update packets */
	u_long	tcps_pawsdrop;		/* segments dropped due to PAWS */
	u_long	tcps_pawsaccept;	/* segments accepted, PAWS tolerance */
	u_long	tcps_predack;		/* times hdr predict ok for acks */
	u_long	tcps_preddat;		/* times hdr predict ok for data pkts */
	u_long	tcps_pcbcachemiss;
	u_long	tcps_cachedrtt;		/* times cached RTT in route updated */
	u_long	tcps_cachedrttvar;	/* times cached rttvar updated */
	u_long	tcps_cachedssthresh;	/* times cached ssthresh updated */
	u_long	tcps_usedrtt;		/* times RTT initialized from route */
	u_long	tcps_usedrttvar;	/* times RTTVAR initialized from rt */
	u_long	tcps_usedssthresh;	/* times ssthresh initialized from rt*/
	u_long	tcps_persistdrop;	/* timeout in persist state */
	u_long	tcps_badsyn;		/* bogus SYN, e.g. premature ACK */
	u_long	tcps_mturesent;		/* resends due to MTU discovery */
	u_long	tcps_listendrop;	/* listen queue overflows */
	u_long	tcps_rcvbadsackopt;	/* rcvd illegal SACK options */
	u_long	tcps_sackrenege;	/* times other side reneged */

	u_long	tcps_sacksbupdate;	/* times SACK scoreboard updated */
	u_long	tcps_sacksboverflow;	/* times SACK scoreboard overflowed */
	u_long	tcps_sacksbreused;	/* times SACK sb-block reused */
	u_long	tcps_sacksbfailed;	/* times SACK sb update failed */
	u_long	tcps_sacksbfast;	/* times SACK sb-block uses cache */

	u_long	tcps_sc_added;		/* entry added to syncache */
	u_long	tcps_sc_retransmitted;	/* syncache entry was retransmitted */
	u_long	tcps_sc_dupsyn;		/* duplicate SYN packet */
	u_long	tcps_sc_dropped;	/* could not reply to packet */
	u_long	tcps_sc_completed;	/* successful extraction of entry */
	u_long	tcps_sc_bucketoverflow;	/* syncache per-bucket limit hit */
	u_long	tcps_sc_cacheoverflow;	/* syncache cache limit hit */
	u_long	tcps_sc_reset;		/* RST removed entry from syncache */
	u_long	tcps_sc_stale;		/* timed out or listen socket gone */
	u_long	tcps_sc_aborted;	/* syncache entry aborted */
	u_long	tcps_sc_badack;		/* removed due to bad ACK */
	u_long	tcps_sc_unreach;	/* ICMP unreachable received */
	u_long	tcps_sc_zonefail;	/* zalloc() failed */
	u_long	tcps_sc_sendcookie;	/* SYN cookie sent */
	u_long	tcps_sc_recvcookie;	/* SYN cookie received */

	u_long	tcps_pad[6];		/* pad to cache line size (64B) */
};

#ifdef _KERNEL

#define tcpstat	tcpstats_percpu[mycpuid]

struct sockopt;

extern struct tcp_stats		tcpstats_percpu[MAXCPU];

static const int tcprexmtthresh = 3;
#endif

/*
 * Structure to hold TCP options that are only used during segment
 * processing (in tcp_input), but not held in the tcpcb.
 * It's basically used to reduce the number of parameters
 * to tcp_dooptions.
 */
struct tcpopt {
	u_int		to_flags;	/* which options are present */
#define	TOF_TS			0x0001	/* timestamp */
#define	TOF_MSS			0x0010
#define	TOF_SCALE		0x0020
#define	TOF_SACK_PERMITTED	0x0040
#define	TOF_SACK		0x0080
#define TOF_SIGNATURE		0x0100	/* signature option present */
#define TOF_SIGLEN		0x0200	/* signature length valid (RFC2385) */
#define TOF_DSACK		0x0400	/* D-SACK */
#define TOF_SACK_REDUNDANT	0x0800	/* all SACK blocks are known */
	u_int32_t	to_tsval;
	u_int32_t	to_tsecr;
	u_int16_t	to_mss;
	u_int8_t	to_requested_s_scale;
	u_int8_t	to_nsackblocks;
	struct raw_sackblock *to_sackblocks;
};

struct syncache {
	inp_gen_t	sc_inp_gencnt;		/* pointer check */
	struct		tcpcb *sc_tp;		/* tcb for listening socket */
	struct		mbuf *sc_ipopts;	/* source route */
	struct		in_conninfo sc_inc;	/* addresses */
#define sc_route	sc_inc.inc_route
#define sc_route6	sc_inc.inc6_route
	u_int32_t	sc_tsrecent;
	tcp_seq		sc_irs;			/* seq from peer */
	tcp_seq		sc_iss;			/* our ISS */
	u_long		sc_rxttime;		/* retransmit time */
	u_int16_t	sc_rxtslot;		/* retransmit counter */
	u_int16_t	sc_peer_mss;		/* peer's MSS */
	u_int16_t	sc_wnd;			/* advertised window */
	u_int8_t	sc_requested_s_scale:4,
			sc_request_r_scale:4;
	u_int8_t	sc_flags;
#define SCF_NOOPT		0x01		/* no TCP options */
#define SCF_WINSCALE		0x02		/* negotiated window scaling */
#define SCF_TIMESTAMP		0x04		/* negotiated timestamps */
#define SCF_UNUSED		0x08		/* unused */
#define SCF_UNREACH		0x10		/* icmp unreachable received */
#define	SCF_SACK_PERMITTED	0x20		/* saw SACK permitted option */
#define SCF_SIGNATURE		0x40		/* send MD5 digests */
#define SCF_MARKER		0x80		/* not a real entry */
	int		sc_rxtused;		/* time spent in SYN|ACK rxt */
	u_long		sc_sndwnd;		/* send window */
	TAILQ_ENTRY(syncache) sc_hash;
	TAILQ_ENTRY(syncache) sc_timerq;
};

struct syncache_head {
	TAILQ_HEAD(, syncache)	sch_bucket;
	u_int		sch_length;
};

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
/* 2 was TCPCTL_DO_RFC1644 */
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
	  { "reserved", CTLTYPE_INT},	/* was rfc1644 */	\
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

#define TCP_DO_SACK(tp)		((tp)->t_flags & TF_SACK_PERMITTED)
#define TCP_DO_NCR(tp)		(((tp)->t_flags & TF_NCR) && TCP_DO_SACK((tp)))
#define TCP_SACK_BLKEND(len, thflags) \
	((len) + (((thflags) & TH_FIN) != 0))

TAILQ_HEAD(tcpcbackqhead,tcpcb);

extern	struct inpcbinfo tcbinfo[];
extern	struct tcpcbackqhead tcpcbackq[];

extern	int tcp_mssdflt;	/* XXX */
extern	int tcp_minmss;
extern	int tcp_delack_enabled;
extern	int path_mtu_discovery;

struct ip;
union netmsg;

int	 tcp_addrcpu(in_addr_t faddr, in_port_t fport,
	    in_addr_t laddr, in_port_t lport);
struct lwkt_port *
	tcp_addrport(in_addr_t faddr, in_port_t fport,
	    in_addr_t laddr, in_port_t lport);
struct lwkt_port *tcp_addrport0(void);
void	 tcp_canceltimers (struct tcpcb *);
struct tcpcb *
	 tcp_close (struct tcpcb *);
void	 tcp_ctlinput(union netmsg *);
void	 tcp_ctloutput(union netmsg *);
inp_notify_t tcp_get_inpnotify(int cmd, const struct sockaddr *sa,
	    int *arg, struct ip **ip0, int *cpuid);
struct tcpcb *
	 tcp_drop (struct tcpcb *, int);
void	 tcp_drain (void);
void	 tcp_init (void);
void	 tcp_thread_init (void);
int	 tcp_input (struct mbuf **, int *, int);
void	 tcp_mss (struct tcpcb *, int);
int	 tcp_mssopt (struct tcpcb *);
void	 tcp_drop_syn_sent (struct inpcb *, int);
void	 tcp_mtudisc (struct inpcb *, int);
struct tcpcb *
	 tcp_newtcpcb (struct inpcb *);
int	 tcp_output(struct tcpcb *);
int	 tcp_output_fair(struct tcpcb *);
void	 tcp_output_init(struct tcpcb *);
void	 tcp_output_cancel(struct tcpcb *);
boolean_t
	 tcp_output_pending(struct tcpcb *);
void	 tcp_quench (struct inpcb *, int);
void	 tcp_respond (struct tcpcb *, void *,
	    struct tcphdr *, struct mbuf *, tcp_seq, tcp_seq, int);
struct rtentry *
	 tcp_rtlookup (struct in_conninfo *);
int	 tcp_sack_bytes_below(const struct scoreboard *scb, tcp_seq seq);
void	 tcp_sack_destroy(struct scoreboard *scb);
void	 tcp_sack_discard(struct tcpcb *tp);
void	 tcp_sack_report_cleanup(struct tcpcb *tp);
boolean_t
	 tcp_sack_report_needed(const struct tcpcb *tp);
int	 tcp_sack_ndsack_blocks(const struct raw_sackblock *blocks,
	    const int numblocks, tcp_seq snd_una);
void	 tcp_sack_fill_report(struct tcpcb *tp, u_char *opt, u_int *plen);
boolean_t
	 tcp_sack_has_sacked(const struct scoreboard *scb, u_int amount);
void	 tcp_sack_tcpcb_init(struct tcpcb *tp);
uint32_t tcp_sack_compute_pipe(const struct tcpcb *tp);
boolean_t
	 tcp_sack_nextseg(struct tcpcb *tp, tcp_seq *nextrexmt, uint32_t *len,
			  boolean_t *rescue);
boolean_t
	 tcp_sack_islost(const struct scoreboard *scb, tcp_seq seq);
void	 tcp_sack_update_lostseq(struct scoreboard *scb, tcp_seq snd_una,
	    u_int maxseg, int rxtthresh);
#ifdef later
void	 tcp_sack_revert_scoreboard(struct scoreboard *scb, tcp_seq snd_una,
				    u_int maxseg);
void	 tcp_sack_save_scoreboard(struct scoreboard *scb);
#endif
void	 tcp_sack_skip_sacked(struct scoreboard *scb, tcp_seq *prexmt);
uint32_t tcp_sack_first_unsacked_len(const struct tcpcb *tp);
void	 tcp_sack_update_scoreboard(struct tcpcb *tp, struct tcpopt *to);
void	 tcp_save_congestion_state(struct tcpcb *tp);
void	 tcp_revert_congestion_state(struct tcpcb *tp);
void	 tcp_setpersist (struct tcpcb *);
struct tcptemp *tcp_maketemplate (struct tcpcb *);
void	 tcp_freetemplate (struct tcptemp *);
void	 tcp_fillheaders (struct tcpcb *, void *, void *, boolean_t);
struct lwkt_port *
	 tcp_soport(struct socket *, struct sockaddr *, struct mbuf **);
struct lwkt_port *
	 tcp_ctlport(int, struct sockaddr *, void *);
struct lwkt_port *
	 tcp_initport(void);
struct tcpcb *
	 tcp_timers (struct tcpcb *, int);
void	 tcp_trace (short, short, struct tcpcb *, void *, struct tcphdr *,
			int);
void	 tcp_xmit_bandwidth_limit(struct tcpcb *tp, tcp_seq ack_seq);
u_long	 tcp_initial_window(struct tcpcb *tp);
void	 tcp_timer_keep_activity(struct tcpcb *tp, int thflags);
void	 syncache_init(void);
void	 syncache_unreach(struct in_conninfo *, const struct tcphdr *);
int	 syncache_expand(struct in_conninfo *, struct tcphdr *,
	     struct socket **, struct mbuf *);
int	 syncache_add(struct in_conninfo *, struct tcpopt *,
	     struct tcphdr *, struct socket *, struct mbuf *);
void	 syncache_chkrst(struct in_conninfo *, struct tcphdr *);
void	 syncache_badack(struct in_conninfo *);
void	 syncache_destroy(struct tcpcb *tp, struct tcpcb *new_tp);

#ifdef TCP_SIGNATURE
int tcpsignature_apply(void *fstate, void *data, unsigned int len);
int tcpsignature_compute(struct mbuf *m, int len, int tcpoptlen,
		u_char *buf, u_int direction);
#endif /* TCP_SIGNATURE */

extern	struct pr_usrreqs tcp_usrreqs;
extern	u_long tcp_sendspace;
extern	u_long tcp_recvspace;
tcp_seq tcp_new_isn (struct tcpcb *);

#endif /* _KERNEL */

#endif /* _NETINET_TCP_VAR_H_ */
