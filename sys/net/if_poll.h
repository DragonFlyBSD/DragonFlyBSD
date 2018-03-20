/*-
 * Copyright (c) 2001-2002 Luigi Rizzo
 *
 * Supported by: the Xorp Project (www.xorp.org)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/kern/kern_poll.c,v 1.2.2.4 2002/06/27 23:26:33 luigi Exp $
 */

#ifndef _NET_IF_POLL_H_
#define _NET_IF_POLL_H_

#ifdef _KERNEL

struct sysctl_ctx_list;
struct sysctl_oid;
struct lwkt_serialize;
struct ifnet;

typedef	void	(*ifpoll_iofn_t)(struct ifnet *, void *, int);
typedef	void	(*ifpoll_stfn_t)(struct ifnet *);

struct ifpoll_status {
	struct lwkt_serialize	*serializer;
	ifpoll_stfn_t		status_func;
};

struct ifpoll_io {
	struct lwkt_serialize	*serializer;
	void			*arg;
	ifpoll_iofn_t		poll_func;
};

struct ifpoll_info {
	struct ifnet		*ifpi_ifp;
	struct ifpoll_status	ifpi_status;
	struct ifpoll_io	ifpi_rx[MAXCPU];
	struct ifpoll_io	ifpi_tx[MAXCPU];
};

struct ifpoll_compat {
	int			ifpc_stcount;
	int			ifpc_stfrac;

	int			ifpc_cpuid;
	struct lwkt_serialize	*ifpc_serializer;
};

void	ifpoll_compat_setup(struct ifpoll_compat *, struct sysctl_ctx_list *,
	    struct sysctl_oid *, int, struct lwkt_serialize *);

#endif	/* _KERNEL */

#endif	/* !_NET_IF_POLL_H_ */
