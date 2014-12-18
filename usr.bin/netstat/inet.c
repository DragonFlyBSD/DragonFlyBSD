/*
 * Copyright (c) 1983, 1988, 1993, 1995
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
 * @(#)inet.c	8.5 (Berkeley) 5/24/95
 * $FreeBSD: src/usr.bin/netstat/inet.c,v 1.37.2.11 2003/11/27 14:46:49 ru Exp $
 */

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/protosw.h>
#include <sys/time.h>

#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_carp.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif /* INET6 */
#include <netinet/in_pcb.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>
#include <netinet/igmp_var.h>
#include <netinet/ip_var.h>
#include <netinet/pim_var.h>
#include <netinet/tcp.h>
#include <netinet/tcpip.h>
#include <netinet/tcp_seq.h>
#define TCPSTATES
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_debug.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <libutil.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "netstat.h"

char	*inetname (struct in_addr *);
void	inetprint (struct in_addr *, int, const char *, int);
#ifdef INET6
extern void	inet6print (struct in6_addr *, int, const char *, int);
static int udp_done, tcp_done;
#endif /* INET6 */

/*
 * Print a summary of connections related to an Internet
 * protocol.  For TCP, also give state of connection.
 * Listening processes (aflag) are suppressed unless the
 * -a (all) flag is specified.
 */

static int ppr_first = 1;
static void outputpcb(int proto, const char *name, struct inpcb *inp, struct xsocket *so, struct tcpcb *tp);

void
protopr(u_long proto, const char *name, int af1 __unused)
{
	int istcp;
	void *buf;
	const char *mibvar;
	size_t i, len;

	istcp = 0;
	switch (proto) {
	case IPPROTO_TCP:
#ifdef INET6
		if (tcp_done != 0)
			return;
		else
			tcp_done = 1;
#endif
		istcp = 1;
		mibvar = "net.inet.tcp.pcblist";
		break;
	case IPPROTO_UDP:
#ifdef INET6
		if (udp_done != 0)
			return;
		else
			udp_done = 1;
#endif
		mibvar = "net.inet.udp.pcblist";
		break;
	case IPPROTO_DIVERT:
		mibvar = "net.inet.divert.pcblist";
		break;
	default:
		mibvar = "net.inet.raw.pcblist";
		break;
	}
	len = 0;
	if (sysctlbyname(mibvar, 0, &len, 0, 0) < 0) {
		if (errno != ENOENT)
			warn("sysctl: %s", mibvar);
		return;
	}
	if (len == 0)
		return;
	if ((buf = malloc(len)) == NULL) {
		warn("malloc %lu bytes", (u_long)len);
		return;
	}
	if (sysctlbyname(mibvar, buf, &len, 0, 0) < 0) {
		warn("sysctl: %s", mibvar);
		free(buf);
		return;
	}

	if (istcp) {
		struct xtcpcb *tcp = buf;
		len /= sizeof(*tcp);
		for (i = 0; i < len; i++) {
			if (tcp[i].xt_len != sizeof(*tcp))
				break;
			outputpcb(proto, name, &tcp[i].xt_inp,
				  &tcp[i].xt_socket, &tcp[i].xt_tp);
		}
	} else {
		struct xinpcb *in = buf;
		len /= sizeof(*in);
		for (i = 0; i < len; i++) {
			if (in[i].xi_len != sizeof(*in))
				break;
			outputpcb(proto, name, &in[i].xi_inp,
				  &in[i].xi_socket, NULL);
		}
	}
	free(buf);
}

