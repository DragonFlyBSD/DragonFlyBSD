/*	$KAME: sctp6_var.h,v 1.6 2003/11/25 06:40:55 ono Exp $	*/

/*
 * Copyright (c) 2001, 2002, 2004 Cisco Systems, Inc.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Cisco Systems, Inc.
 * 4. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CISCO SYSTEMS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL CISCO SYSTEMS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef _NETINET6_SCTP6_VAR_H_
#define _NETINET6_SCTP6_VAR_H_

#if defined(_KERNEL) || (defined(__APPLE__) && defined(KERNEL))

#ifndef _SYS_SYSCTL_H_
#include <sys/sysctl.h>
#endif
#ifndef _SYS_PROTOSW_H_
#include <sys/protosw.h>
#endif

struct mbuf;
struct proc;
struct socket;
struct sockopt;
struct sctp_inpcb;

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__DragonFly__)
SYSCTL_DECL(_net_inet6_sctp6);
extern struct pr_usrreqs sctp6_usrreqs;
int	sctp6_ctloutput(struct socket *, struct sockopt *);
#else
int	sctp6_ctloutput(int, struct socket *, int, int, struct mbuf **);
#if defined(__NetBSD__) || defined(__OpenBSD__)
int	sctp6_usrreq(struct socket *, int, struct mbuf *, struct mbuf *,
		     struct mbuf *, struct proc *);
#else
int	sctp6_usrreq(struct socket *, int, struct mbuf *, struct mbuf *,
		     struct mbuf *);
#endif /* __NetBSD__ || __OpenBSD__ */
#endif /* __FreeBSD__ || __APPLE__ || __DragonFly__ */

#if defined(__APPLE__)
int	sctp6_input(struct mbuf **, int *);
#else
int	sctp6_input(struct mbuf **, int *, int);
#endif
int	sctp6_output(struct sctp_inpcb *, struct mbuf *, struct sockaddr *,
	struct mbuf *, struct proc *);
void	sctp6_ctlinput(int, struct sockaddr *, void *);

#endif /* _KERNEL */
#endif
