/*
 * Copyright (c) 1983, 1988, 1993
 *	Regents of the University of California.  All rights reserved.
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
 * @(#) Copyright (c) 1983, 1988, 1993 Regents of the University of California.  All rights reserved.
 * @(#)main.c	8.4 (Berkeley) 3/1/94
 * $FreeBSD: src/usr.bin/netstat/main.c,v 1.34.2.12 2001/09/17 15:17:46 ru Exp $
 */

#include <sys/param.h>
#include <sys/file.h>
#include <sys/protosw.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <netgraph/socket/ng_socket.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <kvm.h>
#include <limits.h>
#include <netdb.h>
#include <nlist.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "netstat.h"

static struct nlist nl[] = {
#define	N_IFNET		0
	{ .n_name = "_ifnet" },
#define	N_IMP		1
	{ .n_name = "_imp_softc" },
#define	N_RTSTAT	2
	{ .n_name = "_rtstat" },
#define	N_UNIXSW	3
	{ .n_name = "_localsw" },
#define N_IDP		4
	{ .n_name = "_nspcb"},
#define N_IDPSTAT	5
	{ .n_name = "_idpstat"},
#define N_SPPSTAT	6
	{ .n_name = "_spp_istat"},
#define N_NSERR		7
	{ .n_name = "_ns_errstat"},
#define	N_CLNPSTAT	8
	{ .n_name = "_clnp_stat"},
#define	IN_NOTUSED	9
	{ .n_name = "_tp_inpcb" },
#define	ISO_TP		10
	{ .n_name = "_tp_refinfo" },
#define	N_TPSTAT	11
	{ .n_name = "_tp_stat" },
#define	N_ESISSTAT	12
	{ .n_name = "_esis_stat"},
#define N_NIMP		13
	{ .n_name = "_nimp"},
#define N_RTREE		14
	{ .n_name = "_rt_tables"},
#define N_CLTP		15
	{ .n_name = "_cltb"},
#define N_CLTPSTAT	16
	{ .n_name = "_cltpstat"},
#define	N_NFILE		17
	{ .n_name = "_nfile" },
#define	N_FILE		18
	{ .n_name = "_file" },
#define N_MRTSTAT	19
	{ .n_name = "_mrtstat" },
#define N_MFCTABLE	20
	{ .n_name = "_mfctable" },
#define N_VIFTABLE	21
	{ .n_name = "_viftable" },
#define N_NGSOCKS	22
	{ .n_name = "_ngsocklist"},
#define N_IP6STAT	23
	{ .n_name = "_ip6stat" },
#define N_ICMP6STAT	24
	{ .n_name = "_icmp6stat" },
#define N_IPSECSTAT	25
	{ .n_name = "_ipsecstat" },
#define N_IPSEC6STAT	26
	{ .n_name = "_ipsec6stat" },
#define N_PIM6STAT	27
	{ .n_name = "_pim6stat" },
#define N_MRT6PROTO	28
	{ .n_name = "_ip6_mrtproto" },
#define N_MRT6STAT	29
	{ .n_name = "_mrt6stat" },
#define N_MF6CTABLE	30
	{ .n_name = "_mf6ctable" },
#define N_MIF6TABLE	31
	{ .n_name = "_mif6table" },
#define N_PFKEYSTAT	32
	{ .n_name = "_pfkeystat" },
#define N_MBSTAT	33
	{ .n_name = "_mbstat" },
#define N_MBTYPES	34
	{ .n_name = "_mbtypes" },
#define N_NMBCLUSTERS	35
	{ .n_name = "_nmbclusters" },
#define N_NMBUFS	36
	{ .n_name = "_nmbufs" },
#define	N_RTTRASH	37
	{ .n_name = "_rttrash" },
#define	N_NCPUS		38
	{ .n_name = "_ncpus" },
#define	N_CARPSTAT	39
	{ .n_name = "_carpstats" },
#define N_NMBJCLUSTERS	40
	{ .n_name = "_nmbjclusters" },
	{ .n_name = NULL },
};