static void
outputpcb(int proto, const char *name, struct inpcb *inp, struct xsocket *so, struct tcpcb *tp)
{
	const char *vchar;
	static struct clockinfo clockinfo;

	if (clockinfo.hz == 0) {
		size_t size = sizeof(clockinfo);
		sysctlbyname("kern.clockrate", &clockinfo, &size, NULL, 0);
		if (clockinfo.hz == 0)
			clockinfo.hz = 100;
	}

	/* Ignore sockets for protocols other than the desired one. */
	if (so->xso_protocol != (int)proto)
		return;

	if ((af == AF_INET && !INP_ISIPV4(inp))
#ifdef INET6
	    || (af == AF_INET6 && !INP_ISIPV6(inp))
#endif /* INET6 */
	    || (af == AF_UNSPEC && (!INP_ISIPV4(inp)
#ifdef INET6
		&& !INP_ISIPV6(inp)
#endif /* INET6 */
		))
	    ) {
		return;
	}
	if (!aflag && ( 
		(proto == IPPROTO_TCP && tp->t_state == TCPS_LISTEN) ||
		(af == AF_INET && inet_lnaof(inp->inp_laddr) == INADDR_ANY)
#ifdef INET6
	    || (af == AF_INET6 && IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr))
#endif /* INET6 */
	    || (af == AF_UNSPEC && ((INP_ISIPV4(inp) &&
		inet_lnaof(inp->inp_laddr) == INADDR_ANY)
#ifdef INET6
	    || (INP_ISIPV6(inp) &&
		IN6_IS_ADDR_UNSPECIFIED(&inp->in6p_laddr))
#endif
		  ))
	     )) {
		return;
	}

	if (ppr_first) {
		if (!Lflag) {
			printf("Active Internet connections");
			if (aflag)
				printf(" (including servers)");
		} else {
			printf("Current listen queue sizes "
				"(qlen/incqlen/maxqlen)");
		}
		putchar('\n');
		if (Aflag)
			printf("%-8.8s ", "Socket");
		if (Pflag)
			printf("%8.8s %8.8s %8.8s ", "TxWin", "Unacked", "RTT/ms");
		if (Lflag) {
			printf("%-5.5s %-14.14s %-22.22s\n",
				"Proto", "Listen", "Local Address");
		} else {
			printf((Aflag && !Wflag) ?
			    "%-5.5s %-6.6s %-6.6s %-17.17s %-17.17s %s\n" :
			    "%-5.5s %-6.6s %-6.6s %-21.21s %-21.21s %s\n",
			    "Proto", "Recv-Q", "Send-Q",
			    "Local Address", "Foreign Address",
			    "(state)");
		}
		ppr_first = 0;
	}
	if (Lflag && so->so_qlimit == 0)
		return;
	if (Aflag) {
		if (tp)
			printf("%8lx ", (u_long)inp->inp_ppcb);
		else
			printf("%8lx ", (u_long)so->so_pcb);
	}
	if (Pflag) {
		if (tp) {
			int window = MIN(tp->snd_cwnd, tp->snd_bwnd);
			if (window == 1073725440)
				printf("%8s ", "max");
			else
				printf("%8d ", (int)MIN(tp->snd_cwnd, tp->snd_bwnd));
			printf("%8d ", (int)(tp->snd_max - tp->snd_una));
			if (tp->t_srtt == 0)
			    printf("%8s ", "-");
			else
			    printf("%8.3f ", (double)tp->t_srtt * 1000.0 / TCP_RTT_SCALE / clockinfo.hz);
		} else {
			printf("%8s %8s %8s ", "-", "-", "-");
		}
	}
#ifdef INET6
	if (INP_ISIPV6(inp))
		vchar = "6 ";
	else
#endif
		vchar = INP_ISIPV4(inp) ? "4 " : "  ";

	printf("%-3.3s%-2.2s ", name, vchar);
	if (Lflag) {
		char buf[15];

		snprintf(buf, sizeof(buf), "%d/%d/%d", so->so_qlen,
			 so->so_incqlen, so->so_qlimit);
		printf("%-13.13s ", buf);
	} else if (Bflag) {
		printf("%6ld %6ld ",
		       so->so_rcv.sb_hiwat,
		       so->so_snd.sb_hiwat);
	} else {
		printf("%6ld %6ld ",
		       so->so_rcv.sb_cc,
		       so->so_snd.sb_cc);
	}
	if (numeric_port) {
		if (INP_ISIPV4(inp)) {
			inetprint(&inp->inp_laddr, (int)inp->inp_lport,
				  name, 1);
			if (!Lflag)
				inetprint(&inp->inp_faddr,
					  (int)inp->inp_fport, name, 1);
		}
#ifdef INET6
		else if (INP_ISIPV6(inp)) {
			inet6print(&inp->in6p_laddr,
				   (int)inp->inp_lport, name, 1);
			if (!Lflag)
				inet6print(&inp->in6p_faddr,
					   (int)inp->inp_fport, name, 1);
		} /* else nothing printed now */
#endif /* INET6 */
	} else if (inp->inp_flags & INP_ANONPORT) {
		if (INP_ISIPV4(inp)) {
			inetprint(&inp->inp_laddr, (int)inp->inp_lport,
				  name, 1);
			if (!Lflag)
				inetprint(&inp->inp_faddr,
					  (int)inp->inp_fport, name, 0);
		}
#ifdef INET6
		else if (INP_ISIPV6(inp)) {
			inet6print(&inp->in6p_laddr,
				   (int)inp->inp_lport, name, 1);
			if (!Lflag)
				inet6print(&inp->in6p_faddr,
					   (int)inp->inp_fport, name, 0);
		} /* else nothing printed now */
#endif /* INET6 */
	} else {
		if (INP_ISIPV4(inp)) {
			inetprint(&inp->inp_laddr, (int)inp->inp_lport,
				  name, 0);
			if (!Lflag)
				inetprint(&inp->inp_faddr,
					  (int)inp->inp_fport, name,
					  inp->inp_lport !=
						inp->inp_fport);
		}
#ifdef INET6
		else if (INP_ISIPV6(inp)) {
			inet6print(&inp->in6p_laddr,
				   (int)inp->inp_lport, name, 0);
			if (!Lflag)
				inet6print(&inp->in6p_faddr,
					   (int)inp->inp_fport, name,
					   inp->inp_lport !=
						inp->inp_fport);
		} /* else nothing printed now */
#endif /* INET6 */
	}
	if (tp && !Lflag) {
		if (tp->t_state < 0 || tp->t_state >= TCP_NSTATES)
			printf("%d", tp->t_state);
	      else {
			printf("%s", tcpstates[tp->t_state]);
#if defined(TF_NEEDSYN) && defined(TF_NEEDFIN)
		      /* Show T/TCP `hidden state' */
		      if (tp->t_flags & (TF_NEEDSYN|TF_NEEDFIN))
			      putchar('*');
#endif /* defined(TF_NEEDSYN) && defined(TF_NEEDFIN) */
	      }
	}
	putchar('\n');
}



