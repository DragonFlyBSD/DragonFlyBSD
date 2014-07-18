/*
 * Copyright (c) 1988, 1993
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
 * @(#) Copyright (c) 1988, 1993 The Regents of the University of California.  All rights reserved.
 * @(#)hostname.c	8.1 (Berkeley) 5/31/93
 * $FreeBSD: src/bin/hostname/hostname.c,v 1.10.2.1 2001/08/01 02:40:23 obrien Exp $
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/module.h>
#include <sys/linker.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <netinet/in.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netdb.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include <errno.h>

#define HST_IF    (1 << 0)
#define HST_IF_V6 (1 << 1)
#define HST_IF_V4 (1 << 2)

/*
 * Expand the compacted form of addresses as returned via the
 * configuration read via sysctl().
 * Lifted from getifaddrs(3)
 */

static void rt_xaddrs(caddr_t, caddr_t, struct rt_addrinfo *);
static void usage (void);

#ifndef RT_ROUNDUP
#define RT_ROUNDUP(a) \
	((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) : sizeof(long))
#endif
#ifndef RT_ADVANCE
#define RT_ADVANCE(x, n) (x += RT_ROUNDUP((n)->sa_len))
#endif

static
void
rt_xaddrs(caddr_t cp, caddr_t cplim, struct rt_addrinfo *rtinfo)
{
	struct sockaddr *sa;
	int i;

	memset(rtinfo->rti_info, 0, sizeof(rtinfo->rti_info));
	for (i = 0; (i < RTAX_MAX) && (cp < cplim); i++) {
		if ((rtinfo->rti_addrs & (1 << i)) == 0)
			continue;
		rtinfo->rti_info[i] = sa = (struct sockaddr *)cp;
		RT_ADVANCE(cp, sa);
	}
}

