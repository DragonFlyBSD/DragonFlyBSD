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
 *	@(#)tcp_debug.c	8.1 (Berkeley) 6/10/93
 * $FreeBSD: src/sys/netinet/tcp_debug.c,v 1.16.2.1 2000/07/15 07:14:31 kris Exp $
 */

#include "opt_inet.h"
#include "opt_inet6.h"

#ifndef INET
#error The option TCPDEBUG requires option INET.
#endif

/* load symbolic names */
#define PRUREQUESTS
#define TCPSTATES
#define	TCPTIMERS
#define	TANAMES

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/protosw.h>
#include <sys/socket.h>

#include <net/netisr.h>
#include <net/netmsg.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcpip.h>
#include <netinet/tcp_debug.h>

static int	tcpconsdebug = 0;
static struct tcp_debug tcp_debug[TCP_NDEBUG];
static int	tcp_debx;

/*
 * Tcp debug routines
 */
void
tcp_trace(short act, short ostate, struct tcpcb *tp, void *ipgen,
	  struct tcphdr *th, int req)
{
#ifdef INET6
	int isipv6;
#endif /* INET6 */
	tcp_seq seq, ack;
	int len, flags;
	struct tcp_debug *td = &tcp_debug[tcp_debx++];

#ifdef INET6
	isipv6 = (ipgen != NULL && ((struct ip *)ipgen)->ip_v == 6) ? 1 : 0;
#endif /* INET6 */
	td->td_family =
#ifdef INET6
		(isipv6 != 0) ? AF_INET6 :
#endif
		AF_INET;
	if (tcp_debx == TCP_NDEBUG)
		tcp_debx = 0;
	td->td_time = iptime();
	td->td_act = act;
	td->td_ostate = ostate;
	td->td_tcb = (caddr_t)tp;
	if (tp)
		td->td_cb = *tp;
	else
		bzero(&td->td_cb, sizeof *tp);
	if (ipgen) {
		switch (td->td_family) {
		case AF_INET:
			bcopy(ipgen, &td->td_ti.ti_i, sizeof(td->td_ti.ti_i));
			bzero(td->td_ip6buf, sizeof td->td_ip6buf);
			break;
#ifdef INET6
		case AF_INET6:
			bcopy(ipgen, td->td_ip6buf, sizeof td->td_ip6buf);
			bzero(&td->td_ti.ti_i, sizeof td->td_ti.ti_i);
			break;
#endif
		default:
			bzero(td->td_ip6buf, sizeof td->td_ip6buf);
			bzero(&td->td_ti.ti_i, sizeof td->td_ti.ti_i);
			break;
		}
	} else {
		bzero(&td->td_ti.ti_i, sizeof td->td_ti.ti_i);
		bzero(td->td_ip6buf, sizeof td->td_ip6buf);
	}
	if (th) {
		switch (td->td_family) {
		case AF_INET:
			td->td_ti.ti_t = *th;
			bzero(&td->td_ti6.th, sizeof td->td_ti6.th);
			break;
#ifdef INET6
		case AF_INET6:
			td->td_ti6.th = *th;
			bzero(&td->td_ti.ti_t, sizeof td->td_ti.ti_t);
			break;
#endif
		default:
			bzero(&td->td_ti.ti_t, sizeof td->td_ti.ti_t);
			bzero(&td->td_ti6.th, sizeof td->td_ti6.th);
			break;
		}
	} else {
		bzero(&td->td_ti.ti_t, sizeof td->td_ti.ti_t);
		bzero(&td->td_ti6.th, sizeof td->td_ti6.th);
	}
	td->td_req = req;
	if (tcpconsdebug == 0)
		return;
	if (tp)
		kprintf("%p %s:", tp, tcpstates[ostate]);
	else
		kprintf("???????? ");
	kprintf("%s ", tanames[act]);
	switch (act) {

	case TA_INPUT:
	case TA_OUTPUT:
	case TA_DROP:
		if (ipgen == NULL || th == NULL)
			break;
		seq = th->th_seq;
		ack = th->th_ack;
		len =
#ifdef INET6
			isipv6 ? ((struct ip6_hdr *)ipgen)->ip6_plen :
#endif
			((struct ip *)ipgen)->ip_len;
		if (act == TA_OUTPUT) {
			seq = ntohl(seq);
			ack = ntohl(ack);
			len = ntohs((u_short)len);
		}
		if (act == TA_OUTPUT)
			len -= sizeof(struct tcphdr);
		if (len)
			kprintf("[%x..%x)", seq, seq+len);
		else
			kprintf("%x", seq);
		kprintf("@%x, urp=%x", ack, th->th_urp);
		flags = th->th_flags;
		if (flags) {
			char *cp = "<";
#define pf(f) {					\
	if (th->th_flags & TH_##f) {		\
		kprintf("%s%s", cp, #f);		\
		cp = ",";			\
	}					\
}
			pf(SYN); pf(ACK); pf(FIN); pf(RST); pf(PUSH); pf(URG);
			kprintf(">");
		}
		break;

	case TA_USER:
		kprintf("%s", prurequests[req&0xff]);
		if ((req & 0xff) == PRU_SLOWTIMO)
			kprintf("<%s>", tcptimers[req>>8]);
		break;
	}
	if (tp)
		kprintf(" -> %s", tcpstates[tp->t_state]);
	/* print out internal state of tp !?! */
	kprintf("\n");
	if (tp == NULL)
		return;
	kprintf(
	"\trcv_(nxt,wnd,up) (%lx,%lx,%lx) snd_(una,nxt,max) (%lx,%lx,%lx)\n",
	    (u_long)tp->rcv_nxt, tp->rcv_wnd, (u_long)tp->rcv_up,
	    (u_long)tp->snd_una, (u_long)tp->snd_nxt, (u_long)tp->snd_max);
	kprintf("\tsnd_(wl1,wl2,wnd) (%lx,%lx,%lx)\n",
	    (u_long)tp->snd_wl1, (u_long)tp->snd_wl2, tp->snd_wnd);
}