#define CPU_STATS_FUNC(proto,type)                            \
static void                                                   \
proto ##_stats_agg(type *ary, type *ttl, int cpucnt)          \
{                                                             \
    int i, off, siz;                                          \
    siz = sizeof(type);                                       \
                                                              \
    if (!ary && !ttl)                                         \
        return;                                               \
                                                              \
    bzero(ttl, siz);                                          \
    if (cpucnt == 1) {                                        \
        *ttl = ary[0];                                        \
    } else {                                                  \
        for (i = 0; i < cpucnt; ++i) {                        \
            for (off = 0; off < siz; off += sizeof(u_long)) { \
                *(u_long *)((char *)(*(&ttl)) + off) +=       \
                *(u_long *)((char *)&ary[i] + off);           \
            }                                                 \
        }                                                     \
    }                                                         \
}
CPU_STATS_FUNC(tcp, struct tcp_stats);
CPU_STATS_FUNC(ip, struct ip_stats);
CPU_STATS_FUNC(udp, struct udpstat);

/*
 * Dump TCP statistics structure.
 */
void
tcp_stats(u_long off __unused, const char *name, int af1 __unused)
{
	struct tcp_stats tcpstat, *stattmp;
	struct tcp_stats zerostat[SMP_MAXCPU];
	size_t len = sizeof(struct tcp_stats) * SMP_MAXCPU;
	int cpucnt;
	
	if (zflag)
		memset(zerostat, 0, len);

	if ((stattmp = malloc(len)) == NULL) {
		return;
	} else {
		if (sysctlbyname("net.inet.tcp.stats", stattmp, &len,
			zflag ? zerostat : NULL, zflag ? len : 0) < 0) {
			warn("sysctl: net.inet.tcp.stats");
			free(stattmp);
			return;
		} else {
			if ((stattmp = realloc(stattmp, len)) == NULL) {
				warn("tcp_stats");
				return;
			}
		}
	}
	cpucnt = len / sizeof(struct tcp_stats);
	tcp_stats_agg(stattmp, &tcpstat, cpucnt);

#ifdef INET6
	if (tcp_done != 0)
		return;
	else
		tcp_done = 1;
#endif

	printf ("%s:\n", name);

#define	p(f, m) if (tcpstat.f || sflag <= 1) \
    printf(m, tcpstat.f, plural(tcpstat.f))
#define	p1a(f, m) if (tcpstat.f || sflag <= 1) \
    printf(m, tcpstat.f)
#define	p2(f1, f2, m) if (tcpstat.f1 || tcpstat.f2 || sflag <= 1) \
    printf(m, tcpstat.f1, plural(tcpstat.f1), tcpstat.f2, plural(tcpstat.f2))
#define	p2a(f1, f2, m) if (tcpstat.f1 || tcpstat.f2 || sflag <= 1) \
    printf(m, tcpstat.f1, plural(tcpstat.f1), tcpstat.f2)
#define	p3(f, m) if (tcpstat.f || sflag <= 1) \
    printf(m, tcpstat.f, plurales(tcpstat.f))

	p(tcps_sndtotal, "\t%lu packet%s sent\n");
	p2(tcps_sndpack,tcps_sndbyte,
		"\t\t%lu data packet%s (%lu byte%s)\n");
	p2(tcps_sndrexmitpack, tcps_sndrexmitbyte,
		"\t\t%lu data packet%s (%lu byte%s) retransmitted\n");
	p2(tcps_sndsackrtopack, tcps_sndsackrtobyte,
		"\t\t%lu data packet%s (%lu byte%s) retransmitted by SACK\n");
	p2(tcps_sndsackpack, tcps_sndsackbyte,
		"\t\t%lu data packet%s (%lu byte%s) sent by SACK recovery\n");
	p2(tcps_sackrescue, tcps_sackrescue_try,
		"\t\t%lu SACK rescue packet%s sent (of %lu attempt%s)\n");
	p2a(tcps_sndfastrexmit, tcps_sndearlyrexmit,
		"\t\t%lu Fast Retransmit%s (%lu early)\n");
	p(tcps_sndlimited, "\t\t%lu packet%s sent by Limited Transmit\n");
	p2(tcps_sndrtobad, tcps_eifelresponse,
		"\t\t%lu spurious RTO retransmit%s (%lu Eifel-response%s)\n");
	p2a(tcps_sndfastrexmitbad, tcps_sndearlyrexmitbad,
		"\t\t%lu spurious Fast Retransmit%s (%lu early)\n");
	p2a(tcps_eifeldetected, tcps_rttcantdetect,
		"\t\t%lu Eifel-detected spurious retransmit%s (%lu non-RTT)\n");
	p(tcps_rttdetected, "\t\t%lu RTT-detected spurious retransmit%s\n");
	p(tcps_mturesent, "\t\t%lu resend%s initiated by MTU discovery\n");
	p(tcps_sndsackopt, "\t\t%lu SACK option%s sent\n");
	p(tcps_snddsackopt, "\t\t%lu D-SACK option%s sent\n");
	p2a(tcps_sndacks, tcps_delack,
		"\t\t%lu ack-only packet%s (%lu delayed)\n");
	p(tcps_sndurg, "\t\t%lu URG only packet%s\n");
	p(tcps_sndprobe, "\t\t%lu window probe packet%s\n");
	p(tcps_sndwinup, "\t\t%lu window update packet%s\n");
	p(tcps_sndctrl, "\t\t%lu control packet%s\n");
	p(tcps_rcvtotal, "\t%lu packet%s received\n");
	p2(tcps_rcvackpack, tcps_rcvackbyte, "\t\t%lu ack%s (for %lu byte%s)\n");
	p(tcps_rcvdupack, "\t\t%lu duplicate ack%s\n");
	p(tcps_rcvacktoomuch, "\t\t%lu ack%s for unsent data\n");
	p2(tcps_rcvpack, tcps_rcvbyte,
		"\t\t%lu packet%s (%lu byte%s) received in-sequence\n");
	p2(tcps_rcvduppack, tcps_rcvdupbyte,
		"\t\t%lu completely duplicate packet%s (%lu byte%s)\n");
	p2(tcps_pawsdrop, tcps_pawsaccept,
		"\t\t%lu old duplicate packet%s (%lu packet%s accepted)\n");
	p2(tcps_rcvpartduppack, tcps_rcvpartdupbyte,
		"\t\t%lu packet%s with some dup. data (%lu byte%s duped)\n");
	p2(tcps_rcvoopack, tcps_rcvoobyte,
		"\t\t%lu out-of-order packet%s (%lu byte%s)\n");
	p2(tcps_rcvpackafterwin, tcps_rcvbyteafterwin,
		"\t\t%lu packet%s (%lu byte%s) of data after window\n");
	p(tcps_rcvwinprobe, "\t\t%lu window probe%s\n");
	p(tcps_rcvwinupd, "\t\t%lu window update packet%s\n");
	p(tcps_rcvafterclose, "\t\t%lu packet%s received after close\n");
	p(tcps_rcvbadsum, "\t\t%lu discarded for bad checksum%s\n");
	p(tcps_rcvbadoff, "\t\t%lu discarded for bad header offset field%s\n");
	p1a(tcps_rcvshort, "\t\t%lu discarded because packet too short\n");
	p(tcps_rcvbadsackopt, "\t\t%lu bad SACK option%s\n");
	p1a(tcps_sackrenege, "\t\t%lu other side reneged\n");
	p(tcps_connattempt, "\t%lu connection request%s\n");
	p(tcps_accepts, "\t%lu connection accept%s\n");
	p(tcps_badsyn, "\t%lu bad connection attempt%s\n");
	p(tcps_listendrop, "\t%lu listen queue overflow%s\n");
	p(tcps_connects, "\t%lu connection%s established (including accepts)\n");
	p2(tcps_closed, tcps_drops,
		"\t%lu connection%s closed (including %lu drop%s)\n");
	p(tcps_cachedrtt, "\t\t%lu connection%s updated cached RTT on close\n");
	p(tcps_cachedrttvar, 
	  "\t\t%lu connection%s updated cached RTT variance on close\n");
	p(tcps_cachedssthresh,
	  "\t\t%lu connection%s updated cached ssthresh on close\n");
	p(tcps_conndrops, "\t%lu embryonic connection%s dropped\n");
	p2(tcps_rttupdated, tcps_segstimed,
		"\t%lu segment%s updated rtt (of %lu attempt%s)\n");
	p(tcps_rexmttimeo, "\t%lu retransmit timeout%s\n");
	p(tcps_timeoutdrop, "\t\t%lu connection%s dropped by rexmit timeout\n");
	p(tcps_persisttimeo, "\t%lu persist timeout%s\n");
	p(tcps_persistdrop, "\t\t%lu connection%s dropped by persist timeout\n");
	p(tcps_keeptimeo, "\t%lu keepalive timeout%s\n");
	p(tcps_keepprobe, "\t\t%lu keepalive probe%s sent\n");
	p(tcps_keepdrops, "\t\t%lu connection%s dropped by keepalive\n");
	p(tcps_predack, "\t%lu correct ACK header prediction%s\n");
	p(tcps_preddat, "\t%lu correct data packet header prediction%s\n");
	p(tcps_sndidle, "\t%lu send idle%s\n");

	p1a(tcps_sc_added, "\t%lu syncache entries added\n"); 
	p1a(tcps_sc_retransmitted, "\t\t%lu retransmitted\n"); 
	p1a(tcps_sc_dupsyn, "\t\t%lu dupsyn\n"); 
	p1a(tcps_sc_dropped, "\t\t%lu dropped\n"); 
	p1a(tcps_sc_completed, "\t\t%lu completed\n"); 
	p1a(tcps_sc_bucketoverflow, "\t\t%lu bucket overflow\n"); 
	p1a(tcps_sc_cacheoverflow, "\t\t%lu cache overflow\n"); 
	p1a(tcps_sc_reset, "\t\t%lu reset\n"); 
	p1a(tcps_sc_stale, "\t\t%lu stale\n"); 
	p1a(tcps_sc_aborted, "\t\t%lu aborted\n"); 
	p1a(tcps_sc_badack, "\t\t%lu badack\n"); 
	p1a(tcps_sc_unreach, "\t\t%lu unreach\n"); 
	p1a(tcps_sc_zonefail, "\t\t%lu zone failures\n"); 
	p1a(tcps_sc_sendcookie, "\t\t%lu cookies sent\n"); 
	p1a(tcps_sc_recvcookie, "\t\t%lu cookies received\n"); 

	p(tcps_sacksbupdate, "\t%lu SACK scoreboard update%s\n");
	p(tcps_sacksboverflow, "\t\t%lu overflow%s\n");
	p(tcps_sacksbfailed, "\t\t%lu failure%s\n");
	p(tcps_sacksbreused, "\t\t%lu record%s reused\n");
	p(tcps_sacksbfast, "\t\t%lu record%s fast allocated\n");

	free(stattmp);
#undef p
#undef p1a
#undef p2
#undef p2a
#undef p3
}

