/*
 * Copyright (c) 1983, 1988, 1993
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
 * @(#)route.c	8.6 (Berkeley) 4/28/95
 * $FreeBSD: src/usr.bin/netstat/route.c,v 1.41.2.14 2002/07/17 02:22:22 kbyanc Exp $
 */

#include <sys/kinfo.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netgraph/socket/ng_socket.h>

#include <netproto/mpls/mpls.h>

#include <sys/sysctl.h>

#include <arpa/inet.h>
#include <libutil.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <time.h>
#include <kinfo.h>
#include "netstat.h"

#define kget(p, d) (kread((u_long)(p), (char *)&(d), sizeof (d)))


/* alignment constraint for routing socket */
#define ROUNDUP(a) \
       ((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))
#define ADVANCE(x, n) (x += ROUNDUP((n)->sa_len))

/*
 * Definitions for showing gateway flags.
 */
struct bits {
	u_long	b_mask;
	char	b_val;
} bits[] = {
	{ RTF_UP,	'U' },
	{ RTF_GATEWAY,	'G' },
	{ RTF_HOST,	'H' },
	{ RTF_REJECT,	'R' },
	{ RTF_DYNAMIC,	'D' },
	{ RTF_MODIFIED,	'M' },
	{ RTF_DONE,	'd' }, /* Completed -- for routing messages only */
	{ RTF_CLONING,	'C' },
	{ RTF_XRESOLVE,	'X' },
	{ RTF_LLINFO,	'L' },
	{ RTF_STATIC,	'S' },
	{ RTF_PROTO1,	'1' },
	{ RTF_PROTO2,	'2' },
	{ RTF_WASCLONED,'W' },
	{ RTF_PRCLONING,'c' },
	{ RTF_PROTO3,	'3' },
	{ RTF_BLACKHOLE,'B' },
	{ RTF_BROADCAST,'b' },
	{ RTF_MPLSOPS,	'm' },
	{ 0, 0 }
};

typedef union {
	long	dummy;		/* Helps align structure. */
	struct	sockaddr u_sa;
	u_short	u_data[128];
} sa_u;

static sa_u pt_u;

int	do_rtent = 0;
struct	rtentry rtentry;
struct	radix_node rnode;
struct	radix_mask rmask;
struct	radix_node_head *rt_tables[AF_MAX+1];

int	NewTree = 0;

static struct sockaddr *kgetsa (struct sockaddr *);
static void size_cols (int ef, struct radix_node *rn);
static void size_cols_tree (struct radix_node *rn);
static void size_cols_rtentry (struct rtentry *rt);
static void p_tree (struct radix_node *);
static void p_rtnode (void);
static void ntreestuff (void);
static void np_rtentry (struct rt_msghdr *);
static void p_sockaddr (struct sockaddr *, struct sockaddr *, int, int);
static const char *fmt_sockaddr (struct sockaddr *sa, struct sockaddr *mask,
				 int flags);
static void p_flags (int, const char *);
static const char *fmt_flags(int f);
static void p_rtentry (struct rtentry *);
static u_long forgemask (u_long);
static void domask (char *, u_long, u_long);
static const char *labelops(struct rtentry *);

/*
 * Print routing tables.
 */
void
routepr(u_long rtree)
{
	struct radix_node_head *rnh, head;
	int i;

	printf("Routing tables\n");

	if (Aflag == 0 && NewTree) {
		ntreestuff();
	} else {
		if (rtree == 0) {
			printf("rt_tables: symbol not in namelist\n");
			return;
		}
		if (cpuflag >= 0) {
			/*
			 * Severe hack.
			 */
			rtree += cpuflag * (AF_MAX + 1) * sizeof(void *);
		}
		if (kget(rtree, rt_tables) != 0)
			return;
		for (i = 0; i <= AF_MAX; i++) {
			if ((rnh = rt_tables[i]) == NULL)
				continue;
			if (kget(rnh, head) != 0)
				continue;
			if (i == AF_UNSPEC) {
				if (Aflag && af == 0) {
					printf("Netmasks:\n");
					p_tree(head.rnh_treetop);
				}
			} else if (af == AF_UNSPEC || af == i) {
				size_cols(i, head.rnh_treetop);
				pr_family(i);
				do_rtent = 1;
				pr_rthdr(i);
				p_tree(head.rnh_treetop);
			}
		}
	}
}

