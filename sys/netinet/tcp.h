/*
 * Copyright (c) 1982, 1986, 1993
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
 *	@(#)tcp.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/netinet/tcp.h,v 1.13.2.3 2001/03/01 22:08:42 jlemon Exp $
 */

#ifndef _NETINET_TCP_H_
#define	_NETINET_TCP_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif
#include <machine/endian.h>

typedef	u_int32_t	tcp_seq;
typedef	int32_t		tcp_seq_diff_t;

#define	tcp6_seq	tcp_seq	/* for KAME src sync over BSD*'s */
#define	tcp6hdr		tcphdr	/* for KAME src sync over BSD*'s */

/*
 * TCP header.
 * Per RFC 793, September, 1981.
 */
struct tcphdr {
	u_short	th_sport;		/* source port */
	u_short	th_dport;		/* destination port */
	tcp_seq	th_seq;			/* sequence number */
	tcp_seq	th_ack;			/* acknowledgement number */
#if _BYTE_ORDER == _LITTLE_ENDIAN
	u_int	th_x2:4,		/* (unused) */
		th_off:4;		/* data offset */
#elif _BYTE_ORDER == _BIG_ENDIAN
	u_int	th_off:4,		/* data offset */
		th_x2:4;		/* (unused) */
#else
#error "Byte order not implemented"
#endif
	u_char	th_flags;
#define	TH_FIN	0x01
#define	TH_SYN	0x02
#define	TH_RST	0x04
#define	TH_PUSH	0x08
#define	TH_ACK	0x10
#define	TH_URG	0x20
#define	TH_ECE	0x40
#define	TH_CWR	0x80
#define	TH_FLAGS	(TH_FIN|TH_SYN|TH_RST|TH_ACK|TH_URG|TH_ECE|TH_CWR)

	u_short	th_win;			/* window */
	u_short	th_sum;			/* checksum */
	u_short	th_urp;			/* urgent pointer */
};

#define	TCPOPT_EOL		0
#define	TCPOPT_NOP		1
#define	TCPOPT_2NOPs		(TCPOPT_NOP << 24 | TCPOPT_NOP << 16)
#define	TCPOPT_MAXSEG		2
#define	   TCPOLEN_MAXSEG		4
#define	TCPOPT_WINDOW		3
#define	   TCPOLEN_WINDOW		3
#define	TCPOPT_SACK_PERMITTED	4
#define	   TCPOLEN_SACK_PERMITTED		2
#define	   TCPOPT_SACK_PERMITTED_ALIGNED	\
    (TCPOPT_2NOPs | TCPOPT_SACK_PERMITTED << 8 | TCPOLEN_SACK_PERMITTED)
#define	   TCPOLEN_SACK_PERMITTED_ALIGNED	4
#define	TCPOPT_SACK		5
#define	   TCPOLEN_SACK		2
#define	   TCPOLEN_SACK_BLOCK	8
#define	TCPOPT_SACK_ALIGNED	(TCPOPT_2NOPs | TCPOPT_SACK << 8)
#define	   TCPOLEN_SACK_ALIGNED	4
#define	TCPOPT_TIMESTAMP	8
#define	   TCPOLEN_TIMESTAMP		10
#define	   TCPOLEN_TSTAMP_APPA		(TCPOLEN_TIMESTAMP+2) /* appendix A */
#define	   TCPOPT_TSTAMP_HDR		\
    (TCPOPT_2NOPs | TCPOPT_TIMESTAMP << 8 | TCPOLEN_TIMESTAMP)
#define	TCPOPT_CC		11		/* CC options: RFC-1644 */
#define	TCPOPT_CCNEW		12
#define	TCPOPT_CCECHO		13
#define	TCPOPT_SIGNATURE		19      /* Keyed MD5: RFC 2385 */
#define	TCPOLEN_SIGNATURE		18

/*
 * Default maximum segment size for TCP.
 * With an IP MSS of 576, this is 536,
 * but 512 is probably more convenient.
 * This should be defined as MIN(512, IP_MSS - sizeof (struct tcpiphdr)).
 */
#define	TCP_MSS	512

/*
 * TCP_MINMSS is defined to be 256 which is fine for the smallest
 * link MTU (296 bytes, SLIP interface) in the Internet.
 * However it is very unlikely to come across such low MTU interfaces
 * these days (anno dato 2003).
 * Probably it can be set to 512 without ill effects. But we play safe.
 * See tcp_subr.c tcp_minmss SYSCTL declaration for more comments.
 * Setting this to "0" disables the minmss check.
 */
#define	TCP_MINMSS 256

/*
 * Default maximum segment size for TCP6.
 * With an IP6 MSS of 1280, this is 1220,
 * but 1024 is probably more convenient. (xxx kazu in doubt)
 * This should be defined as MIN(1024, IP6_MSS - sizeof (struct tcpip6hdr))
 *
 * NOTE: TCP_MIN_WINSHIFT is used when negotiating window scaling.  Larger
 *	 values are possible but apparently some firewires blows up if
 *	 values larger then 5 are used, so use 5.
 */
#define	TCP6_MSS	1024

#define	TCP_MAXWIN		65535	/* max value for (unscaled) window */

#define	TCP_MIN_WINSHIFT	5	/* requested minimum (x32) */
#define	TCP_MAX_WINSHIFT	14	/* maximum window shift */

#define	TCP_MAXBURST		4	/* maximum segments in a burst */

#define	TCP_MAXHLEN	(0xf<<2)	/* max length of header in bytes */
#define	TCP_MAXOLEN	(TCP_MAXHLEN - sizeof(struct tcphdr))
					/* max space left for options */

/*
 * User-settable options (used with setsockopt).
 */
#define	TCP_NODELAY	0x01	/* don't delay send to coalesce packets */
#define	TCP_MAXSEG	0x02	/* set maximum segment size */
#define	TCP_NOPUSH	0x04	/* don't push last block of write */
#define	TCP_NOOPT	0x08	/* don't use TCP options */
#define	TCP_SIGNATURE_ENABLE    0x10    /* use MD5 digests (RFC2385) */
#define	TCP_KEEPINIT	0x20	/* set max time to establish connection */
/* 0x40 unused */
#define	TCP_FASTKEEP	0x80
#define	TCP_KEEPIDLE	0x100	/* set time before keepalive probes begin */
#define	TCP_KEEPINTVL	0x200	/* set time between keepalive probes */
#define	TCP_KEEPCNT	0x400	/* set max number of keepalive probes */

#endif