struct protox {
	u_char	pr_index;		/* index into nlist of cb head */
	u_char	pr_sindex;		/* index into nlist of stat block */
	u_char	pr_wanted;		/* 1 if wanted, 0 otherwise */
	void	(*pr_cblocks)(u_long, const char *, int);
					/* control blocks printing routine */
	void	(*pr_stats)(u_long, const char *, int);
					/* statistics printing routine */
	void	(*pr_istats)(char *);	/* per/if statistics printing routine */
	const char *pr_name;		/* well-known name */
	u_int	pr_usesysctl;		/* true if we use sysctl, not kvm */
} protox[] = {
	{ -1,		-1,		1,	protopr,
	  tcp_stats,	NULL,		"tcp",	IPPROTO_TCP },
	{ -1,		-1,		1,	protopr,
	  udp_stats,	NULL,		"udp",	IPPROTO_UDP },
	{ -1,		-1,		1,	protopr,
	  NULL,		NULL,		"divert",IPPROTO_DIVERT },
	{ -1,		-1,		1,	protopr,
	  ip_stats,	NULL,		"ip",	IPPROTO_RAW },
	{ -1,		-1,		1,	protopr,
	  icmp_stats,	NULL,		"icmp",	IPPROTO_ICMP },
	{ -1,		-1,		1,	protopr,
	  igmp_stats,	NULL,		"igmp",	IPPROTO_IGMP },
#ifdef IPSEC
	{ -1,		N_IPSECSTAT,	1,	0,
	  ipsec_stats,	NULL,		"ipsec",	0},
#endif
        { -1,           N_CARPSTAT,     1,      0,
          carp_stats,   NULL,           "carp",         0},
	{ -1,		-1,		0,	0,
	  0,		NULL,		NULL,		0}
};

#ifdef INET6
struct protox ip6protox[] = {
	{ -1,		-1,		1,	protopr,
	  tcp_stats,	NULL,		"tcp",	IPPROTO_TCP },
	{ -1,		-1,		1,	protopr,
	  udp_stats,	NULL,		"udp",	IPPROTO_UDP },
	{ -1,		N_IP6STAT,	1,	protopr,
	  ip6_stats,	ip6_ifstats,	"ip6",	IPPROTO_RAW },
	{ -1,		N_ICMP6STAT,	1,	protopr,
	  icmp6_stats,	icmp6_ifstats,	"icmp6",IPPROTO_ICMPV6 },
#ifdef IPSEC
	{ -1,		N_IPSEC6STAT,	1,	0,
	  ipsec_stats,	NULL,		"ipsec6",0 },
#endif
#ifdef notyet
	{ -1,		N_PIM6STAT,	1,	0,
	  pim6_stats,	NULL,		"pim6",	0 },
#endif
	{ -1,		-1,		1,	0,
	  rip6_stats,	NULL,		"rip6",	0 },
	{ -1,		-1,		1,	protopr,
	  pim_stats,	NULL,		"pim",	IPPROTO_PIM },
	{ -1,		-1,		0,	0,
	  0,		NULL,		0,	0 }
};
#endif /*INET6*/

#ifdef IPSEC
struct protox pfkeyprotox[] = {
	{ -1,		N_PFKEYSTAT,	1,	0,
	  pfkey_stats,	NULL,		"pfkey", 0 },
	{ -1,		-1,		0,	0,
	  0,		NULL,		0,	0 }
};
#endif

struct protox netgraphprotox[] = {
	{ N_NGSOCKS,	-1,		1,	netgraphprotopr,
	  NULL,		NULL,		"ctrl",	0 },
	{ N_NGSOCKS,	-1,		1,	netgraphprotopr,
	  NULL,		NULL,		"data",	0 },
	{ -1,		-1,		0,	0,
	  0,		NULL,		NULL,	0 }
};

#ifdef ISO
struct protox isoprotox[] = {
	{ ISO_TP,	N_TPSTAT,	1,	iso_protopr,
	  tp_stats,	NULL,		"tp" },
	{ N_CLTP,	N_CLTPSTAT,	1,	iso_protopr,
	  cltp_stats,	NULL,		"cltp" },
	{ -1,		N_CLNPSTAT,	1,	 0,
	  clnp_stats,	NULL,		"clnp"},
	{ -1,		N_ESISSTAT,	1,	 0,
	  esis_stats,	NULL,		"esis"},
	{ -1,		-1,		0,	0,
	  0,		NULL,		0 }
};
#endif

struct protox *protoprotox[] = {
					 protox,
#ifdef INET6
					 ip6protox,
#endif
#ifdef IPSEC
					 pfkeyprotox,
#endif
#ifdef ISO
					 isoprotox, 
#endif
					 NULL };

static void printproto (struct protox *, const char *, u_long);
static void usage (void);
static struct protox *name2protox (char *);
static struct protox *knownname (char *);