/*
 * Print address family header before a section of the routing table.
 */
void
pr_family(int af1)
{
	const char *afname;

	switch (af1) {
	case AF_INET:
		afname = "Internet";
		break;
#ifdef INET6
	case AF_INET6:
		afname = "Internet6";
		break;
#endif /*INET6*/
	case AF_ISO:
		afname = "ISO";
		break;
	case AF_CCITT:
		afname = "X.25";
		break;
	case AF_NETGRAPH:
		afname = "Netgraph";
		break;
	case AF_MPLS:
		afname = "MPLS";
		break;
	default:
		afname = NULL;
		break;
	}
	if (afname)
		printf("\n%s:\n", afname);
	else
		printf("\nProtocol Family %d:\n", af1);
}

/* column widths; each followed by one space */
#ifndef INET6
#define	WID_DST_DEFAULT(af) 	18	/* width of destination column */
#define	WID_GW_DEFAULT(af)	18	/* width of gateway column */
#define	WID_IF_DEFAULT(af)	(Wflag ? 8 : 6)	/* width of netif column */
#else
#define	WID_DST_DEFAULT(af) \
	((af) == AF_INET6 ? (numeric_addr ? 33: 18) : 18)
#define	WID_GW_DEFAULT(af) \
	((af) == AF_INET6 ? (numeric_addr ? 29 : 18) : 18)
#define	WID_IF_DEFAULT(af)	((af) == AF_INET6 ? 8 : (Wflag ? 8 :6))
#endif /*INET6*/

static int wid_dst;
static int wid_gw;
static int wid_flags;
static int wid_refs;
static int wid_use;
static int wid_mtu;
static int wid_if;
static int wid_expire;
static int wid_mplslops;
static int wid_msl;
static int wid_iwmax;
static int wid_iw;

static void
size_cols(int ef, struct radix_node *rn)
{
	wid_dst = WID_DST_DEFAULT(ef);
	wid_gw = WID_GW_DEFAULT(ef);
	wid_flags = 6;
	wid_refs = 6;
	wid_use = 8;
	wid_mtu = 6;
	wid_if = WID_IF_DEFAULT(ef);
	wid_expire = 6;
	wid_mplslops = 7;
	wid_msl = 7;
	wid_iwmax = 5;
	wid_iw = 2;

	if (Wflag)
		size_cols_tree(rn);
}

static void
size_cols_tree(struct radix_node *rn)
{
again:
	if (kget(rn, rnode) != 0)
		return;
	if (!(rnode.rn_flags & RNF_ACTIVE))
		return;
	if (rnode.rn_bit < 0) {
		if ((rnode.rn_flags & RNF_ROOT) == 0) {
			if (kget(rn, rtentry) != 0)
				return;
			size_cols_rtentry(&rtentry);
		}
		if ((rn = rnode.rn_dupedkey))
			goto again;
	} else {
		rn = rnode.rn_right;
		size_cols_tree(rnode.rn_left);
		size_cols_tree(rn);
	}
}