/*
 * Dump UDP statistics structure.
 */
void
udp_stats(u_long off __unused, const char *name, int af1 __unused)
{
	struct udpstat udpstat, *stattmp;
	struct udpstat zerostat[SMP_MAXCPU];
	size_t len = sizeof(struct udpstat) * SMP_MAXCPU;
	int cpucnt;
	u_long delivered;

	if (zflag)
		memset(&zerostat, 0, len);

	if ((stattmp = malloc(len)) == NULL) {
		return;
	} else {
		if (sysctlbyname("net.inet.udp.stats", stattmp, &len,
			zflag ? zerostat : NULL, zflag ? len : 0) < 0) {
			warn("sysctl: net.inet.udp.stats");
			free(stattmp);
			return;
		} else {
			if ((stattmp = realloc(stattmp, len)) == NULL) {
				warn("udp_stats");
				return;
			}
		}
	}
	cpucnt = len / sizeof(struct udpstat);
	udp_stats_agg(stattmp, &udpstat, cpucnt);

#ifdef INET6
	if (udp_done != 0)
		return;
	else
		udp_done = 1;
#endif

	printf("%s:\n", name);
#define	p(f, m) if (udpstat.f || sflag <= 1) \
    printf(m, udpstat.f, plural(udpstat.f))
#define	p1a(f, m) if (udpstat.f || sflag <= 1) \
    printf(m, udpstat.f)
	p(udps_ipackets, "\t%lu datagram%s received\n");
	p1a(udps_hdrops, "\t%lu with incomplete header\n");
	p1a(udps_badlen, "\t%lu with bad data length field\n");
	p1a(udps_badsum, "\t%lu with bad checksum\n");
	p1a(udps_nosum, "\t%lu with no checksum\n");
	p1a(udps_noport, "\t%lu dropped due to no socket\n");
	p(udps_noportbcast,
	    "\t%lu broadcast/multicast datagram%s dropped due to no socket\n");
	p1a(udps_fullsock, "\t%lu dropped due to full socket buffers\n");
	p1a(udpps_pcbhashmiss, "\t%lu not for hashed pcb\n");
	delivered = udpstat.udps_ipackets -
		    udpstat.udps_hdrops -
		    udpstat.udps_badlen -
		    udpstat.udps_badsum -
		    udpstat.udps_noport -
		    udpstat.udps_noportbcast -
		    udpstat.udps_fullsock;
	if (delivered || sflag <= 1)
		printf("\t%lu delivered\n", delivered);
	p(udps_opackets, "\t%lu datagram%s output\n");
#undef p
#undef p1a
}