static kvm_t *kvmd;
static char *nlistf = NULL, *memf = NULL;

int	Aflag;		/* show addresses of protocol control block */
int	aflag;		/* show all sockets (including servers) */
int	bflag;		/* show i/f total bytes in/out */
int	cpuflag = -1;	/* dump route table from specific cpu */
int	dflag;		/* show i/f dropped packets */
int	gflag;		/* show group (multicast) routing or stats */
int	hflag;		/* show counters in human readable format */
int	iflag;		/* show interfaces */
int	Lflag;		/* show size of listen queues */
int	mflag;		/* show memory stats */
int	Pflag;		/* show more protocol info (go past 80 columns) */
int	numeric_addr;	/* show addresses numerically */
int	numeric_port;	/* show ports numerically */
static int pflag;	/* show given protocol */
int	rflag;		/* show routing tables (or routing stats) */
int	sflag;		/* show protocol statistics */
int	tflag;		/* show i/f watchdog timers */
int	Bflag;		/* show buffer limit instead of buffer use */
int	Wflag;		/* wide display */
int	zflag;		/* zero stats */

int	interval;	/* repeat interval for i/f stats */

char	*interface;	/* desired i/f for stats, or NULL for all i/fs */
int	unit;		/* unit number for above */

int	af;		/* address family */

