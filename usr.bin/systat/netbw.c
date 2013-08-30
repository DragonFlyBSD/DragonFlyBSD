/*
 * Copyright (c) 2013 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
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
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/sysctl.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/route.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif
#include <netinet/in_pcb.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcpip.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_debug.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <nlist.h>
#include <paths.h>
#include "systat.h"
#include "extern.h"

struct mytcpcb {
	RB_ENTRY(mytcpcb)	rb_node;
	int			seq;
	struct xtcpcb		xtcp;
	struct xtcpcb		last_xtcp;
};

static int
mytcpcb_cmp(struct mytcpcb *tcp1, struct mytcpcb *tcp2)
{
	int r;

	/*
	 * Low local or foreign port comes first (local has priority).
	 */
	if (ntohs(tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie_lport) >= 1024 &&
	    ntohs(tcp2->xtcp.xt_inp.inp_inc.inc_ie.ie_lport) >= 1024) {
		if (ntohs(tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie_fport) <
		    ntohs(tcp2->xtcp.xt_inp.inp_inc.inc_ie.ie_fport))
			return(-1);
		if (ntohs(tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie_fport) >
		    ntohs(tcp2->xtcp.xt_inp.inp_inc.inc_ie.ie_fport))
			return(1);
	}

	if (ntohs(tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie_lport) <
	    ntohs(tcp2->xtcp.xt_inp.inp_inc.inc_ie.ie_lport))
		return(-1);
	if (ntohs(tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie_lport) >
	    ntohs(tcp2->xtcp.xt_inp.inp_inc.inc_ie.ie_lport))
		return(1);
	if (ntohs(tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie_fport) <
	    ntohs(tcp2->xtcp.xt_inp.inp_inc.inc_ie.ie_fport))
		return(-1);
	if (ntohs(tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie_fport) >
	    ntohs(tcp2->xtcp.xt_inp.inp_inc.inc_ie.ie_fport))
		return(1);

	/*
	 * Sort IPV4 vs IPV6 addresses
	 */
	if ((tcp1->xtcp.xt_inp.inp_vflag & (INP_IPV4|INP_IPV6)) <
	    (tcp2->xtcp.xt_inp.inp_vflag & (INP_IPV4|INP_IPV6)))
		return(-1);
	if ((tcp1->xtcp.xt_inp.inp_vflag & (INP_IPV4|INP_IPV6)) >
	    (tcp2->xtcp.xt_inp.inp_vflag & (INP_IPV4|INP_IPV6)))
		return(1);

	/*
	 * Local and foreign addresses
	 */
	if (tcp1->xtcp.xt_inp.inp_vflag & INP_IPV4) {
		if (ntohl(tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie_laddr.s_addr) <
		    ntohl(tcp2->xtcp.xt_inp.inp_inc.inc_ie.ie_laddr.s_addr))
			return(-1);
		if (ntohl(tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie_laddr.s_addr) >
		    ntohl(tcp2->xtcp.xt_inp.inp_inc.inc_ie.ie_laddr.s_addr))
			return(1);
		if (ntohl(tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie_faddr.s_addr) <
		    ntohl(tcp2->xtcp.xt_inp.inp_inc.inc_ie.ie_faddr.s_addr))
			return(-1);
		if (ntohl(tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie_faddr.s_addr) >
		    ntohl(tcp2->xtcp.xt_inp.inp_inc.inc_ie.ie_faddr.s_addr))
			return(1);
	} else if (tcp1->xtcp.xt_inp.inp_vflag & INP_IPV6) {
		r = bcmp(&tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie6_faddr,
			 &tcp2->xtcp.xt_inp.inp_inc.inc_ie.ie6_faddr,
			 sizeof(tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie6_faddr));
		if (r)
			return(r);
	} else {
		r = bcmp(&tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie6_faddr,
			 &tcp2->xtcp.xt_inp.inp_inc.inc_ie.ie6_faddr,
			 sizeof(tcp1->xtcp.xt_inp.inp_inc.inc_ie.ie6_faddr));
		if (r)
			return(r);
	}
	return(0);
}