/* 
 * Dump CARP statistics structure.
 */
void
carp_stats(u_long off __unused, const char *name, int af1 __unused)
{
       struct carpstats carpstat, zerostat;
       size_t len = sizeof(struct carpstats);

       if (zflag)
               memset(&zerostat, 0, len);
       if (sysctlbyname("net.inet.carp.stats", &carpstat, &len,
           zflag ? &zerostat : NULL, zflag ? len : 0) < 0) {
               warn("sysctl: net.inet.carp.stats");
               return;
       }

       printf("%s:\n", name);

#define p(f, m) if (carpstat.f || sflag <= 1) \
       printf(m, (uintmax_t)carpstat.f, plural((int)carpstat.f))
#define p2(f, m) if (carpstat.f || sflag <= 1) \
       printf(m, (uintmax_t)carpstat.f)

       p(carps_ipackets, "\t%ju packet%s received (IPv4)\n");
       p(carps_ipackets6, "\t%ju packet%s received (IPv6)\n");
       p(carps_badttl, "\t\t%ju packet%s discarded for wrong TTL\n");
       p(carps_hdrops, "\t\t%ju packet%s shorter than header\n");
       p(carps_badsum, "\t\t%ju discarded for bad checksum%s\n");
       p(carps_badver, "\t\t%ju discarded packet%s with a bad version\n");
       p2(carps_badlen, "\t\t%ju discarded because packet too short\n");
       p2(carps_badauth, "\t\t%ju discarded for bad authentication\n");
       p2(carps_badvhid, "\t\t%ju discarded for bad vhid\n");
       p2(carps_badaddrs, "\t\t%ju discarded because of a bad address list\n");
       p(carps_opackets, "\t%ju packet%s sent (IPv4)\n");
       p(carps_opackets6, "\t%ju packet%s sent (IPv6)\n");
       p2(carps_onomem, "\t\t%ju send failed due to mbuf memory error\n");
#if notyet
       p(carps_ostates, "\t\t%s state update%s sent\n");
#endif
#undef p
#undef p2
}