static void
size_cols_rtentry(struct rtentry *rt)
{
	static struct ifnet ifnet, *lastif;
	struct rtentry parent;
	static char buffer[100];
	const char *bp;
	struct sockaddr *sa;
	sa_u addr, mask;
	int len;

	/*
	 * Don't print protocol-cloned routes unless -a.
	 */
	if (rt->rt_flags & RTF_WASCLONED && !aflag) {
		if (kget(rt->rt_parent, parent) != 0)
			return;
		if (parent.rt_flags & RTF_PRCLONING)
			return;
	}

	bzero(&addr, sizeof(addr));
	if ((sa = kgetsa(rt_key(rt))))
		bcopy(sa, &addr, sa->sa_len);
	bzero(&mask, sizeof(mask));
	if (rt_mask(rt) && (sa = kgetsa(rt_mask(rt))))
		bcopy(sa, &mask, sa->sa_len);
	bp = fmt_sockaddr(&addr.u_sa, &mask.u_sa, rt->rt_flags);
	len = strlen(bp);
	wid_dst = MAX(len, wid_dst);

	bp = fmt_sockaddr(kgetsa(rt->rt_gateway), NULL, RTF_HOST);
	len = strlen(bp);
	wid_gw = MAX(len, wid_gw);

	bp = fmt_flags(rt->rt_flags);
	len = strlen(bp);
	wid_flags = MAX(len, wid_flags);

	if (addr.u_sa.sa_family == AF_INET || Wflag) {
		len = snprintf(buffer, sizeof(buffer), "%ld", rt->rt_refcnt);
		wid_refs = MAX(len, wid_refs);
		len = snprintf(buffer, sizeof(buffer), "%lu", rt->rt_use);
		wid_use = MAX(len, wid_use);
		if (Wflag && rt->rt_rmx.rmx_mtu != 0) {
			len = snprintf(buffer, sizeof(buffer),
				       "%lu", rt->rt_rmx.rmx_mtu);
			wid_mtu = MAX(len, wid_mtu);
		}
	}
	if (rt->rt_ifp) {
		if (rt->rt_ifp != lastif) {
			if (kget(rt->rt_ifp, ifnet) == 0) 
				len = strlen(ifnet.if_xname);
			else
				len = strlen("---");
			lastif = rt->rt_ifp;
			wid_if = MAX(len, wid_if);
		}
		if (rt->rt_rmx.rmx_expire) {
			struct timespec sp;
			int expire_time;

			clock_gettime(CLOCK_MONOTONIC, &sp);

			expire_time = (int)(rt->rt_rmx.rmx_expire - sp.tv_sec);

			if (expire_time > 0) {
				snprintf(buffer, sizeof(buffer), "%d",
					 (int)expire_time);
				wid_expire = MAX(len, wid_expire);
			}
		}
	}
	if (Wflag) {
		if (rt->rt_shim[0] != NULL) {
			len = strlen(labelops(rt));
			wid_mplslops = MAX(len, wid_mplslops);
		}

		if (rt->rt_rmx.rmx_msl) {
			len = snprintf(buffer, sizeof(buffer),
				       "%lu", rt->rt_rmx.rmx_msl);
			wid_msl = MAX(len, wid_msl);
		}
		if (rt->rt_rmx.rmx_iwmaxsegs) {
			len = snprintf(buffer, sizeof(buffer),
				       "%lu", rt->rt_rmx.rmx_iwmaxsegs);
			wid_iwmax = MAX(len, wid_iwmax);
		}
		if (rt->rt_rmx.rmx_iwcapsegs) {
			len = snprintf(buffer, sizeof(buffer),
				       "%lu", rt->rt_rmx.rmx_iwcapsegs);
			wid_iw = MAX(len, wid_iw);
		}
	}
}


/*
 * Print header for routing table columns.
 */
void
pr_rthdr(int af1)
{

	if (Aflag)
		printf("%-8.8s ","Address");
	if (af1 == AF_INET || Wflag) {
		if (Wflag) {
			printf("%-*.*s %-*.*s %-*.*s %*.*s %*.*s %*.*s %*.*s "
			       "%*s %-*s%*s %*s %*s\n",
				wid_dst,	wid_dst,	"Destination",
				wid_gw,		wid_gw,		"Gateway",
				wid_flags,	wid_flags,	"Flags",
				wid_refs,	wid_refs,	"Refs",
				wid_use,	wid_use,	"Use",
				wid_mtu,	wid_mtu,	"Mtu",
				wid_if,		wid_if,		"Netif",
				wid_expire,			"Expire",
				wid_mplslops,			"Labelops",
				wid_msl,			"Msl",
				wid_iwmax,			"IWmax",
				wid_iw,				"IW");
		} else {
			printf("%-*.*s %-*.*s %-*.*s %*.*s %*.*s %*.*s %*s\n",
				wid_dst,	wid_dst,	"Destination",
				wid_gw,		wid_gw,		"Gateway",
				wid_flags,	wid_flags,	"Flags",
				wid_refs,	wid_refs,	"Refs",
				wid_use,	wid_use,	"Use",
				wid_if,		wid_if,		"Netif",
				wid_expire,			"Expire");
		}
	} else {
		printf("%-*.*s %-*.*s %-*.*s  %*.*s %*s\n",
			wid_dst,	wid_dst,	"Destination",
			wid_gw,		wid_gw,		"Gateway",
			wid_flags,	wid_flags,	"Flags",
			wid_if,		wid_if,		"Netif",
			wid_expire,			"Expire");
	}
}