struct mytcpcb_tree;
RB_HEAD(mytcpcb_tree, mytcpcb);
RB_PROTOTYPE(mytcpcb_tree, mytcpcb, rb_node, mytcpcb_cmp);
RB_GENERATE(mytcpcb_tree, mytcpcb, rb_node, mytcpcb_cmp);

static struct mytcpcb_tree mytcp_tree;
static struct timeval tv_curr;
static struct timeval tv_last;
static struct tcp_stats tcp_curr;
static struct tcp_stats tcp_last;
static int tcp_pcb_seq;

static const char *numtok(double value);
const char * netaddrstr(u_char vflags, union in_dependaddr *depaddr,
			u_int16_t port);
static void updatepcb(struct xtcpcb *xtcp);

#define DELTARATE(field)	\
	((double)(tcp_curr.field - tcp_last.field) / delta_time)

#define DELTAELM(field)		\
	((double)(tcp_seq_diff_t)(elm->xtcp.field -		\
				  elm->last_xtcp.field) /	\
	 delta_time)

#define DELTAELMSCALE(field, scale)		\
	((double)((tcp_seq_diff_t)(elm->xtcp.field -		\
				   elm->last_xtcp.field) << scale) / \
         delta_time)

WINDOW *
opennetbw(void)
{
	RB_INIT(&mytcp_tree);
	return (subwin(stdscr, LINES-0-1, 0, 0, 0));
}

void
closenetbw(WINDOW *w)
{
	struct mytcpcb *mytcp;

	while ((mytcp = RB_ROOT(&mytcp_tree)) != NULL) {
		RB_REMOVE(mytcpcb_tree, &mytcp_tree, mytcp);
		free(mytcp);
	}

        if (w != NULL) {
		wclear(w);
		wrefresh(w);
		delwin(w);
	}
}

int
initnetbw(void)
{
	return(1);
}

void
fetchnetbw(void)
{
	struct tcp_stats tcp_array[SMP_MAXCPU];
	struct xtcpcb *tcp_pcbs;
	size_t npcbs;
	size_t len;
	size_t i;
	size_t j;
	size_t ncpus;

	/*
	 * Extract PCB list
	 */
	len = 0;
	if (sysctlbyname("net.inet.tcp.pcblist", NULL, &len, NULL, 0) < 0)
		return;
	len += 128 * sizeof(tcp_pcbs[0]);
	tcp_pcbs = malloc(len);
	if (sysctlbyname("net.inet.tcp.pcblist", tcp_pcbs, &len, NULL, 0) < 0) {
		free(tcp_pcbs);
		return;
	}
	npcbs = len / sizeof(tcp_pcbs[0]);
	++tcp_pcb_seq;

	for (i = 0; i < npcbs; ++i) {
		if (tcp_pcbs[i].xt_len != sizeof(tcp_pcbs[0]))
			break;
		updatepcb(&tcp_pcbs[i]);
	}
	free(tcp_pcbs);

	/*
	 * General stats
	 */
	len = sizeof(tcp_array);
	if (sysctlbyname("net.inet.tcp.stats", tcp_array, &len, NULL, 0) < 0) {
		free(tcp_pcbs);
		return;
	}
	ncpus = len / sizeof(tcp_array[0]);
	tcp_last = tcp_curr;
	tv_last = tv_curr;
	bzero(&tcp_curr, sizeof(tcp_curr));
	gettimeofday(&tv_curr, NULL);

	for (i = 0; i < ncpus; ++i) {
		for (j = 0; j < sizeof(tcp_curr) / sizeof(u_long); ++j) {
			((u_long *)&tcp_curr)[j] +=
				((u_long *)&tcp_array[i])[j];
		}
	}
}