/*
 * Dump IP statistics structure.
 */
void
ip_stats(u_long off __unused, const char *name, int af1 __unused)
{
	struct ip_stats ipstat, *stattmp;
	struct ip_stats zerostat[SMP_MAXCPU];
	size_t len = sizeof(struct ip_stats) * SMP_MAXCPU;
	int cpucnt;

	if (zflag)
		memset(zerostat, 0, len);
	if ((stattmp = malloc(len)) == NULL) {
		return;
	} else {
		if (sysctlbyname("net.inet.ip.stats", stattmp, &len,
			zflag ? zerostat : NULL, zflag ? len : 0) < 0) {
				warn("sysctl: net.inet.ip.stats");
				free(stattmp);
				return;
		} else {
			if ((stattmp = realloc(stattmp, len)) == NULL) {
				warn("ip_stats");
				return;
			}
		}
	}
	cpucnt = len / sizeof(struct ip_stats);
	ip_stats_agg(stattmp, &ipstat, cpucnt);

	printf("%s:\n", name);

#define	p(f, m) if (ipstat.f || sflag <= 1) \
    printf(m, ipstat.f, plural(ipstat.f))
#define	p1a(f, m) if (ipstat.f || sflag <= 1) \
    printf(m, ipstat.f)

	p(ips_total, "\t%lu total packet%s received\n");
	p(ips_badsum, "\t%lu bad header checksum%s\n");
	p1a(ips_toosmall, "\t%lu with size smaller than minimum\n");
	p1a(ips_tooshort, "\t%lu with data size < data length\n");
	p1a(ips_toolong, "\t%lu with ip length > max ip packet size\n");
	p1a(ips_badhlen, "\t%lu with header length < data size\n");
	p1a(ips_badlen, "\t%lu with data length < header length\n");
	p1a(ips_badoptions, "\t%lu with bad options\n");
	p1a(ips_badvers, "\t%lu with incorrect version number\n");
	p(ips_fragments, "\t%lu fragment%s received\n");
	p(ips_fragdropped, "\t%lu fragment%s dropped (dup or out of space)\n");
	p(ips_fragtimeout, "\t%lu fragment%s dropped after timeout\n");
	p(ips_reassembled, "\t%lu packet%s reassembled ok\n");
	p(ips_delivered, "\t%lu packet%s for this host\n");
	p(ips_noproto, "\t%lu packet%s for unknown/unsupported protocol\n");
	p(ips_forward, "\t%lu packet%s forwarded");
	p(ips_fastforward, " (%lu packet%s fast forwarded)");
	if (ipstat.ips_forward || sflag <= 1) 
		putchar('\n');
	p(ips_cantforward, "\t%lu packet%s not forwardable\n");
	p(ips_notmember,
	  "\t%lu packet%s received for unknown multicast group\n");
	p(ips_redirectsent, "\t%lu redirect%s sent\n");
	p(ips_localout, "\t%lu packet%s sent from this host\n");
	p(ips_rawout, "\t%lu packet%s sent with fabricated ip header\n");
	p(ips_odropped,
	  "\t%lu output packet%s dropped due to no bufs, etc.\n");
	p(ips_noroute, "\t%lu output packet%s discarded due to no route\n");
	p(ips_fragmented, "\t%lu output datagram%s fragmented\n");
	p(ips_ofragments, "\t%lu fragment%s created\n");
	p(ips_cantfrag, "\t%lu datagram%s that can't be fragmented\n");
	p(ips_nogif, "\t%lu tunneling packet%s that can't find gif\n");
	p(ips_badaddr, "\t%lu datagram%s with bad address in header\n");
	free(stattmp);
#undef p
#undef p1a
}