static struct sockaddr *
kgetsa(struct sockaddr *dst)
{

	if (kget(dst, pt_u.u_sa) != 0)
		return (NULL);
	if (pt_u.u_sa.sa_len > sizeof (pt_u.u_sa))
		kread((u_long)dst, (char *)pt_u.u_data, pt_u.u_sa.sa_len);
	return (&pt_u.u_sa);
}

static void
p_tree(struct radix_node *rn)
{

again:
	if (kget(rn, rnode) != 0)
		return;
	if (!(rnode.rn_flags & RNF_ACTIVE))
		return;
	if (rnode.rn_bit < 0) {
		if (Aflag)
			printf("%-8.8lx ", (u_long)rn);
		if (rnode.rn_flags & RNF_ROOT) {
			if (Aflag)
				printf("(root node)%s",
				    rnode.rn_dupedkey ? " =>\n" : "\n");
		} else if (do_rtent) {
			if (kget(rn, rtentry) == 0) {
				p_rtentry(&rtentry);
				if (Aflag)
					p_rtnode();
			}
		} else {
			p_sockaddr(kgetsa((struct sockaddr *)rnode.rn_key),
				   NULL, 0, 44);
			putchar('\n');
		}
		if ((rn = rnode.rn_dupedkey))
			goto again;
	} else {
		if (Aflag && do_rtent) {
			printf("%-8.8lx ", (u_long)rn);
			p_rtnode();
		}
		rn = rnode.rn_right;
		p_tree(rnode.rn_left);
		p_tree(rn);
	}
}

char	nbuf[20];

static void
p_rtnode(void)
{
	struct radix_mask *rm = rnode.rn_mklist;

	if (rnode.rn_bit < 0) {
		if (rnode.rn_mask) {
			printf("\t  mask ");
			p_sockaddr(kgetsa((struct sockaddr *)rnode.rn_mask),
				   NULL, 0, -1);
		} else if (rm == NULL)
			return;
	} else {
		sprintf(nbuf, "(%d)", rnode.rn_bit);
		printf("%6.6s %8.8lx : %8.8lx", nbuf, (u_long)rnode.rn_left, (u_long)rnode.rn_right);
	}
	while (rm) {
		if (kget(rm, rmask) != 0)
			break;
		sprintf(nbuf, " %d refs, ", rmask.rm_refs);
		printf(" mk = %8.8lx {(%d),%s",
			(u_long)rm, -1 - rmask.rm_bit, rmask.rm_refs ? nbuf : " ");
		if (rmask.rm_flags & RNF_NORMAL) {
			struct radix_node rnode_aux;
			printf(" <normal>, ");
			if (kget(rmask.rm_leaf, rnode_aux) == 0)
				p_sockaddr(kgetsa((struct sockaddr *)rnode_aux.rn_mask),
				    NULL, 0, -1);
			else
				p_sockaddr(NULL, NULL, 0, -1);
		} else
		    p_sockaddr(kgetsa((struct sockaddr *)rmask.rm_mask),
				NULL, 0, -1);
		putchar('}');
		if ((rm = rmask.rm_next))
			printf(" ->");
	}
	putchar('\n');
}

static void
ntreestuff(void)
{
	size_t needed;
	int mib[7];
	int miblen;
	char *buf, *next, *lim;
	struct rt_msghdr *rtm;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = 0;
	mib[4] = NET_RT_DUMP;
	mib[5] = 0;
	if (cpuflag >= 0) {
		mib[6] = cpuflag;
		miblen = 7;
	} else {
		miblen = 6;
	}
	if (sysctl(mib, miblen, NULL, &needed, NULL, 0) < 0) {
		err(1, "sysctl: net.route.0.0.dump estimate");
	}

	if ((buf = malloc(needed)) == NULL) {
		err(2, "malloc(%lu)", (unsigned long)needed);
	}
	if (sysctl(mib, miblen, buf, &needed, NULL, 0) < 0) {
		err(1, "sysctl: net.route.0.0.dump");
	}
	lim  = buf + needed;
	for (next = buf; next < lim; next += rtm->rtm_msglen) {
		rtm = (struct rt_msghdr *)next;
		np_rtentry(rtm);
	}
}

