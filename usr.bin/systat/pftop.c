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
#include <sys/endian.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/route.h>
#include <net/if.h>
#include <net/pf/pfvar.h>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <nlist.h>
#include <paths.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>

#include "systat.h"
#include "extern.h"

struct mypfstate {
	RB_ENTRY(mypfstate)	rb_node;
	int			seq;
	struct pfsync_state	state;
	struct pfsync_state	last_state;
};

static int
mypfstate_cmp(struct mypfstate *pf1, struct mypfstate *pf2)
{
	struct pfsync_state_key *nk1, *nk2;
	int r;

	if (pf1->state.proto < pf2->state.proto)
		return(-1);
	if (pf1->state.proto > pf2->state.proto)
		return(1);

	if (pf1->state.direction == PF_OUT) {
		nk1 = &pf1->state.key[PF_SK_WIRE];
	} else {
		nk1 = &pf1->state.key[PF_SK_STACK];
	}
	if (pf2->state.direction == PF_OUT) {
		nk2 = &pf2->state.key[PF_SK_WIRE];
	} else {
		nk2 = &pf2->state.key[PF_SK_STACK];
	}
	if (pf1->state.proto == IPPROTO_TCP || pf1->state.proto == IPPROTO_UDP) {
		if (ntohs(nk1->port[0]) >= 1024 &&
		    ntohs(nk2->port[0]) >= 1024) {
			if (ntohs(nk1->port[1]) < ntohs(nk2->port[1]))
				return(-1);
			if (ntohs(nk1->port[1]) > ntohs(nk2->port[1]))
				return(1);
		}
		if (ntohs(nk1->port[0]) < ntohs(nk2->port[0]))
			return(-1);
		if (ntohs(nk1->port[0]) > ntohs(nk2->port[0]))
			return(1);
		if (ntohs(nk1->port[1]) < ntohs(nk2->port[1]))
			return(-1);
		if (ntohs(nk1->port[1]) > ntohs(nk2->port[1]))
			return(1);
	}

	/*
	 * Sort IPV4 vs IPV6 addresses
	 */
	if (pf1->state.af < pf2->state.af)
		return(-1);
	if (pf1->state.af > pf2->state.af)
		return(1);

	/*
	 * Local and foreign addresses
	 */
	if (pf1->state.af == AF_INET) {
		if (ntohl(nk1->addr[0].v4.s_addr) <
		    ntohl(nk2->addr[0].v4.s_addr))
			return(-1);
		if (ntohl(nk1->addr[0].v4.s_addr) >
		    ntohl(nk2->addr[0].v4.s_addr))
			return(1);
		if (ntohl(nk1->addr[1].v4.s_addr) <
		    ntohl(nk2->addr[1].v4.s_addr))
			return(-1);
		if (ntohl(nk1->addr[1].v4.s_addr) >
		    ntohl(nk2->addr[1].v4.s_addr))
			return(1);
	} else if (pf1->state.af == AF_INET6) {
		r = bcmp(&nk1->addr[0].v6,
			 &nk2->addr[0].v6,
			 sizeof(nk1->addr[0].v6));
		if (r)
			return(r);
	} else {
		r = bcmp(&nk1->addr[0].v6,
			 &nk2->addr[0].v6,
			 sizeof(nk1->addr[0].v6));
		if (r)
			return(r);
	}
	return(0);
}

struct mypfstate_tree;
RB_HEAD(mypfstate_tree, mypfstate);
RB_PROTOTYPE(mypfstate_tree, mypfstate, rb_node, mypfstate_cmp);
RB_GENERATE(mypfstate_tree, mypfstate, rb_node, mypfstate_cmp);

static struct mypfstate_tree mypf_tree;
static struct timeval tv_curr;
static struct timeval tv_last;
static int tcp_pcb_seq;

static const char *numtok(double value);
static const char *netaddrstr(sa_family_t af, struct pf_addr *addr,
			u_int16_t port);
static void updatestate(struct pfsync_state *state);
static int statebwcmp(const void *data1, const void *data2);

#define DELTARATE(field)	\
	((double)(be64toh(*(uint64_t *)elm->state.field) - \
		  be64toh(*(uint64_t *)elm->last_state.field)) / delta_time)

