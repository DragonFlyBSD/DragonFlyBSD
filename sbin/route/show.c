/*
 * $OpenBSD: show.c,v 1.26 2003/08/26 08:33:12 itojun Exp $
 * $NetBSD: show.c,v 1.1 1996/11/15 18:01:41 gwr Exp $
 */
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
 */

#include <sys/param.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/mbuf.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/sysctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netdb.h>

#include "extern.h"
#include "keywords.h"

/*
 * Definitions for showing gateway flags.
 */
struct bits {
	int	b_mask;
	char	b_val;
};
static const struct bits bits[] = {
	{ RTF_UP,	'U' },
	{ RTF_GATEWAY,	'G' },
	{ RTF_HOST,	'H' },
	{ RTF_REJECT,	'R' },
	{ RTF_BLACKHOLE, 'B' },
	{ RTF_DYNAMIC,	'D' },
	{ RTF_MODIFIED,	'M' },
	{ RTF_DONE,	'd' }, /* Completed -- for routing messages only */
	{ RTF_CLONING,	'C' },
	{ RTF_XRESOLVE,	'X' },
	{ RTF_LLINFO,	'L' },
	{ RTF_STATIC,	'S' },
	{ RTF_PROTO1,	'1' },
	{ RTF_PROTO2,	'2' },
	{ RTF_PROTO3,	'3' },
	{ 0, 0 }
};

static void	p_rtentry(struct rt_msghdr *);
static int	p_sockaddr(struct sockaddr *, int, int);
static void	p_flags(int, const char *);
static void	pr_rthdr(void);
static void	pr_family(int);

/*
 * Print routing tables.
 */
void
show(int argc, char *argv[])
{
	struct rt_msghdr *rtm;
	char *buf = NULL, *next, *lim = NULL;
	size_t needed;
	int mib[7], af = 0;
	int miblen;
        struct sockaddr *sa;

        if (argc > 1) {
                argv++;
                if (argc == 2 && **argv == '-') {
                    switch (keyword(*argv + 1)) {
                        case K_INET:
                                af = AF_INET;
                                break;
#ifdef INET6
                        case K_INET6:
                                af = AF_INET6;
                                break;
#endif
                        case K_LINK:
                                af = AF_LINK;
                                break;
                        case K_ISO:
                        case K_OSI:
                                af = AF_ISO;
                                break;
                        case K_X25:
                                af = AF_CCITT;
                                break;
                        default:
                                goto bad;
		    }
                } else
bad:                    usage(*argv);
        }
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
	if (sysctl(mib, miblen, NULL, &needed, NULL, 0) < 0)	{
		perror("route-sysctl-estimate");
		exit(1);
	}
	if (needed > 0) {
		if ((buf = malloc(needed)) == NULL) {
			printf("out of space\n");
			exit(1);
		}
		if (sysctl(mib, miblen, buf, &needed, NULL, 0) < 0) {
			perror("sysctl of routing table");
			exit(1);
		}
		lim  = buf + needed;
	}

	printf("Routing tables\n");

	if (buf != NULL) {
		for (next = buf; next < lim; next += rtm->rtm_msglen) {
			rtm = (struct rt_msghdr *)next;
			sa = (struct sockaddr *)(rtm + 1);
			if (af != 0 && sa->sa_family != af)
				continue;
			p_rtentry(rtm);
		}
		free(buf);
	}
}

/* column widths; each followed by one space */
#define	WID_DST		(nflag ? 20 : 32)	/* destination column width */
#define	WID_GW		(nflag ? 20 : 32)	/* gateway column width */

/*
 * Print header for routing table columns.
 */
static void
pr_rthdr(void)
{
	printf("%-*.*s %-*.*s %-6.6s\n",
	    WID_DST, WID_DST, "Destination",
	    WID_GW, WID_GW, "Gateway",
	    "Flags");
}

/*
 * Print a routing table entry.
 */
static void
p_rtentry(struct rt_msghdr *rtm)
{
	struct sockaddr *sa = (struct sockaddr *)(rtm + 1);
#ifdef notdef
	static int masks_done, banner_printed;
#endif
	static int old_af;
	int af = 0, interesting = RTF_UP | RTF_GATEWAY | RTF_HOST;
	int width;

#ifdef notdef
	/* for the moment, netmasks are skipped over */
	if (!banner_printed) {
		printf("Netmasks:\n");
		banner_printed = 1;
	}
	if (masks_done == 0) {
		if (rtm->rtm_addrs != RTA_DST) {
			masks_done = 1;
			af = sa->sa_family;
		}
	} else
#endif
		af = sa->sa_family;
	if (old_af != af) {
		old_af = af;
		pr_family(af);
		pr_rthdr();
	}

	/*
	 * Print the information.  If wflag is set p_sockaddr() can return
	 * a wider width then specified and we try to fit the second
	 * address in any remaining space so the flags still lines up.
	 */
	if (rtm->rtm_addrs == RTA_DST) {
		p_sockaddr(sa, 0, WID_DST + WID_GW + 2);
	} else {
		width = p_sockaddr(sa, rtm->rtm_flags, WID_DST);
		sa = (struct sockaddr *)(RT_ROUNDUP(sa->sa_len) + (char *)sa);
		p_sockaddr(sa, 0, WID_GW + WID_DST - width);
	}
	p_flags(rtm->rtm_flags & interesting, "%-6.6s ");
	putchar('\n');
}

