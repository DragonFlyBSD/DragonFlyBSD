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
 *	@(#)tcp_fsm.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/netinet/tcp_fsm.h,v 1.14 1999/11/07 04:18:30 jlemon Exp $
 * $DragonFly: src/sys/netinet/tcp_fsm.h,v 1.5 2004/09/22 08:54:27 joerg Exp $
 */

#ifndef _NETINET_TCP_FSM_H_
#define _NETINET_TCP_FSM_H_

/*
 * TCP FSM state definitions.
 * Per RFC793, September, 1981.
 */

#define	TCP_NSTATES	12

#define TCPS_TERMINATING	0
#define	TCPS_CLOSED		1	/* closed */
#define	TCPS_LISTEN		2	/* listening for connection */
#define	TCPS_SYN_SENT		3	/* active, have sent syn */
#define	TCPS_SYN_RECEIVED	4	/* have send and received syn */
/* states < TCPS_ESTABLISHED are those where connections not established */
#define	TCPS_ESTABLISHED	5	/* established */
#define	TCPS_CLOSE_WAIT		6	/* rcvd fin, waiting for close */
/* states > TCPS_CLOSE_WAIT are those where user has closed */
#define	TCPS_FIN_WAIT_1		7	/* have closed, sent fin */
#define	TCPS_CLOSING		8	/* closed xchd FIN; await FIN ACK */
#define	TCPS_LAST_ACK		9	/* had fin and close; await FIN ACK */
/* states > TCPS_CLOSE_WAIT && < TCPS_FIN_WAIT_2 await ACK of FIN */
#define	TCPS_FIN_WAIT_2		10	/* have closed, fin is acked */
#define	TCPS_TIME_WAIT		11	/* in 2*msl quiet wait after close */

/* for KAME src sync over BSD*'s */
#define	TCP6_NSTATES		TCP_NSTATES
#define	TCP6S_CLOSED		TCPS_CLOSED
#define	TCP6S_LISTEN		TCPS_LISTEN
#define	TCP6S_SYN_SENT		TCPS_SYN_SENT
#define	TCP6S_SYN_RECEIVED	TCPS_SYN_RECEIVED
#define	TCP6S_ESTABLISHED	TCPS_ESTABLISHED
#define	TCP6S_CLOSE_WAIT	TCPS_CLOSE_WAIT
#define	TCP6S_FIN_WAIT_1	TCPS_FIN_WAIT_1
#define	TCP6S_CLOSING		TCPS_CLOSING
#define	TCP6S_LAST_ACK		TCPS_LAST_ACK
#define	TCP6S_FIN_WAIT_2	TCPS_FIN_WAIT_2
#define	TCP6S_TIME_WAIT		TCPS_TIME_WAIT

#define	TCPS_HAVERCVDSYN(s)	((s) >= TCPS_SYN_RECEIVED)
#define	TCPS_HAVEESTABLISHED(s)	((s) >= TCPS_ESTABLISHED)
#define	TCPS_HAVERCVDFIN(s)	((s) >= TCPS_TIME_WAIT)

#ifdef	TCPOUTFLAGS
/*
 * Flags used when sending segments in tcp_output.
 * Basic flags (TH_RST,TH_ACK,TH_SYN,TH_FIN) are totally
 * determined by state, with the proviso that TH_FIN is sent only
 * if all data queued for output is included in the segment.
 */
static u_char	tcp_outflags[TCP_NSTATES] = {
	TH_RST|TH_ACK,		/* 0, TERMINATING */
	TH_RST|TH_ACK,		/* 1, CLOSED */
	0,			/* 2, LISTEN */
	TH_SYN,			/* 3, SYN_SENT */
	TH_SYN|TH_ACK,		/* 4, SYN_RECEIVED */
	TH_ACK,			/* 5, ESTABLISHED */
	TH_ACK,			/* 6, CLOSE_WAIT */
	TH_FIN|TH_ACK,		/* 7, FIN_WAIT_1 */
	TH_FIN|TH_ACK,		/* 8, CLOSING */
	TH_FIN|TH_ACK,		/* 9, LAST_ACK */
	TH_ACK,			/* 10, FIN_WAIT_2 */
	TH_ACK,			/* 11, TIME_WAIT */
};	
#endif

#ifdef KPROF
int	tcp_acounts[TCP_NSTATES][PRU_NREQ];
#endif

#ifdef	TCPSTATES
const char *tcpstates[] = {
	"TERMINATING",
	"CLOSED",	"LISTEN",	"SYN_SENT",	"SYN_RCVD",
	"ESTABLISHED",	"CLOSE_WAIT",	"FIN_WAIT_1",	"CLOSING",
	"LAST_ACK",	"FIN_WAIT_2",	"TIME_WAIT",
};
#endif

#endif