int
main(int argc, char **argv)
{
	int ch, sflag, rflag, ret, flag6, iflag;
	int silen = 0;
	char hostname[MAXHOSTNAMELEN];
	char *srflag, *siflag;
	struct hostent *hst;
	struct in_addr ia;
	struct in6_addr ia6;

	int mib[6];
	size_t needed;
	char *buf, *next, *p;
	int idx;
	struct sockaddr_dl *sdl;
	struct rt_msghdr *rtm;
	struct if_msghdr *ifm;
	struct ifa_msghdr *ifam;
	struct rt_addrinfo info;
	struct sockaddr_in *sai;
	struct sockaddr_in6 *sai6;

	srflag = NULL;
	siflag = NULL;
	iflag = sflag = rflag = 0;
	flag6 = 0;
	hst = NULL;

	while ((ch = getopt(argc, argv, "46i:r:s")) != -1) {
		switch (ch) {
		case '4':
			iflag |= HST_IF_V4;
			break;
		case '6':
			iflag |= HST_IF_V6;
			break;
		case 'i':
			siflag = optarg;
			silen = strlen(siflag);
			iflag |= HST_IF;
			break;
		case 'r':
			srflag = optarg;
			rflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1)
		usage();

	if (iflag && *argv)
		usage();

	if (rflag && *argv)
		usage();

	if (rflag && (iflag & HST_IF))
		usage();

	if ((iflag & HST_IF_V6) && (iflag & HST_IF_V4))
		usage();

	if (!(iflag & HST_IF) && ((iflag & HST_IF_V6)||iflag & HST_IF_V4))
		usage();

	if (iflag & HST_IF) {
		mib[0] = CTL_NET;
		mib[1] = PF_ROUTE;
		mib[2] = 0;
		mib[3] = 0;
		mib[4] = NET_RT_IFLIST;
		mib[5] = 0;

		idx = 0;
		needed = 1;

		if (sysctl(mib, 6, NULL, &needed, NULL, 0) < 0)
			err(1, "sysctl: iflist-sysctl-estimate");
		if ((buf = malloc(needed)) == NULL)
			err(1, "malloc failed");
		if (sysctl(mib, 6, buf, &needed, NULL, 0) < 0)
			err(1, "sysctl: retrieval of interface table");
	
		for (next = buf; next < buf + needed; next += rtm->rtm_msglen) {
			rtm = (struct rt_msghdr *)(void *)next;
			if (rtm->rtm_version != RTM_VERSION)
				continue;
			switch (rtm->rtm_type) {
			case RTM_IFINFO:
				ifm = (struct if_msghdr *)(void *)rtm;

				if ((ifm->ifm_addrs & RTA_IFP) == 0)
					break;
				sdl = (struct sockaddr_dl *)(ifm + 1);
				if (silen != sdl->sdl_nlen)
					break;
				if (!strncmp(siflag, sdl->sdl_data, silen)) {
					idx = ifm->ifm_index;
				}
				break;
			case RTM_NEWADDR:
				ifam = (struct ifa_msghdr *)(void *)rtm;

				if (ifam->ifam_index == idx) {
					info.rti_addrs = ifam->ifam_addrs;
					rt_xaddrs((char *)(ifam + 1),
						ifam->ifam_msglen + (char *)ifam, &info);
					sai = (struct sockaddr_in *)info.rti_info[RTAX_IFA];

					if (iflag & HST_IF_V6) {
						if (sai->sin_family == AF_INET6) {
							sai6 = (struct sockaddr_in6 *)info.rti_info[RTAX_IFA];
							hst = gethostbyaddr(&sai6->sin6_addr,
									sizeof(sai6->sin6_addr),AF_INET6);

							if (h_errno == NETDB_SUCCESS) {
								next = buf + needed;
								continue;
							}
						}
					} else {
						if ((sai->sin_family == AF_INET)) {

							hst = gethostbyaddr(&sai->sin_addr,
									sizeof(sai->sin_addr),AF_INET);

							if (h_errno == NETDB_SUCCESS) {
								next = buf + needed;
								continue;
							}
						}
					}
				}
				break;
			} /* switch */
		} /* loop */

		free(buf);

		if (idx == 0)
			errx(1,"interface not found");
		if (hst == NULL)
			errx(1, "ip not found on interface");

		if (h_errno == NETDB_SUCCESS) {
			if (sethostname(hst->h_name, (int)strlen(hst->h_name)))
				err(1, "sethostname");
		} else if (h_errno == HOST_NOT_FOUND) {
			errx(1,"hostname not found");
		} else {
			herror("gethostbyaddr");
			exit(1);
		}
	} else if (rflag) {
		ret = inet_pton(AF_INET, srflag, &ia);
		if (ret != 1) {
			/* check IPV6 */
			ret = inet_pton(AF_INET6, srflag, &ia6);

			if (ret != 1) {
				errx(1, "invalid ip address");
			}

			flag6 = 1;
		}
		
		if (flag6 == 1) 
			hst = gethostbyaddr(&ia6, sizeof(ia6), AF_INET6);
		else
			hst = gethostbyaddr(&ia, sizeof(ia), AF_INET);
		if (!hst) {
			if (h_errno == HOST_NOT_FOUND) 
				errx(1,"host not found\n");
		}

		if (sethostname(hst->h_name, (int)strlen(hst->h_name)))
			err(1, "sethostname");
	} else if (*argv) {
		if (sethostname(*argv, (int)strlen(*argv)))
			err(1, "sethostname");
	} else {
		if (gethostname(hostname, (int)sizeof(hostname)))
			err(1, "gethostname");
		if (sflag && (p = strchr(hostname, '.')))
			*p = '\0';
		printf("%s\n", hostname);
	}
	exit(0);
}

static void
usage(void)
{
	fprintf(stderr, "usage: hostname [-s] [name-of-host |"
			" -r ip-address | -i interface [-4 | -6]]\n");
	exit(1);
}