WINDOW *
openpftop(void)
{
	RB_INIT(&mypf_tree);
	return (subwin(stdscr, LINES-0-1, 0, 0, 0));
}

void
closepftop(WINDOW *w)
{
	struct mypfstate *mypf;

	while ((mypf = RB_ROOT(&mypf_tree)) != NULL) {
		RB_REMOVE(mypfstate_tree, &mypf_tree, mypf);
		free(mypf);
	}

        if (w != NULL) {
		wclear(w);
		wrefresh(w);
		delwin(w);
	}
}

int
initpftop(void)
{
	return(1);
}

void
fetchpftop(void)
{
	struct pfioc_states ps;
	struct pfsync_state *states;
	size_t nstates;
	size_t i;
	int fd;

	fd = open("/dev/pf", O_RDONLY);
	if (fd < 0)
		return;

	/*
	 * Extract PCB list
	 */
	bzero(&ps, sizeof(ps));
	if (ioctl(fd, DIOCGETSTATES, &ps) < 0) {
		close(fd);
		return;
	}
	ps.ps_len += 1024 * 1024;
	ps.ps_buf = malloc(ps.ps_len);
	if (ioctl(fd, DIOCGETSTATES, &ps) < 0) {
		free(ps.ps_buf);
		close(fd);
		return;
	}

	states = (void *)ps.ps_buf;
	nstates = ps.ps_len / sizeof(*states);

	++tcp_pcb_seq;

	for (i = 0; i < nstates; ++i)
		updatestate(&states[i]);
	free(ps.ps_buf);
	close(fd);
	states = NULL;
	fd = -1;

	tv_last = tv_curr;
	gettimeofday(&tv_curr, NULL);
}

void
labelpftop(void)
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
showpftop(void)
{
	double delta_time;
	struct mypfstate *elm;
	struct mypfstate *delm;
	struct mypfstate **array;
	size_t i;
	size_t n;
	struct pfsync_state_key *nk;
	int row;

	delta_time = (double)(tv_curr.tv_sec - tv_last.tv_sec) - 1.0 +
		     (tv_curr.tv_usec + 1000000 - tv_last.tv_usec) / 1e6;
	if (delta_time < 0.1)
		return;

	/*
	 * Delete and collect pass
	 */
	delm = NULL;
	i = 0;
	n = 1024;
	array = malloc(n * sizeof(*array));
	RB_FOREACH(elm, mypfstate_tree, &mypf_tree) {
		if (delm) {
			RB_REMOVE(mypfstate_tree, &mypf_tree, delm);
			free(delm);
			delm = NULL;
		}
		if (elm->seq == tcp_pcb_seq &&
		    (DELTARATE(bytes[0]) ||
		     DELTARATE(bytes[1]))
		) {
			array[i++] = elm;
			if (i == n) {
				n *= 2;
				array = realloc(array, n * sizeof(*array));
			}
		} else if (elm->seq != tcp_pcb_seq) {
			delm = elm;
		}
	}
	if (delm) {
		RB_REMOVE(mypfstate_tree, &mypf_tree, delm);
		free(delm);
		delm = NULL;
	}
	qsort(array, i, sizeof(array[0]), statebwcmp);

	row = 2;
	n = i;
	for (i = 0; i < n; ++i) {
		elm = array[i];
		if (elm->state.direction == PF_OUT) {
			nk = &elm->state.key[PF_SK_WIRE];
		} else {
			nk = &elm->state.key[PF_SK_STACK];
		}
		mvwprintw(wnd, row, 0,
			  "%s %s "
			  /*"rxb %s txb %s "*/
			  "rcv %s snd %s ",
			  netaddrstr(elm->state.af, &nk->addr[0], nk->port[0]),
			  netaddrstr(elm->state.af, &nk->addr[1], nk->port[1]),
			  numtok(DELTARATE(bytes[0])),
			  numtok(DELTARATE(bytes[1]))
		);
		if (++row >= LINES-3)
			break;
	}
	free(array);
	wmove(wnd, row, 0);
	wclrtobot(wnd);
	mvwprintw(wnd, LINES-2, 0, "Rate bytes/sec, active pf states");
}