static void
np_rtentry(struct rt_msghdr *rtm)
{
	struct sockaddr *sa = (struct sockaddr *)(rtm + 1);
#ifdef notdef
	static int masks_done, banner_printed;
#endif
	static int old_af;
	int af1 = 0, interesting = RTF_UP | RTF_GATEWAY | RTF_HOST;

#ifdef notdef
	/* for the moment, netmasks are skipped over */
	if (!banner_printed) {
		printf("Netmasks:\n");
		banner_printed = 1;
	}
	if (masks_done == 0) {
		if (rtm->rtm_addrs != RTA_DST ) {
			masks_done = 1;
			af1 = sa->sa_family;
		}
	} else
#endif
		af1 = sa->sa_family;
	if (af1 != old_af) {
		pr_family(af1);
		old_af = af1;
	}
	if (rtm->rtm_addrs == RTA_DST)
		p_sockaddr(sa, NULL, 0, 36);
	else {
		p_sockaddr(sa, NULL, rtm->rtm_flags, 16);
		sa = (struct sockaddr *)(ROUNDUP(sa->sa_len) + (char *)sa);
		p_sockaddr(sa, NULL, 0, 18);
	}
	p_flags(rtm->rtm_flags & interesting, "%-6.6s ");
	putchar('\n');
}

static void
p_sockaddr(struct sockaddr *sa, struct sockaddr *mask, int flags, int width)
{
	const char *cp;

	cp = fmt_sockaddr(sa, mask, flags);

	if (width < 0 )
		printf("%s ", cp);
	else {
		if (numeric_addr)
			printf("%-*s ", width, cp);
		else
			printf("%-*.*s ", width, width, cp);
	}
}

static const char *
fmt_sockaddr(struct sockaddr *sa, struct sockaddr *mask, int flags)
{
	static char workbuf[128];
	const char *cp = workbuf;

	if (sa == NULL)
		return ("null");

	switch(sa->sa_family) {
	case AF_INET:
	    {
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;

		if ((sin->sin_addr.s_addr == INADDR_ANY) &&
			mask &&
			ntohl(((struct sockaddr_in *)mask)->sin_addr.s_addr)
				==0L)
				cp = "default" ;
		else if (flags & RTF_HOST)
			cp = routename(sin->sin_addr.s_addr);
		else if (mask)
			cp = netname(sin->sin_addr.s_addr,
				     ntohl(((struct sockaddr_in *)mask)
					   ->sin_addr.s_addr));
		else
			cp = netname(sin->sin_addr.s_addr, 0L);
		break;
	    }

#ifdef INET6
	case AF_INET6:
	    {
		struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)sa;
		struct in6_addr *in6 = &sa6->sin6_addr;

		/*
		 * XXX: This is a special workaround for KAME kernels.
		 * sin6_scope_id field of SA should be set in the future.
		 */
		if (IN6_IS_ADDR_LINKLOCAL(in6) ||
		    IN6_IS_ADDR_MC_LINKLOCAL(in6)) {
		    /* XXX: override is ok? */
		    sa6->sin6_scope_id = (u_int32_t)ntohs(*(u_short *)&in6->s6_addr[2]);
		    *(u_short *)&in6->s6_addr[2] = 0;
		}

		if (flags & RTF_HOST)
		    cp = routename6(sa6);
		else if (mask)
		    cp = netname6(sa6,
				  &((struct sockaddr_in6 *)mask)->sin6_addr);
		else {
		    cp = netname6(sa6, NULL);
		}
		break;
	    }
#endif /*INET6*/

	case AF_NETGRAPH:
	    {
		printf("%s", ((struct sockaddr_ng *)sa)->sg_data);
		break;
	    }
	case AF_LINK:
	    {
		struct sockaddr_dl *sdl = (struct sockaddr_dl *)sa;

		if (sdl->sdl_nlen == 0 && sdl->sdl_alen == 0 &&
		    sdl->sdl_slen == 0)
			(void) sprintf(workbuf, "link#%d", sdl->sdl_index);
		else
			switch (sdl->sdl_type) {

			case IFT_ETHER:
			case IFT_L2VLAN:
			case IFT_CARP:
				if (sdl->sdl_alen == ETHER_ADDR_LEN) {
					cp = ether_ntoa((struct ether_addr *)
					    (sdl->sdl_data + sdl->sdl_nlen));
					break;
				}
				/* FALLTHROUGH */
			default:
				cp = link_ntoa(sdl);
				break;
			}
		break;
	    }

	case AF_MPLS:
	    {
		struct sockaddr_mpls *smpls = (struct sockaddr_mpls *)sa;

		(void) sprintf(workbuf, "%d", ntohl(smpls->smpls_label));
		break;
	    }
		
	default:
	    {
		u_char *s = (u_char *)sa->sa_data, *slim;
		char *cq, *cqlim;

		cq = workbuf;
		slim =  sa->sa_len + (u_char *) sa;
		cqlim = cq + sizeof(workbuf) - 6;
		cq += sprintf(cq, "(%d)", sa->sa_family);
		while (s < slim && cq < cqlim) {
			cq += sprintf(cq, " %02x", *s++);
			if (s < slim)
			    cq += sprintf(cq, "%02x", *s++);
		}
		cp = workbuf;
	    }
	}

	return (cp);
}

