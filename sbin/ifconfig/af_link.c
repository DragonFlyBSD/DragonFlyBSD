/*
 * Copyright (c) 1983, 1993
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
 * $FreeBSD: src/sbin/ifconfig/af_link.c,v 1.2 2004/12/31 19:46:27 sam Exp $
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/ethernet.h>

#include <err.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ifconfig.h"

static struct ifreq link_ridreq;

static void
link_status(int s __unused, const struct ifaddrs *ifa)
{
	struct sockaddr_dl *sdl =
		__DECONST(struct sockaddr_dl *, ifa->ifa_addr);
		/* drop 'const' qualifier for LLADDR() to suppress warning */
	char addr[18], *p;
	int n;

	if (sdl == NULL || sdl->sdl_alen == 0)
		return;

	if (sdl->sdl_type == IFT_ETHER && sdl->sdl_alen == ETHER_ADDR_LEN) {
		ether_ntoa_r((const struct ether_addr *)LLADDR(sdl), addr);
		if (f_ether != NULL && strcmp(f_ether, "dash") == 0) {
			p = addr;
			while ((p = strchr(p, ':')) != NULL)
				*p = '-';
		}
		printf("\tether %s\n", addr);
	} else {
		n = sdl->sdl_nlen > 0 ? sdl->sdl_nlen + 1 : 0;
		printf("\tlladdr %s\n", link_ntoa(sdl) + n);
	}
}

static void
link_getaddr(const char *addr, int which)
{
	struct sockaddr_dl sdl;
	struct sockaddr *sa = &link_ridreq.ifr_addr;
	char *temp;

	if (which != ADDR)
		errx(1, "can't set link-level netmask or broadcast");
	if ((temp = malloc(strlen(addr) + 2)) == NULL)
		errx(1, "malloc failed");
	temp[0] = ':';
	strcpy(temp + 1, addr);
	sdl.sdl_len = sizeof(sdl);
	link_addr(temp, &sdl);
	free(temp);
	if (sdl.sdl_alen > sizeof(sa->sa_data))
		errx(1, "malformed link-level address");
	sa->sa_family = AF_LINK;
	sa->sa_len = sdl.sdl_alen;
	memcpy(sa->sa_data, LLADDR(&sdl), sdl.sdl_alen);
}

static struct afswtch af_link = {
	.af_name	= "link",
	.af_af		= AF_LINK,
	.af_status	= link_status,
	.af_getaddr	= link_getaddr,
	.af_aifaddr	= SIOCSIFLLADDR,
	.af_addreq	= &link_ridreq,
};
static struct afswtch af_ether = {
	.af_name	= "ether",
	.af_af		= AF_LINK,
	.af_status	= link_status,
	.af_getaddr	= link_getaddr,
	.af_aifaddr	= SIOCSIFLLADDR,
	.af_addreq	= &link_ridreq,
};
static struct afswtch af_lladdr = {
	.af_name	= "lladdr",
	.af_af		= AF_LINK,
	.af_status	= link_status,
	.af_getaddr	= link_getaddr,
	.af_aifaddr	= SIOCSIFLLADDR,
	.af_addreq	= &link_ridreq,
};

__constructor(111)
static void
link_ctor(void)
{
	af_register(&af_link);
	af_register(&af_ether);
	af_register(&af_lladdr);
}