/*
 * Sort by total bytes transfered, highest first
 */
static
int
statebwcmp(const void *data1, const void *data2)
{
	const struct mypfstate *elm1 = *__DECONST(struct mypfstate **, data1);
	const struct mypfstate *elm2 = *__DECONST(struct mypfstate **, data2);
	uint64_t v1;
	uint64_t v2;

	v1 = be64toh(*(const uint64_t *)elm1->state.bytes[0]) +
	     be64toh(*(const uint64_t *)elm1->state.bytes[1]);
	v1 -= be64toh(*(const uint64_t *)elm1->last_state.bytes[0]) +
	     be64toh(*(const uint64_t *)elm1->last_state.bytes[1]);
	v2 = be64toh(*(const uint64_t *)elm2->state.bytes[0]) +
	     be64toh(*(const uint64_t *)elm2->state.bytes[1]);
	v2 -= be64toh(*(const uint64_t *)elm2->last_state.bytes[0]) +
	     be64toh(*(const uint64_t *)elm2->last_state.bytes[1]);
	if (v1 < v2)
		return(1);
	if (v1 > v2)
		return(-1);
	return(0);
}

#if 0
int
cmdpftop(const char *cmd __unused, char *args __unused)
{
	fetchpftop();
	showpftop();
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
	} else if (value < 1000.0) {
		fmt = "%5.1f%s";
	} else {
		fmt = "<huge>";
	}
	snprintf(buf[nexti], sizeof(buf[nexti]),
		 fmt, value, suffixes[suffix]);
	return (buf[nexti]);
}

static const char *
netaddrstr(sa_family_t af, struct pf_addr *addr, u_int16_t port)
{
	static char buf[MAXINDEXES][64];
	static int nexta;
	char bufip[64];

	nexta = (nexta + 1) % MAXINDEXES;

	port = ntohs(port);

	if (af == AF_INET) {
		snprintf(bufip, sizeof(bufip),
			 "%d.%d.%d.%d",
			 (ntohl(addr->v4.s_addr) >> 24) & 255,
			 (ntohl(addr->v4.s_addr) >> 16) & 255,
			 (ntohl(addr->v4.s_addr) >> 8) & 255,
			 (ntohl(addr->v4.s_addr) >> 0) & 255);
		snprintf(buf[nexta], sizeof(buf[nexta]),
			 "%15s:%-5d", bufip, port);
	} else if (af == AF_INET6) {
		snprintf(bufip, sizeof(bufip),
			 "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x",
			 ntohs(addr->v6.s6_addr16[0]),
			 ntohs(addr->v6.s6_addr16[1]),
			 ntohs(addr->v6.s6_addr16[2]),
			 ntohs(addr->v6.s6_addr16[3]),
			 ntohs(addr->v6.s6_addr16[4]),
			 ntohs(addr->v6.s6_addr16[5]),
			 ntohs(addr->v6.s6_addr16[6]),
			 ntohs(addr->v6.s6_addr16[7]));
		snprintf(buf[nexta], sizeof(buf[nexta]),
			 "%39s:%-5d", bufip, port);
	} else {
		snprintf(bufip, sizeof(bufip), "<unknown>:%-5d", port);
		snprintf(buf[nexta], sizeof(buf[nexta]),
			 "%15s:%-5d", bufip, port);
	}
	return (buf[nexta]);
}

static
void
updatestate(struct pfsync_state *state)
{
	struct mypfstate dummy;
	struct mypfstate *elm;

	dummy.state = *state;
	if ((elm = RB_FIND(mypfstate_tree, &mypf_tree, &dummy)) == NULL) {
		elm = malloc(sizeof(*elm));
		bzero(elm, sizeof(*elm));
		elm->state = *state;
		elm->last_state = *state;
		bzero(elm->last_state.bytes,
			sizeof(elm->last_state.bytes));
		bzero(elm->last_state.packets,
			sizeof(elm->last_state.packets));
		RB_INSERT(mypfstate_tree, &mypf_tree, elm);
	} else {
		elm->last_state = elm->state;
		elm->state = *state;
	}
	elm->seq = tcp_pcb_seq;
}