int
main(int argc, char **argv)
{
	struct protox *tp = NULL;  /* for printing cblocks & stats */
	int ch;
	int n;

	af = AF_UNSPEC;

	while ((ch = getopt(argc, argv, "Aabc:df:ghI:iLlM:mN:nPp:rSsBtuWw:z")) != -1)
		switch(ch) {
		case 'A':
			Aflag = 1;
			break;
		case 'a':
			aflag = 1;
			break;
		case 'b':
			bflag = 1;
			break;
		case 'c':
			kread(0, 0, 0);
			kread(nl[N_NCPUS].n_value, (char *)&n, sizeof(n));
			cpuflag = strtol(optarg, NULL, 0);
			if (cpuflag < 0 || cpuflag >= n)
			    errx(1, "cpu %d does not exist", cpuflag);
			break;
		case 'd':
			dflag = 1;
			break;
		case 'f':
			if (strcmp(optarg, "inet") == 0)
				af = AF_INET;
#ifdef INET6
			else if (strcmp(optarg, "inet6") == 0)
				af = AF_INET6;
#endif /*INET6*/
#ifdef INET6
			else if (strcmp(optarg, "pfkey") == 0)
				af = PF_KEY;
#endif /*INET6*/
			else if (strcmp(optarg, "unix") == 0)
				af = AF_UNIX;
			else if (strcmp(optarg, "ng") == 0
			    || strcmp(optarg, "netgraph") == 0)
				af = AF_NETGRAPH;
#ifdef ISO
			else if (strcmp(optarg, "iso") == 0)
				af = AF_ISO;
#endif
			else if (strcmp(optarg, "link") == 0)
				af = AF_LINK;
			else if (strcmp(optarg, "mpls") == 0)
				af = AF_MPLS;
			else {
				errx(1, "%s: unknown address family", optarg);
			}
			break;
		case 'g':
			gflag = 1;
			break;
		case 'h':
			hflag = 1;
			break;
		case 'I': {
			char *cp;

			iflag = 1;
			for (cp = interface = optarg; isalpha(*cp); cp++)
				continue;
			unit = atoi(cp);
			break;
		}
		case 'i':
			iflag = 1;
			break;
		case 'L':
			Lflag = 1;
			break;
		case 'M':
			memf = optarg;
			break;
		case 'm':
			mflag = 1;
			break;
		case 'N':
			nlistf = optarg;
			break;
		case 'n':
			numeric_addr = numeric_port = 1;
			break;
		case 'P':
			Pflag = 1;
			break;
		case 'p':
			if ((tp = name2protox(optarg)) == NULL) {
				errx(1, 
				     "%s: unknown or uninstrumented protocol",
				     optarg);
			}
			pflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 's':
			++sflag;
			break;
		case 'S':
			numeric_addr = 1;
			break;
		case 'B':
			Bflag = 1;
			break;
		case 't':
			tflag = 1;
			break;
		case 'u':
			af = AF_UNIX;
			break;
		case 'W':
		case 'l':
			Wflag = 1;
			break;
		case 'w':
			interval = atoi(optarg);
			iflag = 1;
			break;
		case 'z':
			zflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	argv += optind;
	argc -= optind;

#define	BACKWARD_COMPATIBILITY
#ifdef	BACKWARD_COMPATIBILITY
	if (*argv) {
		if (isdigit(**argv)) {
			interval = atoi(*argv);
			if (interval <= 0)
				usage();
			++argv;
			iflag = 1;
		}
		if (*argv) {
			nlistf = *argv;
			if (*++argv)
				memf = *argv;
		}
	}
#endif

	/*
	 * Discard setgid privileges if not the running kernel so that bad
	 * guys can't print interesting stuff from kernel memory.
	 */
	if (nlistf != NULL || memf != NULL)
		setgid(getgid());

	if (mflag) {
		if (memf != NULL) {
			if (kread(0, 0, 0) == 0)
				mbpr(nl[N_MBSTAT].n_value,
				    nl[N_MBTYPES].n_value,
				    nl[N_NMBCLUSTERS].n_value,
				    nl[N_NMBJCLUSTERS].n_value,
				    nl[N_NMBUFS].n_value,
				    nl[N_NCPUS].n_value);
		} else {
			mbpr(0, 0, 0, 0, 0, 0);
		}
		exit(0);
	}
#if 0
	/*
	 * Keep file descriptors open to avoid overhead
	 * of open/close on each call to get* routines.
	 */
	sethostent(1);
	setnetent(1);
#else
	/*
	 * This does not make sense any more with DNS being default over
	 * the files.  Doing a setXXXXent(1) causes a tcp connection to be
	 * used for the queries, which is slower.
	 */
#endif
	if (iflag && !sflag) {
		kread(0, 0, 0);
		intpr(interval, nl[N_IFNET].n_value, NULL, nl[N_NCPUS].n_value);
		exit(0);
	}
	if (rflag) {
		kread(0, 0, 0);
		if (sflag)
			rt_stats();
		else
			routepr(nl[N_RTREE].n_value);
		exit(0);
	}
	if (gflag) {
		kread(0, 0, 0);
		if (sflag) {
			if (af == AF_INET || af == AF_UNSPEC)
				mrt_stats(nl[N_MRTSTAT].n_value);
#ifdef INET6
			if (af == AF_INET6 || af == AF_UNSPEC)
				mrt6_stats(nl[N_MRT6STAT].n_value);
#endif
		} else {
			if (af == AF_INET || af == AF_UNSPEC)
				mroutepr(nl[N_MFCTABLE].n_value,
					 nl[N_VIFTABLE].n_value);
#ifdef INET6
			if (af == AF_INET6 || af == AF_UNSPEC)
				mroute6pr(nl[N_MF6CTABLE].n_value,
					  nl[N_MIF6TABLE].n_value);
#endif
		}
		exit(0);
	}

	kread(0, 0, 0);
	if (tp) {
		printproto(tp, tp->pr_name, nl[N_NCPUS].n_value);
		exit(0);
	}
	if (af == AF_INET || af == AF_UNSPEC)
		for (tp = protox; tp->pr_name; tp++)
			printproto(tp, tp->pr_name, nl[N_NCPUS].n_value);
#ifdef INET6
	if (af == AF_INET6 || af == AF_UNSPEC)
		for (tp = ip6protox; tp->pr_name; tp++)
			printproto(tp, tp->pr_name, nl[N_NCPUS].n_value);
#endif /*INET6*/
#ifdef IPSEC
	if (af == PF_KEY || af == AF_UNSPEC)
		for (tp = pfkeyprotox; tp->pr_name; tp++)
			printproto(tp, tp->pr_name, nl[N_NCPUS].n_value);
#endif /*IPSEC*/
	if (af == AF_NETGRAPH || af == AF_UNSPEC)
		for (tp = netgraphprotox; tp->pr_name; tp++)
			printproto(tp, tp->pr_name, nl[N_NCPUS].n_value);
#ifdef ISO
	if (af == AF_ISO || af == AF_UNSPEC)
		for (tp = isoprotox; tp->pr_name; tp++)
			printproto(tp, tp->pr_name, nl[N_NCPUS].n_value);
#endif
	if ((af == AF_UNIX || af == AF_UNSPEC) && !Lflag && !sflag)
		unixpr();
	exit(0);
}

/*
 * Print out protocol statistics or control blocks (per sflag).
 * If the interface was not specifically requested, and the symbol
 * is not in the namelist, ignore this one.
 */
static void
printproto(struct protox *tp, const char *name, u_long ncpusaddr)
{
	void (*pr)(u_long, const char *, int);
	u_long off;

	if (sflag) {
		if (iflag) {
			if (tp->pr_istats)
				intpr(interval, nl[N_IFNET].n_value,
				      tp->pr_istats, ncpusaddr);
			else if (pflag)
				printf("%s: no per-interface stats routine\n",
				    tp->pr_name);
			return;
		}
		else {
			pr = tp->pr_stats;
			if (!pr) {
				if (pflag)
					printf("%s: no stats routine\n",
					    tp->pr_name);
				return;
			}
			off = tp->pr_usesysctl ? tp->pr_usesysctl 
				: nl[tp->pr_sindex].n_value;
		}
	} else {
		pr = tp->pr_cblocks;
		if (!pr) {
			if (pflag)
				printf("%s: no PCB routine\n", tp->pr_name);
			return;
		}
		off = tp->pr_usesysctl ? tp->pr_usesysctl
			: nl[tp->pr_index].n_value;
	}
	if (pr != NULL && (off || af != AF_UNSPEC))
		(*pr)(off, name, af);
}

/*
 * Read kernel memory, return 0 on success.
 */
int
kread(u_long addr, char *buf, int size)
{
	if (kvmd == NULL) {
		/*
		 * XXX.
		 */
		kvmd = kvm_openfiles(nlistf, memf, NULL, O_RDONLY, buf);
		if (kvmd != NULL) {
			if (kvm_nlist(kvmd, nl) < 0) {
				if(nlistf)
					errx(1, "%s: kvm_nlist: %s", nlistf,
					     kvm_geterr(kvmd));
				else
					errx(1, "kvm_nlist: %s", kvm_geterr(kvmd));
			}

			if (nl[0].n_type == 0) {
				if(nlistf)
					errx(1, "%s: no namelist", nlistf);
				else
					errx(1, "no namelist");
			}
		} else {
			warnx("kvm not available");
			return(-1);
		}
	}
	if (!buf)
		return (0);
	if (kvm_read(kvmd, addr, buf, size) != size) {
		warnx("%s", kvm_geterr(kvmd));
		return (-1);
	}
	return (0);
}

const char *
plural(int n)
{
	return (n != 1 ? "s" : "");
}

const char *
plurales(int n)
{
	return (n != 1 ? "es" : "");
}

/*
 * Find the protox for the given "well-known" name.
 */
static struct protox *
knownname(char *name)
{
	struct protox **tpp, *tp;

	for (tpp = protoprotox; *tpp; tpp++)
		for (tp = *tpp; tp->pr_name; tp++)
			if (strcmp(tp->pr_name, name) == 0)
				return (tp);
	return (NULL);
}

/*
 * Find the protox corresponding to name.
 */
static struct protox *
name2protox(char *name)
{
	struct protox *tp;
	char **alias;			/* alias from p->aliases */
	struct protoent *p;

	/*
	 * Try to find the name in the list of "well-known" names. If that
	 * fails, check if name is an alias for an Internet protocol.
	 */
	if ((tp = knownname(name)) != NULL)
		return (tp);

	setprotoent(1);			/* make protocol lookup cheaper */
	while ((p = getprotoent()) != NULL) {
		/* assert: name not same as p->name */
		for (alias = p->p_aliases; *alias; alias++)
			if (strcmp(name, *alias) == 0) {
				endprotoent();
				return (knownname(p->p_name));
			}
	}
	endprotoent();
	return (NULL);
}

static void
usage(void)
{
	(void)fprintf(stderr, "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
"usage: netstat [-AaLnPSW] [-c cpu] [-f protocol_family | -p protocol]\n"
"               [-M core] [-N system]",
"       netstat -i | -I interface [-aBbdhnt] [-f address_family]\n"
"               [-M core] [-N system]",
"       netstat -w wait [-I interface] [-dh] [-M core] [-N system]",
"       netstat -s [-s] [-z] [-f protocol_family | -p protocol] [-M core]",
"       netstat -i | -I interface -s [-f protocol_family | -p protocol]\n"
"               [-M core] [-N system]",
"       netstat -m [-M core] [-N system]",
"       netstat -r [-AanW] [-f address_family] [-M core] [-N system]",
"       netstat -rs [-s] [-M core] [-N system]",
"       netstat -g [-W] [-f address_family] [-M core] [-N system]",
"       netstat -gs [-s] [-f address_family] [-M core] [-N system]");
	exit(1);
}