static void
p_flags(int f, const char *format)
{
	printf(format, fmt_flags(f));
}

static const char *
fmt_flags(int f)
{
	static char name[33];
	char *flags;
	struct bits *p = bits;

	for (flags = name; p->b_mask; p++)
		if (p->b_mask & f)
			*flags++ = p->b_val;
	*flags = '\0';
	return (name);
}

static void
p_rtentry(struct rtentry *rt)
{
	static struct ifnet ifnet, *lastif;
	struct rtentry parent;
	static char buffer[128];
	static char prettyname[128];
	struct sockaddr *sa;
	sa_u addr, mask;

	/*
	 * Don't print protocol-cloned routes unless -a.
	 */
	if (rt->rt_flags & RTF_WASCLONED && !aflag) {
		if (kget(rt->rt_parent, parent) != 0)
			return;
		if (parent.rt_flags & RTF_PRCLONING)
			return;
	}

	bzero(&addr, sizeof(addr));
	if ((sa = kgetsa(rt_key(rt))))
		bcopy(sa, &addr, sa->sa_len);
	bzero(&mask, sizeof(mask));
	if (rt_mask(rt) && (sa = kgetsa(rt_mask(rt))))
		bcopy(sa, &mask, sa->sa_len);
	p_sockaddr(&addr.u_sa, &mask.u_sa, rt->rt_flags, wid_dst);
	p_sockaddr(kgetsa(rt->rt_gateway), NULL, RTF_HOST, wid_gw);
	snprintf(buffer, sizeof(buffer), "%%-%d.%ds ", wid_flags, wid_flags);
	p_flags(rt->rt_flags, buffer);
	if (addr.u_sa.sa_family == AF_INET || Wflag) {
		printf("%*ld %*lu ", wid_refs, rt->rt_refcnt,
				     wid_use, rt->rt_use);
		if (Wflag) {
			if (rt->rt_rmx.rmx_mtu != 0)
				printf("%*lu ", wid_mtu, rt->rt_rmx.rmx_mtu);
			else
				printf("%*s ", wid_mtu, "");
		}
	}
	if (rt->rt_ifp) {
		if (rt->rt_ifp != lastif) {
			if (kget(rt->rt_ifp, ifnet) == 0)
				strlcpy(prettyname, ifnet.if_xname,
				    sizeof(prettyname));
			else
				strlcpy(prettyname, "---", sizeof(prettyname));
			lastif = rt->rt_ifp;
		}
		printf("%*.*s", wid_if, wid_if, prettyname);
		if (rt->rt_rmx.rmx_expire) {
			struct timespec sp;
			int expire_time;

			clock_gettime(CLOCK_MONOTONIC, &sp);

			expire_time = (int)(rt->rt_rmx.rmx_expire - sp.tv_sec);
			if (expire_time > 0)
				printf(" %*d", wid_expire, (int)expire_time);
			else
				printf("%*s ", wid_expire, "");
		} else {
			printf("%*s ", wid_expire, "");
		}
		if (rt->rt_nodes[0].rn_dupedkey)
			printf(" =>");
	}
	if (Wflag) {
		if (rt->rt_shim[0] != NULL)
			printf(" %-*s", wid_mplslops, labelops(rt));
		else
			printf(" %-*s", wid_mplslops, "");
		if (rt->rt_rmx.rmx_msl != 0)
			printf(" %*lu", wid_msl, rt->rt_rmx.rmx_msl);
		else
			printf("%*s ", wid_msl, "");
		if (rt->rt_rmx.rmx_iwmaxsegs != 0)
			printf(" %*lu", wid_iwmax, rt->rt_rmx.rmx_iwmaxsegs);
		else
			printf("%*s ", wid_iwmax, "");
		if (rt->rt_rmx.rmx_iwcapsegs != 0)
			printf(" %*lu", wid_iw, rt->rt_rmx.rmx_iwcapsegs);
		else
			printf("%*s ", wid_iw, "");
	}
	putchar('\n');
}