void
labelnetbw(void)
{
	wmove(wnd, 0, 0);
	wclrtobot(wnd);
#if 0
	mvwaddstr(wnd, 0, LADDR, "Local Address");
	mvwaddstr(wnd, 0, FADDR, "Foreign Address");
	mvwaddstr(wnd, 0, PROTO, "Proto");
	mvwaddstr(wnd, 0, RCVCC, "Recv-Q");
	mvwaddstr(wnd, 0, SNDCC, "Send-Q");
	mvwaddstr(wnd, 0, STATE, "(state)");
#endif
}

void
shownetbw(void)
{
	double delta_time;
	struct mytcpcb *elm;
	struct mytcpcb *delm;
	int row;

	delta_time = (double)(tv_curr.tv_sec - tv_last.tv_sec) - 1.0 +
		     (tv_curr.tv_usec + 1000000 - tv_last.tv_usec) / 1e6;
	if (delta_time < 0.1)
		return;

	mvwprintw(wnd, 0, 0,
		  "tcp accepts %s connects %s "
		  "         recv %s send %s",
		  numtok(DELTARATE(tcps_accepts)),
		  numtok(DELTARATE(tcps_connects) - DELTARATE(tcps_accepts)),
		  numtok(DELTARATE(tcps_rcvbyte)),
		  numtok(DELTARATE(tcps_sndbyte)));

	row = 2;
	delm = NULL;
	RB_FOREACH(elm, mytcpcb_tree, &mytcp_tree) {
		if (delm) {
			RB_REMOVE(mytcpcb_tree, &mytcp_tree, delm);
			free(delm);
			delm = NULL;
		}
		if (elm->seq == tcp_pcb_seq &&
		    (elm->xtcp.xt_socket.so_rcv.sb_cc ||
		     elm->xtcp.xt_socket.so_snd.sb_cc ||
		     DELTAELM(xt_tp.snd_max) ||
		     DELTAELM(xt_tp.rcv_nxt)
		    )) {
			mvwprintw(wnd, row, 0,
				  "%s %s "
				  /*"rxb %s txb %s "*/
				  "recv %s send %s "
				  "[%c%c%c%c%c%c%c]",
				  netaddrstr(
				    elm->xtcp.xt_inp.inp_vflag,
				    &elm->xtcp.xt_inp.inp_inc.inc_ie.
					ie_dependladdr,
				    ntohs(elm->xtcp.xt_inp.inp_inc.inc_ie.ie_lport)),
				  netaddrstr(
				    elm->xtcp.xt_inp.inp_vflag,
				    &elm->xtcp.xt_inp.inp_inc.inc_ie.
					ie_dependfaddr,
				    ntohs(elm->xtcp.xt_inp.inp_inc.inc_ie.ie_fport)),
				/*
				  numtok(elm->xtcp.xt_socket.so_rcv.sb_cc),
				  numtok(elm->xtcp.xt_socket.so_snd.sb_cc),
				*/
				  numtok(DELTAELM(xt_tp.rcv_nxt)),
				  numtok(DELTAELM(xt_tp.snd_max)),
				  (elm->xtcp.xt_socket.so_rcv.sb_cc > 15000 ?
				   'R' : ' '),
				  (elm->xtcp.xt_socket.so_snd.sb_cc > 15000 ?
				   'T' : ' '),
				  ((elm->xtcp.xt_tp.t_flags & TF_NODELAY) ?
				   'N' : ' '),
				  ((elm->xtcp.xt_tp.t_flags & TF_RCVD_TSTMP) ?
				   'T' : ' '),
				  ((elm->xtcp.xt_tp.t_flags &
				   TF_SACK_PERMITTED) ?
				   'S' : ' '),
				  ((elm->xtcp.xt_tp.t_flags & TF_RCVD_SCALE) ?
				   'X' : ' '),
				  ((elm->xtcp.xt_tp.t_flags & TF_FASTRECOVERY) ?
				   'F' : ' ')
			);
			if (++row >= LINES-3)
				break;
		} else if (elm->seq != tcp_pcb_seq) {
			delm = elm;
		}
	}
	if (delm) {
		RB_REMOVE(mytcpcb_tree, &mytcp_tree, delm);
		free(delm);
		delm = NULL;
	}
	wmove(wnd, row, 0);
	wclrtobot(wnd);
	mvwprintw(wnd, LINES-2, 0,
		  "Rate/sec, "
		  "R=rxpend T=txpend N=nodelay T=tstmp "
		  "S=sack X=winscale F=fastrec");
}