static	const char *icmpnames[] = {
	"echo reply",
	"#1",
	"#2",
	"destination unreachable",
	"source quench",
	"routing redirect",
	"#6",
	"#7",
	"echo",
	"router advertisement",
	"router solicitation",
	"time exceeded",
	"parameter problem",
	"time stamp",
	"time stamp reply",
	"information request",
	"information request reply",
	"address mask request",
	"address mask reply",
};

/*
 * Dump ICMP statistics.
 */
void
icmp_stats(u_long off __unused, const char *name, int af1 __unused)
{
	struct icmpstat icmpstat, zerostat;
	int i, first;
	int mib[4];		/* CTL_NET + PF_INET + IPPROTO_ICMP + req */
	size_t len;

	mib[0] = CTL_NET;
	mib[1] = PF_INET;
	mib[2] = IPPROTO_ICMP;
	mib[3] = ICMPCTL_STATS;

	len = sizeof icmpstat;
	if (zflag)
		memset(&zerostat, 0, len);
	if (sysctl(mib, 4, &icmpstat, &len,
	    zflag ? &zerostat : NULL, zflag ? len : 0) < 0) {
		warn("sysctl: net.inet.icmp.stats");
		return;
	}

	printf("%s:\n", name);

#define	p(f, m) if (icmpstat.f || sflag <= 1) \
    printf(m, icmpstat.f, plural(icmpstat.f))
#define	p1a(f, m) if (icmpstat.f || sflag <= 1) \
    printf(m, icmpstat.f)
#define	p2(f, m) if (icmpstat.f || sflag <= 1) \
    printf(m, icmpstat.f, plurales(icmpstat.f))

	p(icps_error, "\t%lu call%s to icmp_error\n");
	p(icps_oldicmp,
	    "\t%lu error%s not generated 'cuz old message was icmp\n");
	for (first = 1, i = 0; i < ICMP_MAXTYPE + 1; i++)
		if (icmpstat.icps_outhist[i] != 0) {
			if (first) {
				printf("\tOutput histogram:\n");
				first = 0;
			}
			printf("\t\t%s: %lu\n", icmpnames[i],
				icmpstat.icps_outhist[i]);
		}
	p(icps_badcode, "\t%lu message%s with bad code fields\n");
	p(icps_tooshort, "\t%lu message%s < minimum length\n");
	p(icps_checksum, "\t%lu bad checksum%s\n");
	p(icps_badlen, "\t%lu message%s with bad length\n");
	p1a(icps_bmcastecho, "\t%lu multicast echo requests ignored\n");
	p1a(icps_bmcasttstamp, "\t%lu multicast timestamp requests ignored\n");
	for (first = 1, i = 0; i < ICMP_MAXTYPE + 1; i++)
		if (icmpstat.icps_inhist[i] != 0) {
			if (first) {
				printf("\tInput histogram:\n");
				first = 0;
			}
			printf("\t\t%s: %lu\n", icmpnames[i],
				icmpstat.icps_inhist[i]);
		}
	p(icps_reflect, "\t%lu message response%s generated\n");
	p2(icps_badaddr, "\t%lu invalid return address%s\n");
	p(icps_noroute, "\t%lu no return route%s\n");
#undef p
#undef p1a
#undef p2
	mib[3] = ICMPCTL_MASKREPL;
	len = sizeof i;
	if (sysctl(mib, 4, &i, &len, NULL, 0) < 0)
		return;
	printf("\tICMP address mask responses are %sabled\n", 
	       i ? "en" : "dis");
}

/*
 * Dump IGMP statistics structure.
 */
void
igmp_stats(u_long off __unused, const char *name, int af1 __unused)
{
	struct igmpstat igmpstat, zerostat;
	size_t len = sizeof igmpstat;

	if (zflag)
		memset(&zerostat, 0, len);
	if (sysctlbyname("net.inet.igmp.stats", &igmpstat, &len,
	    zflag ? &zerostat : NULL, zflag ? len : 0) < 0) {
		warn("sysctl: net.inet.igmp.stats");
		return;
	}

	printf("%s:\n", name);

#define	p(f, m) if (igmpstat.f || sflag <= 1) \
    printf(m, igmpstat.f, plural(igmpstat.f))
#define	py(f, m) if (igmpstat.f || sflag <= 1) \
    printf(m, igmpstat.f, igmpstat.f != 1 ? "ies" : "y")
	p(igps_rcv_total, "\t%u message%s received\n");
        p(igps_rcv_tooshort, "\t%u message%s received with too few bytes\n");
        p(igps_rcv_badsum, "\t%u message%s received with bad checksum\n");
        py(igps_rcv_queries, "\t%u membership quer%s received\n");
        py(igps_rcv_badqueries, "\t%u membership quer%s received with invalid field(s)\n");
        p(igps_rcv_reports, "\t%u membership report%s received\n");
        p(igps_rcv_badreports, "\t%u membership report%s received with invalid field(s)\n");
        p(igps_rcv_ourreports, "\t%u membership report%s received for groups to which we belong\n");
        p(igps_snd_reports, "\t%u membership report%s sent\n");
#undef p
#undef py
}