char *
routename(u_long in)
{
	char *cp;
	static char line[MAXHOSTNAMELEN];
	struct hostent *hp;

	cp = NULL;
	if (!numeric_addr) {
		hp = gethostbyaddr(&in, sizeof (struct in_addr), AF_INET);
		if (hp) {
			cp = hp->h_name;
			trimdomain(cp, strlen(cp));
		}
	}
	if (cp) {
		strncpy(line, cp, sizeof(line) - 1);
		line[sizeof(line) - 1] = '\0';
	} else {
#define C(x)	((x) & 0xff)
		in = ntohl(in);
		sprintf(line, "%lu.%lu.%lu.%lu",
		    C(in >> 24), C(in >> 16), C(in >> 8), C(in));
	}
	return (line);
}

static u_long
forgemask(u_long a)
{
	u_long m;

	if (IN_CLASSA(a))
		m = IN_CLASSA_NET;
	else if (IN_CLASSB(a))
		m = IN_CLASSB_NET;
	else
		m = IN_CLASSC_NET;
	return (m);
}

static void
domask(char *dst, u_long addr, u_long mask)
{
	int b, i;

	if (!mask || (forgemask(addr) == mask)) {
		*dst = '\0';
		return;
	}
	i = 0;
	for (b = 0; b < 32; b++)
		if (mask & (1 << b)) {
			int bb;

			i = b;
			for (bb = b+1; bb < 32; bb++)
				if (!(mask & (1 << bb))) {
					i = -1;	/* noncontig */
					break;
				}
			break;
		}
	if (i == -1)
		sprintf(dst, "&0x%lx", mask);
	else
		sprintf(dst, "/%d", 32-i);
}

/*
 * Return the name of the network whose address is given.
 * The address is assumed to be that of a net or subnet, not a host.
 */
char *
netname(u_long in, u_long mask)
{
	char *cp = NULL;
	static char line[MAXHOSTNAMELEN];
	struct netent *np = NULL;
	u_long dmask;
	u_long i;

#define	NSHIFT(m) (							\
	(m) == IN_CLASSA_NET ? IN_CLASSA_NSHIFT :			\
	(m) == IN_CLASSB_NET ? IN_CLASSB_NSHIFT :			\
	(m) == IN_CLASSC_NET ? IN_CLASSC_NSHIFT :			\
	0)

	i = ntohl(in);
	dmask = forgemask(i);
	if (!numeric_addr && i) {
		np = getnetbyaddr(i >> NSHIFT(mask), AF_INET);
		if (np == NULL && mask == 0)
			np = getnetbyaddr(i >> NSHIFT(dmask), AF_INET);
		if (np != NULL) {
			cp = np->n_name;
			trimdomain(cp, strlen(cp));
		}
	}
#undef NSHIFT
	if (cp != NULL) {
		strncpy(line, cp, sizeof(line) - 1);
		line[sizeof(line) - 1] = '\0';
	} else {
		if (mask <= IN_CLASSA_NET &&
			(i & IN_CLASSA_HOST) == 0) {
				sprintf(line, "%lu", C(i >> 24));
		} else if (mask <= IN_CLASSB_NET &&
			(i & IN_CLASSB_HOST) == 0) {
				sprintf(line, "%lu.%lu",
					C(i >> 24), C(i >> 16));
		} else if (mask <= IN_CLASSC_NET &&
			(i & IN_CLASSC_HOST) == 0) {
				sprintf(line, "%lu.%lu.%lu",
					C(i >> 24), C(i >> 16), C(i >> 8));
		} else {
			sprintf(line, "%lu.%lu.%lu.%lu",
				C(i >> 24), C(i >> 16), C(i >> 8), C(i));
		}
	}
	domask(line + strlen(line), i, mask);
	return (line);
}