#if 0
int
cmdnetbw(const char *cmd __unused, char *args __unused)
{
	fetchnetbw();
	shownetbw();
	refresh();

	return (0);
}
#endif

#define MAXINDEXES 8

static
const char *
numtok(double value)
{
	static char buf[MAXINDEXES][32];
	static int nexti;
	static const char *suffixes[] = { " ", "K", "M", "G", "T", NULL };
	int suffix = 0;
	const char *fmt;

	while (value >= 1000.0 && suffixes[suffix+1]) {
		value /= 1000.0;
		++suffix;
	}
	nexti = (nexti + 1) % MAXINDEXES;
	if (value < 0.001) {
		fmt = "      ";
	} else if (value < 1.0) {
		fmt = "%5.3f%s";
	} else if (value < 10.0) {
		fmt = "%5.3f%s";
	} else if (value < 100.0) {
		fmt = "%5.2f%s";
	} else {
		fmt = "%5.1f%s";
	}
	snprintf(buf[nexti], sizeof(buf[nexti]),
		 fmt, value, suffixes[suffix]);
	return (buf[nexti]);
}

const char *
netaddrstr(u_char vflags, union in_dependaddr *depaddr, u_int16_t port)
{
	static char buf[MAXINDEXES][64];
	static int nexta;
	char bufip[64];

	nexta = (nexta + 1) % MAXINDEXES;

	if (vflags & INP_IPV4) {
		snprintf(bufip, sizeof(bufip),
			 "%d.%d.%d.%d",
			 (ntohl(depaddr->id46_addr.ia46_addr4.s_addr) >> 24) &
			  255,
			 (ntohl(depaddr->id46_addr.ia46_addr4.s_addr) >> 16) &
			  255,
			 (ntohl(depaddr->id46_addr.ia46_addr4.s_addr) >> 8) &
			  255,
			 (ntohl(depaddr->id46_addr.ia46_addr4.s_addr) >> 0) &
			  255);
		snprintf(buf[nexta], sizeof(buf[nexta]),
			 "%15s:%-5d", bufip, port);
	} else if (vflags & INP_IPV6) {
		snprintf(buf[nexta], sizeof(buf[nexta]),
			 "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x",
			 ntohs(depaddr->id6_addr.s6_addr16[0]),
			 ntohs(depaddr->id6_addr.s6_addr16[1]),
			 ntohs(depaddr->id6_addr.s6_addr16[2]),
			 ntohs(depaddr->id6_addr.s6_addr16[3]),
			 ntohs(depaddr->id6_addr.s6_addr16[4]),
			 ntohs(depaddr->id6_addr.s6_addr16[5]),
			 ntohs(depaddr->id6_addr.s6_addr16[6]),
			 ntohs(depaddr->id6_addr.s6_addr16[7]));
		snprintf(buf[nexta], sizeof(buf[nexta]),
			 "%39s:%-5d", bufip, port);
	} else {
		snprintf(bufip, sizeof(bufip), "<unknown>:%-5d", port);
	}
	return (buf[nexta]);
}

static
void
updatepcb(struct xtcpcb *xtcp)
{
	struct mytcpcb dummy;
	struct mytcpcb *elm;

	dummy.xtcp = *xtcp;
	if ((elm = RB_FIND(mytcpcb_tree, &mytcp_tree, &dummy)) == NULL) {
		elm = malloc(sizeof(*elm));
		bzero(elm, sizeof(*elm));
		elm->xtcp = *xtcp;
		elm->last_xtcp = *xtcp;
		RB_INSERT(mytcpcb_tree, &mytcp_tree, elm);
	} else {
		elm->last_xtcp = elm->xtcp;
		elm->xtcp = *xtcp;
	}
	elm->seq = tcp_pcb_seq;
}