/*
 * Dump PIM statistics structure.
 */
void
pim_stats(u_long off __unused, const char *name, int af1 __unused)
{
	struct pimstat pimstat, zerostat;
	size_t len = sizeof pimstat;

	if (zflag)
		memset(&zerostat, 0, len);
	if (sysctlbyname("net.inet.pim.stats", &pimstat, &len,
	    zflag ? &zerostat : NULL, zflag ? len : 0) < 0) {
		if (errno != ENOENT)
			warn("sysctl: net.inet.pim.stats");
		return;
	}

	printf("%s:\n", name);

#define	p(f, m) if (pimstat.f || sflag <= 1) \
    printf(m, (uintmax_t)pimstat.f, plural(pimstat.f))
#define	py(f, m) if (pimstat.f || sflag <= 1) \
    printf(m, (uintmax_t)pimstat.f, pimstat.f != 1 ? "ies" : "y")
	p(pims_rcv_total_msgs, "\t%ju message%s received\n");
	p(pims_rcv_total_bytes, "\t%ju byte%s received\n");
	p(pims_rcv_tooshort, "\t%ju message%s received with too few bytes\n");
        p(pims_rcv_badsum, "\t%ju message%s received with bad checksum\n");
	p(pims_rcv_badversion, "\t%ju message%s received with bad version\n");
	p(pims_rcv_registers_msgs, "\t%ju data register message%s received\n");
	p(pims_rcv_registers_bytes, "\t%ju data register byte%s received\n");
	p(pims_rcv_registers_wrongiif, "\t%ju data register message%s received on wrong iif\n");
	p(pims_rcv_badregisters, "\t%ju bad register%s received\n");
	p(pims_snd_registers_msgs, "\t%ju data register message%s sent\n");
	p(pims_snd_registers_bytes, "\t%ju data register byte%s sent\n");
#undef p
#undef py
}

/*
 * Pretty print an Internet address (net address + port).
 */
void
inetprint(struct in_addr *in, int port, const char *proto, int num_port)
{
	struct servent *sp = NULL;
	char line[80], *cp;
	int width;

	if (Wflag)
	    sprintf(line, "%s.", inetname(in));
	else
	    sprintf(line, "%.*s.", (Aflag && !num_port) ? 12 : 16, inetname(in));
	cp = strchr(line, '\0');
	if (!num_port && port)
		sp = getservbyport((int)port, proto);
	if (sp || port == 0)
		sprintf(cp, "%.15s ", sp ? sp->s_name : "*");
	else
		sprintf(cp, "%d ", ntohs((u_short)port));
	width = (Aflag && !Wflag) ? 17 : 21;
	if (Wflag)
	    printf("%-*s ", width, line);
	else
	    printf("%-*.*s ", width, width, line);
}

/*
 * Construct an Internet address representation.
 * If numeric_addr has been supplied, give
 * numeric value, otherwise try for symbolic name.
 */
char *
inetname(struct in_addr *inp)
{
	char *cp;
	static char line[MAXHOSTNAMELEN];
	struct hostent *hp;
	struct netent *np;

	cp = NULL;
	if (!numeric_addr && inp->s_addr != INADDR_ANY) {
		int net = inet_netof(*inp);
		int lna = inet_lnaof(*inp);

		if (lna == INADDR_ANY) {
			np = getnetbyaddr(net, AF_INET);
			if (np)
				cp = np->n_name;
		}
		if (cp == NULL) {
			hp = gethostbyaddr(inp, sizeof (*inp), AF_INET);
			if (hp) {
				cp = hp->h_name;
				trimdomain(cp, strlen(cp));
			}
		}
	}
	if (inp->s_addr == INADDR_ANY)
		strcpy(line, "*");
	else if (cp) {
		strncpy(line, cp, sizeof(line) - 1);
		line[sizeof(line) - 1] = '\0';
	} else {
		inp->s_addr = ntohl(inp->s_addr);
#define C(x)	((u_int)((x) & 0xff))
		sprintf(line, "%u.%u.%u.%u", C(inp->s_addr >> 24),
		    C(inp->s_addr >> 16), C(inp->s_addr >> 8), C(inp->s_addr));
	}
	return (line);
}