/*
 * Print address family header before a section of the routing table.
 */
static void
pr_family(int af)
{
	const char *afname;

	switch (af) {
	case AF_INET:
		afname = "Internet";
		break;
#ifdef INET6
	case AF_INET6:
		afname = "Internet6";
		break;
#endif /* INET6 */
	case AF_ISO:
		afname = "ISO";
		break;
	case AF_CCITT:
		afname = "X.25";
		break;
	default:
		afname = NULL;
		break;
	}
	if (afname != NULL)
		printf("\n%s:\n", afname);
	else
		printf("\nProtocol Family %d:\n", af);
}

static int
p_sockaddr(struct sockaddr *sa, int flags, int width)
{
	char workbuf[128];
	char *cp = workbuf;
	const char *cplim;
	int len = sizeof(workbuf);
	int count;

	switch(sa->sa_family) {

	case AF_LINK:
	    {
		struct sockaddr_dl *sdl = (struct sockaddr_dl *)sa;

		if (sdl->sdl_nlen == 0 && sdl->sdl_alen == 0 &&
		    sdl->sdl_slen == 0) {
			snprintf(workbuf, sizeof(workbuf),
			    "link#%d", sdl->sdl_index);
		} else {
			switch (sdl->sdl_type) {
			case IFT_ETHER:
			case IFT_L2VLAN:
			case IFT_CARP:
			    {
				int i;
				u_char *lla = (u_char *)sdl->sdl_data +
				    sdl->sdl_nlen;

				cplim = "";
				for (i = 0; i < sdl->sdl_alen; i++, lla++) {
					snprintf(cp, len, "%s%02x",
					    cplim, *lla);
					len -= strlen(cp);
					cp += strlen(cp);
					if (len <= 0)
						break;	/* overflow */
					cplim = ":";
				}
				cp = workbuf;
				break;
			    }
			default:
				cp = link_ntoa(sdl);
				break;
			}
		}
		break;
	    }

	case AF_INET:
	    {
		struct sockaddr_in *in = (struct sockaddr_in *)sa;

		if (in->sin_addr.s_addr == 0) {
			/* cp points to workbuf */
			strncpy(cp, "default", len);
		} else
			cp = (flags & RTF_HOST) ? routename(sa) : netname(sa);
		break;
	    }

#ifdef INET6
	case AF_INET6:
	    {
		struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)sa;

		if (IN6_IS_ADDR_UNSPECIFIED(&in6->sin6_addr)) {
			/* cp points to workbuf */
			strncpy(cp, "default", len);
		} else {
			cp = ((flags & RTF_HOST) ? routename(sa)
						 : netname(sa));
		}
		/* make sure numeric address is not truncated */
		if (strchr(cp, ':') != NULL &&
		    (width < 0 || strlen(cp) > (size_t)width))
			width = strlen(cp);
		break;
	    }
#endif /* INET6 */

	default:
	    {
		u_char *s = (u_char *)sa->sa_data, *slim;

		slim =  sa->sa_len + (u_char *) sa;
		cplim = cp + sizeof(workbuf) - 6;
		snprintf(cp, len, "(%d)", sa->sa_family);
		len -= strlen(cp);
		cp += strlen(cp);
		if (len <= 0) {
			cp = workbuf;
			break;		/* overflow */
		}
		while (s < slim && cp < cplim) {
			snprintf(cp, len, " %02x", *s++);
			len -= strlen(cp);
			cp += strlen(cp);
			if (len <= 0)
				break;		/* overflow */
			if (s < slim) {
				snprintf(cp, len, "%02x", *s++);
				len -= strlen(cp);
				cp += strlen(cp);
				if (len <= 0)
					break;		/* overflow */
			}
		}
		cp = workbuf;
	    }
	}
	if (width < 0) {
		count = printf("%s ", cp);
	} else {
		if (nflag || wflag)
			count = printf("%-*s ", width, cp);
		else
			count = printf("%-*.*s ", width, width, cp);
	}
	return(count);
}

static void
p_flags(int f, const char *format)
{
	char name[33], *flags;
	const struct bits *p = bits;

	for (flags = name; p->b_mask && flags < &name[sizeof(name) - 2]; p++) {
		if (p->b_mask & f)
			*flags++ = p->b_val;
	}
	*flags = '\0';
	printf(format, name);
}
