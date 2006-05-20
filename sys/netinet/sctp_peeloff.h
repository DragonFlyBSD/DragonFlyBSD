/*	$KAME: sctp_peeloff.h,v 1.5 2004/08/17 04:06:19 itojun Exp $	*/
/*	$DragonFly: src/sys/netinet/sctp_peeloff.h,v 1.2 2006/05/20 02:42:12 dillon Exp $	*/

#ifndef _NETINET_SCTP_PEELOFF_H_
#define _NETINET_SCTP_PEELOFF_H_

/*
 * Copyright (C) 2002, 2004 Cisco Systems Inc,
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
 */

#include <sys/types.h>
#if !defined(__OpenBSD__)
#include <sys/socketvar.h>
#endif
#include <sys/socket.h>

#if defined(_KERNEL) || (defined(__APPLE__) && defined(KERNEL))

int sctp_can_peel_off(struct socket *, caddr_t);

int sctp_do_peeloff(struct socket *, struct socket *, caddr_t);

struct socket *sctp_get_peeloff(struct socket *, caddr_t, int *);

#ifdef __APPLE__
struct sctp_peeloff_args {
	int	sd;
	caddr_t	name;
};
#endif

#endif /* _KERNEL */

#endif
