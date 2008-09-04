/*	$KAME: rthdr.c,v 1.8 2001/08/20 02:32:40 itojun Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/lib/libc/net/rthdr.c,v 1.2.2.1 2002/04/28 05:40:24 suz Exp $
 * $DragonFly: src/lib/libc/net/rthdr.c,v 1.6 2008/09/04 09:08:21 hasso Exp $
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip6.h>

#include <string.h>
#include <stdio.h>

/* 
 * RFC5095 deprecated Type 0 routing header and we don't support any other
 * routing header type yet.
 */
size_t
inet6_rthdr_space(int type __unused, int seg __unused)
{
	return(0); /* type not suppported */
}

struct cmsghdr *
inet6_rthdr_init(void *bp __unused, int type __unused)
{
	return(NULL); /* type not suppported */
}

int
inet6_rthdr_add(struct cmsghdr *cmsg __unused, const struct in6_addr *addr __unused, u_int flags __unused)
{
	return(-1); /* type not suppported */
}

int
inet6_rthdr_lasthop(struct cmsghdr *cmsg __unused, unsigned int flags __unused)
{
	return (-1); /* type not suppported */
}

int
inet6_rthdr_reverse(const struct cmsghdr *in __unused, struct cmsghdr *out __unused)
{
	return -1; /* type not suppported */
}

int
inet6_rthdr_segments(const struct cmsghdr *cmsg __unused)
{
	return -1; /* type not suppported */
}

struct in6_addr *
inet6_rthdr_getaddr(struct cmsghdr *cmsg __unused, int idx __unused)
{
	return NULL; /* type not suppported */
}

int
inet6_rthdr_getflags(const struct cmsghdr *cmsg __unused, int idx __unused)
{
	return -1; /* type not suppported */
}

/*
 * RFC3542 (2292bis) API
 */
socklen_t
inet6_rth_space(int type __unused, int segments __unused)
{
	return (0);	/* type not suppported */
}

void *
inet6_rth_init(void *bp __unused, socklen_t bp_len __unused, int type __unused,
	       int segments __unused)
{
	return (NULL);	/* type not supported */
}

int
inet6_rth_add(void *bp __unused, const struct in6_addr *addr __unused)
{
	return (-1);	/* type not supported */
}

int
inet6_rth_reverse(const void *in __unused, void *out __unused)
{
	return (-1);	/* type not supported */
}

int
inet6_rth_segments(const void *bp __unused)
{
	return (-1);	/* type not supported */
}

struct in6_addr *
inet6_rth_getaddr(const void *bp __unused, int idx __unused)
{
	return (NULL);	/* type not supported */
}