#ifdef INET6
const char *
netname6(struct sockaddr_in6 *sa6, struct in6_addr *mask)
{
	static char line[MAXHOSTNAMELEN];
	u_char *p = (u_char *)mask;
	u_char *lim;
	int masklen, illegal = 0, flag = NI_WITHSCOPEID;

	if (mask) {
		for (masklen = 0, lim = p + 16; p < lim; p++) {
			switch (*p) {
			 case 0xff:
				 masklen += 8;
				 break;
			 case 0xfe:
				 masklen += 7;
				 break;
			 case 0xfc:
				 masklen += 6;
				 break;
			 case 0xf8:
				 masklen += 5;
				 break;
			 case 0xf0:
				 masklen += 4;
				 break;
			 case 0xe0:
				 masklen += 3;
				 break;
			 case 0xc0:
				 masklen += 2;
				 break;
			 case 0x80:
				 masklen += 1;
				 break;
			 case 0x00:
				 break;
			 default:
				 illegal ++;
				 break;
			}
		}
		if (illegal)
			fprintf(stderr, "illegal prefixlen\n");
	}
	else
		masklen = 128;

	if (masklen == 0 && IN6_IS_ADDR_UNSPECIFIED(&sa6->sin6_addr))
		return("default");

	if (numeric_addr)
		flag |= NI_NUMERICHOST;
	getnameinfo((struct sockaddr *)sa6, sa6->sin6_len, line, sizeof(line),
		    NULL, 0, flag);

	if (numeric_addr)
		sprintf(&line[strlen(line)], "/%d", masklen);

	return line;
}

char *
routename6(struct sockaddr_in6 *sa6)
{
	static char line[MAXHOSTNAMELEN];
	int flag = NI_WITHSCOPEID;
	/* use local variable for safety */
	struct sockaddr_in6 sa6_local;

	sa6_local.sin6_family = AF_INET6;
	sa6_local.sin6_len = sizeof(sa6_local);
	sa6_local.sin6_addr = sa6->sin6_addr;
	sa6_local.sin6_scope_id = sa6->sin6_scope_id;

	if (numeric_addr)
		flag |= NI_NUMERICHOST;

	getnameinfo((struct sockaddr *)&sa6_local, sa6_local.sin6_len,
		    line, sizeof(line), NULL, 0, flag);

	return line;
}
#endif /*INET6*/

/*
 * Print routing statistics
 */
void
rt_stats(void)
{
	struct rtstatistics rts;
	int error = 0;

	error = kinfo_get_net_rtstatistics(&rts);
	if (error) {
		printf("routing: could not retrieve statistics\n");
		return;
	}
	printf("routing:\n");

#define	p(f, m) if (rts.f || sflag <= 1) \
	printf(m, rts.f, plural(rts.f))

	p(rts_badredirect, "\t%lu bad routing redirect%s\n");
	p(rts_dynamic, "\t%lu dynamically created route%s\n");
	p(rts_newgateway, "\t%lu new gateway%s due to redirects\n");
	p(rts_unreach, "\t%lu destination%s found unreachable\n");
	p(rts_wildcard, "\t%lu use%s of a wildcard route\n");
#undef p
}

void
upHex(char *p0)
{
	char *p = p0;

	for (; *p; p++)
		switch (*p) {

		case 'a':
		case 'b':
		case 'c':
		case 'd':
		case 'e':
		case 'f':
			*p += ('A' - 'a');
			break;
		}
}

static const char *
labelops(struct rtentry *rt)
{
	const char *lops[] = { "push", "pop", "swap", "pop all" };
	static char buffer[100];
	char *cp = buffer;
	struct sockaddr_mpls *smpls;
	int i;

	for (i=0; i<MPLS_MAXLOPS; ++i) {

		if (rt->rt_shim[i] == NULL)
			break;
		if (i>0) {
			cp += snprintf(cp,
				       sizeof(buffer) - (cp - buffer),
				       ", ");
		}
		smpls = (struct sockaddr_mpls *)kgetsa(rt->rt_shim[i]);
		if (smpls->smpls_op != MPLSLOP_POP &&
		    smpls->smpls_op != MPLSLOP_POPALL){
			cp += snprintf(cp,
				       sizeof(buffer) - (cp - buffer),
				       "%s %d",
				       lops[smpls->smpls_op - 1],
				       ntohl(smpls->smpls_label));
		} else {
			cp += snprintf(cp,
				       sizeof(buffer) - (cp - buffer),
				       "%s",
				       lops[smpls->smpls_op - 1]);
		}
	}

	return (buffer);
}
